/* RTIO submit implementation moved from max32664c_async.c
 * Provides non-blocking RTIO submission and completion callback.
 */

/* This file implements sensor driver RTIO helpers; only build when sensor API is enabled */
#ifdef CONFIG_SENSOR

#include <zephyr/rtio/work.h>
#include <zephyr/drivers/sensor_clock.h>
#include <string.h>
#include <zephyr/logging/log.h>

#include "max32664c_sensor.h"

LOG_MODULE_REGISTER(MAX32664C_RTIO, CONFIG_SENSOR_LOG_LEVEL);

#include <zephyr/kernel.h>

void max32664c_submit(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe)
{
    uint32_t min_buf_len = sizeof(struct max32664c_encoded_data);
    uint8_t *buf;
    uint32_t buf_len;

    int rc = rtio_sqe_rx_buf(iodev_sqe, min_buf_len, min_buf_len, &buf, &buf_len);
    if (rc != 0) {
        LOG_ERR("rx_buf too small for encoded frame (need %u)", min_buf_len);
        rtio_iodev_sqe_err(iodev_sqe, rc);
        return;
    }

    /* Use existing helper to fetch and parse a frame, then encode into buffer */
    uint32_t green[16] = {0}, ir[16] = {0}, red[16] = {0};
    uint32_t num_samples = 0;
    uint16_t spo2 = 0, hr = 0, rtor = 0;
    uint8_t spo2_conf = 0, spo2_vpc = 0, spo2_lq = 0, spo2_motion = 0, spo2_low_pi = 0, spo2_state = 0;
    uint8_t hr_conf = 0, rtor_conf = 0, scd_state = 0, activity_class = 0, chip_op_mode = 0;
    uint32_t steps_run = 0, steps_walk = 0;

    rc = max32664c_async_sample_fetch(dev, green, ir, red,
                                      &num_samples, &spo2, &spo2_conf, &spo2_vpc, &spo2_lq,
                                      &spo2_motion, &spo2_low_pi, &spo2_state, &hr, &hr_conf, &rtor,
                                      &rtor_conf, &scd_state, &activity_class, &steps_run, &steps_walk, &chip_op_mode);

    struct max32664c_encoded_data *edata = (struct max32664c_encoded_data *)buf;
    memset(edata, 0, sizeof(*edata));
    edata->header.timestamp = (uint64_t)k_uptime_get();
    edata->chip_op_mode = chip_op_mode;
    edata->num_samples = num_samples;

    /* Clamp copies to struct capacity (32) */
    uint32_t copy_n = num_samples;
    if (copy_n > 32) {
        copy_n = 32;
    }
    for (uint32_t i = 0; i < copy_n; i++) {
        edata->green_samples[i] = green[i];
        edata->red_samples[i] = red[i];
        edata->ir_samples[i] = ir[i];
    }

    edata->hr = hr;
    edata->hr_confidence = hr_conf;
    edata->spo2 = spo2;
    edata->spo2_confidence = spo2_conf;
    edata->spo2_valid_percent_complete = spo2_vpc;
    edata->spo2_low_quality = spo2_lq;
    edata->spo2_excessive_motion = spo2_motion;
    edata->spo2_low_pi = spo2_low_pi;
    edata->spo2_state = spo2_state;
    edata->rtor = rtor;
    edata->rtor_confidence = rtor_conf;
    edata->scd_state = scd_state;
    edata->activity_class = activity_class;
    edata->steps_run = steps_run;
    edata->steps_walk = steps_walk;

    if (rc < 0) {
        /* Even on error, we provide an encoded header so callers can decide; also signal error */
        LOG_DBG("async_sample_fetch failed: %d", rc);
        rtio_iodev_sqe_err(iodev_sqe, rc);
    } else {
        rtio_iodev_sqe_ok(iodev_sqe, 0);
    }
}


