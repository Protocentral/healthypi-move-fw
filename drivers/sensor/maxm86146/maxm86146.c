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
 * MAXM86146 Integrated Optical Biosensing Module Driver
 * Based on MAX32664 Sensor Hub protocol - adapted from MAX32664C driver
 */

#define DT_DRV_COMPAT maxim_maxm86146

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

#include "maxm86146.h"

LOG_MODULE_REGISTER(MAXM86146, CONFIG_MAXM86146_LOG_LEVEL);

#define CALIBVECTOR_SIZE 827
#define DATE_TIME_VECTOR_SIZE 11
#define SPO2_CAL_COEFFS_SIZE 15

/* I2C wrapper implementations */

#ifdef CONFIG_SENSOR_ASYNC_API

static int maxm86146_i2c_rtio_write_impl(struct rtio *r, struct rtio_iodev *iodev, const void *buf, size_t len)
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

    rc = rtio_cqe_copy_out(r, &cqe, 1, K_FOREVER);
    if (rc != 1)
    {
        return -EIO;
    }

    rc = cqe.result;
    rtio_cqe_release(r, &cqe);

    return rc;
}

static int maxm86146_i2c_rtio_read_impl(struct rtio *r, struct rtio_iodev *iodev, void *buf, size_t len)
{
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

#if defined(CONFIG_SENSOR_ASYNC_API) && defined(MAXM86146_USE_RTIO_IMPL)
static struct rtio *maxm86146_rtio_ctx = NULL;
static struct rtio_iodev *maxm86146_rtio_iodev = NULL;

void maxm86146_register_rtio_context(struct rtio *r, struct rtio_iodev *iodev)
{
    maxm86146_rtio_ctx = r;
    maxm86146_rtio_iodev = iodev;
}
#endif

static int maxm86146_i2c_write_impl(const struct i2c_dt_spec *i2c, const void *buf, size_t len)
{
#if defined(CONFIG_SENSOR_ASYNC_API) && defined(MAXM86146_USE_RTIO_IMPL)
    if (maxm86146_rtio_ctx && maxm86146_rtio_iodev)
    {
        return maxm86146_i2c_rtio_write_impl(maxm86146_rtio_ctx, maxm86146_rtio_iodev, buf, len);
    }
#else
    return i2c_write_dt(i2c, buf, len);
#endif
}

static int maxm86146_i2c_read_impl(const struct i2c_dt_spec *i2c, void *buf, size_t len)
{
#if defined(CONFIG_SENSOR_ASYNC_API) && defined(MAXM86146_USE_RTIO_IMPL)
    if (maxm86146_rtio_ctx && maxm86146_rtio_iodev)
    {
        return maxm86146_i2c_rtio_read_impl(maxm86146_rtio_ctx, maxm86146_rtio_iodev, buf, len);
    }
#else
    return i2c_read_dt(i2c, buf, len);
#endif
}

/* Public wrappers */
int maxm86146_i2c_write(const struct i2c_dt_spec *i2c, const void *buf, size_t len)
{
    return maxm86146_i2c_write_impl(i2c, buf, len);
}

int maxm86146_i2c_read(const struct i2c_dt_spec *i2c, void *buf, size_t len)
{
    return maxm86146_i2c_read_impl(i2c, buf, len);
}

#ifdef CONFIG_SENSOR_ASYNC_API
int maxm86146_i2c_rtio_write(struct rtio *r, struct rtio_iodev *iodev, const void *buf, size_t len)
{
    return maxm86146_i2c_rtio_write_impl(r, iodev, buf, len);
}

int maxm86146_i2c_rtio_read(struct rtio *r, struct rtio_iodev *iodev, void *buf, size_t len)
{
    return maxm86146_i2c_rtio_read_impl(r, iodev, buf, len);
}
#endif /* CONFIG_SENSOR_ASYNC_API */

static int m_read_op_mode(const struct device *dev)
{
    const struct maxm86146_config *config = dev->config;
    uint8_t rd_buf[2] = {0x00, 0x00};
    uint8_t wr_buf[2] = {0x02, 0x00};

    k_sleep(K_USEC(300));
    maxm86146_i2c_write(&config->i2c, wr_buf, sizeof(wr_buf));
    k_sleep(K_MSEC(45));
    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));
    maxm86146_i2c_read(&config->i2c, rd_buf, sizeof(rd_buf));
    k_sleep(K_MSEC(45));
    gpio_pin_set_dt(&config->mfio_gpio, 1);

    LOG_DBG("Op mode = %x ", rd_buf[1]);

    return rd_buf[1];
}

