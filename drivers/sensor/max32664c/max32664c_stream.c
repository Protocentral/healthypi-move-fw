/*
 * MAX32664C Async RTIO Stream Implementation
 * Based on ICM45686 reference architecture
 * (c) 2024 Protocentral Electronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "max32664c.h"
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(MAX32664C_STREAM, CONFIG_SENSOR_LOG_LEVEL);

static void max32664c_stream_irq_handler(const struct device *port, 
                                        struct gpio_callback *cb, 
                                        uint32_t pins)
{
    struct max32664c_data *data = CONTAINER_OF(cb, struct max32664c_data, async.int_callback);
    const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(max32664c));
    
    ARG_UNUSED(port);
    ARG_UNUSED(pins);
    
    /* Record interrupt timestamp */
    data->async.last_interrupt_time = k_cycle_get_32();
    
    /* Schedule work to handle the interrupt */
    k_work_schedule(&data->async.work, K_NO_WAIT);
}

static uint32_t max32664c_calculate_buffer_size(struct max32664c_data *data)
{
    uint32_t base_size = sizeof(struct max32664c_decoder_header);
    uint32_t payload_size = 0;
    
    switch (data->op_mode) {
    case MAX32664C_OP_MODE_RAW:
        payload_size = data->buffer_state.fifo_count * 24; // 3 LEDs * 8 bytes each
        break;
    case MAX32664C_OP_MODE_ALGO_AEC:
    case MAX32664C_OP_MODE_ALGO_AGC:
        payload_size = data->buffer_state.fifo_count * 48; // PPG + accel + algo
        break;
    case MAX32664C_OP_MODE_ALGO_EXTENDED:
        payload_size = data->buffer_state.fifo_count * 70; // Extended algo data
        break;
    case MAX32664C_OP_MODE_SCD:
        payload_size = data->buffer_state.fifo_count * 1; // SCD state only
        break;
    default:
        payload_size = sizeof(struct max32664c_encoded_data);
    }
    
    return base_size + payload_size;
}

static int max32664c_read_fifo_count(const struct device *dev, uint8_t *fifo_count)
{
    const struct max32664c_config *config = dev->config;
    struct max32664c_data *data = dev->data;
    uint8_t rd_buf[2] = {0x00, 0x00};
    uint8_t wr_buf[2] = {0x12, 0x00};
    int rc;
    bool was_interrupt_enabled = data->async.interrupt_enabled;
    
    // Temporarily disable interrupt and configure MFIO as output for control
    if (was_interrupt_enabled) {
        gpio_pin_interrupt_configure_dt(&config->mfio_gpio, GPIO_INT_DISABLE);
        gpio_pin_configure_dt(&config->mfio_gpio, GPIO_OUTPUT);
    }
    
    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));
    
    rc = i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
    if (rc != 0) {
        goto cleanup;
    }
    
    rc = i2c_read_dt(&config->i2c, rd_buf, sizeof(rd_buf));
    if (rc != 0) {
        goto cleanup;
    }
    
    *fifo_count = rd_buf[1];
    
cleanup:
    gpio_pin_set_dt(&config->mfio_gpio, 1);
    
    // Restore interrupt configuration if it was enabled
    if (was_interrupt_enabled) {
        gpio_pin_configure_dt(&config->mfio_gpio, GPIO_INPUT);
        gpio_pin_interrupt_configure_dt(&config->mfio_gpio, GPIO_INT_EDGE_RISING);
    }
    
    return rc;
}

static int max32664c_read_sensor_data(const struct device *dev, uint8_t *buf, 
                                     uint32_t buf_len, uint8_t fifo_count)
{
    const struct max32664c_config *config = dev->config;
    struct max32664c_data *data = dev->data;
    uint8_t wr_buf[2] = {0x12, 0x01};
    uint32_t sample_len;
    uint32_t total_len;
    int rc;
    bool was_interrupt_enabled = data->async.interrupt_enabled;
    
    // Calculate sample length based on mode
    switch (data->op_mode) {
    case MAX32664C_OP_MODE_RAW:
        sample_len = 24;
        break;
    case MAX32664C_OP_MODE_ALGO_AEC:
    case MAX32664C_OP_MODE_ALGO_AGC:
        sample_len = 48;
        break;
    case MAX32664C_OP_MODE_ALGO_EXTENDED:
        sample_len = 70;
        break;
    case MAX32664C_OP_MODE_SCD:
        sample_len = 1;
        break;
    default:
        return -ENOTSUP;
    }
    
    total_len = (sample_len * fifo_count) + 1; // +1 for offset
    
    // Validate buffer size
    if (total_len > buf_len) {
        LOG_ERR("Buffer too small: need %u, have %u", total_len, buf_len);
        return -ENOMEM;
    }
    
    // Temporarily disable interrupt and configure MFIO as output for control
    if (was_interrupt_enabled) {
        gpio_pin_interrupt_configure_dt(&config->mfio_gpio, GPIO_INT_DISABLE);
        gpio_pin_configure_dt(&config->mfio_gpio, GPIO_OUTPUT);
    }
    
    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));
    
    rc = i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
    if (rc != 0) {
        goto cleanup;
    }
    
    rc = i2c_read_dt(&config->i2c, buf, total_len);
    if (rc != 0) {
        goto cleanup;
    }
    
