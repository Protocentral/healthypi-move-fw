/*
 * HealthyPi Move
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 *
 * MAXM86146 Async/RTIO Driver Implementation
 * Based on MAX32664 Sensor Hub protocol - adapted from MAX32664C driver
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(MAXM86146_ASYNC, CONFIG_SENSOR_LOG_LEVEL);

#include "maxm86146.h"

#define MAXM86146_SENSOR_DATA_OFFSET 1

/* shared FIFO read buffer for async operations */
static uint8_t maxm86146_fifo_buf[2048];

/* Helper to read FIFO via I2C while toggling MFIO. Returns 0 on success. */
static int maxm86146_read_fifo_i2c(const struct device *dev, uint8_t *buf, int sample_len, int fifo_count)
{
    const struct maxm86146_config *config = dev->config;
    uint8_t wr_buf[2] = {0x12, 0x01};
    if (fifo_count <= 0 || buf == NULL) {
        return -EINVAL;
    }

    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));

    int rc = maxm86146_i2c_write(&config->i2c, wr_buf, sizeof(wr_buf));
    if (rc != 0) {
        gpio_pin_set_dt(&config->mfio_gpio, 1);
        LOG_ERR("I2C write (FIFO read cmd) failed: %d", rc);
        return rc;
    }

    rc = maxm86146_i2c_read(&config->i2c, buf, ((sample_len * fifo_count) + MAXM86146_SENSOR_DATA_OFFSET));
    if (rc != 0) {
        gpio_pin_set_dt(&config->mfio_gpio, 1);
        LOG_ERR("I2C read (FIFO data) failed: %d", rc);
        return rc;
    }

    k_sleep(K_USEC(300));
    gpio_pin_set_dt(&config->mfio_gpio, 1);
    return 0;
}

int maxm86146_get_fifo_count(const struct device *dev)
{
    const struct maxm86146_config *config = dev->config;
    uint8_t rd_buf[2] = {0x00, 0x00};
    uint8_t wr_buf[2] = {0x12, 0x00};

    uint8_t fifo_count = 0;

    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));

    int rc = maxm86146_i2c_write(&config->i2c, wr_buf, sizeof(wr_buf));
    if (rc != 0) {
        gpio_pin_set_dt(&config->mfio_gpio, 1);
        LOG_ERR("I2C write (get fifo count) failed: %d", rc);
        return rc;
    }

    rc = maxm86146_i2c_read(&config->i2c, rd_buf, sizeof(rd_buf));
    if (rc != 0) {
        gpio_pin_set_dt(&config->mfio_gpio, 1);
        LOG_ERR("I2C read (get fifo count) failed: %d", rc);
        return rc;
    }

    k_sleep(K_USEC(300));
    gpio_pin_set_dt(&config->mfio_gpio, 1);

    fifo_count = rd_buf[1];

    return (int)fifo_count;
}

static int maxm86146_async_sample_fetch_scd(const struct device *dev, uint8_t *chip_op_mode, uint8_t *scd_state)
{
    struct maxm86146_data *data = dev->data;

    int sample_len = 1;

    uint8_t hub_stat = maxm86146_read_hub_status(dev);
    if (hub_stat & MAXM86146_HUB_STAT_DRDY_MASK)
    {
        int fifo_count = maxm86146_get_fifo_count(dev);

        if (fifo_count > 8)
        {
            fifo_count = 8;
        }

        if (fifo_count > 0)
        {
            sample_len = 1;
            *chip_op_mode = data->op_mode;

            maxm86146_read_fifo_i2c(dev, maxm86146_fifo_buf, sample_len, fifo_count);
            for (int i = 0; i < fifo_count; i++)
            {
                uint8_t scd_state_val = (uint8_t)maxm86146_fifo_buf[(sample_len * i) + 0 + MAXM86146_SENSOR_DATA_OFFSET];
                *scd_state = scd_state_val;
            }
        }
    }

    return 0;
}

