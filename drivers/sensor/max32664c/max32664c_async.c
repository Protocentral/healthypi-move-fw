#include <zephyr/drivers/sensor.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MAX32664C_ASYNC, CONFIG_SENSOR_LOG_LEVEL);

#include "max32664c.h"

#define MAX32664C_SENSOR_DATA_OFFSET 0

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

static int max32664c_async_sample_fetch_scd(const struct device *dev, uint8_t chip_op_mode, uint8_t *scd_state)
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
            chip_op_mode = data->op_mode;

            gpio_pin_set_dt(&config->mfio_gpio, 0);
            k_sleep(K_USEC(300));
            i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));

            i2c_read_dt(&config->i2c, buf, ((sample_len * fifo_count) + MAX32664C_SENSOR_DATA_OFFSET));
            gpio_pin_set_dt(&config->mfio_gpio, 1);

            for (int i = 0; i < fifo_count; i++)
            {
                uint8_t scd_state_val = (uint8_t)buf[(sample_len * i) + 0 + MAX32664C_SENSOR_DATA_OFFSET];
                *scd_state = scd_state_val;
                printk("SCD: %d\n", scd_state_val);
            }
        }
    }

    if (hub_stat & MAX32664C_HUB_STAT_SCD_MASK)
    {
        // printk("SCD ");
    }
}

static int max32664c_async_sample_fetch_raw(const struct device *dev, uint32_t green_samples[16], uint32_t ir_samples[16], uint32_t red_samples[16], uint32_t *num_samples, uint8_t chip_op_mode)
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

        printk("F: %d | ", fifo_count);

        if (fifo_count > 16)
        {
            fifo_count = 16;
        }

        *num_samples = fifo_count;

        if (fifo_count > 0)
        {
            chip_op_mode = data->op_mode;

            gpio_pin_set_dt(&config->mfio_gpio, 0);
            k_sleep(K_USEC(300));
            i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));

            i2c_read_dt(&config->i2c, buf, ((sample_len * fifo_count) + MAX32664C_SENSOR_DATA_OFFSET));
            gpio_pin_set_dt(&config->mfio_gpio, 1);

            for(int i = 0; i < fifo_count; i++)
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
                                        uint32_t *num_samples, uint16_t *spo2, uint16_t *hr, uint16_t *rtor, uint8_t *scd_state, uint8_t *activity_class,
                                        uint32_t *steps_run, uint32_t *steps_walk, uint8_t chip_op_mode)
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
        printk("F: %d | ", fifo_count);

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

            chip_op_mode = data->op_mode;

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

                uint16_t spo2_val = (uint16_t)buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 11 + MAX32664C_SENSOR_DATA_OFFSET] << 8;
                spo2_val |= (uint16_t)buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 12 + MAX32664C_SENSOR_DATA_OFFSET];

                *spo2 = (spo2_val / 10);

                uint16_t rtor_val = (uint16_t)buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 4 + MAX32664C_SENSOR_DATA_OFFSET] << 8;
                rtor_val |= (uint16_t)buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 5 + MAX32664C_SENSOR_DATA_OFFSET];

                *rtor = (rtor_val / 10);

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
}

int max32664c_submit(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe)
{
    uint32_t min_buf_len = sizeof(struct max32664c_encoded_data);
    int rc;
    uint8_t *buf;
    uint32_t buf_len;

    struct max32664c_encoded_data *m_edata;
    struct max32664c_data *data = dev->data;

    /* Get the buffer for the frame, it may be allocated dynamically by the rtio context */
    rc = rtio_sqe_rx_buf(iodev_sqe, min_buf_len, min_buf_len, &buf, &buf_len);
    if (rc != 0)
    {
        LOG_ERR("Failed to get a read buffer of size %u bytes", min_buf_len);
        rtio_iodev_sqe_err(iodev_sqe, rc);
        return rc;
    }

    // printk("Fetch ");
    if (data->op_mode == MAX32664C_OP_MODE_ALGO_AGC || data->op_mode == MAX32664C_OP_MODE_ALGO_AEC ||
        data->op_mode == MAX32664C_OP_MODE_ALGO_EXTENDED)
    {
        m_edata = (struct max32664c_encoded_data *)buf;
        m_edata->header.timestamp = k_ticks_to_ns_floor64(k_uptime_ticks());
        rc = max32664c_async_sample_fetch(dev, m_edata->green_samples, m_edata->ir_samples, m_edata->red_samples,
                                          &m_edata->num_samples, &m_edata->spo2, &m_edata->hr, &m_edata->rtor, &m_edata->scd_state,
                                          &m_edata->activity_class, &m_edata->steps_run, &m_edata->steps_walk, &m_edata->chip_op_mode);
        // printk("Device is in idle mode\n");
    }
    else if (data->op_mode == MAX32664C_OP_MODE_RAW)
    {
        m_edata = (struct max32664c_encoded_data *)buf;
        m_edata->header.timestamp = k_ticks_to_ns_floor64(k_uptime_ticks());
        rc = max32664c_async_sample_fetch_raw(dev, m_edata->green_samples, m_edata->ir_samples, m_edata->red_samples, &m_edata->num_samples, &m_edata->chip_op_mode);
    }
    else if (data->op_mode == MAX32664C_OP_MODE_SCD)
    {
        m_edata = (struct max32664c_encoded_data *)buf;
        m_edata->header.timestamp = k_ticks_to_ns_floor64(k_uptime_ticks());
        rc = max32664c_async_sample_fetch_scd(dev, &m_edata->chip_op_mode, &m_edata->scd_state);
    }
    else
    {
        LOG_ERR("Invalid operation mode");
        // return 4;
    }

    if (rc != 0)
    {
        // LOG_ERR("Failed to fetch samples");
        rtio_iodev_sqe_err(iodev_sqe, rc);
        return rc;
    }

    rtio_iodev_sqe_ok(iodev_sqe, 0);

    return 0;
}