int max32664c_get_fifo_count(const struct device *dev)
{
    const struct max32664c_config *config = dev->config;
    struct max32664c_data *data = dev->data;
    uint8_t rd_buf[2] = {0x00, 0x00};
    uint8_t wr_buf[2] = {0x12, 0x00};
    uint8_t fifo_count = 0;

    struct rtio *ctx = data->r;
    struct rtio_iodev *iodev = data->iodev;
    struct rtio_sqe *write_sqe, *read_sqe;
    struct rtio_cqe *cqe;
    int err = 0;

    /* If RTIO is not available, fall back to blocking I2C */
    if (!ctx || !iodev) {
        gpio_pin_set_dt(&config->mfio_gpio, 0);
        k_sleep(K_USEC(300));
        max32664c_i2c_write(dev, wr_buf, sizeof(wr_buf));
        max32664c_i2c_read(dev, rd_buf, sizeof(rd_buf));
        gpio_pin_set_dt(&config->mfio_gpio, 1);
        fifo_count = rd_buf[1];
        return (int)fifo_count;
    }

    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));

    /* Submit write SQE */
    write_sqe = rtio_sqe_acquire(ctx);
    if (!write_sqe) {
        gpio_pin_set_dt(&config->mfio_gpio, 1);
        return -ENOMEM;
    }

    rtio_sqe_prep_tiny_write(write_sqe, iodev, RTIO_PRIO_HIGH, wr_buf, sizeof(wr_buf), NULL);
    write_sqe->iodev_flags |= RTIO_IODEV_I2C_STOP;

    err = rtio_submit(ctx, 0);
    if (err) {
        gpio_pin_set_dt(&config->mfio_gpio, 1);
        return err;
    }

    /* Wait for write completion */
    cqe = rtio_cqe_consume_block(ctx);
    if (cqe != NULL) {
        if (cqe->result < 0) {
            err = cqe->result;
        }
        rtio_cqe_release(ctx, cqe);
    }
    while ((cqe = rtio_cqe_consume(ctx)) != NULL) {
        if (cqe->result < 0) {
            err = cqe->result;
        }
        rtio_cqe_release(ctx, cqe);
    }

    if (err < 0) {
        gpio_pin_set_dt(&config->mfio_gpio, 1);
        return err;
    }

    /* Small device command delay before read */
    k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));

    /* Submit read SQE */
    read_sqe = rtio_sqe_acquire(ctx);
    if (!read_sqe) {
        gpio_pin_set_dt(&config->mfio_gpio, 1);
        return -ENOMEM;
    }

    rtio_sqe_prep_read(read_sqe, iodev, RTIO_PRIO_HIGH, rd_buf, sizeof(rd_buf), NULL);
    read_sqe->iodev_flags |= RTIO_IODEV_I2C_STOP;

    err = rtio_submit(ctx, 0);
    if (err) {
        gpio_pin_set_dt(&config->mfio_gpio, 1);
        return err;
    }

    /* Wait for read completion */
    cqe = rtio_cqe_consume_block(ctx);
    if (cqe != NULL) {
        if (cqe->result < 0) {
            err = cqe->result;
        }
        rtio_cqe_release(ctx, cqe);
    }
    while ((cqe = rtio_cqe_consume(ctx)) != NULL) {
        if (cqe->result < 0) {
            err = cqe->result;
        }
        rtio_cqe_release(ctx, cqe);
    }

    gpio_pin_set_dt(&config->mfio_gpio, 1);

    if (err < 0) {
        return err;
    }

    fifo_count = rd_buf[1];
    return (int)fifo_count;
}


/* Converted blocking sample-fetch moved here. This function keeps the same
 * prototype as the previous helper so callers can call it directly. It will
 * use RTIO if available or fall back to blocking I2C inside.
 */
