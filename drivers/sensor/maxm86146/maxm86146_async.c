#include <zephyr/drivers/sensor.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MAXM86146_ASYNC, CONFIG_SENSOR_LOG_LEVEL);

#include "maxm86146.h"

int maxm86146_get_fifo_count(const struct device *dev)
{
    const struct maxm86146_config *config = dev->config;
    uint8_t rd_buf[2] = {0x00, 0x00};
    uint8_t wr_buf[2] = {0x12, 0x00};

    uint8_t fifo_count;
    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));

    i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
    i2c_read_dt(&config->i2c, rd_buf, sizeof(rd_buf));

    gpio_pin_set_dt(&config->mfio_gpio, 1);

    fifo_count = rd_buf[1];
    return (int)fifo_count;
}

static int maxm86146_async_sample_fetch(const struct device *dev, uint32_t green_samples[16], uint32_t ir_samples[16], uint32_t red_samples[16],
                                        uint32_t *num_samples, uint16_t *spo2, uint16_t *hr, uint16_t *rtor, uint8_t *scd_state, uint8_t *activity_class,
                                        uint32_t *steps_run, uint32_t *steps_walk)
{
    struct maxm86146_data *data = dev->data;
    const struct maxm86146_config *config = dev->config;

    uint8_t wr_buf[2] = {0x12, 0x01};
    static uint8_t buf[2048];

    static int sample_len = 62;

#define MAXM86146_SENSOR_DATA_OFFSET 1
#define MAXM86146_ALGO_DATA_OFFSET 42

    uint8_t hub_stat = maxm86146_read_hub_status(dev);
    if (hub_stat & MAXM86146_HUB_STAT_DRDY_MASK)
    {
        // printk("DRDY ");
        int fifo_count = maxm86146_get_fifo_count(dev);
        // printk("F: %d | ", fifo_count);

        if (fifo_count > 16)
        {
            fifo_count = 16;
        }

        *num_samples = fifo_count;

        if (fifo_count > 0)
        {
            if (data->op_mode == MAXM86146_OP_MODE_ALGO)
            {
                sample_len = 62; // 42 data + 20 algo
            }
            else if (data->op_mode == MAXM86146_OP_MODE_ALGO_EXTENDED)
            {
                sample_len = 94; // 42 data + 52 algo
            }

            gpio_pin_set_dt(&config->mfio_gpio, 0);
            k_sleep(K_USEC(300));
            i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));

            i2c_read_dt(&config->i2c, buf, ((sample_len * fifo_count) + MAXM86146_SENSOR_DATA_OFFSET));
            // k_sleep(K_USEC(300));
            gpio_pin_set_dt(&config->mfio_gpio, 1);

            for (int i = 0; i < fifo_count; i++)
            {
                uint32_t led_green = (uint32_t)buf[(sample_len * i) + 0 + MAXM86146_SENSOR_DATA_OFFSET] << 16;
                led_green |= (uint32_t)buf[(sample_len * i) + 1 + MAXM86146_SENSOR_DATA_OFFSET] << 8;
                led_green |= (uint32_t)buf[(sample_len * i) + 2 + MAXM86146_SENSOR_DATA_OFFSET];

                green_samples[i] = led_green;

                uint32_t led_red = (uint32_t)buf[(sample_len * i) + 21 + MAXM86146_SENSOR_DATA_OFFSET] << 16;
                led_red |= (uint32_t)buf[(sample_len * i) + 22 + MAXM86146_SENSOR_DATA_OFFSET] << 8;
                led_red |= (uint32_t)buf[(sample_len * i) + 23 + MAXM86146_SENSOR_DATA_OFFSET];

                red_samples[i] = led_red;

                uint32_t led_ir = (uint32_t)buf[(sample_len * i) + 24] << 16;
                led_ir |= (uint32_t)buf[(sample_len * i) + 25] << 8;
                led_ir |= (uint32_t)buf[(sample_len * i) + 26];

                ir_samples[i] = led_ir;

                if (data->op_mode == MAXM86146_OP_MODE_ALGO)
                {

                    uint16_t hr_val = (uint16_t)buf[(sample_len * i) + MAXM86146_ALGO_DATA_OFFSET + 1 + MAXM86146_SENSOR_DATA_OFFSET] << 8;
                    hr_val |= (uint16_t)buf[(sample_len * i) + MAXM86146_ALGO_DATA_OFFSET + 2 + MAXM86146_SENSOR_DATA_OFFSET];

                    *hr = (hr_val / 10);

                    uint16_t spo2_val = (uint16_t)buf[(sample_len * i) + MAXM86146_ALGO_DATA_OFFSET + 11 + MAXM86146_SENSOR_DATA_OFFSET] << 8;
                    spo2_val |= (uint16_t)buf[(sample_len * i) + MAXM86146_ALGO_DATA_OFFSET + 12 + MAXM86146_SENSOR_DATA_OFFSET];

                    *spo2 = (spo2_val / 10);

                    uint16_t rtor_val = (uint16_t)buf[(sample_len * i) + MAXM86146_ALGO_DATA_OFFSET + 4 + MAXM86146_SENSOR_DATA_OFFSET] << 8;
                    rtor_val |= (uint16_t)buf[(sample_len * i) + MAXM86146_ALGO_DATA_OFFSET + 5 + MAXM86146_SENSOR_DATA_OFFSET];

                    *rtor = (rtor_val / 10);

                    *scd_state = buf[(sample_len * i) + MAXM86146_ALGO_DATA_OFFSET + 19 + MAXM86146_SENSOR_DATA_OFFSET];
                }
                else if (data->op_mode == MAXM86146_OP_MODE_ALGO_EXTENDED)
                {
                    uint16_t hr_val = (uint16_t)buf[(sample_len * i) + MAXM86146_ALGO_DATA_OFFSET + 1 + MAXM86146_SENSOR_DATA_OFFSET] << 8;
                    hr_val |= (uint16_t)buf[(sample_len * i) + MAXM86146_ALGO_DATA_OFFSET + 2 + MAXM86146_SENSOR_DATA_OFFSET];

                    *hr = (hr_val / 10);

                    uint16_t rtor_val = (uint16_t)buf[(sample_len * i) + MAXM86146_ALGO_DATA_OFFSET + 4 + MAXM86146_SENSOR_DATA_OFFSET] << 8;
                    rtor_val |= (uint16_t)buf[(sample_len * i) + MAXM86146_ALGO_DATA_OFFSET + 5 + MAXM86146_SENSOR_DATA_OFFSET];

                    *rtor = (rtor_val / 10);

                    uint16_t spo2_val = (uint16_t)buf[(sample_len * i) + MAXM86146_ALGO_DATA_OFFSET + 44 + MAXM86146_SENSOR_DATA_OFFSET] << 8;
                    spo2_val |= (uint16_t)buf[(sample_len * i) + MAXM86146_ALGO_DATA_OFFSET + 45 + MAXM86146_SENSOR_DATA_OFFSET];

                    *spo2 = (spo2_val / 10);

                    uint32_t walk_steps = (uint32_t)(buf[(sample_len * i) + MAXM86146_ALGO_DATA_OFFSET + 8 + MAXM86146_SENSOR_DATA_OFFSET] << 24 
                    | buf[(sample_len * i) + MAXM86146_ALGO_DATA_OFFSET + 9 + MAXM86146_SENSOR_DATA_OFFSET] << 16 
                    | buf[(sample_len * i) + MAXM86146_ALGO_DATA_OFFSET + 10 + MAXM86146_SENSOR_DATA_OFFSET] << 8 
                    | buf[(sample_len * i) + MAXM86146_ALGO_DATA_OFFSET + 11 + MAXM86146_SENSOR_DATA_OFFSET]);

                    *steps_walk = walk_steps;

                    uint32_t run_steps = (uint32_t)(buf[(sample_len * i) + MAXM86146_ALGO_DATA_OFFSET + 12 + MAXM86146_SENSOR_DATA_OFFSET] << 24
                    | buf[(sample_len * i) + MAXM86146_ALGO_DATA_OFFSET + 13 + MAXM86146_SENSOR_DATA_OFFSET] << 16
                    | buf[(sample_len * i) + MAXM86146_ALGO_DATA_OFFSET + 14 + MAXM86146_SENSOR_DATA_OFFSET] << 8
                    | buf[(sample_len * i) + MAXM86146_ALGO_DATA_OFFSET + 15 + MAXM86146_SENSOR_DATA_OFFSET]);

                    *steps_run = run_steps;
                }
            }
        }
    }
}

