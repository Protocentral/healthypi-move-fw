/*
 * (c) 2024 Protocentral Electronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/rtio/rtio.h>
#include "max32664c.h"

LOG_MODULE_DECLARE(MAX32664C_ASYNC, CONFIG_SENSOR_LOG_LEVEL);

/* Forward declarations for RTIO callback functions */
static void max32664c_hub_status_callback(struct rtio *r, const struct rtio_sqe *sqe, void *arg);
static void max32664c_fifo_count_callback(struct rtio *r, const struct rtio_sqe *sqe, void *arg);

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
 * @brief Hub status read completion callback
 *
 * This callback is called when the asynchronous hub status read completes.
 * It checks the status and proceeds to read FIFO count if data is ready.
 * 
 * @param r RTIO context
 * @param sqe Completed SQE
 * @param arg Device instance
 */
static void max32664c_hub_status_callback(struct rtio *r, const struct rtio_sqe *sqe, void *arg)
{
    const struct device *dev = (const struct device *)arg;
    struct max32664c_data *data = (struct max32664c_data *)dev->data;
    const struct max32664c_config *config = dev->config;
    uint8_t hub_stat = data->hub_status_buf[1]; /* Status is in byte 1 */
    
    /* Check if there is data ready */
    if (!(hub_stat & MAX32664C_HUB_STAT_DRDY_MASK)) {
        /* No data ready, complete with empty result and re-enable interrupt */
        rtio_iodev_sqe_ok(data->sqe, 0);
        data->sqe = NULL;
        gpio_pin_interrupt_configure_dt(&config->mfio_gpio, GPIO_INT_EDGE_FALLING);
        return;
    }
    
    /* Data is ready, now read FIFO count asynchronously */
    struct rtio_sqe *write_fifo_sqe = rtio_sqe_acquire(r);
    struct rtio_sqe *read_fifo_sqe = rtio_sqe_acquire(r);
    struct rtio_sqe *callback_sqe = rtio_sqe_acquire(r);
    
    if (!write_fifo_sqe || !read_fifo_sqe || !callback_sqe) {
        LOG_ERR("Failed to acquire SQEs for FIFO count read");
        rtio_iodev_sqe_err(data->sqe, -ENOMEM);
        data->sqe = NULL;
        gpio_pin_interrupt_configure_dt(&config->mfio_gpio, GPIO_INT_EDGE_FALLING);
        return;
    }
    
    /* Prepare I2C write command for FIFO count (0x12, 0x00) */
    data->fifo_count_cmd[0] = 0x12;
    data->fifo_count_cmd[1] = 0x00;
    
    rtio_sqe_prep_tiny_write(write_fifo_sqe, 
                           (struct rtio_iodev *)&config->i2c, 
                           RTIO_PRIO_NORM, 
                           data->fifo_count_cmd, 2, NULL);
    write_fifo_sqe->flags = RTIO_SQE_TRANSACTION;
    
    rtio_sqe_prep_read(read_fifo_sqe,
                      (struct rtio_iodev *)&config->i2c,
                      RTIO_PRIO_NORM,
                      data->fifo_count_buf, 2, NULL);
    read_fifo_sqe->flags = RTIO_SQE_CHAINED;
    
    rtio_sqe_prep_callback(callback_sqe, max32664c_fifo_count_callback, arg, NULL);
    
    rtio_submit(r, 0);
}

/**
 * @brief FIFO count read completion callback
 *
 * This callback is called when the asynchronous FIFO count read completes.
 * It processes the data and completes the streaming operation.
 * 
 * @param r RTIO context
 * @param sqe Completed SQE
 * @param arg Device instance
 */
