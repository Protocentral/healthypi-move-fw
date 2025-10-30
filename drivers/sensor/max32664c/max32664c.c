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

#define DT_DRV_COMPAT maxim_max32664c

#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/reboot.h>
#include <nrfx.h>
#include <errno.h>

#ifdef CONFIG_SENSOR_ASYNC_API
#include <zephyr/rtio/rtio.h>
#endif

#include "max32664c.h"

LOG_MODULE_REGISTER(MAX32664C, CONFIG_MAX32664C_LOG_LEVEL);

#define CALIBVECTOR_SIZE 827 // Command 3 bytes + 824 bytes of calib vectors
#define DATE_TIME_VECTOR_SIZE 11
#define SPO2_CAL_COEFFS_SIZE 15

#define DEFAULT_SPO2_A 1.5958422
#define DEFAULT_SPO2_B -34.659664
#define DEFAULT_SPO2_C 112.68987

#define MAX32664C_INT_THRESHOLD 0x04
#define MAX32664C_REPORT_PERIOD 0x01

/* I2C wrapper implementations - centralize error handling or logging here */

#ifdef CONFIG_SENSOR_ASYNC_API

static int max32664c_i2c_rtio_write_impl(struct rtio *r, struct rtio_iodev *iodev, const void *buf, size_t len)
{
    int rc;
    struct rtio_sqe sqe;
    struct rtio_cqe cqe;

    rtio_sqe_prep_write(&sqe, iodev, 0, (const uint8_t *)buf, (uint32_t)len, NULL);

    rc = rtio_sqe_copy_in(r, &sqe, 1);
    if (rc < 0)
    {
        return rc;
    }

    rc = rtio_submit(r, 1);
    if (rc < 0)
    {
        return rc;
    }

    /* Wait for completion and copy out one CQE */
    rc = rtio_cqe_copy_out(r, &cqe, 1, K_FOREVER);
    if (rc != 1)
    {
        return -EIO;
    }

    /* Extract result and release the CQE */
    rc = cqe.result;
    rtio_cqe_release(r, &cqe);

    return rc;
}

static int max32664c_i2c_rtio_read_impl(struct rtio *r, struct rtio_iodev *iodev, void *buf, size_t len)
{
    /* RTIO-only implementation: prepare a read SQE, copy it into the RTIO
     * context, submit and block for the completion, then return the result
     */
    int rc;
    struct rtio_sqe sqe;
    struct rtio_cqe cqe;

    rtio_sqe_prep_read(&sqe, iodev, 0, (uint8_t *)buf, (uint32_t)len, NULL);

    rc = rtio_sqe_copy_in(r, &sqe, 1);
    if (rc < 0)
    {
        return rc;
    }

    rc = rtio_submit(r, 1);
    if (rc < 0)
    {
        return rc;
    }

    /* Wait for completion */
    rc = rtio_cqe_copy_out(r, &cqe, 1, K_FOREVER);
    if (rc != 1)
    {
        return -EIO;
    }

    rc = cqe.result;
    rtio_cqe_release(r, &cqe);

    return rc;
}
#endif /* CONFIG_SENSOR_ASYNC_API */

#if defined(CONFIG_SENSOR_ASYNC_API) && defined(MAX32664C_USE_RTIO_IMPL)
static struct rtio *max32664c_rtio_ctx = NULL;
static struct rtio_iodev *max32664c_rtio_iodev = NULL;

void max32664c_register_rtio_context(struct rtio *r, struct rtio_iodev *iodev)
{
    max32664c_rtio_ctx = r;
    max32664c_rtio_iodev = iodev;
}
#endif

static int max32664c_i2c_write_impl(const struct i2c_dt_spec *i2c, const void *buf, size_t len)
{
#if defined(CONFIG_SENSOR_ASYNC_API) && defined(MAX32664C_USE_RTIO_IMPL)
    if (max32664c_rtio_ctx && max32664c_rtio_iodev)
    {
        return max32664c_i2c_rtio_write_impl(max32664c_rtio_ctx, max32664c_rtio_iodev, buf, len);
    }
#else
    return i2c_write_dt(i2c, buf, len);
#endif
}

static int max32664c_i2c_read_impl(const struct i2c_dt_spec *i2c, void *buf, size_t len)
{
#if defined(CONFIG_SENSOR_ASYNC_API) && defined(MAX32664C_USE_RTIO_IMPL)
    if (max32664c_rtio_ctx && max32664c_rtio_iodev)
    {
        return max32664c_i2c_rtio_read_impl(max32664c_rtio_ctx, max32664c_rtio_iodev, buf, len);
    }
#else
    return i2c_read_dt(i2c, buf, len);
#endif
}

/* Public wrappers used by this driver */
int max32664c_i2c_write(const struct i2c_dt_spec *i2c, const void *buf, size_t len)
{
    return max32664c_i2c_write_impl(i2c, buf, len);
}