int max32664c_async_sample_fetch(const struct device *dev, uint32_t green_samples[16], uint32_t ir_samples[16], uint32_t red_samples[16],
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
    if (hub_stat & MAX32664C_HUB_STAT_DRDY_MASK)
    {
        int fifo_count = max32664c_get_fifo_count(dev);

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

            struct rtio *ctx = data->r;
            struct rtio_iodev *iodev = data->iodev;
            struct rtio_sqe *write_sqe = NULL, *read_sqe = NULL;
            struct rtio_cqe *cqe = NULL;
            int err = 0;
            size_t read_len = (sample_len * fifo_count) + MAX32664C_SENSOR_DATA_OFFSET;

            if (read_len > sizeof(buf)) {
                /* clamp to buffer size */
                read_len = sizeof(buf);
            }

            /* Use RTIO if available; otherwise fall back to blocking I2C */
            if (ctx && iodev) {
                gpio_pin_set_dt(&config->mfio_gpio, 0);
                k_sleep(K_USEC(300));

                /* write command SQE */
                write_sqe = rtio_sqe_acquire(ctx);
                if (!write_sqe) {
                    gpio_pin_set_dt(&config->mfio_gpio, 1);
                    return -ENOMEM;
                }
                rtio_sqe_prep_tiny_write(write_sqe, iodev, RTIO_PRIO_HIGH, wr_buf, sizeof(wr_buf), NULL);
                write_sqe->iodev_flags |= RTIO_IODEV_I2C_STOP;

                err = rtio_submit(ctx, 0);
                if (err) {
                    gpio_pin_set_dt(&config->mfio_gpio, 1);
                    return err;
                }

                /* wait for write completion */
                cqe = rtio_cqe_consume_block(ctx);
                if (cqe != NULL) {
                    if (cqe->result < 0) {
                        err = cqe->result;
                    }
                    rtio_cqe_release(ctx, cqe);
                }
                while ((cqe = rtio_cqe_consume(ctx)) != NULL) {
                    if (cqe->result < 0) {
                        err = cqe->result;
                    }
                    rtio_cqe_release(ctx, cqe);
                }

                if (err < 0) {
                    gpio_pin_set_dt(&config->mfio_gpio, 1);
                    return err;
                }

                k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));

                /* read SQE into local buf */
                read_sqe = rtio_sqe_acquire(ctx);
                if (!read_sqe) {
                    gpio_pin_set_dt(&config->mfio_gpio, 1);
                    return -ENOMEM;
                }
                rtio_sqe_prep_read(read_sqe, iodev, RTIO_PRIO_HIGH, buf, read_len, NULL);
                read_sqe->iodev_flags |= RTIO_IODEV_I2C_STOP;

                err = rtio_submit(ctx, 0);
                if (err) {
                    gpio_pin_set_dt(&config->mfio_gpio, 1);
                    return err;
                }

                /* wait for read completion */
                cqe = rtio_cqe_consume_block(ctx);
                if (cqe != NULL) {
                    if (cqe->result < 0) {
                        err = cqe->result;
                    }
                    rtio_cqe_release(ctx, cqe);
                }
                while ((cqe = rtio_cqe_consume(ctx)) != NULL) {
                    if (cqe->result < 0) {
                        err = cqe->result;
                    }
                    rtio_cqe_release(ctx, cqe);
                }

                gpio_pin_set_dt(&config->mfio_gpio, 1);

                if (err < 0) {
                    return err;
                }

                /* successful RTIO read filled `buf` */
            } else {
                /* fallback blocking path */
                gpio_pin_set_dt(&config->mfio_gpio, 0);
                k_sleep(K_USEC(300));
                max32664c_i2c_write(dev, wr_buf, sizeof(wr_buf));

                max32664c_i2c_read(dev, buf, ((sample_len * fifo_count) + MAX32664C_SENSOR_DATA_OFFSET));
                k_sleep(K_USEC(300));
                gpio_pin_set_dt(&config->mfio_gpio, 1);
            }

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
            }
        }
    }
    return 0;
}

#endif /* CONFIG_SENSOR */
