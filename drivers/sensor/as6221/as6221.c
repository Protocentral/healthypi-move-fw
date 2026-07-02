// ProtoCentral Electronics (ashwin@protocentral.com)
// SPDX-License-Identifier: Apache-2.0
//
// Driver for the ams AS6221 digital temperature sensor. Used on newer
// HealthyPi Move boards in place of the MAX30208. Both expose temperature
// through SENSOR_CHAN_AMBIENT_TEMP in degrees Celsius so they can be used
// interchangeably by the application.

#define DT_DRV_COMPAT ams_as6221

#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/kernel.h>

#include "as6221.h"

LOG_MODULE_REGISTER(AS6221, CONFIG_SENSOR_LOG_LEVEL);

/* Read a 16-bit big-endian register (register pointer write + 2-byte read). */
static int as6221_read_reg16(const struct device *dev, uint8_t reg, uint16_t *val)
{
	const struct as6221_config *config = dev->config;
	uint8_t buf[2] = {0};
	int ret;

	ret = i2c_burst_read_dt(&config->i2c, reg, buf, sizeof(buf));
	if (ret < 0)
	{
		LOG_ERR("Failed to read register 0x%02X: %d", reg, ret);
		return ret;
	}

	*val = ((uint16_t)buf[0] << 8) | (uint16_t)buf[1];
	return 0;
}

static int as6221_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	struct as6221_data *data = dev->data;
	uint16_t raw;
	int ret;

	if (chan != SENSOR_CHAN_ALL && chan != SENSOR_CHAN_AMBIENT_TEMP)
	{
		LOG_ERR("Unsupported sensor channel: %d", chan);
		return -ENOTSUP;
	}

	/* AS6221 runs continuous conversions by default; just read TVAL. */
	ret = as6221_read_reg16(dev, AS6221_REG_TVAL, &raw);
	if (ret < 0)
	{
		return ret;
	}

	data->raw_temp = (int16_t)raw; /* TVAL is two's complement */
	return 0;
}

static int as6221_channel_get(const struct device *dev, enum sensor_channel chan,
							  struct sensor_value *val)
{
	struct as6221_data *data = dev->data;

	if (chan != SENSOR_CHAN_AMBIENT_TEMP)
	{
		LOG_ERR("Unsupported sensor channel: %d", chan);
		return -ENOTSUP;
	}

	/* temperature [°C] = raw * 0.0078125 ; convert to sensor_value (deg + micro) */
	int64_t micro_c = ((int64_t)data->raw_temp * AS6221_LSB_UCELSIUS_X10) / 10;

	val->val1 = (int32_t)(micro_c / 1000000);
	val->val2 = (int32_t)(micro_c % 1000000);

	return 0;
}

static const struct sensor_driver_api as6221_driver_api = {
	.sample_fetch = as6221_sample_fetch,
	.channel_get = as6221_channel_get,
};

static int as6221_init(const struct device *dev)
{
	const struct as6221_config *config = dev->config;
	uint16_t cfg;
	int ret;

	if (!device_is_ready(config->i2c.bus))
	{
		LOG_ERR("Bus device is not ready");
		return -ENODEV;
	}

	/* AS6221 has no chip-ID register; probe by reading the config register.
	 * A successful ACK on the dedicated address confirms presence. */
	ret = as6221_read_reg16(dev, AS6221_REG_CONFIG, &cfg);
	if (ret < 0)
	{
		LOG_ERR("AS6221 not responding on I2C (probe failed): %d", ret);
		return -ENODEV;
	}

	LOG_INF("AS6221 temperature sensor initialized (config=0x%04X)", cfg);
	return 0;
}

#ifdef CONFIG_PM_DEVICE

static int as6221_pm_action(const struct device *dev, enum pm_device_action action)
{
	ARG_UNUSED(dev);
	int ret = 0;

	switch (action)
	{
	case PM_DEVICE_ACTION_RESUME:
		LOG_DBG("AS6221 resume");
		break;

	case PM_DEVICE_ACTION_SUSPEND:
		LOG_DBG("AS6221 suspend");
		break;

	default:
		LOG_ERR("Unsupported PM action: %d", action);
		ret = -ENOTSUP;
		break;
	}

	return ret;
}
#endif /* CONFIG_PM_DEVICE */

#define AS6221_DEFINE(inst)                                      \
	static struct as6221_data as6221_data_##inst;                \
	static const struct as6221_config as6221_config_##inst =     \
		{                                                        \
			.i2c = I2C_DT_SPEC_INST_GET(inst),                   \
	};                                                           \
	PM_DEVICE_DT_INST_DEFINE(inst, as6221_pm_action);            \
	SENSOR_DEVICE_DT_INST_DEFINE(inst,                           \
								 as6221_init,                    \
								 PM_DEVICE_DT_INST_GET(inst),    \
								 &as6221_data_##inst,            \
								 &as6221_config_##inst,          \
								 POST_KERNEL,                    \
								 CONFIG_SENSOR_INIT_PRIORITY,    \
								 &as6221_driver_api);

DT_INST_FOREACH_STATUS_OKAY(AS6221_DEFINE)
