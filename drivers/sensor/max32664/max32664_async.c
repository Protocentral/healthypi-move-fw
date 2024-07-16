#include <zephyr/drivers/sensor.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MAX32664_ASYNC, CONFIG_SENSOR_LOG_LEVEL);

#include "max32664.h"

static int max32664_async_calib_fetch(const struct device *dev, uint8_t calib_vector[824])
{
    const struct max32664_config *config = dev->config;
    struct max32664_data *data = dev->data;

    static uint8_t rd_buf[1024];
    uint8_t wr_buf[3] = {0x51, 0x04, 0x03};

    i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
    k_sleep(K_USEC(300));
    i2c_read_dt(&config->i2c, rd_buf, 826);

    data->calib_vector[0] = 0x50;
    data->calib_vector[1] = 0x04;
    data->calib_vector[2] = 0x03;

    for (int i = 0; i < 824; i++)
    {
        calib_vector[i] = rd_buf[i + 2];
        data->calib_vector[i + 3] = calib_vector[i];
        printk("%x ", calib_vector[i]);
    }
    printk("Calibration vector fetched\n");

    data->op_mode = MAX32664_OP_MODE_IDLE;

    return 0;
}

static int max32664_async_sample_fetch(const struct device *dev,
                                       uint32_t ir_samples[16], uint32_t red_samples[16], uint32_t *num_samples, uint16_t *spo2,
                                       uint16_t *hr, uint8_t *bpt_status, uint8_t *bpt_progress, uint8_t *bpt_sys, uint8_t *bpt_dia)
{
    struct max32664_data *data = dev->data;
    const struct max32664_config *config = dev->config;

    uint8_t wr_buf[2] = {0x12, 0x01};
    static uint8_t buf[2048];

    uint8_t hub_stat = m_read_hub_status(dev);
    if (hub_stat & MAX32664_HUB_STAT_DRDY_MASK)
    {
        // printk("DRDY ");
        int fifo_count = max32664_get_fifo_count(dev);
        // printk("F: %d | \n", fifo_count);

        if (fifo_count > 16)
        {
            fifo_count = 16;
        }

        *num_samples = fifo_count;

        if (fifo_count > 0)
        {
            int sample_len;

            if (data->op_mode == MAX32664_OP_MODE_RAW)
            {
                sample_len = 12;
            }
            else if ((data->op_mode == MAX32664_OP_MODE_BPT) || (data->op_mode == MAX32664_OP_MODE_BPT_CAL_START))
            {
                sample_len = 23;
            }

            i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
            k_sleep(K_USEC(300));
            i2c_read_dt(&config->i2c, buf, ((sample_len * fifo_count) + 1));

            for (int i = 0; i < fifo_count; i++)
            {
                uint32_t led_ir = (uint32_t)buf[(sample_len * i) + 1] << 16;
                led_ir |= (uint32_t)buf[(sample_len * i) + 2] << 8;
                led_ir |= (uint32_t)buf[(sample_len * i) + 3];

                // samples[i].ir_sample = led_ir;
                ir_samples[i] = led_ir;

                uint32_t led_red = (uint32_t)buf[(sample_len * i) + 4] << 16;
                led_red |= (uint32_t)buf[(sample_len * i) + 5] << 8;
                led_red |= (uint32_t)buf[(sample_len * i) + 6];

                // samples[i].red_sample = led_red;
                red_samples[i] = led_red;

                // bytes 7,8,9, 10,11,12 are ignored

                if ((data->op_mode == MAX32664_OP_MODE_BPT) || (data->op_mode == MAX32664_OP_MODE_BPT_CAL_START))
                {
                    *bpt_status = buf[(sample_len * i) + 13];
                    *bpt_progress = buf[(sample_len * i) + 14];

                    uint16_t bpt_hr = (uint16_t)buf[(sample_len * i) + 15] << 8;
                    bpt_hr |= (uint16_t)buf[(sample_len * i) + 16];

                    *hr = (bpt_hr / 10);

                    *bpt_sys = buf[(sample_len * i) + 17];
                    *bpt_dia = buf[(sample_len * i) + 18];

                    uint16_t bpt_spo2 = (uint16_t)buf[(sample_len * i) + 19] << 8;
                    bpt_spo2 |= (uint16_t)buf[(sample_len * i) + 20];

                    *spo2 = (bpt_spo2 / 10);

                    uint16_t spo2_r_val = (uint16_t)buf[(sample_len * i) + 21] << 8;
                    spo2_r_val |= (uint16_t)buf[(sample_len * i) + 22];

                    // data->spo2_r_val = (spo2_r_val / 1000);
                    // data->hr_above_resting = buf[(sample_len * i) + 23];*/
                }
            }
        }
        else
        {
            printk("FIFO empty\n");
            return 4;
        }
    }
    else
    {
        // printk("FIFO empty\n");
        return 4;
    }

    return 0;
}

int max32664_submit(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe)
{
    uint32_t min_buf_len = sizeof(struct max32664_encoded_data);
    int rc;
    uint8_t *buf;
    uint32_t buf_len;
    struct max32664_encoded_data *edata;
    struct max32664_enc_calib_data *calib_data;

    struct maxm86146_encoded_data *m_edata;

    struct max32664_data *data = dev->data;

    /* Get the buffer for the frame, it may be allocated dynamically by the rtio context */
    rc = rtio_sqe_rx_buf(iodev_sqe, min_buf_len, min_buf_len, &buf, &buf_len);
    if (rc != 0)
    {
        LOG_ERR("Failed to get a read buffer of size %u bytes", min_buf_len);
        rtio_iodev_sqe_err(iodev_sqe, rc);
        return rc;
    }

    // printk("Fetching samples...\n");
    if ((data->op_mode == MAX32664_OP_MODE_BPT) || (data->op_mode == MAX32664_OP_MODE_RAW) || (data->op_mode == MAX32664_OP_MODE_BPT_CAL_START))
    {
        edata = (struct max32664_encoded_data *)buf;
        edata->header.timestamp = k_ticks_to_ns_floor64(k_uptime_ticks());
        rc = max32664_async_sample_fetch(dev, edata->ir_samples, edata->red_samples, &edata->num_samples, &edata->spo2,
                                         &edata->hr, &edata->bpt_status, &edata->bpt_progress, &edata->bpt_sys, &edata->bpt_dia);
    }
    else if (data->op_mode == MAX32664_OP_MODE_BPT_CAL_GET_VECTOR)
    {
        // Get calibration vector
        calib_data = (struct max32664_enc_calib_data *)buf;

        rc = max32664_async_calib_fetch(dev, calib_data->calib_vector);
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