uint8_t maxm86146_read_hub_status(const struct device *dev)
{
    const struct maxm86146_config *config = dev->config;
    uint8_t rd_buf[3] = {0x00, 0x00, 0x00};
    uint8_t wr_buf[2] = {0x00, 0x00};

    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));

    maxm86146_i2c_write(&config->i2c, wr_buf, sizeof(wr_buf));
    maxm86146_i2c_read(&config->i2c, rd_buf, sizeof(rd_buf));

    k_sleep(K_USEC(300));
    gpio_pin_set_dt(&config->mfio_gpio, 1);

    LOG_DBG("Hub status bytes: %02x %02x", rd_buf[0], rd_buf[1]);

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
    const struct maxm86146_config *config = dev->config;
    uint8_t wr_buf[2];
    uint8_t rd_buf[1];

    wr_buf[0] = byte1;
    wr_buf[1] = byte2;

    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));

    maxm86146_i2c_write(&config->i2c, wr_buf, sizeof(wr_buf));
    k_sleep(K_MSEC(MAXM86146_DEFAULT_CMD_DELAY));
    maxm86146_i2c_read(&config->i2c, rd_buf, sizeof(rd_buf));
    k_sleep(K_MSEC(MAXM86146_DEFAULT_CMD_DELAY));

    gpio_pin_set_dt(&config->mfio_gpio, 1);

    LOG_DBG("CMD: %x %x | RSP: %x ", wr_buf[0], wr_buf[1], rd_buf[0]);

    k_sleep(K_MSEC(MAXM86146_DEFAULT_CMD_DELAY));

    return 0;
}

static int m_i2c_write_cmd_3(const struct device *dev, uint8_t byte1, uint8_t byte2, uint8_t byte3, uint16_t cmd_delay)
{
    const struct maxm86146_config *config = dev->config;
    uint8_t wr_buf[3];
    uint8_t rd_buf[1] = {0x00};

    wr_buf[0] = byte1;
    wr_buf[1] = byte2;
    wr_buf[2] = byte3;

    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));

    maxm86146_i2c_write(&config->i2c, wr_buf, sizeof(wr_buf));
    k_sleep(K_MSEC(cmd_delay));
    maxm86146_i2c_read(&config->i2c, rd_buf, sizeof(rd_buf));

    k_sleep(K_USEC(300));
    gpio_pin_set_dt(&config->mfio_gpio, 1);

    LOG_DBG("CMD: %x %x %x | RSP: %x ", wr_buf[0], wr_buf[1], wr_buf[2], rd_buf[0]);

    k_sleep(K_MSEC(10));

    return 0;
}

static int m_i2c_write_cmd_4(const struct device *dev, uint8_t byte1, uint8_t byte2, uint8_t byte3, uint8_t byte4, uint16_t cmd_delay)
{
    const struct maxm86146_config *config = dev->config;
    uint8_t wr_buf[4];
    uint8_t rd_buf[1];

    wr_buf[0] = byte1;
    wr_buf[1] = byte2;
    wr_buf[2] = byte3;
    wr_buf[3] = byte4;

    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));

    maxm86146_i2c_write(&config->i2c, wr_buf, sizeof(wr_buf));
    k_sleep(K_MSEC(cmd_delay));
    maxm86146_i2c_read(&config->i2c, rd_buf, 1);
    k_sleep(K_MSEC(MAXM86146_DEFAULT_CMD_DELAY));

    gpio_pin_set_dt(&config->mfio_gpio, 1);

    LOG_DBG("CMD: %x %x %x %x | RSP: %x ", wr_buf[0], wr_buf[1], wr_buf[2], wr_buf[3], rd_buf[0]);

    k_sleep(K_MSEC(MAXM86146_DEFAULT_CMD_DELAY));

    return 0;
}

static int m_i2c_write_cmd_5(const struct device *dev, uint8_t byte1, uint8_t byte2, uint8_t byte3, uint8_t byte4, uint8_t byte5)
{
    const struct maxm86146_config *config = dev->config;
    uint8_t wr_buf[5];
    uint8_t rd_buf[1];

    wr_buf[0] = byte1;
    wr_buf[1] = byte2;
    wr_buf[2] = byte3;
    wr_buf[3] = byte4;
    wr_buf[4] = byte5;

    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));

    maxm86146_i2c_write(&config->i2c, wr_buf, sizeof(wr_buf));
    k_sleep(K_MSEC(MAXM86146_DEFAULT_CMD_DELAY));
    maxm86146_i2c_read(&config->i2c, rd_buf, 1);
    k_sleep(K_MSEC(MAXM86146_DEFAULT_CMD_DELAY));

    gpio_pin_set_dt(&config->mfio_gpio, 1);

    LOG_DBG("CMD: %x %x %x %x %x | RSP: %x ", wr_buf[0], wr_buf[1], wr_buf[2], wr_buf[3], wr_buf[4], rd_buf[0]);

    k_sleep(K_MSEC(MAXM86146_DEFAULT_CMD_DELAY));

    return 0;
}

