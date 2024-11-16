// ProtoCentral Electronics (info@protocentral.com)
// SPDX-License-Identifier: Apache-2.0

#define DT_DRV_COMPAT maxim_max30208

#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/kernel.h>

#include "max30208.h"

#define BUFFER_LENGTH 200
#define N_DECIMAL_POINTS_PRECISION 1000000

LOG_MODULE_REGISTER(MAX30208, CONFIG_SENSOR_LOG_LEVEL);

uint8_t m_read_reg_2(const struct device *dev, uint8_t reg, uint8_t *read_buf)
{
	const struct max30208_config *config = dev->config;
	i2c_write_read_dt(&config->i2c, &reg, sizeof(reg), read_buf, sizeof(read_buf));
	return 0;
}

static int max30208_get_fifo_avail(const struct device *dev)
{
	uint8_t read_buf[2] = {0, 0};
	m_read_reg_2(dev, MAX30208_TEMPERATURE, read_buf);
	int16_t raw = read_buf[0] << 8 | read_buf[1];
	//return readRegister8(MAX30208_FIFO_DATA_COUNTER);
	return raw;
}

static int max30208_sample_fetch(const struct device *dev,
								 enum sensor_channel chan)
{
	struct max30208_data *data = dev->data;
	/*uint8_t read_buf[2] = {0, 0};
	m_read_reg_2(dev, MAX30208_TEMPERATURE, read_buf);
	int16_t raw = read_buf[0] << 8 | read_buf[1];
	data->temperature = raw * 0.00390625; // convert to temperature
	data->temp_int = data->temperature * 1000;
	*/
	return 0;
}

static int max30208_channel_get(const struct device *dev, enum sensor_channel chan, struct sensor_value *val)
{
	struct max30208_data *data = dev->data;
	switch (chan)
	{
	case SENSOR_CHAN_AMBIENT_TEMP:
		val->val1 = (int)((data->temp_int));
		val->val2 = (int)((data->temp_int));
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

	/* Get the I2C device */
	if (!device_is_ready(config->i2c.bus))
	{
		LOG_ERR("Bus device is not ready");
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
								 PM_DEVICE_DT_INST_GET(inst),	 \
								 &max30208_data_##inst,          \
								 &max30208_config_##inst,        \
								 POST_KERNEL,                    \
								 CONFIG_SENSOR_INIT_PRIORITY,    \
								 &max30208_driver_api)

DT_INST_FOREACH_STATUS_OKAY(MAX30208_DEFINE)
