#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(MAX32664C_ASYNC, CONFIG_SENSOR_LOG_LEVEL);

#include "max32664c.h"

#define MAX32664C_SENSOR_DATA_OFFSET 1

int max32664c_get_fifo_count(const struct device *dev)
{
    const struct max32664c_config *config = dev->config;
    uint8_t rd_buf[2] = {0x00, 0x00};
    uint8_t wr_buf[2] = {0x12, 0x00};

    uint8_t fifo_count;
    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));

    i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
    i2c_read_dt(&config->i2c, rd_buf, sizeof(rd_buf));

    gpio_pin_set_dt(&config->mfio_gpio, 1);

    fifo_count = rd_buf[1];
    // printk("FIFO Count: %d\n", fifo_count);
    return (int)fifo_count;
}

static int max32664c_async_sample_fetch_scd(const struct device *dev, uint8_t *chip_op_mode, uint8_t *scd_state)
{
    struct max32664c_data *data = dev->data;
    const struct max32664c_config *config = dev->config;

    uint8_t wr_buf[2] = {0x12, 0x01};
    static uint8_t buf[2048];
    static int sample_len = 1;

    uint8_t hub_stat = max32664c_read_hub_status(dev);
    if (hub_stat & MAX32664C_HUB_STAT_DRDY_MASK)
    {
        int fifo_count = max32664c_get_fifo_count(dev);

        if (fifo_count > 8)
        {
            fifo_count = 8;
        }

        if (fifo_count > 0)
        {
            sample_len = 1;
            *chip_op_mode = data->op_mode;

            gpio_pin_set_dt(&config->mfio_gpio, 0);
            k_sleep(K_USEC(300));
            i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));

            i2c_read_dt(&config->i2c, buf, ((sample_len * fifo_count) + MAX32664C_SENSOR_DATA_OFFSET));
            k_sleep(K_USEC(300));
            gpio_pin_set_dt(&config->mfio_gpio, 1);

            for (int i = 0; i < fifo_count; i++)
            {
                uint8_t scd_state_val = (uint8_t)buf[(sample_len * i) + 0 + MAX32664C_SENSOR_DATA_OFFSET];
                *scd_state = scd_state_val;
                // printk("SCD: %d\n", scd_state_val);
            }
        }
    }

    if (hub_stat & MAX32664C_HUB_STAT_SCD_MASK)
    {
        // printk("SCD ");
    }
    return 0;
}

static int max32664c_async_sample_fetch_wake_on_motion(const struct device *dev, uint8_t *chip_op_mode)
{
    struct max32664c_data *data = dev->data;
    const struct max32664c_config *config = dev->config;

    uint8_t wr_buf[2] = {0x12, 0x01};
    static uint8_t buf[2048];
    static int sample_len = 1;

    uint8_t hub_stat = max32664c_read_hub_status(dev);
    if (hub_stat & MAX32664C_HUB_STAT_DRDY_MASK)
    {
        int fifo_count = max32664c_get_fifo_count(dev);

        if (fifo_count > 8)
        {
            fifo_count = 8;
        }

        if (fifo_count > 0)
        {
            sample_len = 1;
            *chip_op_mode = data->op_mode;

            gpio_pin_set_dt(&config->mfio_gpio, 0);
            k_sleep(K_USEC(300));
            i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));

            i2c_read_dt(&config->i2c, buf, ((sample_len * fifo_count) + MAX32664C_SENSOR_DATA_OFFSET));
            k_sleep(K_USEC(300));
            gpio_pin_set_dt(&config->mfio_gpio, 1);

            for (int i = 0; i < fifo_count; i++)
            {
                uint8_t algo_op_mode = (uint8_t)buf[(sample_len * i) + 0 + MAX32664C_SENSOR_DATA_OFFSET];
                LOG_INF("Algo Op Mode: %d", algo_op_mode);
            }
        }
    }
    return 0;
}