static int m_i2c_write_cmd_6(const struct device *dev, uint8_t byte1, uint8_t byte2, uint8_t byte3, uint8_t byte4, uint8_t byte5, uint8_t byte6)
{
    const struct maxm86146_config *config = dev->config;
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

    maxm86146_i2c_write(&config->i2c, wr_buf, sizeof(wr_buf));
    k_sleep(K_MSEC(MAXM86146_DEFAULT_CMD_DELAY));
    maxm86146_i2c_read(&config->i2c, rd_buf, 1);
    k_sleep(K_MSEC(MAXM86146_DEFAULT_CMD_DELAY));

    gpio_pin_set_dt(&config->mfio_gpio, 1);

    LOG_DBG("CMD: %x %x %x %x %x %x | RSP: %x ", wr_buf[0], wr_buf[1], wr_buf[2], wr_buf[3], wr_buf[4], wr_buf[5], rd_buf[0]);

    k_sleep(K_MSEC(MAXM86146_DEFAULT_CMD_DELAY));

    return 0;
}

static int m_i2c_write(const struct device *dev, uint8_t *wr_buf, uint32_t wr_len)
{
    const struct maxm86146_config *config = dev->config;
    uint8_t rd_buf[1] = {0x00};

    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));
    maxm86146_i2c_write(&config->i2c, wr_buf, wr_len);

    k_sleep(K_MSEC(MAXM86146_DEFAULT_CMD_DELAY));

    k_sleep(K_USEC(300));
    maxm86146_i2c_read(&config->i2c, rd_buf, sizeof(rd_buf));
    k_sleep(K_MSEC(MAXM86146_DEFAULT_CMD_DELAY));

    gpio_pin_set_dt(&config->mfio_gpio, 1);

    LOG_DBG("Write %d bytes | RSP: %d ", wr_len, rd_buf[0]);

    k_sleep(K_MSEC(45));

    return 0;
}

static int maxm86146_set_spo2_coeffs(const struct device *dev, float a, float b, float c)
{
    uint8_t wr_buf[15] = {0x50, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xD7, 0xFB, 0xDD, 0x00, 0xAB, 0x61, 0xFE};
    m_i2c_write(dev, wr_buf, sizeof(wr_buf));
    return 0;
}

static int m_i2c_write_cmd_3_rsp_3(const struct device *dev, uint8_t byte1, uint8_t byte2, uint8_t byte3, uint8_t *rsp)
{
    const struct maxm86146_config *config = dev->config;
    uint8_t wr_buf[3];
    uint8_t rd_buf[3] = {0x00, 0x00, 0x00};

    wr_buf[0] = byte1;
    wr_buf[1] = byte2;
    wr_buf[2] = byte3;

    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));
    maxm86146_i2c_write(&config->i2c, wr_buf, sizeof(wr_buf));

    k_sleep(K_MSEC(MAXM86146_DEFAULT_CMD_DELAY));

    k_sleep(K_USEC(300));
    maxm86146_i2c_read(&config->i2c, rd_buf, sizeof(rd_buf));
    k_sleep(K_MSEC(500));

    gpio_pin_set_dt(&config->mfio_gpio, 1);

    LOG_DBG("CMD: %x %x %x | RSP: %x %x %x ", wr_buf[0], wr_buf[1], wr_buf[2], rd_buf[0], rd_buf[1], rd_buf[2]);

    memcpy(rsp, rd_buf, 3);

    k_sleep(K_MSEC(10));

    return 0;
}

static int maxm86146_check_sensors(const struct device *dev)
{
    LOG_DBG("MAXM86146 checking sensors...");

    struct maxm86146_data *data = dev->data;

    uint8_t rsp[3] = {0x00, 0x00, 0x00};

    maxm86146_do_enter_app(dev);

    /* Read AFE WHOAMI - MAXM86146 has integrated AFE */
    m_i2c_write_cmd_3_rsp_3(dev, 0x41, 0x00, 0xFF, rsp);
    data->afe_id = rsp[1];

    /* MAXM86146 may report different AFE ID than MAX86141 */
    if (data->afe_id != MAXM86146_INTEGRATED_AFE_ID && data->afe_id != MAXM86146_AFE_ID_ALT)
    {
        LOG_WRN("MAXM86146 AFE ID unexpected: 0x%02x (expected 0x%02x or 0x%02x)",
                data->afe_id, MAXM86146_INTEGRATED_AFE_ID, MAXM86146_AFE_ID_ALT);
    }
    else
    {
        LOG_DBG("MAXM86146 AFE OK: 0x%02x", data->afe_id);
    }

    /* Check for accelerometer - may not be present on all MAXM86146 variants */
    m_i2c_write_cmd_3_rsp_3(dev, 0x41, 0x04, 0x0F, rsp);
    data->accel_id = rsp[1];

    if (data->accel_id == MAXM86146_ACC_ID)
    {
        LOG_DBG("MAXM86146 Accelerometer OK: 0x%02x", data->accel_id);
        data->has_accel = true;
    }
    else if (data->accel_id == 0x00 || data->accel_id == 0xFF)
    {
        LOG_INF("MAXM86146 Accelerometer not present (integrated module)");
        data->has_accel = false;
    }
    else
    {
        LOG_WRN("MAXM86146 Accelerometer ID unexpected: 0x%02x", data->accel_id);
        data->has_accel = false;
    }

    return 0;
}

