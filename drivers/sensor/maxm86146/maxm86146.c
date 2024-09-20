/*
 * (c) 2024 Protocentral Electronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT maxim_maxm86146

#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/drivers/gpio.h>

#include "maxm86146.h"

LOG_MODULE_REGISTER(MAXM86146, CONFIG_SENSOR_LOG_LEVEL);

#define CALIBVECTOR_SIZE 827 // Command 3 bytes + 824 bytes of calib vectors
#define DATE_TIME_VECTOR_SIZE 11
#define SPO2_CAL_COEFFS_SIZE 15

#define DEFAULT_SPO2_A 1.5958422
#define DEFAULT_SPO2_B -34.659664
#define DEFAULT_SPO2_C 112.68987

#define MAXM86146_FW_BIN_INCLUDE 0

static int m_read_op_mode(const struct device *dev)
{
    // struct maxm86146_data *data = dev->data;
    const struct maxm86146_config *config = dev->config;
    uint8_t rd_buf[2] = {0x00, 0x00};

    uint8_t wr_buf[2] = {0x02, 0x00};

    k_sleep(K_USEC(300));
    i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
    k_sleep(K_MSEC(45));
    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));
    i2c_read_dt(&config->i2c, rd_buf, sizeof(rd_buf));
    k_sleep(K_MSEC(45));
    gpio_pin_set_dt(&config->mfio_gpio, 1);

    // LOG_INF("Op mode = %x\n", rd_buf[1]);

    return rd_buf[1];
}

uint8_t maxm86146_read_hub_status(const struct device *dev)
{
    /*
    Table 7. Sensor Hub Status Byte
    BIT 7 6 5 4 3 2 1 0
    Field Reserved HostAccelUfInt FifoInOverInt FifoOutOvrInt DataRdyInt Err2 Err1 Err0
    */

    const struct maxm86146_config *config = dev->config;
    uint8_t rd_buf[2] = {0x00, 0x00};
    uint8_t wr_buf[2] = {0x00, 0x00};

    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));

    i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
    i2c_read_dt(&config->i2c, rd_buf, sizeof(rd_buf));
    k_sleep(K_USEC(300));
    gpio_pin_set_dt(&config->mfio_gpio, 1);

    // printk("Stat %x | ", rd_buf[1]);

    return rd_buf[1];
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

    i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
    k_sleep(K_MSEC(cmd_delay));

    i2c_read_dt(&config->i2c, rd_buf, sizeof(rd_buf));
    k_sleep(K_USEC(300));

    gpio_pin_set_dt(&config->mfio_gpio, 1);

    printk("CMD: %x %x %x | RSP: %x\n", wr_buf[0], wr_buf[1], wr_buf[2], rd_buf[0]);

    k_sleep(K_MSEC(10));

    return 0;
}

static int m_i2c_write_cmd_4(const struct device *dev, uint8_t byte1, uint8_t byte2, uint8_t byte3, uint8_t byte4)
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

    i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
    k_sleep(K_MSEC(MAXM86146_DEFAULT_CMD_DELAY));
    i2c_read_dt(&config->i2c, rd_buf, 1);
    k_sleep(K_MSEC(MAXM86146_DEFAULT_CMD_DELAY));

    gpio_pin_set_dt(&config->mfio_gpio, 1);

    printk("CMD: %x %x %x %x | RSP: %x\n", wr_buf[0], wr_buf[1], wr_buf[2], wr_buf[3], rd_buf[0]);

    k_sleep(K_MSEC(45));

    return 0;
}

static int m_i2c_write(const struct device *dev, uint8_t *wr_buf, uint32_t wr_len)
{
    const struct maxm86146_config *config = dev->config;

    uint8_t rd_buf[1] = {0x00};

    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));
    i2c_write_dt(&config->i2c, wr_buf, wr_len);

    k_sleep(K_MSEC(MAXM86146_DEFAULT_CMD_DELAY));

    k_sleep(K_USEC(300));
    i2c_read_dt(&config->i2c, rd_buf, sizeof(rd_buf));
    k_sleep(K_MSEC(MAXM86146_DEFAULT_CMD_DELAY));

    gpio_pin_set_dt(&config->mfio_gpio, 1);

    printk("Write %d bytes | RSP: %d \n", wr_len, rd_buf[0]);

    k_sleep(K_MSEC(45));

    return 0;
}