int max32664c_i2c_read(const struct i2c_dt_spec *i2c, void *buf, size_t len)
{
    return max32664c_i2c_read_impl(i2c, buf, len);
}

#ifdef CONFIG_SENSOR_ASYNC_API
int max32664c_i2c_rtio_write(struct rtio *r, struct rtio_iodev *iodev, const void *buf, size_t len)
{
    return max32664c_i2c_rtio_write_impl(r, iodev, buf, len);
}

int max32664c_i2c_rtio_read(struct rtio *r, struct rtio_iodev *iodev, void *buf, size_t len)
{
    return max32664c_i2c_rtio_read_impl(r, iodev, buf, len);
}
#endif /* CONFIG_SENSOR_ASYNC_API */

static int m_read_op_mode(const struct device *dev)
{
    // struct max32664c_data *data = dev->data;
    const struct max32664c_config *config = dev->config;
    uint8_t rd_buf[2] = {0x00, 0x00};

    uint8_t wr_buf[2] = {0x02, 0x00};

    k_sleep(K_USEC(300));
    max32664c_i2c_write(&config->i2c, wr_buf, sizeof(wr_buf));
    k_sleep(K_MSEC(45));
    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));
    max32664c_i2c_read(&config->i2c, rd_buf, sizeof(rd_buf));
    k_sleep(K_MSEC(45));
    gpio_pin_set_dt(&config->mfio_gpio, 1);

    LOG_DBG("Op mode = %x ", rd_buf[1]);

    return rd_buf[1];
}

uint8_t max32664c_read_hub_status(const struct device *dev)
{
    /*
    Table 7. Sensor Hub Status Byte
    BIT 7 6 5 4 3 2 1 0
    Field Reserved HostAccelUfInt FifoInOverInt FifoOutOvrInt DataRdyInt Err2 Err1 Err0
    */

    const struct max32664c_config *config = dev->config;
    uint8_t rd_buf[3] = {0x00, 0x00, 0x00};
    uint8_t wr_buf[2] = {0x00, 0x00};

    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));

    max32664c_i2c_write(&config->i2c, wr_buf, sizeof(wr_buf));
    // k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));
    max32664c_i2c_read(&config->i2c, rd_buf, sizeof(rd_buf));

    k_sleep(K_USEC(300));
    gpio_pin_set_dt(&config->mfio_gpio, 1);

    LOG_DBG("Hub status bytes: %02x %02x", rd_buf[0], rd_buf[1]);

    /* Check for overflow and error bits and log at INFO/ERR as appropriate */
    if (rd_buf[1] & 0x10) {
        LOG_WRN("Hub FIFO output overflow detected (FifoOutOvrInt)");
    }
    if (rd_buf[1] & 0x20) {
        LOG_WRN("Hub FIFO input overflow detected (FifoInOverInt)");
    }
    if (rd_buf[1] & 0x01) {
        LOG_ERR("Hub reported sensor comm error (Err0)");
    }

    return rd_buf[1];
}

static int m_i2c_write_cmd_2(const struct device *dev, uint8_t byte1, uint8_t byte2)
{
    const struct max32664c_config *config = dev->config;
    uint8_t wr_buf[2];
    uint8_t rd_buf[1];

    wr_buf[0] = byte1;
    wr_buf[1] = byte2;

    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));

    max32664c_i2c_write(&config->i2c, wr_buf, sizeof(wr_buf));
    k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));
    max32664c_i2c_read(&config->i2c, rd_buf, sizeof(rd_buf));
    k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));

    gpio_pin_set_dt(&config->mfio_gpio, 1);

    LOG_DBG("CMD: %x %x | RSP: %x ", wr_buf[0], wr_buf[1], rd_buf[0]);

    k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));

    return 0;
}

static int m_i2c_write_cmd_3(const struct device *dev, uint8_t byte1, uint8_t byte2, uint8_t byte3, uint16_t cmd_delay)
{
    const struct max32664c_config *config = dev->config;
    uint8_t wr_buf[3];

    uint8_t rd_buf[1] = {0x00};

    wr_buf[0] = byte1;
    wr_buf[1] = byte2;
    wr_buf[2] = byte3;

    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));

    max32664c_i2c_write(&config->i2c, wr_buf, sizeof(wr_buf));

    k_sleep(K_MSEC(cmd_delay));

    max32664c_i2c_read(&config->i2c, rd_buf, sizeof(rd_buf));

    k_sleep(K_USEC(300));
    gpio_pin_set_dt(&config->mfio_gpio, 1);

    LOG_DBG("CMD: %x %x %x | RSP: %x ", wr_buf[0], wr_buf[1], wr_buf[2], rd_buf[0]);

    k_sleep(K_MSEC(10));

    return 0;
}