/**
 * @brief Configure MAXM86146 LED firing sequence and PD mapping
 *
 * This configures the LED/PD arrangement specific to the MAXM86146 integrated module
 * based on the MAXM86146EVSYS development kit configuration:
 * - LED Firing Sequence: Slot1=Green1, Slot2=Green2, Slot3=Red, Slot4=IR
 * - HR Inputs: Input1=Slot1/PD1, Input2=Slot2/PD2
 * - SpO2 Inputs: IR=Slot4/PD1, Red=Slot3/PD1
 */
static int maxm86146_configure_led_pd_mapping(const struct device *dev)
{
    LOG_DBG("Configuring MAXM86146 LED/PD mapping...");

    /* Map LED Firing Sequence (0x50 0x07 0x19)
     * Slot1=LED1(Green1), Slot2=LED3(Green2), Slot3=LED5(Red), Slot4=LED6(IR) */
    m_i2c_write_cmd_6(dev, 0x50, 0x07, 0x19,
                      MAXM86146_LED_SEQ_BYTE1,
                      MAXM86146_LED_SEQ_BYTE2,
                      MAXM86146_LED_SEQ_BYTE3);

    /* Map HR Inputs (0x50 0x07 0x17)
     * HR Input 1: Slot 1, PD1; HR Input 2: Slot 2, PD2 */
    m_i2c_write_cmd_5(dev, 0x50, 0x07, 0x17,
                      MAXM86146_HR_MAP_BYTE1,
                      MAXM86146_HR_MAP_BYTE2);

    /* Map SpO2 Inputs (0x50 0x07 0x18)
     * SpO2 IR: Slot 4, PD1; SpO2 Red: Slot 3, PD1 */
    m_i2c_write_cmd_5(dev, 0x50, 0x07, 0x18,
                      MAXM86146_SPO2_MAP_BYTE1,
                      MAXM86146_SPO2_MAP_BYTE2);

    LOG_DBG("MAXM86146 LED/PD mapping configured");
    return 0;
}

static int maxm86146_set_mode_extended_algo(const struct device *dev)
{
    LOG_DBG("MAXM86146 entering extended ALGO mode...");

    /* 1. Set SPO2 calibration coefficients */
    maxm86146_set_spo2_coeffs(dev, MAXM86146_SPO2_COEFF_A, MAXM86146_SPO2_COEFF_B, MAXM86146_SPO2_COEFF_C);

    /* 2. Set output format to sensor + algorithm data */
    m_i2c_write_cmd_3(dev, 0x10, 0x00, 0x03, MAXM86146_DEFAULT_CMD_DELAY);

    /* 3. Set sensor hub DataRdyInt threshold */
    m_i2c_write_cmd_3(dev, 0x10, 0x01, 0x01, MAXM86146_DEFAULT_CMD_DELAY);

    /* 4. Set samples report period */
    m_i2c_write_cmd_3(dev, 0x10, 0x02, MAXM86146_REPORT_PERIOD, MAXM86146_DEFAULT_CMD_DELAY);

    /* 5. Set algorithm operation mode to Continuous HRM and Continuous SpO2 */
    m_i2c_write_cmd_4(dev, 0x50, 0x07, 0x0A, 0x00, MAXM86146_DEFAULT_CMD_DELAY);

    /* 6. Enable AEC */
    m_i2c_write_cmd_4(dev, 0x50, 0x07, 0x0B, 0x01, MAXM86146_DEFAULT_CMD_DELAY);

    /* 7. Enable Auto PD Current Calculation */
    m_i2c_write_cmd_4(dev, 0x50, 0x07, 0x12, 0x01, MAXM86146_DEFAULT_CMD_DELAY);

    /* 8. Enable Skin Contact Detection */
    m_i2c_write_cmd_4(dev, 0x50, 0x07, 0x0C, 0x01, MAXM86146_DEFAULT_CMD_DELAY);

    /* 9. Configure MAXM86146-specific LED/PD mapping */
    maxm86146_configure_led_pd_mapping(dev);

    /* 10. Enable HR and SpO2 algorithm (extended mode = 0x02) */
    m_i2c_write_cmd_3(dev, 0x52, 0x07, 0x02, 500);
    k_sleep(K_MSEC(500));

    return 0;
}

static int maxm86146_set_mode_raw(const struct device *dev)
{
    LOG_INF("MAXM86146 entering RAW mode...");

    /* Output mode Raw */
    m_i2c_write_cmd_3(dev, 0x10, 0x00, 0x01, MAXM86146_DEFAULT_CMD_DELAY);

    /* Set interrupt threshold */
    m_i2c_write_cmd_3(dev, 0x10, 0x01, MAXM86146_INT_THRESHOLD, MAXM86146_DEFAULT_CMD_DELAY);

    /* Enable accel (if present) */
    m_i2c_write_cmd_4(dev, 0x44, 0x04, 0x01, 0x00, 200);

    /* Enable AFE */
    m_i2c_write_cmd_4(dev, 0x44, 0x00, 0x01, 0x00, 500);

    /* Set AFE sample rate - MAXM86146 uses 100Hz default */
    m_i2c_write_cmd_4(dev, 0x40, 0x00, 0x12, MAXM86146_SAMPLE_RATE_DEFAULT, 50);

    /* Set LED1 (Green) current - MAXM86146 integrated LEDs need lower current */
    m_i2c_write_cmd_4(dev, 0x40, 0x00, 0x23, MAXM86146_LED1_CURRENT_DEFAULT, 50);

    /* Set LED2 (Red) current */
    m_i2c_write_cmd_4(dev, 0x40, 0x00, 0x24, MAXM86146_LED2_CURRENT_DEFAULT, 50);

    /* Set LED3 (IR) current */
    m_i2c_write_cmd_4(dev, 0x40, 0x00, 0x25, MAXM86146_LED3_CURRENT_DEFAULT, 50);

    k_sleep(K_MSEC(500));

    return 0;
}

