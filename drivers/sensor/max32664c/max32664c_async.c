/*
 * HealthyPi Move
 * 
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Author: Ashwin Whitchurch, Protocentral Electronics
 * Contact: ashwin@protocentral.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(MAX32664C_ASYNC, CONFIG_SENSOR_LOG_LEVEL);

#include "max32664c.h"

#define MAX32664C_SENSOR_DATA_OFFSET 1

/* shared FIFO read buffer for async operations */
static uint8_t max32664c_fifo_buf[2048];

/* Helper to read FIFO via I2C while toggling MFIO. Returns 0 on success. */
static int max32664c_read_fifo_i2c(const struct device *dev, uint8_t *buf, int sample_len, int fifo_count)
{
    const struct max32664c_config *config = dev->config;
    uint8_t wr_buf[2] = {0x12, 0x01};
    if (fifo_count <= 0 || buf == NULL) {
        return -EINVAL;
    }

    /* FIFO read attempt (debug removed) */

    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));

    int rc = max32664c_i2c_write(&config->i2c, wr_buf, sizeof(wr_buf));
    if (rc != 0) {
        gpio_pin_set_dt(&config->mfio_gpio, 1);
        LOG_ERR("I2C write (FIFO read cmd) failed: %d", rc);
        return rc;
    }

    rc = max32664c_i2c_read(&config->i2c, buf, ((sample_len * fifo_count) + MAX32664C_SENSOR_DATA_OFFSET));
    if (rc != 0) {
        gpio_pin_set_dt(&config->mfio_gpio, 1);
        LOG_ERR("I2C read (FIFO data) failed: %d", rc);
        return rc;
    }

    k_sleep(K_USEC(300));
    gpio_pin_set_dt(&config->mfio_gpio, 1);
    return 0;
}

int max32664c_get_fifo_count(const struct device *dev)
{
    const struct max32664c_config *config = dev->config;
    uint8_t rd_buf[2] = {0x00, 0x00};
    uint8_t wr_buf[2] = {0x12, 0x00};

    uint8_t fifo_count = 0;

    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));

    int rc = max32664c_i2c_write(&config->i2c, wr_buf, sizeof(wr_buf));
    if (rc != 0) {
        gpio_pin_set_dt(&config->mfio_gpio, 1);
        LOG_ERR("I2C write (get fifo count) failed: %d", rc);
        return rc;
    }

    rc = max32664c_i2c_read(&config->i2c, rd_buf, sizeof(rd_buf));
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

static int max32664c_async_sample_fetch_scd(const struct device *dev, uint8_t *chip_op_mode, uint8_t *scd_state)
{
    struct max32664c_data *data = dev->data;
    const struct max32664c_config *config = dev->config;

    int sample_len = 1;

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

            max32664c_read_fifo_i2c(dev, max32664c_fifo_buf, sample_len, fifo_count);
            for (int i = 0; i < fifo_count; i++)
            {
                uint8_t scd_state_val = (uint8_t)max32664c_fifo_buf[(sample_len * i) + 0 + MAX32664C_SENSOR_DATA_OFFSET];
                *scd_state = scd_state_val;
            }
        }
    }

    if (hub_stat & MAX32664C_HUB_STAT_SCD_MASK)
    {
        // printk("SCD ");
    }
    return 0;
}

int max32664c_async_sample_fetch_wake_on_motion(const struct device *dev, uint8_t *chip_op_mode)
{
    struct max32664c_data *data = dev->data;
    const struct max32664c_config *config = dev->config;

    int sample_len = 7; // 1 byte op mode + 6 bytes accel data
    bool motion_detected = false;

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
            *chip_op_mode = MAX32664C_OP_MODE_WAKE_ON_MOTION;

            /* Read FIFO into shared buffer */
            max32664c_read_fifo_i2c(dev, max32664c_fifo_buf, sample_len, fifo_count);
            for (int i = 0; i < fifo_count; i++)
            {
                uint8_t algo_op_mode = (uint8_t)max32664c_fifo_buf[(sample_len * i) + 0 + MAX32664C_SENSOR_DATA_OFFSET];

                int16_t accel_x = (int16_t)((max32664c_fifo_buf[(sample_len * i) + 1 + MAX32664C_SENSOR_DATA_OFFSET] << 8) |
                                            max32664c_fifo_buf[(sample_len * i) + 2 + MAX32664C_SENSOR_DATA_OFFSET]);
                int16_t accel_y = (int16_t)((max32664c_fifo_buf[(sample_len * i) + 3 + MAX32664C_SENSOR_DATA_OFFSET] << 8) |
                                            max32664c_fifo_buf[(sample_len * i) + 4 + MAX32664C_SENSOR_DATA_OFFSET]);
                int16_t accel_z = (int16_t)((max32664c_fifo_buf[(sample_len * i) + 5 + MAX32664C_SENSOR_DATA_OFFSET] << 8) |
                                            max32664c_fifo_buf[(sample_len * i) + 6 + MAX32664C_SENSOR_DATA_OFFSET]);

                int32_t magnitude = (int32_t)accel_x * accel_x + (int32_t)accel_y * accel_y + (int32_t)accel_z * accel_z;
                if (magnitude > 1000)
                {
                    motion_detected = true;
                }

                LOG_DBG("Motion poll: mode=%d, accel=[%d,%d,%d], mag=%d", algo_op_mode, accel_x, accel_y, accel_z, magnitude);
            }
        }
        else
        {
            // Even if no FIFO data, still set the chip mode for polling
            *chip_op_mode = MAX32664C_OP_MODE_WAKE_ON_MOTION;
            LOG_DBG("Wake-on-motion: No FIFO data, continuing to poll");
        }
    }
    else
    {
        // No data ready, but still set mode for continuous polling
        *chip_op_mode = MAX32664C_OP_MODE_WAKE_ON_MOTION;
        LOG_DBG("Wake-on-motion: No data ready, continuing to poll");
    }
    return 0;
}