static int maxm86146_set_spo2_coeffs(const struct device *dev, float a, float b, float c)
{
    const struct max32664_config *config = dev->config;

    uint8_t wr_buf[15] = {0x50, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t rd_buf[1] = {0x00};

    int32_t a_int = (int32_t)(a * 100000);

    wr_buf[3] = (a_int & 0xff000000) >> 24;
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

    m_i2c_write(dev, wr_buf, sizeof(wr_buf));

    return 0;
}

static int maxm86146_set_mode_extended_algo(const struct device *dev)
{
    LOG_DBG("MAXM86146 entering extended ALGO mode...\n");

    maxm86146_set_spo2_coeffs(dev, DEFAULT_SPO2_A, DEFAULT_SPO2_B, DEFAULT_SPO2_C);

    // Output mode sensor + algo data
    m_i2c_write_cmd_3(dev, 0x10, 0x00, 0x03, MAXM86146_DEFAULT_CMD_DELAY);

    // Set interrupt threshold
    m_i2c_write_cmd_3(dev, 0x10, 0x01, 0x02, MAXM86146_DEFAULT_CMD_DELAY);

    // Set report period
    m_i2c_write_cmd_3(dev, 0x10, 0x02, 0x01, MAXM86146_DEFAULT_CMD_DELAY);

    // Set continuous mode
    m_i2c_write_cmd_4(dev, 0x50, 0x07, 0x0A, 0x00);

    // Enable AEC
    m_i2c_write_cmd_4(dev, 0x50, 0x07, 0x0B, 0x01);

    // Disable Auto PD
    m_i2c_write_cmd_4(dev, 0x50, 0x07, 0x12, 0x01);

    // Disable SCD
    m_i2c_write_cmd_4(dev, 0x50, 0x07, 0x0C, 0x01);

    // Set AGC target PD current
    // m_i2c_write_cmd_5(dev, 0x50, 0x07, 0x11, 0x00, 0x64);

    // Enable HR, SpO2 algo
    m_i2c_write_cmd_3(dev, 0x52, 0x07, 0x02, 500);
    k_sleep(K_MSEC(500));

    return 0;
}

static int maxm86146_set_mode_algo(const struct device *dev)
{
    LOG_DBG("MAXM86146 entering ALGO mode...\n");

    maxm86146_set_spo2_coeffs(dev, DEFAULT_SPO2_A, DEFAULT_SPO2_B, DEFAULT_SPO2_C);

    // Output mode sensor + algo data
    m_i2c_write_cmd_3(dev, 0x10, 0x00, 0x03, MAXM86146_DEFAULT_CMD_DELAY);

    // Set interrupt threshold
    m_i2c_write_cmd_3(dev, 0x10, 0x01, 0x02, MAXM86146_DEFAULT_CMD_DELAY);

    // Set report period
    m_i2c_write_cmd_3(dev, 0x10, 0x02, 0x01, MAXM86146_DEFAULT_CMD_DELAY);

    // Set continuous mode
    m_i2c_write_cmd_4(dev, 0x50, 0x07, 0x0A, 0x00);

    // Enable AEC
    m_i2c_write_cmd_4(dev, 0x50, 0x07, 0x0B, 0x01);

    // Disable Auto PD
    m_i2c_write_cmd_4(dev, 0x50, 0x07, 0x12, 0x01);

    // Disable SCD
    m_i2c_write_cmd_4(dev, 0x50, 0x07, 0x0C, 0x01);

    // Set AGC target PD current
    // m_i2c_write_cmd_5(dev, 0x50, 0x07, 0x11, 0x00, 0x64);

    // Read  WHOAMI
    // m_i2c_write_cmd_3_rsp_2(dev, 0x41, 0x04, 0xFF);
    // m_i2c_write_cmd_3_rsp_2(dev, 0x41, 0x04, 0x0F);

    // Enable HR, SpO2 algo
    m_i2c_write_cmd_3(dev, 0x52, 0x07, 0x01, 500);
    k_sleep(K_MSEC(500));

    return 0;
}

static int maxm86146_get_ver(const struct device *dev, uint8_t *ver_buf)
{
    const struct maxm86146_config *config = dev->config;

    uint8_t wr_buf[2] = {0xFF, 0x03};

    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));
    i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
    k_sleep(K_MSEC(MAXM86146_DEFAULT_CMD_DELAY));

    i2c_read_dt(&config->i2c, ver_buf, 4);
    k_sleep(K_MSEC(MAXM86146_DEFAULT_CMD_DELAY));

     gpio_pin_set_dt(&config->mfio_gpio, 1);

    LOG_INF("Version (decimal) = %d.%d.%d\n", ver_buf[1], ver_buf[2], ver_buf[3]);

    if (ver_buf[1] == 0x00 && ver_buf[2] == 0x00 && ver_buf[3] == 0x00)
    {
        return -ENODEV;
    }
    return 0;
}