static int max32664c_async_sample_fetch_raw(const struct device *dev, uint32_t green_samples[16], uint32_t ir_samples[16], uint32_t red_samples[16], uint32_t *num_samples, uint8_t *chip_op_mode)
{
    struct max32664c_data *data = dev->data;
    const struct max32664c_config *config = dev->config;

    uint8_t wr_buf[2] = {0x12, 0x01};
    static uint8_t buf[2048];
    static int sample_len = 24;

    uint8_t hub_stat = max32664c_read_hub_status(dev);
    if (hub_stat & MAX32664C_HUB_STAT_DRDY_MASK)
    {
        int fifo_count = max32664c_get_fifo_count(dev);

        // printk("F: %d | ", fifo_count);

        if (fifo_count > 16)
        {
            fifo_count = 8;
        }

        *num_samples = fifo_count;

        if (fifo_count > 0)
        {
            *chip_op_mode = data->op_mode;

            gpio_pin_set_dt(&config->mfio_gpio, 0);
            k_sleep(K_USEC(300));
            i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));

            i2c_read_dt(&config->i2c, buf, ((sample_len * fifo_count) + MAX32664C_SENSOR_DATA_OFFSET));
            k_sleep(K_USEC(300));
            gpio_pin_set_dt(&config->mfio_gpio, 1);

            for (int i = 0; i < fifo_count; i++)
            {
                uint32_t led_green = (uint32_t)buf[(sample_len * i) + 0 + MAX32664C_SENSOR_DATA_OFFSET] << 16;
                led_green |= (uint32_t)buf[(sample_len * i) + 1 + MAX32664C_SENSOR_DATA_OFFSET] << 8;
                led_green |= (uint32_t)buf[(sample_len * i) + 2 + MAX32664C_SENSOR_DATA_OFFSET];

                green_samples[i] = led_green;

                uint32_t led_ir = (uint32_t)buf[(sample_len * i) + 3 + MAX32664C_SENSOR_DATA_OFFSET] << 16;
                led_ir |= (uint32_t)buf[(sample_len * i) + 4 + MAX32664C_SENSOR_DATA_OFFSET] << 8;
                led_ir |= (uint32_t)buf[(sample_len * i) + 5 + MAX32664C_SENSOR_DATA_OFFSET];

                ir_samples[i] = led_ir;

                uint32_t led_red = (uint32_t)buf[(sample_len * i) + 6 + MAX32664C_SENSOR_DATA_OFFSET] << 16;
                led_red |= (uint32_t)buf[(sample_len * i) + 7 + MAX32664C_SENSOR_DATA_OFFSET] << 8;
                led_red |= (uint32_t)buf[(sample_len * i) + 8 + MAX32664C_SENSOR_DATA_OFFSET];

                red_samples[i] = led_red;
            }
        }
    }
    return 0;
}