static int max32664c_async_sample_fetch_raw(const struct device *dev, uint32_t green_samples[16], uint32_t ir_samples[16], uint32_t red_samples[16], uint32_t *num_samples, uint8_t *chip_op_mode)
{
    struct max32664c_data *data = dev->data;
    const struct max32664c_config *config = dev->config;

    int sample_len = 24;

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

            max32664c_read_fifo_i2c(dev, max32664c_fifo_buf, sample_len, fifo_count);

            for (int i = 0; i < fifo_count; i++)
            {
                uint32_t led_green = (uint32_t)max32664c_fifo_buf[(sample_len * i) + 0 + MAX32664C_SENSOR_DATA_OFFSET] << 16;
                led_green |= (uint32_t)max32664c_fifo_buf[(sample_len * i) + 1 + MAX32664C_SENSOR_DATA_OFFSET] << 8;
                led_green |= (uint32_t)max32664c_fifo_buf[(sample_len * i) + 2 + MAX32664C_SENSOR_DATA_OFFSET];
                /* Normalize assembled 24-bit value down by 4 bits to provide canonical scale to UI */
                green_samples[i] = (led_green >> 4);

                uint32_t led_ir = (uint32_t)max32664c_fifo_buf[(sample_len * i) + 3 + MAX32664C_SENSOR_DATA_OFFSET] << 16;
                led_ir |= (uint32_t)max32664c_fifo_buf[(sample_len * i) + 4 + MAX32664C_SENSOR_DATA_OFFSET] << 8;
                led_ir |= (uint32_t)max32664c_fifo_buf[(sample_len * i) + 5 + MAX32664C_SENSOR_DATA_OFFSET];
                /* Normalize assembled 24-bit value down by 4 bits to provide canonical scale to UI */
                ir_samples[i] = (led_ir >> 4);

                uint32_t led_red = (uint32_t)max32664c_fifo_buf[(sample_len * i) + 6 + MAX32664C_SENSOR_DATA_OFFSET] << 16;
                led_red |= (uint32_t)max32664c_fifo_buf[(sample_len * i) + 7 + MAX32664C_SENSOR_DATA_OFFSET] << 8;
                led_red |= (uint32_t)max32664c_fifo_buf[(sample_len * i) + 8 + MAX32664C_SENSOR_DATA_OFFSET];
                /* Normalize assembled 24-bit value down by 4 bits to provide canonical scale to UI */
                red_samples[i] = (led_red >> 4);
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

            /* Read FIFO into shared buffer */
            max32664c_read_fifo_i2c(dev, max32664c_fifo_buf, sample_len, fifo_count);

            /*
             * Datasheet note: the MAX32664 provides LED samples as 24-bit MSB-first
             * bytes in the FIFO. The ADC effective resolution is 20 bits; here
             * assemble the 24-bit words and right-shift by 4 bits so upper layers
             * receive 20-bit right-aligned integers matching the datasheet.
             */
            for (int i = 0; i < fifo_count; i++)
            {
                uint32_t led_green = (uint32_t)max32664c_fifo_buf[(sample_len * i) + 0 + MAX32664C_SENSOR_DATA_OFFSET] << 16;
                led_green |= (uint32_t)max32664c_fifo_buf[(sample_len * i) + 1 + MAX32664C_SENSOR_DATA_OFFSET] << 8;
                led_green |= (uint32_t)max32664c_fifo_buf[(sample_len * i) + 2 + MAX32664C_SENSOR_DATA_OFFSET];
                /* normalize to datasheet 20-bit resolution */
                green_samples[i] = (led_green >> 4);

                uint32_t led_ir = (uint32_t)max32664c_fifo_buf[(sample_len * i) + 3 + MAX32664C_SENSOR_DATA_OFFSET] << 16;
                led_ir |= (uint32_t)max32664c_fifo_buf[(sample_len * i) + 4 + MAX32664C_SENSOR_DATA_OFFSET] << 8;
                led_ir |= (uint32_t)max32664c_fifo_buf[(sample_len * i) + 5 + MAX32664C_SENSOR_DATA_OFFSET];
                ir_samples[i] = (led_ir >> 4);

                uint32_t led_red = (uint32_t)max32664c_fifo_buf[(sample_len * i) + 6 + MAX32664C_SENSOR_DATA_OFFSET] << 16;
                led_red |= (uint32_t)max32664c_fifo_buf[(sample_len * i) + 7 + MAX32664C_SENSOR_DATA_OFFSET] << 8;
                led_red |= (uint32_t)max32664c_fifo_buf[(sample_len * i) + 8 + MAX32664C_SENSOR_DATA_OFFSET];
                red_samples[i] = (led_red >> 4);

                uint16_t hr_val = (uint16_t)max32664c_fifo_buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 1 + MAX32664C_SENSOR_DATA_OFFSET] << 8;
                hr_val |= (uint16_t)max32664c_fifo_buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 2 + MAX32664C_SENSOR_DATA_OFFSET];

                *hr = (hr_val / 10);

                *hr_conf = max32664c_fifo_buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 3 + MAX32664C_SENSOR_DATA_OFFSET];

                uint16_t rtor_val = (uint16_t)max32664c_fifo_buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 4 + MAX32664C_SENSOR_DATA_OFFSET] << 8;
                rtor_val |= (uint16_t)max32664c_fifo_buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 5 + MAX32664C_SENSOR_DATA_OFFSET];

                *rtor = (rtor_val / 10);

                *rtor_conf = max32664c_fifo_buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 6 + MAX32664C_SENSOR_DATA_OFFSET];

                *spo2_conf = max32664c_fifo_buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 10 + MAX32664C_SENSOR_DATA_OFFSET];

                uint16_t spo2_val = (uint16_t)max32664c_fifo_buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 11 + MAX32664C_SENSOR_DATA_OFFSET] << 8;
                spo2_val |= (uint16_t)max32664c_fifo_buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 12 + MAX32664C_SENSOR_DATA_OFFSET];

                *spo2 = (spo2_val / 10);

                *spo2_valid_percent_complete = max32664c_fifo_buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 13 + MAX32664C_SENSOR_DATA_OFFSET];
                *spo2_low_quality = max32664c_fifo_buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 14 + MAX32664C_SENSOR_DATA_OFFSET];
                *spo2_excessive_motion = max32664c_fifo_buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 15 + MAX32664C_SENSOR_DATA_OFFSET];
                *spo2_low_pi = max32664c_fifo_buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 16 + MAX32664C_SENSOR_DATA_OFFSET];
                *spo2_state = max32664c_fifo_buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 18 + MAX32664C_SENSOR_DATA_OFFSET];

                *scd_state = max32664c_fifo_buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 19 + MAX32664C_SENSOR_DATA_OFFSET];

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