static int m_i2c_write_cmd_4(const struct device *dev, uint8_t byte1, uint8_t byte2, uint8_t byte3, uint8_t byte4, uint16_t cmd_delay)
{
    const struct max32664c_config *config = dev->config;
    uint8_t wr_buf[4];
    uint8_t rd_buf[1];

    wr_buf[0] = byte1;
    wr_buf[1] = byte2;
    wr_buf[2] = byte3;
    wr_buf[3] = byte4;

    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));

    max32664c_i2c_write(&config->i2c, wr_buf, sizeof(wr_buf));
    k_sleep(K_MSEC(cmd_delay));
    max32664c_i2c_read(&config->i2c, rd_buf, 1);
    k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));

    gpio_pin_set_dt(&config->mfio_gpio, 1);

    LOG_DBG("CMD: %x %x %x %x | RSP: %x ", wr_buf[0], wr_buf[1], wr_buf[2], wr_buf[3], rd_buf[0]);

    k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));

    return 0;
}

static int m_i2c_write_cmd_5(const struct device *dev, uint8_t byte1, uint8_t byte2, uint8_t byte3, uint8_t byte4, uint8_t byte5)
{
    const struct max32664c_config *config = dev->config;
    uint8_t wr_buf[5];
    uint8_t rd_buf[1];

    wr_buf[0] = byte1;
    wr_buf[1] = byte2;
    wr_buf[2] = byte3;
    wr_buf[3] = byte4;
    wr_buf[4] = byte5;

    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));

    max32664c_i2c_write(&config->i2c, wr_buf, sizeof(wr_buf));
    k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));
    max32664c_i2c_read(&config->i2c, rd_buf, 1);
    k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));

    gpio_pin_set_dt(&config->mfio_gpio, 1);

    LOG_DBG("CMD: %x %x %x %x %x | RSP: %x ", wr_buf[0], wr_buf[1], wr_buf[2], wr_buf[3], wr_buf[4], rd_buf[0]);

    k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));

    return 0;
}

static int m_i2c_write_cmd_6(const struct device *dev, uint8_t byte1, uint8_t byte2, uint8_t byte3, uint8_t byte4, uint8_t byte5, uint8_t byte6)
{
    const struct max32664c_config *config = dev->config;
    uint8_t wr_buf[6];
    uint8_t rd_buf[1];

    wr_buf[0] = byte1;
    wr_buf[1] = byte2;
    wr_buf[2] = byte3;
    wr_buf[3] = byte4;
    wr_buf[4] = byte5;
    wr_buf[5] = byte6;

    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));

    max32664c_i2c_write(&config->i2c, wr_buf, sizeof(wr_buf));
    k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));
    max32664c_i2c_read(&config->i2c, rd_buf, 1);
    k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));

    gpio_pin_set_dt(&config->mfio_gpio, 1);

    LOG_DBG("CMD: %x %x %x %x %x %x | RSP: %x ", wr_buf[0], wr_buf[1], wr_buf[2], wr_buf[3], wr_buf[4], wr_buf[5], rd_buf[0]);

    k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));

    return 0;
}

static int m_i2c_write(const struct device *dev, uint8_t *wr_buf, uint32_t wr_len)
{
    const struct max32664c_config *config = dev->config;

    uint8_t rd_buf[1] = {0x00};

    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));
    max32664c_i2c_write(&config->i2c, wr_buf, wr_len);

    k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));

    k_sleep(K_USEC(300));
    max32664c_i2c_read(&config->i2c, rd_buf, sizeof(rd_buf));
    k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));

    gpio_pin_set_dt(&config->mfio_gpio, 1);

    LOG_DBG("Write %d bytes | RSP: %d ", wr_len, rd_buf[0]);

    k_sleep(K_MSEC(45));

    return 0;
}

static int max32664c_set_spo2_coeffs(const struct device *dev, float a, float b, float c)
{
    uint8_t wr_buf[15] = {0x50, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xD7, 0xFB, 0xDD, 0x00, 0xAB, 0x61, 0xFE};

    // int32_t a_int = (int32_t)(a * 100000);

    /*wr_buf[3] = (a_int & 0xff000000) >> 24;
    wr_buf[4] = (a_int & 0x00ff0000) >> 16;
    wr_buf[5] = (a_int & 0x0000ff00) >> 8;
    wr_buf[6] = (a_int & 0x000000ff);

    int32_t b_int = (int32_t)(b * 100000);

    wr_buf[7] = (b_int & 0xff000000) >> 24;
    wr_buf[8] = (b_int & 0x00ff0000) >> 16;
    wr_buf[9] = (b_int & 0x0000ff00) >> 8;
    wr_buf[10] = (b_int & 0x000000ff);

    int32_t c_int = (int32_t)(c * 100000);

    wr_buf[11] = (c_int & 0xff000000) >> 24;
    wr_buf[12] = (c_int & 0x00ff0000) >> 16;
    wr_buf[13] = (c_int & 0x0000ff00) >> 8;
    wr_buf[14] = (c_int & 0x000000ff);
    */

    m_i2c_write(dev, wr_buf, sizeof(wr_buf));

    return 0;
}