static void max32664c_fifo_count_callback(struct rtio *r, const struct rtio_sqe *sqe, void *arg)
{
    const struct device *dev = (const struct device *)arg;
    struct max32664c_data *data = (struct max32664c_data *)dev->data;
    const struct max32664c_config *config = dev->config;
    struct rtio_iodev_sqe *current_sqe = data->sqe;
    int fifo_count = (int)data->fifo_count_buf[1]; /* FIFO count is in byte 1 */
    uint8_t *buf;
    uint32_t buf_len;
    int rc;
    
    if (fifo_count <= 0) {
        /* No data in FIFO, complete with empty result and re-enable interrupt */
        rtio_iodev_sqe_ok(current_sqe, 0);
        data->sqe = NULL;
        gpio_pin_interrupt_configure_dt(&config->mfio_gpio, GPIO_INT_EDGE_FALLING);
        return;
    }
    
    /* Calculate buffer size based on operational mode */
    uint32_t min_buf_size = sizeof(struct max32664c_decoder_header) + 32;
    uint32_t max_buf_size = sizeof(struct max32664c_encoded_data);
    
    switch (data->op_mode) {
        case MAX32664C_OP_MODE_SCD:
        case MAX32664C_OP_MODE_WAKE_ON_MOTION:
            min_buf_size = sizeof(struct max32664c_decoder_header) + 16;
            break;
        case MAX32664C_OP_MODE_RAW:
            min_buf_size = sizeof(struct max32664c_decoder_header) + 200;
            break;
        case MAX32664C_OP_MODE_ALGO_AGC:
        case MAX32664C_OP_MODE_ALGO_AEC:
        case MAX32664C_OP_MODE_ALGO_EXTENDED:
            min_buf_size = sizeof(struct max32664c_decoder_header) + 300;
            break;
        default:
            min_buf_size = sizeof(struct max32664c_decoder_header) + 32;
            break;
    }

    /* Get a buffer for the RTIO response */
    rc = rtio_sqe_rx_buf(current_sqe, min_buf_size, max_buf_size, &buf, &buf_len);
    if (rc != 0) {
        LOG_ERR("Failed to get buffer for RTIO response (min: %u, max: %u)", min_buf_size, max_buf_size);
        rtio_iodev_sqe_err(current_sqe, -ENOMEM);
        data->sqe = NULL;
        gpio_pin_interrupt_configure_dt(&config->mfio_gpio, GPIO_INT_EDGE_FALLING);
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

    /* Complete the operation */
    if (rc != 0) {
        rtio_iodev_sqe_err(current_sqe, rc);
    } else {
        rtio_iodev_sqe_ok(current_sqe, 0);
    }

    /* Clear SQE and re-enable interrupt for next operation */
    data->sqe = NULL;
    gpio_pin_interrupt_configure_dt(&config->mfio_gpio, GPIO_INT_EDGE_FALLING);
    
    /* Restore MFIO to command mode when done */
    max32664c_configure_mfio(dev, true);
}

/**
 * @brief Handle MFIO interrupt for streaming (RTIO-based non-blocking approach)
 *
 * This function is called from the MFIO interrupt handler.
 * It creates an RTIO chain to read hub status asynchronously instead of blocking.
 * 
 * @param dev Device instance
 */
void max32664c_stream_irq_handler(const struct device *dev)
{
    struct max32664c_data *data = (struct max32664c_data *)dev->data;
    const struct max32664c_config *config = dev->config;
    struct rtio_iodev_sqe *current_sqe = data->sqe;

    /* Check if we have a pending SQE to process */
    if (current_sqe == NULL) {
        return;
    }

    /* Disable interrupt while we set up RTIO chain */
    gpio_pin_interrupt_configure_dt(&config->mfio_gpio, GPIO_INT_DISABLE);
    
    /* Save timestamp */
    data->timestamp = k_ticks_to_ns_floor64(k_uptime_ticks());

    /* Configure MFIO for command mode */
    max32664c_configure_mfio(dev, true);

    /* 
     * For a proper RTIO implementation, we would need access to the RTIO context
     * to create async I2C operations. However, since we don't have access to the 
     * RTIO context in the interrupt handler, we use the minimal blocking approach
     * with reduced delays until full RTIO infrastructure is available.
     */
    
    /* Read hub status with minimal delay */
    uint8_t hub_status_cmd[2] = {0x00, 0x00};
    
    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_busy_wait(50);  /* Minimal delay for hardware requirements */

    int rc = i2c_write_dt(&config->i2c, hub_status_cmd, sizeof(hub_status_cmd));
    if (rc != 0) {
        LOG_ERR("Failed to write hub status command: %d", rc);
        rtio_iodev_sqe_err(current_sqe, rc);
        data->sqe = NULL;
        gpio_pin_interrupt_configure_dt(&config->mfio_gpio, GPIO_INT_EDGE_FALLING);
        return;
    }

    rc = i2c_read_dt(&config->i2c, data->hub_status_buf, sizeof(data->hub_status_buf));
    if (rc != 0) {
        LOG_ERR("Failed to read hub status: %d", rc);
        rtio_iodev_sqe_err(current_sqe, rc);
        data->sqe = NULL;
        gpio_pin_interrupt_configure_dt(&config->mfio_gpio, GPIO_INT_EDGE_FALLING);
        return;
    }

    k_busy_wait(50);  
    gpio_pin_set_dt(&config->mfio_gpio, 1);
    
    uint8_t hub_stat = data->hub_status_buf[1];
    
    /* Check if there is data ready */
    if (!(hub_stat & MAX32664C_HUB_STAT_DRDY_MASK)) {
        /* No data ready, complete with empty result and re-enable interrupt */
        rtio_iodev_sqe_ok(current_sqe, 0);
        data->sqe = NULL;
        gpio_pin_interrupt_configure_dt(&config->mfio_gpio, GPIO_INT_EDGE_FALLING);
        return;
    }

    /* Read FIFO count with minimal delay */
    data->fifo_count_cmd[0] = 0x12;
    data->fifo_count_cmd[1] = 0x00;
    
    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_busy_wait(50); 

    rc = i2c_write_dt(&config->i2c, data->fifo_count_cmd, sizeof(data->fifo_count_cmd));
    if (rc != 0) {
        LOG_ERR("Failed to write FIFO count command: %d", rc);
        rtio_iodev_sqe_err(current_sqe, rc);
        data->sqe = NULL;
        gpio_pin_interrupt_configure_dt(&config->mfio_gpio, GPIO_INT_EDGE_FALLING);
        return;
    }
    
    rc = i2c_read_dt(&config->i2c, data->fifo_count_buf, sizeof(data->fifo_count_buf));
    if (rc != 0) {
        LOG_ERR("Failed to read FIFO count: %d", rc);
        rtio_iodev_sqe_err(current_sqe, rc);
        data->sqe = NULL;
        gpio_pin_interrupt_configure_dt(&config->mfio_gpio, GPIO_INT_EDGE_FALLING);
        return;
    }

    gpio_pin_set_dt(&config->mfio_gpio, 1);

    int fifo_count = (int)data->fifo_count_buf[1];
    if (fifo_count <= 0) {
        /* No data in FIFO, complete with empty result and re-enable interrupt */
        rtio_iodev_sqe_ok(current_sqe, 0);
        data->sqe = NULL;
        gpio_pin_interrupt_configure_dt(&config->mfio_gpio, GPIO_INT_EDGE_FALLING);
        return;
    }

    /* Clear the SQE reference since we're processing it now */
    data->sqe = NULL;

    /* Calculate buffer size based on operational mode */
    uint32_t min_buf_size = sizeof(struct max32664c_decoder_header) + 32;
    uint32_t max_buf_size = sizeof(struct max32664c_encoded_data);
    
    switch (data->op_mode) {
        case MAX32664C_OP_MODE_SCD:
        case MAX32664C_OP_MODE_WAKE_ON_MOTION:
            min_buf_size = sizeof(struct max32664c_decoder_header) + 16;
            break;
        case MAX32664C_OP_MODE_RAW:
            min_buf_size = sizeof(struct max32664c_decoder_header) + 200;
            break;
        case MAX32664C_OP_MODE_ALGO_AGC:
        case MAX32664C_OP_MODE_ALGO_AEC:
        case MAX32664C_OP_MODE_ALGO_EXTENDED:
            min_buf_size = sizeof(struct max32664c_decoder_header) + 300;
            break;
        default:
            min_buf_size = sizeof(struct max32664c_decoder_header) + 32;
            break;
    }

    /* Get a buffer for the RTIO response */
    uint8_t *buf;
    uint32_t buf_len;
    rc = rtio_sqe_rx_buf(current_sqe, min_buf_size, max_buf_size, &buf, &buf_len);
    if (rc != 0) {
        LOG_ERR("Failed to get buffer for RTIO response (min: %u, max: %u)", min_buf_size, max_buf_size);
        rtio_iodev_sqe_err(current_sqe, -ENOMEM);
        gpio_pin_interrupt_configure_dt(&config->mfio_gpio, GPIO_INT_EDGE_FALLING);
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

    /* Complete the operation */
    if (rc != 0) {
        rtio_iodev_sqe_err(current_sqe, rc);
    } else {
        rtio_iodev_sqe_ok(current_sqe, 0);
    }

    /* Re-enable interrupt for next operation */
    gpio_pin_interrupt_configure_dt(&config->mfio_gpio, GPIO_INT_EDGE_FALLING);
    
    /* Restore MFIO to command mode when done */
    max32664c_configure_mfio(dev, true);
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
