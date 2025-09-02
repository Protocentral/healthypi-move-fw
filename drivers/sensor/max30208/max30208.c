// ProtoCentral Electronics (ashwin@protocentral.com)
// SPDX-License-Identifier: Apache-2.0

#define DT_DRV_COMPAT maxim_max30208

#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/kernel.h>

#include "max30208.h"

#define BUFFER_LENGTH 200
#define N_DECIMAL_POINTS_PRECISION 1000000

LOG_MODULE_REGISTER(MAX30208, CONFIG_SENSOR_LOG_LEVEL);

static uint8_t m_read_reg(const struct device *dev, uint8_t reg, uint8_t *read_buf)
{
	const struct max30208_config *config = dev->config;

	struct i2c_msg msgs[2] = {
		{
			.buf = &reg,
			.len = 1,
			.flags = I2C_MSG_WRITE,
		},
		{
			.buf = read_buf,
			.len = 1,
			.flags = I2C_MSG_RESTART | I2C_MSG_READ | I2C_MSG_STOP,
		},
	};

	return i2c_transfer_dt(&config->i2c, msgs, 2);
}

static uint8_t max30208_read_reg(const struct device *dev, uint8_t reg, uint8_t *read_buf, uint8_t read_len)
{
	const struct max30208_config *config = dev->config;

	/*struct i2c_msg m_msgs[2];
	uint8_t reg_buf[1] = {reg};

	m_msgs[0].buf = reg_buf;
	m_msgs[0].len = 1U;
	m_msgs[0].flags = I2C_MSG_WRITE;

	m_msgs[1].buf = read_buf;
	m_msgs[1].len = read_len;
	m_msgs[1].flags = I2C_MSG_READ | I2C_MSG_STOP | I2C_MSG_RESTART;

	int ret = i2c_transfer_dt(&config->i2c, m_msgs, 2);
	if (ret < 0)
	{
		LOG_ERR("Failed to read register: %d", ret);
	}*/

	uint8_t wr_buf[1] = {reg};
	int ret = 0;

	ret = i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
	k_sleep(K_MSEC(10));
	ret = i2c_read_dt(&config->i2c, read_buf, read_len);

	// printk("Read register: %x\n", read_buf[0]);
	return ret;
}

static int max30208_write_reg(const struct device *dev, uint8_t reg, uint8_t val)
{
	const struct max30208_config *config = dev->config;

	uint8_t write_buf[2] = {reg, val};

	i2c_write_dt(&config->i2c, write_buf, sizeof(write_buf));

	return 0;
}

static int max30208_get_chip_id(const struct device *dev, uint8_t *id)
{
	uint8_t read_buf[1] = {0};

	max30208_read_reg(dev, MAX30208_REG_CHIP_ID, read_buf, 1U);
	LOG_DBG("MAX30208 Chip ID: %x", read_buf[0]);
	id[0] = read_buf[0];
	return 0;
}

static int max30208_start_convert(const struct device *dev)
{
	max30208_write_reg(dev, MAX30208_REG_TEMP_SENSOR_SETUP, MAX30208_CONVERT_T);
	return 0;
}

static uint8_t max30208_get_status(const struct device *dev)
{
	uint8_t read_buf[1] = {0};
	max30208_read_reg(dev, MAX30208_REG_STATUS, read_buf, 1U);
	// LOG_DBG("MAX30208 Status: %x\n", read_buf[0]);
	return read_buf[0];
}

static int max30208_get_temp(const struct device *dev)
{
	uint8_t read_buf[2] = {0, 0};
	max30208_read_reg(dev, MAX30208_REG_FIFO_DATA, read_buf, 2U);
	int16_t raw = read_buf[0] << 8 | read_buf[1];
	// LOG_DBG("Raw Temp: %d\n", raw);
	return raw;
}

static int max30208_sample_fetch(const struct device *dev,
								 enum sensor_channel chan)
{
	struct max30208_data *data = dev->data;

	max30208_start_convert(dev);

	// while(!(max30208_get_status(dev) & 0x01))
	//{
	//	k_sleep(K_MSEC(10));
	// }
	k_sleep(K_MSEC(100));

	data->temp_int = max30208_get_temp(dev);

	return 0;
}

static int max30208_channel_get(const struct device *dev, enum sensor_channel chan, struct sensor_value *val)
{
	struct max30208_data *data = dev->data;
	switch (chan)
	{
	case SENSOR_CHAN_AMBIENT_TEMP:
		val->val1 = (int)((data->temp_int));

		val->val2 = 0;

		break;
	default:
		LOG_ERR("Unsupported sensor channel");
		return -ENOTSUP;
	}

	return 0;
}

static const struct sensor_driver_api max30208_driver_api = {
	.sample_fetch = max30208_sample_fetch,
	.channel_get = max30208_channel_get,
};

static int max30208_init(const struct device *dev)
{
	const struct max30208_config *config = dev->config;
	int ret = 0;

	uint8_t chip_id[1] = {0};

	/* Get the I2C device */
	if (!device_is_ready(config->i2c.bus))
	{
		LOG_ERR("Bus device is not ready");
		return -ENODEV;
	}

	ret = max30208_get_chip_id(dev, chip_id);
	if (ret < 0)
	{
		LOG_ERR("Failed to get chip id");
		return -ENODEV;
	}

	if (chip_id[0] != MAX30208_CHIP_ID)
	{
		LOG_ERR("Invalid chip id: %x", chip_id[0]);
		return -ENODEV;
	}

	return 0;
}

#ifdef CONFIG_PM_DEVICE

static int max30208_pm_action(const struct device *dev, enum pm_device_action action)
{
	switch (action)
	{
	case PM_DEVICE_ACTION_RESUME:
		/* Enable sensor */
		break;

	case PM_DEVICE_ACTION_SUSPEND:
		/* Disable sensor */
		break;

	default:
		return -ENOTSUP;
	}

	return 0;
}
#endif /* CONFIG_PM_DEVICE */

#define MAX30208_DEFINE(inst)                                    \
	static struct max30208_data max30208_data_##inst;            \
	static const struct max30208_config max30208_config_##inst = \
		{                                                        \
			.i2c = I2C_DT_SPEC_INST_GET(inst),                   \
	};                                                           \
	PM_DEVICE_DT_INST_DEFINE(inst, max30208_pm_action);          \
	SENSOR_DEVICE_DT_INST_DEFINE(inst,                           \
								 max30208_init,                  \
								 PM_DEVICE_DT_INST_GET(inst),    \
								 &max30208_data_##inst,          \
								 &max30208_config_##inst,        \
								 POST_KERNEL,                    \
								 CONFIG_SENSOR_INIT_PRIORITY,    \
								 &max30208_driver_api)

DT_INST_FOREACH_STATUS_OKAY(MAX30208_DEFINE)