static int m_i2c_write_cmd_3_rsp_3(const struct device *dev, uint8_t byte1, uint8_t byte2, uint8_t byte3, uint8_t *rsp)
{
    const struct max32664c_config *config = dev->config;
    uint8_t wr_buf[3];

    uint8_t rd_buf[3] = {0x00, 0x00, 0x00};

    wr_buf[0] = byte1;
    wr_buf[1] = byte2;
    wr_buf[2] = byte3;

    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));
    max32664c_i2c_write(&config->i2c, wr_buf, sizeof(wr_buf));

    k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));

    // gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));
    max32664c_i2c_read(&config->i2c, rd_buf, sizeof(rd_buf));
    k_sleep(K_MSEC(500));

    gpio_pin_set_dt(&config->mfio_gpio, 1);

    LOG_DBG("CMD: %x %x %x | RSP: %x %x %x ", wr_buf[0], wr_buf[1], wr_buf[2], rd_buf[0], rd_buf[1], rd_buf[2]);

    memcpy(rsp, rd_buf, 3);

    k_sleep(K_MSEC(10));

    return 0;
}

static int max32664c_check_sensors(const struct device *dev)
{
    LOG_DBG("MAX32664C checking sensors...");

    struct max32664c_data *data = dev->data;

    uint8_t rsp[3] = {0x00, 0x00, 0x00};

    max32664c_do_enter_app(dev);

    // Read MAX86141 WHOAMI
    m_i2c_write_cmd_3_rsp_3(dev, 0x41, 0x00, 0xFF, rsp);

    data->max86141_id = rsp[1];

    // LOG_INF("MAX86141 WHOAMI: %x %x %x", rsp[0], rsp[1], rsp[2]);

    if (data->max86141_id != MAX32664C_AFE_ID)
    {
        LOG_ERR("MAX86141 WHOAMI failed: %x", data->max86141_id);
    }
    else
    {
        LOG_DBG("MAX86141 WHOAMI OK: %x", data->max86141_id);
    }

    // Read Accelerometer WHOAMI
    m_i2c_write_cmd_3_rsp_3(dev, 0x41, 0x04, 0x0F, rsp);

    data->accel_id = rsp[1];

    // LOG_INF("Accelerometer WHOAMI: %x %x %x", rsp[0], rsp[1], rsp[2]);

    if (data->accel_id != MAX32664C_ACC_ID)
    {
        LOG_ERR("Accelerometer WHOAMI failed: %x", data->accel_id);
    }
    else
    {
        LOG_DBG("Accelerometer WHOAMI OK: %x", data->accel_id);
    }

    return 0;
}

static int max32664c_set_mode_extended_algo(const struct device *dev)
{
    LOG_DBG("MAX32664C entering extended ALGO mode...");

    max32664c_set_spo2_coeffs(dev, DEFAULT_SPO2_A, DEFAULT_SPO2_B, DEFAULT_SPO2_C);

    // Output mode sensor + algo data
    m_i2c_write_cmd_3(dev, 0x10, 0x00, 0x03, MAX32664C_DEFAULT_CMD_DELAY);

    // Set interrupt threshold (extended ALGO value)
    m_i2c_write_cmd_3(dev, 0x10, 0x01, MAX32664C_INT_THRESHOLD, MAX32664C_DEFAULT_CMD_DELAY);

    // Set report period
    m_i2c_write_cmd_3(dev, 0x10, 0x02, MAX32664C_REPORT_PERIOD, MAX32664C_DEFAULT_CMD_DELAY);

    // Set continuous mode
    m_i2c_write_cmd_4(dev, 0x50, 0x07, 0x0A, 0x00, MAX32664C_DEFAULT_CMD_DELAY);

    // Enable AEC
    m_i2c_write_cmd_4(dev, 0x50, 0x07, 0x0B, 0x01, MAX32664C_DEFAULT_CMD_DELAY);

    // Disable Auto PD
    m_i2c_write_cmd_4(dev, 0x50, 0x07, 0x12, 0x01, MAX32664C_DEFAULT_CMD_DELAY);

    // Disable SCD
    m_i2c_write_cmd_4(dev, 0x50, 0x07, 0x0C, 0x01, MAX32664C_DEFAULT_CMD_DELAY);

    // Set AGC target PD current
    // m_i2c_write_cmd_5(dev, 0x50, 0x07, 0x11, 0x00, 0x64);

    // Enable HR, SpO2 algo
    m_i2c_write_cmd_3(dev, 0x52, 0x07, 0x02, 500);
    k_sleep(K_MSEC(500));

    return 0;
}