static int maxm86146_get_ver(const struct device *dev, uint8_t *ver_buf)
{
    const struct maxm86146_config *config = dev->config;

    uint8_t wr_buf[2] = {0xFF, 0x03};

    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));
    maxm86146_i2c_write(&config->i2c, wr_buf, sizeof(wr_buf));
    k_sleep(K_MSEC(4));

    maxm86146_i2c_read(&config->i2c, ver_buf, 4);
    k_sleep(K_USEC(300));

    gpio_pin_set_dt(&config->mfio_gpio, 1);

    if (ver_buf[1] == 0x00 && ver_buf[2] == 0x00 && ver_buf[3] == 0x00)
    {
        LOG_ERR("MAXM86146 not found");
        return -ENODEV;
    }
    return 0;
}

static int maxm86146_stop_algo(const struct device *dev)
{
    LOG_DBG("Stopping Algo...");

    m_i2c_write_cmd_3(dev, 0x52, 0x07, 0x00, 120);
    m_i2c_write_cmd_4(dev, 0x44, 0x00, 0x00, 0x00, 250);
    m_i2c_write_cmd_4(dev, 0x44, 0x04, 0x00, 0x00, 20);

    return 0;
}

static int maxm86146_set_mode_scd(const struct device *dev)
{
    LOG_DBG("MAXM86146 entering SCD mode...");

    maxm86146_stop_algo(dev);

    m_i2c_write_cmd_2(dev, 0xE5, 0x02);
    m_i2c_write_cmd_3(dev, 0x10, 0x00, 0x02, MAXM86146_DEFAULT_CMD_DELAY);
    m_i2c_write_cmd_3(dev, 0x10, 0x01, MAXM86146_INT_THRESHOLD, MAXM86146_DEFAULT_CMD_DELAY);
    m_i2c_write_cmd_3(dev, 0x10, 0x02, MAXM86146_REPORT_PERIOD, 100);
    m_i2c_write_cmd_4(dev, 0x44, 0x00, 0x01, 0x00, 500);
    m_i2c_write_cmd_4(dev, 0x44, 0x04, 0x01, 0x00, 30);
    m_i2c_write_cmd_3(dev, 0x52, 0x07, 0x03, 500);

    return 0;
}

/**
 * @brief Configure SCD-based wake detection (for MAXM86146 without accelerometer)
 *
 * When the MAXM86146 variant doesn't have an integrated accelerometer,
 * we use periodic SCD (Skin Contact Detection) polling as an alternative
 * wake mechanism. The SCD mode uses minimal LED current to detect skin contact.
 */
static int maxm86146_set_mode_scd_wake(const struct device *dev)
{
    LOG_INF("MAXM86146 entering SCD-based wake mode (no accelerometer)...");

    maxm86146_stop_algo(dev);

    /* Configure for low-power SCD polling */
    m_i2c_write_cmd_2(dev, 0xE5, 0x02);  /* Enable SCD */

    /* Output mode sensor only */
    m_i2c_write_cmd_3(dev, 0x10, 0x00, 0x02, MAXM86146_DEFAULT_CMD_DELAY);

    /* Set minimal interrupt threshold for low power */
    m_i2c_write_cmd_3(dev, 0x10, 0x01, 0x01, MAXM86146_DEFAULT_CMD_DELAY);

    /* Set report period */
    m_i2c_write_cmd_3(dev, 0x10, 0x02, MAXM86146_REPORT_PERIOD, 100);

    /* Enable AFE with minimal LED current for SCD */
    m_i2c_write_cmd_4(dev, 0x44, 0x00, 0x01, 0x00, 500);

    /* Set very low LED current for SCD mode (power saving) */
    m_i2c_write_cmd_4(dev, 0x40, 0x00, 0x23, MAXM86146_LED_SCD_CURRENT, 50);
    m_i2c_write_cmd_4(dev, 0x40, 0x00, 0x24, MAXM86146_LED_SCD_CURRENT, 50);
    m_i2c_write_cmd_4(dev, 0x40, 0x00, 0x25, MAXM86146_LED_SCD_CURRENT, 50);

    /* Enable SCD algorithm only */
    m_i2c_write_cmd_3(dev, 0x52, 0x07, 0x03, 500);

    LOG_INF("SCD-based wake mode configured (poll interval: %d sec)",
            MAXM86146_SCD_POLL_INTERVAL_S);
    return 0;
}