static int max32664c_async_sample_fetch(const struct device *dev, uint32_t green_samples[16], uint32_t ir_samples[16], uint32_t red_samples[16],
                                        uint32_t *num_samples, uint16_t *spo2, uint8_t *spo2_conf, uint8_t *spo2_valid_percent_complete, uint8_t *spo2_low_quality,
                                        uint8_t *spo2_excessive_motion, uint8_t *spo2_low_pi, uint8_t *spo2_state, uint16_t *hr, uint8_t *hr_conf, uint16_t *rtor,
                                        uint8_t *rtor_conf, uint8_t *scd_state, uint8_t *activity_class, uint32_t *steps_run, uint32_t *steps_walk, uint8_t *chip_op_mode)
{
    struct max32664c_data *data = dev->data;
    const struct max32664c_config *config = dev->config;

    uint8_t wr_buf[2] = {0x12, 0x01};
    static uint8_t buf[2048];

    static int sample_len = 62;

#define MAX32664C_ALGO_DATA_OFFSET 24

    uint8_t hub_stat = max32664c_read_hub_status(dev);
    // int fifo_count = max32664c_get_fifo_count(dev);
    if (hub_stat & MAX32664C_HUB_STAT_DRDY_MASK)
    {
        // printk("DRDY ");
        int fifo_count = max32664c_get_fifo_count(dev);
        // printk("AL F: %d | ", fifo_count);

        if (fifo_count > 16)
        {
            fifo_count = 16;
        }

        *num_samples = fifo_count;

        if (fifo_count > 0)
        {
            if (data->op_mode == MAX32664C_OP_MODE_ALGO_AGC || data->op_mode == MAX32664C_OP_MODE_ALGO_AEC)
            {
                sample_len = 48; // 18 PPG + 6 accel data + 24 algo
            }
            else if (data->op_mode == MAX32664C_OP_MODE_ALGO_EXTENDED)
            {
                sample_len = 70; // 18 data + 52 algo
            }

            *chip_op_mode = data->op_mode;

            gpio_pin_set_dt(&config->mfio_gpio, 0);
            k_sleep(K_USEC(300));
            i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));

            i2c_read_dt(&config->i2c, buf, ((sample_len * fifo_count) + MAX32664C_SENSOR_DATA_OFFSET));
            k_sleep(K_USEC(300));
            gpio_pin_set_dt(&config->mfio_gpio, 1);

            for (int i = 0; i < fifo_count; i++)
            {
                uint32_t led_green = (uint32_t)buf[(sample_len * i) + 0 + MAX32664C_SENSOR_DATA_OFFSET] << 16;
                led_green |= (uint32_t)buf[(sample_len * i) + 1 + MAX32664C_SENSOR_DATA_OFFSET] << 8;
                led_green |= (uint32_t)buf[(sample_len * i) + 2 + MAX32664C_SENSOR_DATA_OFFSET];

                green_samples[i] = led_green;

                uint32_t led_ir = (uint32_t)buf[(sample_len * i) + 3 + MAX32664C_SENSOR_DATA_OFFSET] << 16;
                led_ir |= (uint32_t)buf[(sample_len * i) + 4 + MAX32664C_SENSOR_DATA_OFFSET] << 8;
                led_ir |= (uint32_t)buf[(sample_len * i) + 5 + MAX32664C_SENSOR_DATA_OFFSET];

                ir_samples[i] = led_ir;

                uint32_t led_red = (uint32_t)buf[(sample_len * i) + 6 + MAX32664C_SENSOR_DATA_OFFSET] << 16;
                led_red |= (uint32_t)buf[(sample_len * i) + 7 + MAX32664C_SENSOR_DATA_OFFSET] << 8;
                led_red |= (uint32_t)buf[(sample_len * i) + 8 + MAX32664C_SENSOR_DATA_OFFSET];

                red_samples[i] = led_red;

                uint16_t hr_val = (uint16_t)buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 1 + MAX32664C_SENSOR_DATA_OFFSET] << 8;
                hr_val |= (uint16_t)buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 2 + MAX32664C_SENSOR_DATA_OFFSET];

                *hr = (hr_val / 10);

                *hr_conf = buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 3 + MAX32664C_SENSOR_DATA_OFFSET];

                uint16_t rtor_val = (uint16_t)buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 4 + MAX32664C_SENSOR_DATA_OFFSET] << 8;
                rtor_val |= (uint16_t)buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 5 + MAX32664C_SENSOR_DATA_OFFSET];

                *rtor = (rtor_val / 10);

                *rtor_conf = buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 6 + MAX32664C_SENSOR_DATA_OFFSET];

                *spo2_conf = buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 10 + MAX32664C_SENSOR_DATA_OFFSET];

                uint16_t spo2_val = (uint16_t)buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 11 + MAX32664C_SENSOR_DATA_OFFSET] << 8;
                spo2_val |= (uint16_t)buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 12 + MAX32664C_SENSOR_DATA_OFFSET];

                *spo2 = (spo2_val / 10);

                *spo2_valid_percent_complete = buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 13 + MAX32664C_SENSOR_DATA_OFFSET];
                *spo2_low_quality = buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 14 + MAX32664C_SENSOR_DATA_OFFSET];
                *spo2_excessive_motion = buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 15 + MAX32664C_SENSOR_DATA_OFFSET];
                *spo2_low_pi = buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 16 + MAX32664C_SENSOR_DATA_OFFSET];
                *spo2_state = buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 18 + MAX32664C_SENSOR_DATA_OFFSET];

                *scd_state = buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 19 + MAX32664C_SENSOR_DATA_OFFSET];

                /*
                else if (data->op_mode == MAX32664C_OP_MODE_ALGO_EXTENDED)
                {
                    uint16_t hr_val = (uint16_t)buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 1 + MAX32664C_SENSOR_DATA_OFFSET] << 8;
                    hr_val |= (uint16_t)buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 2 + MAX32664C_SENSOR_DATA_OFFSET];

                    *hr = (hr_val / 10);

                    uint16_t rtor_val = (uint16_t)buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 4 + MAX32664C_SENSOR_DATA_OFFSET] << 8;
                    rtor_val |= (uint16_t)buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 5 + MAX32664C_SENSOR_DATA_OFFSET];

                    *rtor = (rtor_val / 10);

                    uint16_t spo2_val = (uint16_t)buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 44 + MAX32664C_SENSOR_DATA_OFFSET] << 8;
                    spo2_val |= (uint16_t)buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 45 + MAX32664C_SENSOR_DATA_OFFSET];

                    *spo2 = (spo2_val / 10);

                    uint32_t walk_steps = (uint32_t)(buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 8 + MAX32664C_SENSOR_DATA_OFFSET] << 24 | buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 9 + MAX32664C_SENSOR_DATA_OFFSET] << 16 | buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 10 + MAX32664C_SENSOR_DATA_OFFSET] << 8 | buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 11 + MAX32664C_SENSOR_DATA_OFFSET]);

                    *steps_walk = walk_steps;

                    uint32_t run_steps = (uint32_t)(buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 12 + MAX32664C_SENSOR_DATA_OFFSET] << 24 | buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 13 + MAX32664C_SENSOR_DATA_OFFSET] << 16 | buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 14 + MAX32664C_SENSOR_DATA_OFFSET] << 8 | buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 15 + MAX32664C_SENSOR_DATA_OFFSET]);

                    *steps_run = run_steps;
                }*/
            }
        }
    }
    return 0;
}