static int max32664c_set_mode_raw(const struct device *dev)
{
    LOG_INF("MAX32664C entering RAW mode...");

    // Output mode Raw
    m_i2c_write_cmd_3(dev, 0x10, 0x00, 0x01, MAX32664C_DEFAULT_CMD_DELAY);

    // Set interrupt threshold
    m_i2c_write_cmd_3(dev, 0x10, 0x01, MAX32664C_INT_THRESHOLD, MAX32664C_DEFAULT_CMD_DELAY);

    // Enable accel
    m_i2c_write_cmd_4(dev, 0x44, 0x04, 0x01, 0x00, 200);

    // Enable AFE
    m_i2c_write_cmd_4(dev, 0x44, 0x00, 0x01, 0x00, 500);

    // Enabled AFE Sample rate 100
    m_i2c_write_cmd_4(dev, 0x40, 0x00, 0x12, 0x18, 50);

    // Set LED1 current
    m_i2c_write_cmd_4(dev, 0x40, 0x00, 0x23, 0x7F, 50);

    // Set LED2 current
    m_i2c_write_cmd_4(dev, 0x40, 0x00, 0x24, 0x7F, 50);

    // Set LED3 current
    m_i2c_write_cmd_4(dev, 0x40, 0x00, 0x25, 0xFF, 50);

    // Set sequence
    // m_i2c_write_cmd_4(dev, 0x40, 0x00, 0x20, 0x21, 50);

    // Set sequence
    // m_i2c_write_cmd_4(dev, 0x40, 0x00, 0x21, 0xA3, 50);

    // Set sequence
    // m_i2c_write_cmd_4(dev, 0x40, 0x00, 0x22, 0x21, 50);

    k_sleep(K_MSEC(500));

    return 0;
}

static int max32664c_get_ver(const struct device *dev, uint8_t *ver_buf)
{
    const struct max32664c_config *config = dev->config;

    uint8_t wr_buf[2] = {0xFF, 0x03};

    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));
    max32664c_i2c_write(&config->i2c, wr_buf, sizeof(wr_buf));
    k_sleep(K_MSEC(4));

    max32664c_i2c_read(&config->i2c, ver_buf, 4);
    k_sleep(K_USEC(300));

    gpio_pin_set_dt(&config->mfio_gpio, 1);

    // LOG_DBG("Version (decimal) = %d.%d.%d\n", ver_buf[1], ver_buf[2], ver_buf[3]);

    if (ver_buf[1] == 0x00 && ver_buf[2] == 0x00 && ver_buf[3] == 0x00)
    {
        LOG_ERR("MAX32664C not found");
        return -ENODEV;
    }
    return 0;
}

static int max32664c_stop_algo(const struct device *dev)
{
    LOG_DBG("Stopping Algo...");

    // Stop Algorithm
    m_i2c_write_cmd_3(dev, 0x52, 0x07, 0x00, 120);

    // Disable AFE
    m_i2c_write_cmd_4(dev, 0x44, 0x00, 0x00, 0x00, 250);

    // Disable Accel
    m_i2c_write_cmd_4(dev, 0x44, 0x04, 0x00, 0x00, 20);

    return 0;
}

static int max32664c_set_mode_scd(const struct device *dev)
{
    LOG_DBG("MAX32664C entering SCD mode...");

    max32664c_stop_algo(dev);

    // max32664c_set_spo2_coeffs(dev, DEFAULT_SPO2_A, DEFAULT_SPO2_B, DEFAULT_SPO2_C);

    // Set LED for SCD
    m_i2c_write_cmd_2(dev, 0xE5, 0x02);

    // Set output mode to algo data
    m_i2c_write_cmd_3(dev, 0x10, 0x00, 0x02, MAX32664C_DEFAULT_CMD_DELAY);

    // Set interrupt threshold
    m_i2c_write_cmd_3(dev, 0x10, 0x01, MAX32664C_INT_THRESHOLD, MAX32664C_DEFAULT_CMD_DELAY);

    // Set report period
    m_i2c_write_cmd_3(dev, 0x10, 0x02, MAX32664C_REPORT_PERIOD, 100);

    // Enable AFE
    m_i2c_write_cmd_4(dev, 0x44, 0x00, 0x01, 0x00, 500);

    // Enable Accel
    m_i2c_write_cmd_4(dev, 0x44, 0x04, 0x01, 0x00, 30);

    // Enable SCD Only algo
    m_i2c_write_cmd_3(dev, 0x52, 0x07, 0x03, 500);

    return 0;
}

static int max32664c_set_mode_wake_on_motion(const struct device *dev)
{
    LOG_DBG("MAX32664C entering wake on motion mode per datasheet...");

    m_i2c_write_cmd_3(dev, 0x52, 0x07, 0x00, MAX32664C_DEFAULT_CMD_DELAY);

    m_i2c_write_cmd_4(dev, 0x44, 0x04, 0x00, 0x00, MAX32664C_DEFAULT_CMD_DELAY);

    m_i2c_write_cmd_6(dev, 0x46, 0x04, 0x00, 0x01, MAX32664C_MOTION_WUFC, MAX32664C_MOTION_ATH);

    m_i2c_write_cmd_3(dev, 0x10, 0x00, 0x01, MAX32664C_DEFAULT_CMD_DELAY);

    m_i2c_write_cmd_3(dev, 0x10, 0x02, 0x01, MAX32664C_DEFAULT_CMD_DELAY);

    m_i2c_write_cmd_4(dev, 0x44, 0x04, 0x01, 0x00, MAX32664C_DEFAULT_CMD_DELAY);

    LOG_INF("Wake-on-motion configured: WUFC=0x%02x (0.4s), ATH=0x%02x (1.5g)", 
            MAX32664C_MOTION_WUFC, MAX32664C_MOTION_ATH);
    return 0;
}