static int maxm86146_set_mode_wake_on_motion(const struct device *dev)
{
    struct maxm86146_data *data = dev->data;

    LOG_DBG("MAXM86146 entering wake on motion mode...");

    if (!data->has_accel)
    {
        /* Fall back to SCD-based wake detection when no accelerometer */
        LOG_INF("MAXM86146: No accelerometer - using SCD-based wake detection");
        return maxm86146_set_mode_scd_wake(dev);
    }

    /* Standard wake-on-motion with accelerometer */
    m_i2c_write_cmd_3(dev, 0x52, 0x07, 0x00, MAXM86146_DEFAULT_CMD_DELAY);
    m_i2c_write_cmd_4(dev, 0x44, 0x04, 0x00, 0x00, MAXM86146_DEFAULT_CMD_DELAY);
    m_i2c_write_cmd_6(dev, 0x46, 0x04, 0x00, 0x01, MAXM86146_MOTION_WUFC, MAXM86146_MOTION_ATH);
    m_i2c_write_cmd_3(dev, 0x10, 0x00, 0x01, MAXM86146_DEFAULT_CMD_DELAY);
    m_i2c_write_cmd_3(dev, 0x10, 0x02, 0x01, MAXM86146_DEFAULT_CMD_DELAY);
    m_i2c_write_cmd_4(dev, 0x44, 0x04, 0x01, 0x00, MAXM86146_DEFAULT_CMD_DELAY);

    LOG_INF("Wake-on-motion configured: WUFC=0x%02x, ATH=0x%02x",
            MAXM86146_MOTION_WUFC, MAXM86146_MOTION_ATH);
    return 0;
}

static int maxm86146_exit_mode_wake_on_motion(const struct device *dev)
{
    struct maxm86146_data *data = dev->data;

    LOG_DBG("MAXM86146 exiting wake/sleep mode...");

    if (!data->has_accel)
    {
        /* Exit SCD-based wake mode */
        LOG_DBG("Exiting SCD-based wake mode");
        maxm86146_stop_algo(dev);
    }
    else
    {
        /* Exit accelerometer wake-on-motion mode */
        m_i2c_write_cmd_6(dev, 0x46, 0x04, 0x00, 0x00, 0xFF, 0xFF);
        m_i2c_write_cmd_4(dev, 0x44, 0x04, 0x00, 0x00, MAXM86146_DEFAULT_CMD_DELAY);
    }

    LOG_DBG("Wake/sleep mode disabled");
    return 0;
}

static int maxm86146_set_mode_algo(const struct device *dev, enum maxm86146_mode mode, uint8_t algo_mode)
{
    LOG_DBG("MAXM86146 entering ALGO mode...");

    maxm86146_stop_algo(dev);

    /* 1. Set SPO2 calibration coefficients (0x50 0x07 0x00) */
    maxm86146_set_spo2_coeffs(dev, MAXM86146_SPO2_COEFF_A, MAXM86146_SPO2_COEFF_B, MAXM86146_SPO2_COEFF_C);

    /* 2. Set output format to sensor + algorithm data (0x10 0x00 0x03) */
    m_i2c_write_cmd_3(dev, 0x10, 0x00, 0x03, MAXM86146_DEFAULT_CMD_DELAY);

    /* 3. Set sensor hub DataRdyInt threshold (0x10 0x01 0x01) */
    m_i2c_write_cmd_3(dev, 0x10, 0x01, 0x01, MAXM86146_DEFAULT_CMD_DELAY);

    /* 4. Set samples report period to 40ms (0x10 0x02 0x01) */
    m_i2c_write_cmd_3(dev, 0x10, 0x02, MAXM86146_REPORT_PERIOD, MAXM86146_DEFAULT_CMD_DELAY);

    /* 5. Set algorithm operation mode (0x50 0x07 0x0A) */
    m_i2c_write_cmd_4(dev, 0x50, 0x07, 0x0A, algo_mode, MAXM86146_DEFAULT_CMD_DELAY);

    if (mode == MAXM86146_OP_MODE_ALGO_AEC)
    {
        LOG_DBG("MAXM86146 entering AEC ALGO mode...");

        /* 6. Enable AEC (0x50 0x07 0x0B 0x01) */
        m_i2c_write_cmd_4(dev, 0x50, 0x07, 0x0B, 0x01, MAXM86146_DEFAULT_CMD_DELAY);

        /* 7. Enable Auto PD Current Calculation (0x50 0x07 0x12 0x01) */
        m_i2c_write_cmd_4(dev, 0x50, 0x07, 0x12, 0x01, MAXM86146_DEFAULT_CMD_DELAY);

        /* 8. Enable Skin Contact Detection (0x50 0x07 0x0C 0x01) */
        m_i2c_write_cmd_4(dev, 0x50, 0x07, 0x0C, 0x01, MAXM86146_DEFAULT_CMD_DELAY);

        /* 9. Configure MAXM86146-specific LED/PD mapping */
        maxm86146_configure_led_pd_mapping(dev);

        /* 10. Enable HR and SpO2 algorithm (0x52 0x07 0x01) */
        m_i2c_write_cmd_3(dev, 0x52, 0x07, 0x01, 500);
    }
    else if (mode == MAXM86146_OP_MODE_ALGO_AGC)
    {
        LOG_DBG("MAXM86146 entering AGC ALGO mode...");

        /* Disable Auto PD for AGC mode */
        m_i2c_write_cmd_4(dev, 0x50, 0x07, 0x12, 0x00, MAXM86146_DEFAULT_CMD_DELAY);

        /* Disable SCD for AGC mode */
        m_i2c_write_cmd_4(dev, 0x50, 0x07, 0x0C, 0x00, MAXM86146_DEFAULT_CMD_DELAY);

        /* Set AGC target PD current */
        m_i2c_write_cmd_5(dev, 0x50, 0x07, 0x11, 0x00, 0xFF);

        /* Configure MAXM86146-specific LED/PD mapping */
        maxm86146_configure_led_pd_mapping(dev);
        m_i2c_write_cmd_3(dev, 0x52, 0x07, 0x01, 500);
    }
    return 0;
}

