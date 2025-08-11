/*
 * (c) 2024 Protocentral Electronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include "max32664c.h"

LOG_MODULE_DECLARE(MAX32664C_ASYNC, CONFIG_SENSOR_LOG_LEVEL);

/**
 * @brief Configure MFIO pin mode
 *
 * Helper function to properly configure the MFIO pin for either
 * command mode (output) or interrupt mode (input).
 *
 * @param dev Device instance
 * @param command_mode True for command mode (output), false for interrupt mode (input)
 * @return 0 on success, negative errno on failure
 */
int max32664c_configure_mfio(const struct device *dev, bool command_mode)
{
    const struct max32664c_config *config = dev->config;
    int rc;

    if (command_mode) {
        /* Configure MFIO as output for command mode */
        rc = gpio_pin_configure_dt(&config->mfio_gpio, GPIO_OUTPUT);
        if (rc < 0) {
            LOG_ERR("Failed to configure MFIO as output");
            return rc;
        }
        
        /* Disable any interrupts when in command mode */
        rc = gpio_pin_interrupt_configure_dt(&config->mfio_gpio, GPIO_INT_DISABLE);
        if (rc < 0) {
            LOG_ERR("Failed to disable MFIO interrupt");
            return rc;
        }
    } else {
        /* Configure MFIO as input for interrupt mode */
        rc = gpio_pin_configure_dt(&config->mfio_gpio, GPIO_INPUT);
        if (rc < 0) {
            LOG_ERR("Failed to configure MFIO as input");
            return rc;
        }
    }

    return 0;
}

/**
 * @brief Submit a streaming request to the MAX32664C sensor.
 * 
 * This function is called when a new streaming request is received.
 * It configures the MFIO interrupt and stores the SQE for later processing.
 * 
 * @param dev Device instance
 * @param iodev_sqe RTIO SQE to process
 */
void max32664c_submit_stream(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe)
{
    const struct sensor_read_config *cfg =
        (const struct sensor_read_config *)iodev_sqe->sqe.iodev->data;
    struct max32664c_data *data = (struct max32664c_data *)dev->data;
    const struct max32664c_config *config = dev->config;
    int rc;

    /* Temporarily disable MFIO interrupt while we set up streaming */
    rc = gpio_pin_interrupt_configure_dt(&config->mfio_gpio, GPIO_INT_DISABLE);
    if (rc < 0) {
        LOG_ERR("Failed to disable MFIO interrupt");
        return;
    }

    /* Configure MFIO for interrupt mode */
    rc = max32664c_configure_mfio(dev, false);
    if (rc < 0) {
        return;
    }

    /* Store the SQE for later processing when MFIO interrupt happens */
    data->sqe = iodev_sqe;

    /* Store RTIO context and device references for use in interrupt handler */
    data->rtio_ctx = iodev_sqe->sqe.iodev->ctx;
    data->iodev = iodev_sqe->sqe.iodev;
    data->sensor_dev = dev;  /* Store reference to self */

    /* Re-enable the MFIO interrupt for falling edge (active low) */
    rc = gpio_pin_interrupt_configure_dt(&config->mfio_gpio, GPIO_INT_EDGE_FALLING);
    if (rc < 0) {
        LOG_ERR("Failed to re-enable MFIO interrupt");
        data->sqe = NULL;
        return;
    }
}

/**
 * @brief MAX32664C MFIO GPIO callback function
 *
 * This function is called when the MFIO pin triggers an interrupt.
 * It triggers the stream processing.
 * 
 * @param dev GPIO device that triggered the interrupt
 * @param cb Callback structure
 * @param pins Pin mask that triggered the interrupt
 */
static void max32664c_gpio_callback(const struct device *dev,
                                 struct gpio_callback *cb, uint32_t pins)
{
    struct max32664c_data *data = CONTAINER_OF(cb, struct max32664c_data, mfio_cb);
    const struct device *sensor_dev = data->sensor_dev;

    /* Call the interrupt handler */
    max32664c_stream_irq_handler(sensor_dev);
}

/**
 * @brief Handle MFIO interrupt for streaming
 *
 * This function is called from the MFIO interrupt handler.
 * It processes FIFO data and completes the RTIO SQE.
 * 
 * @param dev Device instance
 */