static int max32664c_exit_mode_wake_on_motion(const struct device *dev)
{
    LOG_DBG("MAX32664C exiting wake on motion mode per datasheet...");

    // Step 1: Disable Wake-Up on Motion configuration
    // Command: AA 46 04 00 00 FF FF (disable motion detection)
    m_i2c_write_cmd_6(dev, 0x46, 0x04, 0x00, 0x00, 0xFF, 0xFF);

    // Step 2: Disable Accelerometer 
    // Command: AA 44 04 00 00
    m_i2c_write_cmd_4(dev, 0x44, 0x04, 0x00, 0x00, MAX32664C_DEFAULT_CMD_DELAY);

    LOG_DBG("Wake-on-motion mode disabled");
    return 0;
}

static int max32664c_set_mode_algo(const struct device *dev, enum max32664c_mode mode, uint8_t algo_mode)
{
    LOG_DBG("MAX32664C entering ALGO mode...");

    max32664c_stop_algo(dev);

    max32664c_set_spo2_coeffs(dev, DEFAULT_SPO2_A, DEFAULT_SPO2_B, DEFAULT_SPO2_C);

    // Output mode sensor + algo data
    m_i2c_write_cmd_3(dev, 0x10, 0x00, 0x03, MAX32664C_DEFAULT_CMD_DELAY);

    // Set interrupt threshold
    m_i2c_write_cmd_3(dev, 0x10, 0x01, MAX32664C_INT_THRESHOLD, 200);

    // Set report period
    m_i2c_write_cmd_3(dev, 0x10, 0x02, MAX32664C_REPORT_PERIOD, MAX32664C_DEFAULT_CMD_DELAY);

    // Set Algorithm mode
    m_i2c_write_cmd_4(dev, 0x50, 0x07, 0x0A, algo_mode, MAX32664C_DEFAULT_CMD_DELAY);

    if (mode == MAX32664C_OP_MODE_ALGO_AEC)
    {
        LOG_DBG("MAX32664C entering AEC ALGO mode...");

        // Enable AEC
        m_i2c_write_cmd_4(dev, 0x50, 0x07, 0x0B, 0x01, MAX32664C_DEFAULT_CMD_DELAY);

        // EN Auto PD
        m_i2c_write_cmd_4(dev, 0x50, 0x07, 0x12, 0x01, MAX32664C_DEFAULT_CMD_DELAY);

        // EN SCD
        m_i2c_write_cmd_4(dev, 0x50, 0x07, 0x0C, 0x01, MAX32664C_DEFAULT_CMD_DELAY);

        // m_i2c_write_cmd_6(dev, 0x50, 0x07, 0x19, 0x42, 0x30, 0x00);
        // m_i2c_write_cmd_5(dev, 0x50, 0x07, 0x17, 0x01, 0x01);
        // m_i2c_write_cmd_5(dev, 0x50, 0x07, 0x18, 0x11, 0x21);

        // m_i2c_write_cmd_6(dev, 0x50, 0x07, 0x19, 0x74, 0x50, 0x00);
        // m_i2c_write_cmd_5(dev, 0x50, 0x07, 0x17, 0x00, 0x01);
        // m_i2c_write_cmd_5(dev, 0x50, 0x07, 0x18, 0x11, 0x21);

        // Enable HR, SpO2 algo
        m_i2c_write_cmd_3(dev, 0x52, 0x07, 0x01, 500);
        // k_sleep(K_MSEC(500));
    }
    else if (mode == MAX32664C_OP_MODE_ALGO_AGC)
    {
        LOG_DBG("MAX32664C entering AGC ALGO mode...");

        // DIS Auto PD
        m_i2c_write_cmd_4(dev, 0x50, 0x07, 0x12, 0x00, MAX32664C_DEFAULT_CMD_DELAY);

        // DIS SCD
        m_i2c_write_cmd_4(dev, 0x50, 0x07, 0x0C, 0x00, MAX32664C_DEFAULT_CMD_DELAY);

        // Set AGC target PD current
        m_i2c_write_cmd_5(dev, 0x50, 0x07, 0x11, 0x00, 0xFF);

        // m_i2c_write_cmd_6(dev, 0x50, 0x07, 0x19, 0x13, 0x56, 0x00);
        // m_i2c_write_cmd_5(dev, 0x50, 0x07, 0x17, 0x00, 0x11);
        // m_i2c_write_cmd_5(dev, 0x50, 0x07, 0x18, 0x30, 0x20);

        // Read  WHOAMI
        // m_i2c_write_cmd_3_rsp_3(dev, 0x41, 0x00, 0xFF);
        // m_i2c_write_cmd_3_rsp_3(dev, 0x41, 0x04, 0x0F);

        // Enable HR, SpO2 algo
        m_i2c_write_cmd_3(dev, 0x52, 0x07, 0x01, 500);
        // k_sleep(K_MSEC(500));
    }
    return 0;
}