int maxm86146_submit(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe)
{
    uint32_t min_buf_len = sizeof(struct maxm86146_encoded_data);
    int rc;
    uint8_t *buf;
    uint32_t buf_len;

    // struct maxm86146_encoded_data *edata;
    // struct maxm86146_enc_calib_data *calib_data;

    struct maxm86146_encoded_data *m_edata;

    struct maxm86146_data *data = dev->data;

    /* Get the buffer for the frame, it may be allocated dynamically by the rtio context */
    rc = rtio_sqe_rx_buf(iodev_sqe, min_buf_len, min_buf_len, &buf, &buf_len);
    if (rc != 0)
    {
        LOG_ERR("Failed to get a read buffer of size %u bytes", min_buf_len);
        rtio_iodev_sqe_err(iodev_sqe, rc);
        return rc;
    }

    // printk("Fetching samples...\n");
    if ((data->op_mode == MAXM86146_OP_MODE_ALGO) || (data->op_mode == MAXM86146_OP_MODE_ALGO_EXTENDED))
    {
        m_edata = (struct maxm86146_encoded_data *)buf;
        m_edata->header.timestamp = k_ticks_to_ns_floor64(k_uptime_ticks());
        rc = maxm86146_async_sample_fetch(dev, m_edata->green_samples, m_edata->ir_samples, m_edata->red_samples,
                                          &m_edata->num_samples, &m_edata->spo2, &m_edata->hr, &m_edata->rtor, &m_edata->scd_state,
                                          &m_edata->activity_class, &m_edata->steps_run, &m_edata->steps_walk);
        // printk("Device is in idle mode\n");
    }
    else
    {
        // printk("Invalid operation mode\n");
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