int max32664c_submit(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe)
{
    struct max32664c_data *data = dev->data;
    const struct rtio_sqe *sqe = &iodev_sqe->sqe;
    int rc;
    
    // Check if operation is already in progress
    if (!atomic_cas(&data->async.in_progress, 0, 1)) {
        LOG_DBG("Operation already in progress");
        rtio_iodev_sqe_err(iodev_sqe, -EBUSY);
        return -EBUSY;
    }
    
    // Validate operation
    if (sqe->op != RTIO_OP_RX) {
        LOG_ERR("Unsupported operation: %d", sqe->op);
        atomic_clear(&data->async.in_progress);
        rtio_iodev_sqe_err(iodev_sqe, -ENOTSUP);
        return -ENOTSUP;
    }
    
    // Check if sensor is in idle mode
    if (data->op_mode == MAX32664C_OP_MODE_IDLE) {
        LOG_ERR("Sensor is in idle mode");
        atomic_clear(&data->async.in_progress);
        rtio_iodev_sqe_err(iodev_sqe, -ENODEV);
        return -ENODEV;
    }
    
    // Store the pending SQE
    data->async.pending_sqe = iodev_sqe;
    
    // Enable interrupts if not already enabled
    rc = max32664c_stream_enable(dev);
    if (rc != 0) {
        LOG_ERR("Failed to enable stream: %d", rc);
        data->async.pending_sqe = NULL;
        atomic_clear(&data->async.in_progress);
        rtio_iodev_sqe_err(iodev_sqe, rc);
        return rc;
    }
    
    // Check if data is already available
    uint8_t hub_status = max32664c_read_hub_status(dev);
    if (hub_status & MAX32664C_HUB_STAT_DRDY_MASK) {
        LOG_DBG("Data ready, triggering immediate processing");
        k_work_schedule(&data->async.work, K_NO_WAIT);
    }
    
    return 0;
}