int max32664c_do_enter_app(const struct device *dev)
{
    const struct max32664c_config *config = dev->config;

    LOG_DBG("Set app mode");

    gpio_pin_configure_dt(&config->mfio_gpio, GPIO_OUTPUT);

    gpio_pin_set_dt(&config->mfio_gpio, 1);
    k_sleep(K_MSEC(10));

    gpio_pin_set_dt(&config->reset_gpio, 0);
    k_sleep(K_MSEC(20));

    gpio_pin_set_dt(&config->reset_gpio, 1);
    k_sleep(K_MSEC(1600));

    m_read_op_mode(dev);

    return 0;
}

static int max32664c_sample_fetch(const struct device *dev,
                                  enum sensor_channel chan)
{
    // Not implemented

    return 0;
}

static int max32664c_channel_get(const struct device *dev,
                                 enum sensor_channel chan,
                                 struct sensor_value *val)
{
    // Not implemented

    return 0;
}

static int max32664c_attr_set(const struct device *dev,
                              enum sensor_channel chan,
                              enum sensor_attribute attr,
                              const struct sensor_value *val)
{
    struct max32664c_data *data = dev->data;

    switch (attr)
    {
    case MAX32664C_ATTR_OP_MODE:
        if (val->val1 == MAX32664C_OP_MODE_ALGO_AEC)
        {
            max32664c_set_mode_algo(dev, MAX32664C_OP_MODE_ALGO_AEC, val->val2); // MAX32664C_ALGO_OP_MODE_CONT_HR_CONT_SPO2);
            data->op_mode = MAX32664C_OP_MODE_ALGO_AEC;
        }
        else if (val->val1 == MAX32664C_OP_MODE_ALGO_AGC)
        {
            max32664c_set_mode_algo(dev, MAX32664C_OP_MODE_ALGO_AGC, val->val2); // MAX32664C_ALGO_OP_MODE_CONT_HR_CONT_SPO2);
            data->op_mode = MAX32664C_OP_MODE_ALGO_AGC;
        }
        else if (val->val1 == MAX32664C_OP_MODE_ALGO_EXTENDED)
        {
            max32664c_set_mode_extended_algo(dev);
            data->op_mode = MAX32664C_OP_MODE_ALGO_EXTENDED;
        }
        else if (val->val1 == MAX32664C_OP_MODE_RAW)
        {
            max32664c_set_mode_raw(dev);
            data->op_mode = MAX32664C_OP_MODE_RAW;
        }
        else if (val->val1 == MAX32664C_OP_MODE_SCD)
        {
            max32664c_set_mode_scd(dev);
            data->op_mode = MAX32664C_OP_MODE_SCD;
        }
        else if (val->val1 == MAX32664C_OP_MODE_WAKE_ON_MOTION)
        {
            max32664c_set_mode_wake_on_motion(dev);
            data->op_mode = MAX32664C_OP_MODE_WAKE_ON_MOTION;
        }
        else if (val->val1 == MAX32664C_OP_MODE_EXIT_WAKE_ON_MOTION)
        {
            max32664c_exit_mode_wake_on_motion(dev);
            data->op_mode = MAX32664C_OP_MODE_IDLE;
        }
        else if (val->val1 == MAX32664C_OP_MODE_STOP_ALGO)
        {
            max32664c_stop_algo(dev);
            data->op_mode = MAX32664C_OP_MODE_IDLE;
        }
        else
        {
            LOG_ERR("Unsupported sensor operation mode");
            return -ENOTSUP;
        }
        break;
    case MAX32664C_ATTR_ENTER_BOOTLOADER:
        // max32664c_do_enter_bl(dev);
        break;
    default:
        LOG_ERR("Unsupported sensor attribute");
        return -ENOTSUP;
    }

    return 0;
}

static int max32664c_check_app_present(const struct device *dev)
{
    struct max32664c_data *data = dev->data;

    if (data->hub_ver[1] == 0x08 && data->hub_ver[2] == 0x00 && data->hub_ver[3] == 0x00)
    {
        LOG_ERR("App not present !!");
        return 8;
    }
    else
    {
        LOG_DBG("App present !!");
        return 1;
    }

    return 0;
}

static int max32664c_attr_get(const struct device *dev,
                              enum sensor_channel chan,
                              enum sensor_attribute attr,
                              struct sensor_value *val)
{
    struct max32664c_data *data = dev->data;

