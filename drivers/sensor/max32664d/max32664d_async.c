#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(max32664d_async, CONFIG_SENSOR_LOG_LEVEL);

#include "max32664d.h"

#define MAX32664D_SENSOR_DATA_OFFSET 1

static int max32664_async_sample_fetch(const struct device *dev,
                                       uint32_t ir_samples[32], uint32_t red_samples[32], uint8_t *num_samples, uint16_t *spo2, uint8_t *spo2_conf,
                                       uint16_t *hr, uint8_t *bpt_status, uint8_t *bpt_progress, uint8_t *bpt_sys, uint8_t *bpt_dia)
{
    struct max32664d_data *data = dev->data;
    const struct max32664d_config *config = dev->config;

    uint8_t wr_buf[2] = {0x12, 0x01};
    static uint8_t buf[2048];

    static int sample_len = 29;
    uint64_t start_time = k_uptime_get();
    uint64_t timeout_ms = 1000;

    uint8_t hub_stat = max32664d_read_hub_status(dev);
    while (!(hub_stat & MAX32664D_HUB_STAT_DRDY_MASK))
    {
        hub_stat = max32664d_read_hub_status(dev);

        if (k_uptime_get() - start_time > timeout_ms)
        {
            LOG_ERR("Timeout waiting for DRDY flag");
            return -ETIMEDOUT; // Return a timeout error code
        }
    }

    if (hub_stat & MAX32664D_HUB_STAT_DRDY_MASK)
    {
        int fifo_count = max32664d_get_fifo_count(dev);
        // printk("F: %d | ", fifo_count);

        if (fifo_count > 32)
        {
            fifo_count = 32;
        }

        *num_samples = fifo_count;

        if (fifo_count > 0)
        {
            if (data->op_mode == MAX32664D_OP_MODE_RAW)
            {
                sample_len = 12;
            }
            else if ((data->op_mode == MAX32664D_OP_MODE_BPT_EST) || (data->op_mode == MAX32664D_OP_MODE_BPT_CAL_START))
            {
                sample_len = 29;
            }

            i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
            // k_sleep(K_USEC(300));
            i2c_read_dt(&config->i2c, buf, ((sample_len * fifo_count) + MAX32664D_SENSOR_DATA_OFFSET));

            for (int i = 0; i < fifo_count; i++)
            {
                uint32_t led_ir = (uint32_t)buf[(sample_len * i) + MAX32664D_SENSOR_DATA_OFFSET] << 16;
                led_ir |= (uint32_t)buf[(sample_len * i) + 1 + MAX32664D_SENSOR_DATA_OFFSET] << 8;
                led_ir |= (uint32_t)buf[(sample_len * i) + 2 + MAX32664D_SENSOR_DATA_OFFSET];

                ir_samples[i] = led_ir;

                uint32_t led_red = (uint32_t)buf[(sample_len * i) + 3 + MAX32664D_SENSOR_DATA_OFFSET] << 16;
                led_red |= (uint32_t)buf[(sample_len * i) + 4 + MAX32664D_SENSOR_DATA_OFFSET] << 8;
                led_red |= (uint32_t)buf[(sample_len * i) + 5 + MAX32664D_SENSOR_DATA_OFFSET];

                red_samples[i] = led_red;

                // bytes 7,8,9, 10,11,12 are ignored

                *bpt_status = buf[(sample_len * i) + 12 + MAX32664D_SENSOR_DATA_OFFSET];
                *bpt_progress = buf[(sample_len * i) + 13 + MAX32664D_SENSOR_DATA_OFFSET];

                uint16_t bpt_hr = (uint16_t)buf[(sample_len * i) + 14 + MAX32664D_SENSOR_DATA_OFFSET] << 8;
                bpt_hr |= (uint16_t)buf[(sample_len * i) + 15 + MAX32664D_SENSOR_DATA_OFFSET];

                *hr = (bpt_hr / 10);

                *bpt_sys = buf[(sample_len * i) + 16 + MAX32664D_SENSOR_DATA_OFFSET];
                *bpt_dia = buf[(sample_len * i) + 17 + MAX32664D_SENSOR_DATA_OFFSET];

                uint16_t bpt_spo2 = (uint16_t)buf[(sample_len * i) + 18 + MAX32664D_SENSOR_DATA_OFFSET] << 8;
                bpt_spo2 |= (uint16_t)buf[(sample_len * i) + 19 + MAX32664D_SENSOR_DATA_OFFSET];

                *spo2 = (bpt_spo2 / 10);
                *spo2_conf = buf[(sample_len * i) + 25 + MAX32664D_SENSOR_DATA_OFFSET]; 

                uint16_t spo2_r_val = (uint16_t)buf[(sample_len * i) + 20 + MAX32664D_SENSOR_DATA_OFFSET] << 8;
                spo2_r_val |= (uint16_t)buf[(sample_len * i) + 21 + MAX32664D_SENSOR_DATA_OFFSET];

                
            }
        }
    }

    return 0;
}

int max32664d_submit(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe)
{
    uint32_t min_buf_len = sizeof(struct max32664d_encoded_data);
    int rc;
    uint8_t *buf;
    uint32_t buf_len;

    struct max32664d_encoded_data *edata;
    struct max32664_enc_calib_data *calib_data;
    struct max32664d_data *data = dev->data;

    /* Get the buffer for the frame, it may be allocated dynamically by the rtio context */
    rc = rtio_sqe_rx_buf(iodev_sqe, min_buf_len, min_buf_len, &buf, &buf_len);
    if (rc != 0)
    {
        LOG_ERR("Failed to get a read buffer of size %u bytes", min_buf_len);
        rtio_iodev_sqe_err(iodev_sqe, rc);
        return rc;
    }

    if ((data->op_mode == MAX32664D_OP_MODE_BPT_EST) || (data->op_mode == MAX32664D_OP_MODE_RAW) || (data->op_mode == MAX32664D_OP_MODE_BPT_CAL_START))
    {
        edata = (struct max32664d_encoded_data *)buf;
        edata->header.timestamp = k_ticks_to_ns_floor64(k_uptime_ticks());
        rc = max32664_async_sample_fetch(dev, edata->ir_samples, edata->red_samples, &edata->num_samples, &edata->spo2, &edata->spo2_conf,
                                         &edata->hr, &edata->bpt_status, &edata->bpt_progress, &edata->bpt_sys, &edata->bpt_dia);
    }
    else
    {
        LOG_ERR("Invalid operation mode\n");
        // return 4;
    }

    if (rc != 0)
    {
        // LOG_ERR("Failed: %d", rc);
        rtio_iodev_sqe_err(iodev_sqe, rc);
        return rc;
    }

    rtio_iodev_sqe_ok(iodev_sqe, 0);

    return 0;
}