cleanup:
    k_sleep(K_USEC(300));
    gpio_pin_set_dt(&config->mfio_gpio, 1);
    
    // Restore interrupt configuration if it was enabled
    if (was_interrupt_enabled) {
        gpio_pin_configure_dt(&config->mfio_gpio, GPIO_INPUT);
        gpio_pin_interrupt_configure_dt(&config->mfio_gpio, GPIO_INT_EDGE_RISING);
    }
    
    return rc;
}

static void max32664c_process_data(const struct device *dev, uint8_t *raw_buf,
                                  uint32_t raw_len, struct max32664c_encoded_data *edata)
{
    struct max32664c_data *data = dev->data;
    const uint8_t *payload = raw_buf + 1; // Skip offset byte
    uint32_t offset = 0;
    
    // Fill header
    edata->header.timestamp = k_ticks_to_ns_floor64(k_uptime_ticks());
    edata->chip_op_mode = data->op_mode;
    edata->num_samples = data->buffer_state.fifo_count;
    
    // Process based on mode
    switch (data->op_mode) {
    case MAX32664C_OP_MODE_RAW:
        for (int i = 0; i < data->buffer_state.fifo_count; i++) {
            // Extract GREEN, IR, RED values (3 bytes each)
            edata->green_samples[i] = (payload[offset] << 16) | 
                                     (payload[offset + 1] << 8) | 
                                     payload[offset + 2];
            offset += 3;
            
            edata->ir_samples[i] = (payload[offset] << 16) | 
                                  (payload[offset + 1] << 8) | 
                                  payload[offset + 2];
            offset += 3;
            
            edata->red_samples[i] = (payload[offset] << 16) | 
                                   (payload[offset + 1] << 8) | 
                                   payload[offset + 2];
            offset += 3;
        }
        break;
        
    case MAX32664C_OP_MODE_ALGO_AEC:
    case MAX32664C_OP_MODE_ALGO_AGC:
        // Process PPG + algorithm data
        for (int i = 0; i < data->buffer_state.fifo_count; i++) {
            // PPG data (first 24 bytes)
            edata->green_samples[i] = (payload[offset] << 16) | 
                                     (payload[offset + 1] << 8) | 
                                     payload[offset + 2];
            offset += 3;
            
            edata->ir_samples[i] = (payload[offset] << 16) | 
                                  (payload[offset + 1] << 8) | 
                                  payload[offset + 2];
            offset += 3;
            
            edata->red_samples[i] = (payload[offset] << 16) | 
                                   (payload[offset + 1] << 8) | 
                                   payload[offset + 2];
            offset += 3;
            
            // Skip accelerometer data (15 bytes)
            offset += 15;
            
            // Algorithm data (24 bytes)
            edata->hr = (payload[offset + 1] << 8) | payload[offset + 2];
            edata->hr /= 10;
            edata->hr_confidence = payload[offset + 3];
            
            edata->rtor = (payload[offset + 4] << 8) | payload[offset + 5];
            edata->rtor /= 10;
            edata->rtor_confidence = payload[offset + 6];
            
            edata->spo2 = (payload[offset + 11] << 8) | payload[offset + 12];
            edata->spo2 /= 10;
            edata->spo2_confidence = payload[offset + 10];
            
            edata->scd_state = payload[offset + 19];
            
            offset += 24;
        }
        break;
        
    case MAX32664C_OP_MODE_SCD:
        edata->scd_state = payload[0];
        break;
        
    default:
        break;
    }
}

