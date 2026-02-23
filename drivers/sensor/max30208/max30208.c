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

static uint8_t max30208_read_reg(const struct device *dev, uint8_t reg, uint8_t *read_buf, uint8_t read_len)
{
	const struct max30208_config *config = dev->config;
	uint8_t wr_buf[1] = {reg};
	int ret = 0;

	ret = i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
	if (ret < 0)
	{
		LOG_ERR("Failed to write register: %d", ret);
		return (uint8_t)ret;
	}

	k_sleep(K_MSEC(10));

	ret = i2c_read_dt(&config->i2c, read_buf, read_len);
	if (ret < 0)
	{
		LOG_ERR("Failed to read register: %d", ret);
	}

	return (uint8_t)ret;
}

static int max30208_write_reg(const struct device *dev, uint8_t reg, uint8_t val)
{
	const struct max30208_config *config = dev->config;
	uint8_t write_buf[2] = {reg, val};
	int ret;

	ret = i2c_write_dt(&config->i2c, write_buf, sizeof(write_buf));
	if (ret < 0)
	{
		LOG_ERR("Failed to write register 0x%02X: %d", reg, ret);
	}

	return ret;
}

static int max30208_get_chip_id(const struct device *dev, uint8_t *id)
{
	uint8_t read_buf[1] = {0};
	int ret;

	ret = (int)max30208_read_reg(dev, MAX30208_REG_CHIP_ID, read_buf, 1U);
	if (ret < 0)
	{
		LOG_ERR("Failed to read chip ID: %d", ret);
		return ret;
	}

	LOG_DBG("MAX30208 Chip ID: 0x%02X", read_buf[0]);
	*id = read_buf[0];
	return 0;
}

static int max30208_start_convert(const struct device *dev)
{
	int ret;

	ret = max30208_write_reg(dev, MAX30208_REG_TEMP_SENSOR_SETUP, MAX30208_CONVERT_T);
	if (ret < 0)
	{
		LOG_ERR("Failed to start temperature conversion: %d", ret);
	}

	return ret;
}

static int max30208_get_temp(const struct device *dev)
{
	uint8_t read_buf[2] = {0, 0};
	(void)max30208_read_reg(dev, MAX30208_REG_FIFO_DATA, read_buf, 2U);
	int16_t raw = ((int16_t)read_buf[0] << 8) | (int16_t)read_buf[1];
	return (int)raw;
}

static int max30208_sample_fetch(const struct device *dev,
								 enum sensor_channel chan)
{
	struct max30208_data *data = dev->data;
	int ret;

	/* Validate input parameter */
	if (chan != SENSOR_CHAN_ALL && chan != SENSOR_CHAN_AMBIENT_TEMP)
	{
		LOG_ERR("Unsupported sensor channel: %d", chan);
		return -ENOTSUP;
	}

	ret = max30208_start_convert(dev);
	if (ret < 0)
	{
		LOG_ERR("Failed to start conversion: %d", ret);
		return ret;
	}

	/* Wait for conversion to complete */
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
	{
		/* MAX30208 resolution: 0.005 °C/LSB
		 * Convert to standard Zephyr sensor_value (val1 = °C, val2 = micro-°C)
		 */
		int64_t micro_c = (int64_t)data->temp_int * 5000;
		val->val1 = (int32_t)(micro_c / 1000000);
		val->val2 = (int32_t)(micro_c % 1000000);

		if (micro_c < 0 && val->val2 != 0)
		{
			val->val1--;
			val->val2 += 1000000;
		}
		break;
	}

	default:
		LOG_ERR("Unsupported sensor channel: %d", chan);
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
	uint8_t chip_id = 0;

	/* Validate input parameter */
	if (dev == NULL)
	{
		LOG_ERR("Device pointer is NULL");
		return -EINVAL;
	}

	/* Get the I2C device */
	if (!device_is_ready(config->i2c.bus))
	{
		LOG_ERR("Bus device is not ready");
		return -ENODEV;
	}

	ret = max30208_get_chip_id(dev, &chip_id);
	if (ret < 0)
	{
		LOG_ERR("Failed to get chip id: %d", ret);
		return ret;
	}

	if (chip_id != MAX30208_CHIP_ID)
	{
		LOG_ERR("Invalid chip id: 0x%02X (expected 0x%02X)", chip_id, MAX30208_CHIP_ID);
		return -ENODEV;
	}

	LOG_INF("MAX30208 temperature sensor initialized successfully");
	return 0;
}

#ifdef CONFIG_PM_DEVICE

static int max30208_pm_action(const struct device *dev, enum pm_device_action action)
{
	int ret = 0;

	switch (action)
	{
	case PM_DEVICE_ACTION_RESUME:
		/* Enable sensor */
		LOG_DBG("MAX30208 resume");
		break;

	case PM_DEVICE_ACTION_SUSPEND:
		/* Disable sensor */
		LOG_DBG("MAX30208 suspend");
		break;

	default:
		LOG_ERR("Unsupported PM action: %d", action);
		ret = -ENOTSUP;
		break;
	}

	return ret;
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