    switch (attr)
    {
    case MAX32664C_ATTR_OP_MODE:
        val->val1 = data->op_mode;
        val->val2 = 0;
        break;
    case MAX32664C_ATTR_IS_APP_PRESENT:
        val->val1 = max32664c_check_app_present(dev);
        val->val2 = 0;
        break;
    case MAX32664C_ATTR_APP_VER:
        val->val1 = data->hub_ver[2];
        val->val2 = data->hub_ver[3];
        break;
    case MAX32664C_ATTR_SENSOR_IDS:
        val->val1 = data->max86141_id;
        val->val2 = data->accel_id;
        break;
    default:
        LOG_ERR("Unsupported sensor attribute");
        return -ENOTSUP;
    }

    return 0;
}

static const struct sensor_driver_api max32664c_driver_api = {
    .attr_set = max32664c_attr_set,
    .attr_get = max32664c_attr_get,

    .sample_fetch = max32664c_sample_fetch,
    .channel_get = max32664c_channel_get,

#ifdef CONFIG_SENSOR_ASYNC_API
    .get_decoder = (sensor_get_decoder_t)max32664c_get_decoder,
    .submit = (sensor_submit_t)max32664c_submit,
#endif
};

static int max32664c_chip_init(const struct device *dev)
{
    const struct max32664c_config *config = dev->config;
    struct max32664c_data *data = dev->data;

    if (!device_is_ready(config->i2c.bus))
    {
        LOG_ERR("I2C not ready");
        return -ENODEV;
    }

    gpio_pin_configure_dt(&config->reset_gpio, GPIO_OUTPUT);
    gpio_pin_configure_dt(&config->mfio_gpio, GPIO_OUTPUT);

    max32664c_do_enter_app(dev);

    bool hub_found = false;

    if (max32664c_get_ver(dev, data->hub_ver) == 0)
    {
        hub_found = true;
        LOG_DBG("Hub Version: %d.%d.%d", data->hub_ver[1], data->hub_ver[2], data->hub_ver[3]);
    }
    else
    {
        LOG_ERR("MAX32664C not responding");
        return -ENODEV;
    }

    max32664c_check_sensors(dev);

    return 0;
}

#ifdef CONFIG_PM_DEVICE
static int max32664c_pm_action(const struct device *dev,
                               enum pm_device_action action)
{
    int ret = 0;

    switch (action)
    {
    case PM_DEVICE_ACTION_RESUME:
        /* Re-initialize the chip */

        break;
    case PM_DEVICE_ACTION_SUSPEND:
        /* Put the chip into sleep mode */

        break;
    default:
        return -ENOTSUP;
    }

    return ret;
}
#endif /* CONFIG_PM_DEVICE */

int max32664c_test_motion_detection(const struct device *dev)
{
    LOG_INF("Testing MAX32664C motion detection...");
    
    struct max32664c_data *data = dev->data;
    uint8_t original_mode = data->op_mode;
    
    // Set wake-on-motion mode
    max32664c_set_mode_wake_on_motion(dev);
    data->op_mode = MAX32664C_OP_MODE_WAKE_ON_MOTION;
    
    LOG_INF("Motion detection active - move the device now!");
    LOG_INF("Monitoring for 10 seconds...");
    
    // Monitor motion for 10 seconds
    for (int i = 0; i < 100; i++) {
        uint8_t hub_stat = max32664c_read_hub_status(dev);
        if (hub_stat & MAX32664C_HUB_STAT_DRDY_MASK) {
            LOG_INF("Motion event detected! Hub status: 0x%02x", hub_stat);
            
            // Trigger the async fetch to decode motion data
            uint8_t chip_op_mode;
            max32664c_async_sample_fetch_wake_on_motion(dev, &chip_op_mode);
        }
        k_msleep(100);
    }
    
    // Restore original mode
    data->op_mode = original_mode;
    LOG_INF("Motion detection test complete");
    
    return 0;
}

#define MAX32664C_DEFINE(inst)                                      \
    static struct max32664c_data max32664c_data_##inst;             \
    static const struct max32664c_config max32664c_config_##inst =  \
        {                                                           \
            .i2c = I2C_DT_SPEC_INST_GET(inst),                      \
            .reset_gpio = GPIO_DT_SPEC_INST_GET(inst, reset_gpios), \
            .mfio_gpio = GPIO_DT_SPEC_INST_GET(inst, mfio_gpios),   \
    };                                                              \
    PM_DEVICE_DT_INST_DEFINE(inst, max32664c_pm_action);            \
    SENSOR_DEVICE_DT_INST_DEFINE(inst,                              \
                                 max32664c_chip_init,               \
                                 PM_DEVICE_DT_INST_GET(inst),       \
                                 &max32664c_data_##inst,            \
                                 &max32664c_config_##inst,          \
                                 POST_KERNEL,                       \
                                 CONFIG_SENSOR_INIT_PRIORITY,       \
                                 &max32664c_driver_api)

DT_INST_FOREACH_STATUS_OKAY(MAX32664C_DEFINE)