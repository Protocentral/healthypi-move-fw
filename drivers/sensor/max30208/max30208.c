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

/* sensor-specific headers */
#if defined(CONFIG_SENSOR)
#include <zephyr/rtio/rtio.h>
#include <zephyr/drivers/sensor.h>

static int max30208_i2c_read(const struct device *dev, uint8_t reg, uint8_t *buf, size_t len)
{
	const struct max30208_config *config = dev->config;

	int ret = i2c_write_read_dt(&config->i2c, &reg, 1, buf, len);
	if (ret < 0) {
		LOG_ERR("i2c write_read failed reg=0x%02x ret=%d", reg, ret);
	}
	return ret;
}

static int max30208_i2c_write(const struct device *dev, uint8_t reg, uint8_t val)
{
	const struct max30208_config *config = dev->config;
	int ret = i2c_reg_write_byte_dt(&config->i2c, reg, val);
	if (ret < 0) {
		LOG_ERR("i2c reg write failed reg=0x%02x val=0x%02x ret=%d", reg, val, ret);
	}
	return ret;
}

static int max30208_get_chip_id(const struct device *dev, uint8_t* id)
{
	uint8_t read_buf[1] = {0};

	max30208_i2c_read(dev, MAX30208_REG_CHIP_ID, read_buf, 1U);
	LOG_DBG("MAX30208 Chip ID: %x", read_buf[0]);
	id[0] = read_buf[0];
	return 0;
}

static int max30208_start_convert(const struct device *dev)
{
	max30208_i2c_write(dev, MAX30208_REG_TEMP_SENSOR_SETUP, MAX30208_CONVERT_T);
	return 0;
}

static uint8_t max30208_get_status(const struct device *dev)
{
	uint8_t read_buf[1] = {0};
	max30208_i2c_read(dev, MAX30208_REG_STATUS, read_buf, 1U);
	// LOG_DBG("MAX30208 Status: %x\n", read_buf[0]);
	return read_buf[0];
}

/* Blocking spot measurement sequence:
 * 1) write TEMP_SENSOR_SETUP to request a one-shot conversion
 * 2) poll STATUS until data ready or timeout
 * 3) read two bytes from FIFO data and return signed int16 raw value
 */
int max30208_get_temp(const struct device *dev, int32_t timeout_ms)
{
	uint8_t buf[2] = {0};
	int ret;

	/* Start a single conversion */
	ret = max30208_i2c_write(dev, MAX30208_REG_TEMP_SENSOR_SETUP, MAX30208_CONVERT_T);
	if (ret < 0) {
		return ret;
	}

	/* Poll status register until bit0 indicates data ready or timeout */
	uint32_t elapsed = 0U;
	const uint32_t poll_ms = 5U;
	while (elapsed < (uint32_t)timeout_ms) {
		uint8_t status = 0;
		ret = max30208_i2c_read(dev, MAX30208_REG_STATUS, &status, 1);
		if (ret < 0) {
			return ret;
		}
		if (status & 0x01) {
			break;
		}
		k_msleep(poll_ms);
		elapsed += poll_ms;
	}
	if (elapsed >= (uint32_t)timeout_ms) {
		LOG_ERR("MAX30208: conversion timeout after %d ms", timeout_ms);
		return -ETIMEDOUT;
	}

	ret = max30208_i2c_read(dev, MAX30208_REG_FIFO_DATA, buf, 2);
	if (ret < 0) {
		return ret;
	}

	int16_t raw = (int16_t)((buf[0] << 8) | buf[1]);
	return raw;
}

static int max30208_sample_fetch(const struct device *dev,
								 enum sensor_channel chan)
{
	struct max30208_data *data = dev->data;
	int ret;

	ARG_UNUSED(chan);

	ret = max30208_get_temp(dev, 500); /* 500 ms timeout */
	if (ret < 0) {
		LOG_ERR("sample_fetch failed: %d", ret);
		return ret;
	}

	data->temp_int = ret;
	return 0;
}

static int max30208_channel_get(const struct device *dev, enum sensor_channel chan, struct sensor_value *val)
{
	struct max30208_data *data = dev->data;
	switch (chan)
	{
	case SENSOR_CHAN_AMBIENT_TEMP:
	/* data->temp_int is raw counts (LSB = 0.005 degC) */
	val->val1 = data->temp_int; /* keep raw in val1 for backwards compat */
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

#ifdef CONFIG_SENSOR_ASYNC_API
static const struct sensor_driver_api max30208_driver_api_async = {
	.sample_fetch = max30208_sample_fetch,
	.channel_get = max30208_channel_get,
	.submit = (void (*)(const struct device *, struct rtio_iodev_sqe *))max30208_submit,
	.get_decoder = max30208_get_decoder,
};
#endif

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

	if(chip_id[0] != MAX30208_CHIP_ID)
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

/* Define optional RTIO iodev and ctx per instance when async API enabled */
/* Per-instance RTIO/iodev definitions when async API is enabled */
#ifdef CONFIG_SENSOR_ASYNC_API
#define MAX30208_RTIO_INIT(inst)           \
	I2C_DT_IODEV_DEFINE(max30208_iodev_##inst, DT_DRV_INST(inst)); \
	/* Use a larger RTIO SQE/CQE pool (matches other reference drivers) */ \
	RTIO_DEFINE(max30208_rtio_##inst, 8, 8);                        \
	static struct max30208_data max30208_data_##inst = {             \
		.r = &max30208_rtio_##inst,                                   \
		.iodev = &max30208_iodev_##inst,                              \
	};
#else
#define MAX30208_RTIO_INIT(inst)           \
	static struct max30208_data max30208_data_##inst;
#endif

#define MAX30208_DEFINE(inst)                                    \
	MAX30208_RTIO_INIT(inst)                                     \
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
								 IS_ENABLED(CONFIG_SENSOR_ASYNC_API) ? &max30208_driver_api_async : &max30208_driver_api)

DT_INST_FOREACH_STATUS_OKAY(MAX30208_DEFINE)

#ifdef CONFIG_SENSOR_ASYNC_API
/* The application should use SENSOR_DT_READ_IODEV to obtain an iodev symbol */
#endif

#endif /* CONFIG_SENSOR */