int maxm86146_do_enter_app(const struct device *dev)
{
    const struct maxm86146_config *config = dev->config;

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

static int maxm86146_sample_fetch(const struct device *dev,
                                  enum sensor_channel chan)
{
    return 0;
}

static int maxm86146_channel_get(const struct device *dev,
                                 enum sensor_channel chan,
                                 struct sensor_value *val)
{
    return 0;
}

static int maxm86146_attr_set(const struct device *dev,
                              enum sensor_channel chan,
                              enum sensor_attribute attr,
                              const struct sensor_value *val)
{
    struct maxm86146_data *data = dev->data;

    switch (attr)
    {
    case MAXM86146_ATTR_OP_MODE:
        if (val->val1 == MAXM86146_OP_MODE_ALGO_AEC)
        {
            maxm86146_set_mode_algo(dev, MAXM86146_OP_MODE_ALGO_AEC, val->val2);
            data->op_mode = MAXM86146_OP_MODE_ALGO_AEC;
        }
        else if (val->val1 == MAXM86146_OP_MODE_ALGO_AGC)
        {
            maxm86146_set_mode_algo(dev, MAXM86146_OP_MODE_ALGO_AGC, val->val2);
            data->op_mode = MAXM86146_OP_MODE_ALGO_AGC;
        }
        else if (val->val1 == MAXM86146_OP_MODE_ALGO_EXTENDED)
        {
            maxm86146_set_mode_extended_algo(dev);
            data->op_mode = MAXM86146_OP_MODE_ALGO_EXTENDED;
        }
        else if (val->val1 == MAXM86146_OP_MODE_RAW)
        {
            maxm86146_set_mode_raw(dev);
            data->op_mode = MAXM86146_OP_MODE_RAW;
        }
        else if (val->val1 == MAXM86146_OP_MODE_SCD)
        {
            maxm86146_set_mode_scd(dev);
            data->op_mode = MAXM86146_OP_MODE_SCD;
        }
        else if (val->val1 == MAXM86146_OP_MODE_WAKE_ON_MOTION)
        {
            maxm86146_set_mode_wake_on_motion(dev);
            data->op_mode = MAXM86146_OP_MODE_WAKE_ON_MOTION;
        }
        else if (val->val1 == MAXM86146_OP_MODE_EXIT_WAKE_ON_MOTION)
        {
            maxm86146_exit_mode_wake_on_motion(dev);
            data->op_mode = MAXM86146_OP_MODE_IDLE;
        }
        else if (val->val1 == MAXM86146_OP_MODE_STOP_ALGO)
        {
            maxm86146_stop_algo(dev);
            data->op_mode = MAXM86146_OP_MODE_IDLE;
        }
        else
        {
            LOG_ERR("Unsupported sensor operation mode");
            return -ENOTSUP;
        }
        break;
    case MAXM86146_ATTR_ENTER_BOOTLOADER:
        break;
    default:
        LOG_ERR("Unsupported sensor attribute");
        return -ENOTSUP;
    }

    return 0;
}

static int maxm86146_check_app_present(const struct device *dev)
{
    struct maxm86146_data *data = dev->data;

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

static int maxm86146_attr_get(const struct device *dev,
                              enum sensor_channel chan,
                              enum sensor_attribute attr,
                              struct sensor_value *val)
{
    struct maxm86146_data *data = dev->data;

    switch (attr)
    {
    case MAXM86146_ATTR_OP_MODE:
        val->val1 = data->op_mode;
        val->val2 = 0;
        break;
    case MAXM86146_ATTR_IS_APP_PRESENT:
        val->val1 = maxm86146_check_app_present(dev);
        val->val2 = 0;
        break;
    case MAXM86146_ATTR_APP_VER:
        val->val1 = data->hub_ver[2];
        val->val2 = data->hub_ver[3];
        break;
    case MAXM86146_ATTR_SENSOR_IDS:
        val->val1 = data->afe_id;
        val->val2 = data->accel_id;
        break;
    case MAXM86146_ATTR_FW_PREFIX:
        val->val1 = data->hub_ver[1];  /* Device type prefix: 32=MAX32664C, 33=MAXM86146 */
        val->val2 = 0;
        break;
    default:
        LOG_ERR("Unsupported sensor attribute");
        return -ENOTSUP;
    }

    return 0;
}

static const struct sensor_driver_api maxm86146_driver_api = {
    .attr_set = maxm86146_attr_set,
    .attr_get = maxm86146_attr_get,

    .sample_fetch = maxm86146_sample_fetch,
    .channel_get = maxm86146_channel_get,

#ifdef CONFIG_SENSOR_ASYNC_API
    .get_decoder = (sensor_get_decoder_t)maxm86146_get_decoder,
    .submit = (sensor_submit_t)maxm86146_submit,
#endif
};

static int maxm86146_chip_init(const struct device *dev)
{
    const struct maxm86146_config *config = dev->config;
    struct maxm86146_data *data = dev->data;

    if (!device_is_ready(config->i2c.bus))
    {
        LOG_ERR("I2C not ready");
        return -ENODEV;
    }

    gpio_pin_configure_dt(&config->reset_gpio, GPIO_OUTPUT);
    gpio_pin_configure_dt(&config->mfio_gpio, GPIO_OUTPUT);

    maxm86146_do_enter_app(dev);

    bool hub_found = false;

    if (maxm86146_get_ver(dev, data->hub_ver) == 0)
    {
        hub_found = true;
        LOG_INF("MAXM86146 Hub Version: %d.%d.%d", data->hub_ver[1], data->hub_ver[2], data->hub_ver[3]);
    }
    else
    {
        LOG_ERR("MAXM86146 not responding");
        return -ENODEV;
    }

    maxm86146_check_sensors(dev);

    return 0;
}

#ifdef CONFIG_PM_DEVICE
static int maxm86146_pm_action(const struct device *dev,
                               enum pm_device_action action)
{
    int ret = 0;

    switch (action)
    {
    case PM_DEVICE_ACTION_RESUME:
        break;
    case PM_DEVICE_ACTION_SUSPEND:
        break;
    default:
        return -ENOTSUP;
    }

    return ret;
}
#endif /* CONFIG_PM_DEVICE */

int maxm86146_test_motion_detection(const struct device *dev)
{
    struct maxm86146_data *data = dev->data;

    if (!data->has_accel)
    {
        LOG_INF("Testing MAXM86146 SCD-based wake detection (no accelerometer)...");
    }
    else
    {
        LOG_INF("Testing MAXM86146 motion detection...");
    }

    uint8_t original_mode = data->op_mode;

    maxm86146_set_mode_wake_on_motion(dev);
    data->op_mode = MAXM86146_OP_MODE_WAKE_ON_MOTION;

    if (data->has_accel)
    {
        LOG_INF("Motion detection active - move the device now!");
    }
    else
    {
        LOG_INF("SCD wake detection active - place device on skin!");
    }
    LOG_INF("Monitoring for 10 seconds...");

    for (int i = 0; i < 100; i++) {
        uint8_t hub_stat = maxm86146_read_hub_status(dev);
        if (hub_stat & MAXM86146_HUB_STAT_DRDY_MASK) {
            if (data->has_accel)
            {
                LOG_INF("Motion event detected! Hub status: 0x%02x", hub_stat);
            }
            else
            {
                LOG_INF("SCD event detected! Hub status: 0x%02x", hub_stat);
            }

            uint8_t chip_op_mode;
            maxm86146_async_sample_fetch_wake_on_motion(dev, &chip_op_mode);
        }
        k_msleep(100);
    }

    data->op_mode = original_mode;
    LOG_INF("Wake detection test complete");

    return 0;
}

#define MAXM86146_DEFINE(inst)                                      \
    static struct maxm86146_data maxm86146_data_##inst;             \
    static const struct maxm86146_config maxm86146_config_##inst =  \
        {                                                           \
            .i2c = I2C_DT_SPEC_INST_GET(inst),                      \
            .reset_gpio = GPIO_DT_SPEC_INST_GET(inst, reset_gpios), \
            .mfio_gpio = GPIO_DT_SPEC_INST_GET(inst, mfio_gpios),   \
    };                                                              \
    PM_DEVICE_DT_INST_DEFINE(inst, maxm86146_pm_action);            \
    SENSOR_DEVICE_DT_INST_DEFINE(inst,                              \
                                 maxm86146_chip_init,               \
                                 PM_DEVICE_DT_INST_GET(inst),       \
                                 &maxm86146_data_##inst,            \
                                 &maxm86146_config_##inst,          \
                                 POST_KERNEL,                       \
                                 CONFIG_SENSOR_INIT_PRIORITY,       \
                                 &maxm86146_driver_api)

DT_INST_FOREACH_STATUS_OKAY(MAXM86146_DEFINE)