int maxm86146_async_sample_fetch_wake_on_motion(const struct device *dev, uint8_t *chip_op_mode)
{
    int sample_len = 7;

    uint8_t hub_stat = maxm86146_read_hub_status(dev);
    if (hub_stat & MAXM86146_HUB_STAT_DRDY_MASK)
    {
        int fifo_count = maxm86146_get_fifo_count(dev);

        if (fifo_count > 8)
        {
            fifo_count = 8;
        }

        if (fifo_count > 0)
        {
            *chip_op_mode = MAXM86146_OP_MODE_WAKE_ON_MOTION;

            maxm86146_read_fifo_i2c(dev, maxm86146_fifo_buf, sample_len, fifo_count);
            for (int i = 0; i < fifo_count; i++)
            {
                uint8_t algo_op_mode = (uint8_t)maxm86146_fifo_buf[(sample_len * i) + 0 + MAXM86146_SENSOR_DATA_OFFSET];

                int16_t accel_x = (int16_t)((maxm86146_fifo_buf[(sample_len * i) + 1 + MAXM86146_SENSOR_DATA_OFFSET] << 8) |
                                            maxm86146_fifo_buf[(sample_len * i) + 2 + MAXM86146_SENSOR_DATA_OFFSET]);
                int16_t accel_y = (int16_t)((maxm86146_fifo_buf[(sample_len * i) + 3 + MAXM86146_SENSOR_DATA_OFFSET] << 8) |
                                            maxm86146_fifo_buf[(sample_len * i) + 4 + MAXM86146_SENSOR_DATA_OFFSET]);
                int16_t accel_z = (int16_t)((maxm86146_fifo_buf[(sample_len * i) + 5 + MAXM86146_SENSOR_DATA_OFFSET] << 8) |
                                            maxm86146_fifo_buf[(sample_len * i) + 6 + MAXM86146_SENSOR_DATA_OFFSET]);

                int32_t magnitude = (int32_t)accel_x * accel_x + (int32_t)accel_y * accel_y + (int32_t)accel_z * accel_z;

                LOG_DBG("Motion poll: mode=%d, accel=[%d,%d,%d], mag=%d", algo_op_mode, accel_x, accel_y, accel_z, magnitude);
            }
        }
        else
        {
            *chip_op_mode = MAXM86146_OP_MODE_WAKE_ON_MOTION;
            LOG_DBG("Wake-on-motion: No FIFO data, continuing to poll");
        }
    }
    else
    {
        *chip_op_mode = MAXM86146_OP_MODE_WAKE_ON_MOTION;
        LOG_DBG("Wake-on-motion: No data ready, continuing to poll");
    }
    return 0;
}

/*
 * MAXM86146 Sensor Data Format (42 bytes total - FIXED regardless of slot config):
 * ---------------------------------------------------------------------------------
 * PPG Data (36 bytes = 12 PPG channels × 3 bytes each):
 *   PD1 channels (18 bytes):
 *     PPG1  (PD1) bytes 0-2:   Green counts
 *     PPG2  (PD1) bytes 3-5:   N/A
 *     PPG3  (PD1) bytes 6-8:   N/A
 *     PPG4  (PD1) bytes 9-11:  N/A
 *     PPG5  (PD1) bytes 12-14: N/A
 *     PPG6  (PD1) bytes 15-17: N/A
 *   PD2 channels (18 bytes):
 *     PPG7  (PD2) bytes 18-20: N/A
 *     PPG8  (PD2) bytes 21-23: Red counts
 *     PPG9  (PD2) bytes 24-26: IR counts
 *     PPG10 (PD2) bytes 27-29: N/A
 *     PPG11 (PD2) bytes 30-32: N/A
 *     PPG12 (PD2) bytes 33-35: N/A
 * Accelerometer (6 bytes):
 *   X @ bytes 36-37, Y @ bytes 38-39, Z @ bytes 40-41
 */

/* PPG data byte offsets for MAXM86146 (from datasheet) */
#define MAXM86146_PPG_GREEN_OFFSET  0   /* PPG1 (PD1) */
#define MAXM86146_PPG_RED_OFFSET    21  /* PPG8 (PD2) */
#define MAXM86146_PPG_IR_OFFSET     24  /* PPG9 (PD2) */

/* Sensor data sample length: 36 PPG + 6 accel = 42 bytes (FIXED) */
#define MAXM86146_RAW_SAMPLE_LEN    42