static void max32664c_stream_work_handler(struct k_work *work)
{
    struct k_work_delayable *delayable = k_work_delayable_from_work(work);
    struct max32664c_data *data = CONTAINER_OF(delayable, struct max32664c_data, async.work);
    const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(max32664c));
    struct rtio_iodev_sqe *iodev_sqe = data->async.pending_sqe;
    
    if (!iodev_sqe) {
        LOG_WRN("No pending SQE for interrupt");
        return;
    }
    
    uint8_t fifo_count;
    int rc = max32664c_read_fifo_count(dev, &fifo_count);
    if (rc != 0) {
        LOG_ERR("Failed to read FIFO count: %d", rc);
        rtio_iodev_sqe_err(iodev_sqe, rc);
        return;
    }
    
    data->buffer_state.fifo_count = fifo_count;
    
    if (fifo_count == 0) {
        LOG_DBG("No data in FIFO");
        return;
    }
    
    // Limit FIFO count to prevent overflow
    if (fifo_count > 16) {
        fifo_count = 16;
        data->buffer_state.fifo_count = fifo_count;
    }
    
    uint32_t required_size = max32664c_calculate_buffer_size(data);
    
    uint8_t *buf;
    uint32_t buf_len;
    rc = rtio_sqe_rx_buf(iodev_sqe, required_size, required_size, &buf, &buf_len);
    if (rc != 0) {
        LOG_ERR("Failed to get buffer: %d", rc);
        rtio_iodev_sqe_err(iodev_sqe, rc);
        return;
    }
    
    // Read raw sensor data
    uint8_t raw_buf[512]; // Temporary buffer for raw I2C data
    rc = max32664c_read_sensor_data(dev, raw_buf, sizeof(raw_buf), fifo_count);
    if (rc != 0) {
        LOG_ERR("Failed to read sensor data: %d", rc);
        rtio_iodev_sqe_err(iodev_sqe, rc);
        return;
    }
    
    // Process and encode data
    struct max32664c_encoded_data *edata = (struct max32664c_encoded_data *)buf;
    max32664c_process_data(dev, raw_buf, sizeof(raw_buf), edata);
    
    // Clear pending state
    data->async.pending_sqe = NULL;
    atomic_clear(&data->async.in_progress);
    
    // Complete the operation
    rtio_iodev_sqe_ok(iodev_sqe, 0);
}

int max32664c_stream_init(const struct device *dev)
{
    const struct max32664c_config *config = dev->config;
    struct max32664c_data *data = dev->data;
    int rc;
    
    if (!gpio_is_ready_dt(&config->mfio_gpio)) {
        LOG_ERR("MFIO GPIO not ready");
        return -ENODEV;
    }
    
    // Initialize work queue
    k_work_init_delayable(&data->async.work, max32664c_stream_work_handler);
    
    // Setup interrupt callback (MFIO pin will be configured as interrupt later)
    gpio_init_callback(&data->async.int_callback, max32664c_stream_irq_handler,
                      BIT(config->mfio_gpio.pin));
    
    rc = gpio_add_callback(config->mfio_gpio.port, &data->async.int_callback);
    if (rc != 0) {
        LOG_ERR("Failed to add interrupt callback: %d", rc);
        return rc;
    }
    
    // Initialize atomic state
    atomic_set(&data->async.in_progress, 0);
    
    return 0;
}

int max32664c_stream_enable(const struct device *dev)
{
    const struct max32664c_config *config = dev->config;
    struct max32664c_data *data = dev->data;
    int rc;
    
    if (data->async.interrupt_enabled) {
        return 0;
    }
    
    // Configure MFIO as input for interrupt (after initialization it's no longer used as output)
    rc = gpio_pin_configure_dt(&config->mfio_gpio, GPIO_INPUT);
    if (rc != 0) {
        LOG_ERR("Failed to configure MFIO as input: %d", rc);
        return rc;
    }
    
    // Enable interrupt on MFIO pin
    rc = gpio_pin_interrupt_configure_dt(&config->mfio_gpio, GPIO_INT_EDGE_RISING);
    if (rc != 0) {
        LOG_ERR("Failed to configure interrupt: %d", rc);
        return rc;
    }
    
    data->async.interrupt_enabled = true;
    LOG_DBG("Stream interrupts enabled on MFIO");
    
    return 0;
}

int max32664c_stream_disable(const struct device *dev)
{
    const struct max32664c_config *config = dev->config;
    struct max32664c_data *data = dev->data;
    int rc;
    
    if (!data->async.interrupt_enabled) {
        return 0;
    }
    
    // Disable interrupt on MFIO pin
    rc = gpio_pin_interrupt_configure_dt(&config->mfio_gpio, GPIO_INT_DISABLE);
    if (rc != 0) {
        LOG_ERR("Failed to disable interrupt: %d", rc);
        return rc;
    }
    
    // Reconfigure MFIO as output for control operations
    rc = gpio_pin_configure_dt(&config->mfio_gpio, GPIO_OUTPUT);
    if (rc != 0) {
        LOG_ERR("Failed to configure MFIO as output: %d", rc);
        return rc;
    }
    
    data->async.interrupt_enabled = false;
    LOG_DBG("Stream interrupts disabled on MFIO");
    
    return 0;
}