void max32664c_stream_irq_handler(const struct device *dev)
{
    struct max32664c_data *data = (struct max32664c_data *)dev->data;
    const struct max32664c_config *config = dev->config;
    struct rtio_iodev_sqe *current_sqe = data->sqe;
    uint8_t hub_stat;
    int fifo_count;
    uint8_t *buf;
    uint32_t buf_len;
    int rc;

    /* Check if we have a pending SQE to process */
    if (current_sqe == NULL) {
        return;
    }

    /* Configure MFIO for command mode to read data */
    max32664c_configure_mfio(dev, true);
    
    /* Disable interrupt while we process the data */
    gpio_pin_interrupt_configure_dt(&config->mfio_gpio, GPIO_INT_DISABLE);

    /* Save timestamp */
    data->timestamp = k_ticks_to_ns_floor64(k_uptime_ticks());

    /* Check if there is data ready */
    hub_stat = max32664c_read_hub_status(dev);
    if (!(hub_stat & MAX32664C_HUB_STAT_DRDY_MASK)) {
        /* No data ready, re-enable interrupt and return */
        gpio_pin_interrupt_configure_dt(&config->mfio_gpio, GPIO_INT_EDGE_TO_ACTIVE);
        return;
    }

    /* Get the FIFO count */
    fifo_count = max32664c_get_fifo_count(dev);
    if (fifo_count <= 0) {
        /* No data in FIFO, re-enable interrupt and return */
        gpio_pin_interrupt_configure_dt(&config->mfio_gpio, GPIO_INT_EDGE_TO_ACTIVE);
        return;
    }

    /* Clear the SQE reference since we're processing it now */
    data->sqe = NULL;

    /* Get a buffer for the RTIO response */
    rc = rtio_sqe_rx_buf(current_sqe, sizeof(struct max32664c_encoded_data),
                        sizeof(struct max32664c_encoded_data), &buf, &buf_len);
    if (rc != 0) {
        LOG_ERR("Failed to get buffer for RTIO response");
        rtio_iodev_sqe_err(current_sqe, -ENOMEM);
        gpio_pin_interrupt_configure_dt(&config->mfio_gpio, GPIO_INT_EDGE_TO_ACTIVE);
        return;
    }

    /* Process the data and fill the buffer */
    struct max32664c_encoded_data *m_edata = (struct max32664c_encoded_data *)buf;
    m_edata->header.timestamp = data->timestamp;

    /* Fetch the sample data based on operational mode */
    if (data->op_mode == MAX32664C_OP_MODE_ALGO_AGC || 
        data->op_mode == MAX32664C_OP_MODE_ALGO_AEC ||
        data->op_mode == MAX32664C_OP_MODE_ALGO_EXTENDED) {
        rc = max32664c_async_sample_fetch(dev, m_edata->green_samples, m_edata->ir_samples, m_edata->red_samples,
                                        &m_edata->num_samples, &m_edata->spo2, &m_edata->spo2_confidence, 
                                        &m_edata->spo2_valid_percent_complete, &m_edata->spo2_low_quality, 
                                        &m_edata->spo2_excessive_motion, &m_edata->spo2_low_pi, &m_edata->spo2_state,
                                        &m_edata->hr, &m_edata->hr_confidence, &m_edata->rtor, &m_edata->rtor_confidence, 
                                        &m_edata->scd_state, &m_edata->activity_class, &m_edata->steps_run, 
                                        &m_edata->steps_walk, &m_edata->chip_op_mode);
    } else if (data->op_mode == MAX32664C_OP_MODE_RAW) {
        rc = max32664c_async_sample_fetch_raw(dev, m_edata->green_samples, m_edata->ir_samples, 
                                            m_edata->red_samples, &m_edata->num_samples, &m_edata->chip_op_mode);
    } else if (data->op_mode == MAX32664C_OP_MODE_SCD) {
        rc = max32664c_async_sample_fetch_scd(dev, &m_edata->chip_op_mode, &m_edata->scd_state);
    } else if (data->op_mode == MAX32664C_OP_MODE_WAKE_ON_MOTION) {
        rc = max32664c_async_sample_fetch_wake_on_motion(dev, &m_edata->chip_op_mode);
    } else {
        LOG_ERR("Invalid operation mode %d", data->op_mode);
        rc = -EINVAL;
    }

    if (rc != 0) {
        rtio_iodev_sqe_err(current_sqe, rc);
    } else {
        rtio_iodev_sqe_ok(current_sqe, 0);
    }

    /* Re-enable the interrupt for future data (falling edge) */
    gpio_pin_interrupt_configure_dt(&config->mfio_gpio, GPIO_INT_EDGE_FALLING);
    
    /* If we're done with this streaming request and there are no new ones,
     * we can restore MFIO to command mode
     */
    if (data->sqe == NULL) {
        max32664c_configure_mfio(dev, true);
    }
}

/**
 * @brief Initialize MFIO interrupt for streaming
 * 
 * This function should be called during driver initialization
 * to set up the GPIO callback for MFIO interrupt.
 * 
 * @param dev Device instance
 * @return 0 on success, negative errno on failure
 */
int max32664c_init_streaming(const struct device *dev)
{
    struct max32664c_data *data = (struct max32664c_data *)dev->data;
    const struct max32664c_config *config = dev->config;
    
    /* Store device reference for callback use */
    data->sensor_dev = dev;

    /* Initialize the GPIO callback for MFIO */
    gpio_init_callback(&data->mfio_cb, max32664c_gpio_callback, BIT(config->mfio_gpio.pin));
    int rc = gpio_add_callback(config->mfio_gpio.port, &data->mfio_cb);
    if (rc < 0) {
        LOG_ERR("Failed to add GPIO callback for MFIO");
        return rc;
    }

    /* Disable interrupt initially, it will be enabled when streaming is requested */
    rc = gpio_pin_interrupt_configure_dt(&config->mfio_gpio, GPIO_INT_DISABLE);
    if (rc < 0) {
        LOG_ERR("Failed to disable MFIO interrupt");
        return rc;
    }

    return 0;
}