static int maxm86146_async_sample_fetch_raw(const struct device *dev, uint32_t green_samples[16], uint32_t ir_samples[16], uint32_t red_samples[16], uint32_t *num_samples, uint8_t *chip_op_mode)
{
    struct maxm86146_data *data = dev->data;

    int sample_len = MAXM86146_RAW_SAMPLE_LEN;

    uint8_t hub_stat = maxm86146_read_hub_status(dev);
    if (hub_stat & MAXM86146_HUB_STAT_DRDY_MASK)
    {
        int fifo_count = maxm86146_get_fifo_count(dev);

        if (fifo_count > 16)
        {
            fifo_count = 8;
        }

        *num_samples = fifo_count;

        if (fifo_count > 0)
        {
            *chip_op_mode = data->op_mode;

            maxm86146_read_fifo_i2c(dev, maxm86146_fifo_buf, sample_len, fifo_count);

            for (int i = 0; i < fifo_count; i++)
            {
                /* Green LED - PPG1 (PD1) - bytes 0-2 */
                uint32_t led_green = (uint32_t)maxm86146_fifo_buf[(sample_len * i) + MAXM86146_PPG_GREEN_OFFSET + MAXM86146_SENSOR_DATA_OFFSET] << 16;
                led_green |= (uint32_t)maxm86146_fifo_buf[(sample_len * i) + MAXM86146_PPG_GREEN_OFFSET + 1 + MAXM86146_SENSOR_DATA_OFFSET] << 8;
                led_green |= (uint32_t)maxm86146_fifo_buf[(sample_len * i) + MAXM86146_PPG_GREEN_OFFSET + 2 + MAXM86146_SENSOR_DATA_OFFSET];
                green_samples[i] = (led_green >> 4);

                /* IR LED - PPG9 (PD2) - bytes 24-26 */
                uint32_t led_ir = (uint32_t)maxm86146_fifo_buf[(sample_len * i) + MAXM86146_PPG_IR_OFFSET + MAXM86146_SENSOR_DATA_OFFSET] << 16;
                led_ir |= (uint32_t)maxm86146_fifo_buf[(sample_len * i) + MAXM86146_PPG_IR_OFFSET + 1 + MAXM86146_SENSOR_DATA_OFFSET] << 8;
                led_ir |= (uint32_t)maxm86146_fifo_buf[(sample_len * i) + MAXM86146_PPG_IR_OFFSET + 2 + MAXM86146_SENSOR_DATA_OFFSET];
                ir_samples[i] = (led_ir >> 4);

                /* Red LED - PPG8 (PD2) - bytes 21-23 */
                uint32_t led_red = (uint32_t)maxm86146_fifo_buf[(sample_len * i) + MAXM86146_PPG_RED_OFFSET + MAXM86146_SENSOR_DATA_OFFSET] << 16;
                led_red |= (uint32_t)maxm86146_fifo_buf[(sample_len * i) + MAXM86146_PPG_RED_OFFSET + 1 + MAXM86146_SENSOR_DATA_OFFSET] << 8;
                led_red |= (uint32_t)maxm86146_fifo_buf[(sample_len * i) + MAXM86146_PPG_RED_OFFSET + 2 + MAXM86146_SENSOR_DATA_OFFSET];
                red_samples[i] = (led_red >> 4);
            }
        }
    }
    return 0;
}

/*
 * MAXM86146 Algorithm Mode Sample Lengths:
 * -----------------------------------------
 * For firmware 33.13.31 (MAXM86146), the algorithm data is SHORTER than MAX32664C:
 * - IBI Offset, Unreliable orientation flag, and RESERVED (4 bytes) are NOT sent
 * - Normal algo data: 20 bytes (not 24)
 * - Extended algo data: 52 bytes (not 56)
 *
 * ALGO AEC/AGC:  62 bytes = 42 sensor + 20 algo
 * ALGO EXTENDED: 94 bytes = 42 sensor + 52 algo
 *
 * Algorithm data starts at byte offset 42 (after sensor data).
 */

/* Algorithm data starts after sensor data (42 bytes) */
#define MAXM86146_ALGO_DATA_OFFSET 42

/* Sample lengths for algorithm modes (firmware 33.13.31) */
#define MAXM86146_ALGO_AEC_SAMPLE_LEN      62  /* 42 sensor + 20 algo */
#define MAXM86146_ALGO_EXTENDED_SAMPLE_LEN 94  /* 42 sensor + 52 algo */