void max32664c_submit(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe)
{
    uint32_t min_buf_len = sizeof(struct max32664c_encoded_data);
    int rc;
    uint8_t *buf;
    uint32_t buf_len;

    struct max32664c_encoded_data *m_edata;
    struct max32664c_data *data = dev->data;

    rc = rtio_sqe_rx_buf(iodev_sqe, min_buf_len, min_buf_len, &buf, &buf_len);
    if (rc != 0)
    {
        LOG_ERR("Failed to get a read buffer of size %u bytes", min_buf_len);
        rtio_iodev_sqe_err(iodev_sqe, rc);
        return;
    }

    if (data->op_mode == MAX32664C_OP_MODE_ALGO_AGC || data->op_mode == MAX32664C_OP_MODE_ALGO_AEC ||
        data->op_mode == MAX32664C_OP_MODE_ALGO_EXTENDED)
    {
        m_edata = (struct max32664c_encoded_data *)buf;
        m_edata->header.timestamp = k_ticks_to_ns_floor64(k_uptime_ticks());
        rc = max32664c_async_sample_fetch(dev, m_edata->green_samples, m_edata->ir_samples, m_edata->red_samples,
                                          &m_edata->num_samples, &m_edata->spo2, &m_edata->spo2_confidence, &m_edata->spo2_valid_percent_complete,
                                          &m_edata->spo2_low_quality, &m_edata->spo2_excessive_motion, &m_edata->spo2_low_pi, &m_edata->spo2_state,
                                          &m_edata->hr, &m_edata->hr_confidence, &m_edata->rtor, &m_edata->rtor_confidence, &m_edata->scd_state,
                                          &m_edata->activity_class, &m_edata->steps_run, &m_edata->steps_walk, &m_edata->chip_op_mode);
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
    else if (data->op_mode == MAX32664C_OP_MODE_WAKE_ON_MOTION)
    {
        m_edata = (struct max32664c_encoded_data *)buf;
        m_edata->header.timestamp = k_ticks_to_ns_floor64(k_uptime_ticks());
        rc = max32664c_async_sample_fetch_wake_on_motion(dev, &m_edata->chip_op_mode);
    }
    else if (data->op_mode == MAX32664C_OP_MODE_IDLE)
    {
        // Idle mode, do nothing, take a break
    }
    else
    {
        LOG_ERR("Invalid operation mode %d", data->op_mode);
        // return 4;
    }

    if (rc != 0)
    {
        rtio_iodev_sqe_err(iodev_sqe, rc);
        return;
    }

    rtio_iodev_sqe_ok(iodev_sqe, 0);
    return;
}