int maxm86146_do_enter_app(const struct device *dev)
{
    const struct maxm86146_config *config = dev->config;

    LOG_DBG("MAXM86146 entering application mode\n");

    gpio_pin_configure_dt(&config->mfio_gpio, GPIO_OUTPUT);

    // Enter APPLICATION mode
    gpio_pin_set_dt(&config->mfio_gpio, 1);
    k_sleep(K_MSEC(10));

    gpio_pin_set_dt(&config->reset_gpio, 0);
    k_sleep(K_MSEC(20));

    gpio_pin_set_dt(&config->reset_gpio, 1);
    k_sleep(K_MSEC(1000));
    // End of APPLICATION mode

    // gpio_pin_configure_dt(&config->mfio_gpio, GPIO_INPUT);
    k_sleep(K_MSEC(10));

    m_read_op_mode(dev);

    uint8_t ver_buf[4] = {0};
    if (maxm86146_get_ver(dev, ver_buf) == 0)
    {
        LOG_INF("MAXM86146 version: %d.%d.%d\n", ver_buf[1], ver_buf[2], ver_buf[3]);
    }
    else
    {
        //LOG_ERR("MAXM86146 not responding\n");
        return -ENODEV;
    }

    maxm86146_read_hub_status(dev);
    k_sleep(K_MSEC(200));
    maxm86146_read_hub_status(dev);

    return 0;
}

static int maxm86146_sample_fetch(const struct device *dev,
                                  enum sensor_channel chan)
{
    // Not implemented

    return 0;
}

static int maxm86146_channel_get(const struct device *dev,
                                 enum sensor_channel chan,
                                 struct sensor_value *val)
{
    // Not implemented

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
        if (val->val1 == MAXM86146_OP_MODE_ALGO)
        {
            maxm86146_set_mode_algo(dev);
            data->op_mode = MAXM86146_OP_MODE_ALGO;
        }
        else if (val->val1 == MAXM86146_OP_MODE_ALGO_EXTENDED)
        {
            maxm86146_set_mode_extended_algo(dev);
            data->op_mode = MAXM86146_OP_MODE_ALGO_EXTENDED;
        }
        else
        {
            LOG_ERR("Unsupported sensor operation mode");
            return -ENOTSUP;
        }
        break;
    case MAXM86146_ATTR_ENTER_BOOTLOADER:
		maxm86146_do_enter_bl(dev);
		break;
    default:
        LOG_ERR("Unsupported sensor attribute");
        return -ENOTSUP;
    }

    return 0;
}

static const struct sensor_driver_api maxm86146_driver_api = {
    .attr_set = maxm86146_attr_set,

    .sample_fetch = maxm86146_sample_fetch,
    .channel_get = maxm86146_channel_get,

#ifdef CONFIG_SENSOR_ASYNC_API
    .submit = maxm86146_submit,
    .get_decoder = maxm86146_get_decoder,
#endif
};

static int maxm86146_chip_init(const struct device *dev)
{
    const struct maxm86146_config *config = dev->config;

    if (!device_is_ready(config->i2c.bus))
    {
        LOG_ERR("I2C device is not ready");
        return -ENODEV;
    }

    gpio_pin_configure_dt(&config->reset_gpio, GPIO_OUTPUT);
    gpio_pin_configure_dt(&config->mfio_gpio, GPIO_OUTPUT);

    return maxm86146_do_enter_app(dev);
}

#ifdef CONFIG_PM_DEVICE
static int maxm86146_pm_action(const struct device *dev,
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

/*
 * Main instantiation macro, which selects the correct bus-specific
 * instantiation macros for the instance.
 */
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