static int maxm86146_async_sample_fetch(const struct device *dev, uint32_t green_samples[16], uint32_t ir_samples[16], uint32_t red_samples[16],
                                        uint32_t *num_samples, uint16_t *spo2, uint8_t *spo2_conf, uint8_t *spo2_valid_percent_complete, uint8_t *spo2_low_quality,
                                        uint8_t *spo2_excessive_motion, uint8_t *spo2_low_pi, uint8_t *spo2_state, uint16_t *hr, uint8_t *hr_conf, uint16_t *rtor,
                                        uint8_t *rtor_conf, uint8_t *scd_state, uint8_t *activity_class, uint32_t *steps_run, uint32_t *steps_walk, uint8_t *chip_op_mode)
{
    struct maxm86146_data *data = dev->data;

    int sample_len;

    uint8_t hub_stat = maxm86146_read_hub_status(dev);
    if (hub_stat & MAXM86146_HUB_STAT_DRDY_MASK)
    {
        int fifo_count = maxm86146_get_fifo_count(dev);

        if (fifo_count > 16)
        {
            fifo_count = 16;
        }

        *num_samples = fifo_count;

        if (fifo_count > 0)
        {
            if (data->op_mode == MAXM86146_OP_MODE_ALGO_AGC || data->op_mode == MAXM86146_OP_MODE_ALGO_AEC)
            {
                sample_len = MAXM86146_ALGO_AEC_SAMPLE_LEN;  /* 66 bytes */
            }
            else if (data->op_mode == MAXM86146_OP_MODE_ALGO_EXTENDED)
            {
                sample_len = MAXM86146_ALGO_EXTENDED_SAMPLE_LEN;  /* 98 bytes */
            }
            else
            {
                sample_len = MAXM86146_ALGO_AEC_SAMPLE_LEN;  /* Default to 66 */
            }

            *chip_op_mode = data->op_mode;

            maxm86146_read_fifo_i2c(dev, maxm86146_fifo_buf, sample_len, fifo_count);

            for (int i = 0; i < fifo_count; i++)
            {
                /* Green LED - PPG1 (PD1) - bytes 0-2 */
                uint32_t led_green = (uint32_t)maxm86146_fifo_buf[(sample_len * i) + MAXM86146_PPG_GREEN_OFFSET + MAXM86146_SENSOR_DATA_OFFSET] << 16;
                led_green |= (uint32_t)maxm86146_fifo_buf[(sample_len * i) + MAXM86146_PPG_GREEN_OFFSET + 1 + MAXM86146_SENSOR_DATA_OFFSET] << 8;
                led_green |= (uint32_t)maxm86146_fifo_buf[(sample_len * i) + MAXM86146_PPG_GREEN_OFFSET + 2 + MAXM86146_SENSOR_DATA_OFFSET];
                green_samples[i] = (led_green >> 4);

                /* IR LED - PPG9 (PD2) - bytes 24-26 */
                uint32_t led_ir = (uint32_t)maxm86146_fifo_buf[(sample_len * i) + MAXM86146_PPG_IR_OFFSET + MAXM86146_SENSOR_DATA_OFFSET] << 16;
                led_ir |= (uint32_t)maxm86146_fifo_buf[(sample_len * i) + MAXM86146_PPG_IR_OFFSET + 1 + MAXM86146_SENSOR_DATA_OFFSET] << 8;
                led_ir |= (uint32_t)maxm86146_fifo_buf[(sample_len * i) + MAXM86146_PPG_IR_OFFSET + 2 + MAXM86146_SENSOR_DATA_OFFSET];
                ir_samples[i] = (led_ir >> 4);

                /* Red LED - PPG8 (PD2) - bytes 21-23 */
                uint32_t led_red = (uint32_t)maxm86146_fifo_buf[(sample_len * i) + MAXM86146_PPG_RED_OFFSET + MAXM86146_SENSOR_DATA_OFFSET] << 16;
                led_red |= (uint32_t)maxm86146_fifo_buf[(sample_len * i) + MAXM86146_PPG_RED_OFFSET + 1 + MAXM86146_SENSOR_DATA_OFFSET] << 8;
                led_red |= (uint32_t)maxm86146_fifo_buf[(sample_len * i) + MAXM86146_PPG_RED_OFFSET + 2 + MAXM86146_SENSOR_DATA_OFFSET];
                red_samples[i] = (led_red >> 4);

                /* Algorithm output data starts at MAXM86146_ALGO_DATA_OFFSET (byte 42) */
                int algo_base = (sample_len * i) + MAXM86146_ALGO_DATA_OFFSET + MAXM86146_SENSOR_DATA_OFFSET;

                uint16_t hr_val = (uint16_t)maxm86146_fifo_buf[algo_base + 1] << 8;
                hr_val |= (uint16_t)maxm86146_fifo_buf[algo_base + 2];

                *hr = (hr_val / 10);

                *hr_conf = maxm86146_fifo_buf[algo_base + 3];

                uint16_t rtor_val = (uint16_t)maxm86146_fifo_buf[algo_base + 4] << 8;
                rtor_val |= (uint16_t)maxm86146_fifo_buf[algo_base + 5];

                *rtor = (rtor_val / 10);

                *rtor_conf = maxm86146_fifo_buf[algo_base + 6];

                *spo2_conf = maxm86146_fifo_buf[algo_base + 10];

                uint16_t spo2_val = (uint16_t)maxm86146_fifo_buf[algo_base + 11] << 8;
                spo2_val |= (uint16_t)maxm86146_fifo_buf[algo_base + 12];

                *spo2 = (spo2_val / 10);

                *spo2_valid_percent_complete = maxm86146_fifo_buf[algo_base + 13];
                *spo2_low_quality = maxm86146_fifo_buf[algo_base + 14];
                *spo2_excessive_motion = maxm86146_fifo_buf[algo_base + 15];
                *spo2_low_pi = maxm86146_fifo_buf[algo_base + 16];
                *spo2_state = maxm86146_fifo_buf[algo_base + 18];

                *scd_state = maxm86146_fifo_buf[algo_base + 19];

                /* Debug: log parsed values for first sample */
                if (i == 0)
                {
                    LOG_DBG("MAXM86146 algo: hr=%d, spo2=%d, spo2_prog=%d%%, scd=%d, state=%d",
                            *hr, *spo2, *spo2_valid_percent_complete, *scd_state, *spo2_state);
                }
            }
        }
    }
    return 0;
}

void maxm86146_submit(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe)
{
    uint32_t min_buf_len = sizeof(struct maxm86146_encoded_data);
    int rc;
    uint8_t *buf;
    uint32_t buf_len;

    struct maxm86146_encoded_data *m_edata;
    struct maxm86146_data *data = dev->data;

    rc = rtio_sqe_rx_buf(iodev_sqe, min_buf_len, min_buf_len, &buf, &buf_len);
    if (rc != 0)
    {
        LOG_ERR("Failed to get a read buffer of size %u bytes", min_buf_len);
        rtio_iodev_sqe_err(iodev_sqe, rc);
        return;
    }

    if (data->op_mode == MAXM86146_OP_MODE_ALGO_AGC || data->op_mode == MAXM86146_OP_MODE_ALGO_AEC ||
        data->op_mode == MAXM86146_OP_MODE_ALGO_EXTENDED)
    {
        m_edata = (struct maxm86146_encoded_data *)buf;
        m_edata->header.timestamp = k_ticks_to_ns_floor64(k_uptime_ticks());
        rc = maxm86146_async_sample_fetch(dev, m_edata->green_samples, m_edata->ir_samples, m_edata->red_samples,
                                          &m_edata->num_samples, &m_edata->spo2, &m_edata->spo2_confidence, &m_edata->spo2_valid_percent_complete,
                                          &m_edata->spo2_low_quality, &m_edata->spo2_excessive_motion, &m_edata->spo2_low_pi, &m_edata->spo2_state,
                                          &m_edata->hr, &m_edata->hr_confidence, &m_edata->rtor, &m_edata->rtor_confidence, &m_edata->scd_state,
                                          &m_edata->activity_class, &m_edata->steps_run, &m_edata->steps_walk, &m_edata->chip_op_mode);
    }
    else if (data->op_mode == MAXM86146_OP_MODE_RAW)
    {
        m_edata = (struct maxm86146_encoded_data *)buf;
        m_edata->header.timestamp = k_ticks_to_ns_floor64(k_uptime_ticks());
        rc = maxm86146_async_sample_fetch_raw(dev, m_edata->green_samples, m_edata->ir_samples, m_edata->red_samples, &m_edata->num_samples, &m_edata->chip_op_mode);
    }
    else if (data->op_mode == MAXM86146_OP_MODE_SCD)
    {
        m_edata = (struct maxm86146_encoded_data *)buf;
        m_edata->header.timestamp = k_ticks_to_ns_floor64(k_uptime_ticks());
        rc = maxm86146_async_sample_fetch_scd(dev, &m_edata->chip_op_mode, &m_edata->scd_state);
    }
    else if (data->op_mode == MAXM86146_OP_MODE_WAKE_ON_MOTION)
    {
        m_edata = (struct maxm86146_encoded_data *)buf;
        m_edata->header.timestamp = k_ticks_to_ns_floor64(k_uptime_ticks());
        rc = maxm86146_async_sample_fetch_wake_on_motion(dev, &m_edata->chip_op_mode);
    }
    else if (data->op_mode == MAXM86146_OP_MODE_IDLE)
    {
        /* Idle mode, do nothing */
    }
    else
    {
        LOG_ERR("Invalid operation mode %d", data->op_mode);
    }

    if (rc != 0)
    {
        rtio_iodev_sqe_err(iodev_sqe, rc);
        return;
    }

    rtio_iodev_sqe_ok(iodev_sqe, 0);
    return;
}
