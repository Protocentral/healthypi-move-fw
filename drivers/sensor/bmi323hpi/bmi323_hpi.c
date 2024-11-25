/*
 * Copyright (c) 2023 Trackunit Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bmi323_hpi.h"
// #include "bmi323_spi.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bosch_bmi323hpi);

#define DT_DRV_COMPAT bosch_bmi323hpi

/* Value taken from BMI323 Datasheet section 5.8.1 */
#define IMU_BOSCH_FEATURE_ENGINE_STARTUP_CONFIG (0x012C)

#define IMU_BOSCH_DIE_TEMP_OFFSET_MICRO_DEG_CELCIUS (23000000LL)
#define IMU_BOSCH_DIE_TEMP_MICRO_DEG_CELCIUS_LSB (1953L)

typedef void (*bosch_bmi323_gpio_callback_ptr)(const struct device *dev, struct gpio_callback *cb,
											   uint32_t pins);

struct bmi323_config
{
	const struct i2c_dt_spec bus;
	const struct gpio_dt_spec int_gpio;

	const bosch_bmi323_gpio_callback_ptr int_gpio_callback;
};

struct bosch_bmi323_data
{
	struct k_mutex lock;

	struct sensor_value acc_samples[3];
	struct sensor_value gyro_samples[3];
	struct sensor_value temperature;

	bool acc_samples_valid;
	bool gyro_samples_valid;
	bool temperature_valid;

	uint32_t acc_full_scale;
	uint32_t gyro_full_scale;

	struct gpio_callback gpio_callback;
	const struct sensor_trigger *trigger;
	sensor_trigger_handler_t trigger_handler;
	struct k_work callback_work;
	const struct device *dev;

	struct bmi323_chip_internal_cfg chip_cfg;
};

static int bmi323_reg_read_i2c(const struct device *dev, uint8_t start, uint8_t *data, uint16_t len)
{
	const struct bmi323_config *config = (const struct bmi323_config *)dev->config;
	return i2c_burst_read_dt(&config->bus, start, data, len);
}

static int bmi323_reg_write_i2c(const struct device *dev, uint8_t start, const uint8_t *data, uint16_t len)
{
	const struct bmi323_config *config = (const struct bmi323_config *)dev->config;
	return i2c_burst_write_dt(&config->bus, start, data, len);
}

static int bmi323_read_word_16(const struct device *dev, uint8_t offset, uint16_t *words,
							   uint16_t words_count)
{
	return bmi323_reg_read_i2c(dev, offset, (uint8_t *)words, words_count * 2);
	// return bus->api->read_words(bus->context, offset, words, words_count);
}

static int bosch_bmi323_bus_write_words(const struct device *dev, uint8_t offset, uint16_t *words,
										uint16_t words_count)
{
	return bmi323_reg_write_i2c(dev, offset, (uint8_t *)words, words_count * 2);
	// return bus->api->write_words(bus->context, offset, words, words_count);
}

static int32_t bosch_bmi323_lsb_from_fullscale(int64_t fullscale)
{
	return (fullscale * 1000) / INT16_MAX;
}

/* lsb is the value of one 1/1000000 LSB */
static int64_t bosch_bmi323_value_to_micro(int16_t value, int32_t lsb)
{
	return ((int64_t)value) * lsb;
}

/* lsb is the value of one 1/1000000 LSB */
static void bosch_bmi323_value_to_sensor_value(struct sensor_value *result, int16_t value,
											   int32_t lsb)
{
	int64_t ll_value = (int64_t)value * lsb;
	int32_t int_part = (int32_t)(ll_value / 1000000);
	int32_t frac_part = (int32_t)(ll_value % 1000000);

	result->val1 = int_part;
	result->val2 = frac_part;
}

static void bosch_bmi323_sensor_value_from_micro(struct sensor_value *result, int64_t micro)
{
	int32_t int_part = (int32_t)(micro / 1000000);
	int32_t frac_part = (int32_t)(micro % 1000000);

	result->val1 = int_part;
	result->val2 = frac_part;
}

static int bmi323_read_word_8(const struct device *dev, uint8_t offset, uint8_t *data)
{
	const struct bmi323_config *config = (const struct bmi323_config *)dev->config;
	uint8_t tmp_buff[3];
	int ret;
	ret = i2c_burst_read_dt(&config->bus, offset, tmp_buff, 3);
	if (ret < 0)
	{
		LOG_ERR("Error reading ID %d", ret);
		return ret;
	}

	*data = tmp_buff[2];
	return 0;
}

static int bmi323_write_reg_16(const struct device *dev, uint8_t addr, uint16_t data)
{
	const struct bmi323_config *config = (const struct bmi323_config *)dev->config;
	uint8_t tmp_buff[2] = {data >> 8, data & 0xFF};

	return i2c_burst_write_dt(&config->bus, addr, tmp_buff, 2);
}

static int bmi323_write_bytes(const struct device *dev, uint8_t addr, uint8_t *data, uint16_t len)
{
	const struct bmi323_config *config = (const struct bmi323_config *)dev->config;
	return i2c_burst_write_dt(&config->bus, addr, data, len);
}

static int bmi323_read_word16(const struct device *dev, uint8_t offset, uint16_t *data, uint16_t len)
{
	const struct bmi323_config *config = (const struct bmi323_config *)dev->config;
	return i2c_burst_read_dt(&config->bus, offset, (uint8_t *)data, len);
}

static int bmi323_get_chip_id(const struct device *dev)
{
	uint8_t chip_id;
	int ret;

	ret = bmi323_read_word_8(dev, BMI3_REG_CHIP_ID, &chip_id);

	if (ret < 0)
	{
		LOG_ERR("Error reading ID %d", ret);
		return ret;
	}

	LOG_INF("Chip ID: 0x%x", chip_id);
	if ((chip_id & 0xFF) != 0x43)
	{
		LOG_ERR("Invalid chip ID 0x%x", chip_id);
		return -ENODEV;
	}

	return 0;
}

static int bmi323_enable_feature_engine(const struct device *dev)
{
	int ret;
	uint8_t data = 0;

	ret = bmi323_write_reg_16(dev, BMI3_REG_FEATURE_IO2, 0x012C);
	if (ret < 0)
	{
		LOG_ERR("Error enabling feature engine %d", ret);
		return ret;
	}

	ret = bmi323_write_reg_16(dev, BMI3_REG_FEATURE_IO_STATUS, 0x0001);
	if (ret < 0)
	{
		LOG_ERR("Error enabling feature engine %d", ret);
		return ret;
	}

	ret = bmi323_write_reg_16(dev, BMI3_REG_FEATURE_CTRL, 0x0001);
	if (ret < 0)
	{
		LOG_ERR("Error enabling feature engine %d", ret);
		return ret;
	}

	LOG_INF("Feature engine enabled");

	return 0;
}

static int bmi323_enable_step_counter(const struct device *dev)
{
	int ret;
	struct bosch_bmi323_data *data = (struct bosch_bmi323_data *)dev->data;

	// Enable Accel

	data->chip_cfg.reg_acc_conf.bit.acc_mode = BMI3_ACC_MODE_NORMAL;
	data->chip_cfg.reg_acc_conf.bit.acc_odr = BMI3_ACC_ODR_50HZ;
	data->chip_cfg.reg_acc_conf.bit.acc_range = BMI3_ACC_RANGE_8G;
	data->chip_cfg.reg_acc_conf.bit.acc_bw = BMI3_ACC_BW_ODR_HALF;
	data->chip_cfg.reg_acc_conf.bit.acc_avg_num = BMI3_ACC_AVG1;

	ret = bmi323_write_reg_16(dev, BMI3_REG_ACC_CONF, data->chip_cfg.reg_acc_conf.all);
	if (ret < 0)
	{
		LOG_ERR("Error writing acc config %d", ret);
		// return ret;
	}

	// Enable Step counter watermark

	uint8_t step_config[24] = {0};
	/*
			step_config[0] = (uint8_t)watermark1;
			step_config[1] = (uint8_t)((watermark2 | reset_counter) >> 8);
			step_config[2] = (uint8_t)env_min_dist_up1;
			step_config[3] = (uint8_t)(env_min_dist_up2 >> 8);
			step_config[4] = (uint8_t)env_coef_up1;
			step_config[5] = (uint8_t)(env_coef_up2 >> 8);
			step_config[6] = (uint8_t)env_min_dist_down1;
			step_config[7] = (uint8_t)(env_min_dist_down2 >> 8);
			step_config[8] = (uint8_t)env_coef_down1;
			step_config[9] = (uint8_t)(env_coef_down2 >> 8);
			step_config[10] = (uint8_t)mean_val_decay1;
			step_config[11] = (uint8_t)(mean_val_decay2 >> 8);
			step_config[12] = (uint8_t)mean_step_dur1;
			step_config[13] = (uint8_t)(mean_step_dur2 >> 8);
			step_config[14] = (uint8_t)(step_buffer_size | filter_cascade_enabled | step_counter_increment1);
			step_config[15] = (uint8_t)(step_counter_increment2 >> 8);
			step_config[16] = (uint8_t)peak_duration_min_walking;
			step_config[17] = (uint8_t)(peak_duration_min_running >> 8);
			step_config[18] = (uint8_t)(activity_detection_factor | activity_detection_threshold1);
			step_config[19] = (uint8_t)(activity_detection_threshold2 >> 8);
			step_config[20] = (uint8_t)step_duration_max;
			step_config[21] = (uint8_t)(step_duration_window >> 8);
			step_config[22] =
				(uint8_t)(step_duration_pp_enabled | step_duration_threshold | mean_crossing_pp_enabled |
						  mcr_threshold1);
			step_config[23] = (uint8_t)((mcr_threshold2 | sc_12_res) >> 8);
	*/
	ret = bmi323_write_bytes(dev, BMI3_REG_FEATURE_DATA_TX, step_config, 24);
}

static int bosch_bmi323_driver_api_attr_set(const struct device *dev, enum sensor_channel chan,
											enum sensor_attribute attr,
											const struct sensor_value *val)
{
	struct bosch_bmi323_data *data = (struct bosch_bmi323_data *)dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	switch (chan)
	{
	case SENSOR_CHAN_ACCEL_XYZ:
		switch (attr)
		{
		case SENSOR_ATTR_SAMPLING_FREQUENCY:
			// ret = bosch_bmi323_driver_api_set_acc_odr(dev, val);

			break;

		case SENSOR_ATTR_FULL_SCALE:
			// ret = bosch_bmi323_driver_api_set_acc_full_scale(dev, val);

			break;

		case SENSOR_ATTR_FEATURE_MASK:
			// ret = bosch_bmi323_driver_api_set_acc_feature_mask(dev, val);

			break;

		default:
			ret = -ENODEV;

			break;
		}

		break;

	case SENSOR_CHAN_GYRO_XYZ:
		switch (attr)
		{
		case SENSOR_ATTR_SAMPLING_FREQUENCY:
			// ret = bosch_bmi323_driver_api_set_gyro_odr(dev, val);

			break;

		case SENSOR_ATTR_FULL_SCALE:
			// ret = bosch_bmi323_driver_api_set_gyro_full_scale(dev, val);

			break;

		case SENSOR_ATTR_FEATURE_MASK:
			// ret = bosch_bmi323_driver_api_set_gyro_feature_mask(dev, val);

			break;

		default:
			ret = -ENODEV;

			break;
		}

		break;

	default:
		ret = -ENODEV;

		break;
	}

	k_mutex_unlock(&data->lock);

	return ret;
}

static int bosch_bmi323_driver_api_attr_get(const struct device *dev, enum sensor_channel chan,
											enum sensor_attribute attr, struct sensor_value *val)
{
	struct bosch_bmi323_data *data = (struct bosch_bmi323_data *)dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	switch (chan)
	{
	case SENSOR_CHAN_ACCEL_XYZ:
		switch (attr)
		{
		case SENSOR_ATTR_SAMPLING_FREQUENCY:
			// ret = bosch_bmi323_driver_api_get_acc_odr(dev, val);

			break;

		case SENSOR_ATTR_FULL_SCALE:
			// ret = bosch_bmi323_driver_api_get_acc_full_scale(dev, val);

			break;

		case SENSOR_ATTR_FEATURE_MASK:
			// ret = bosch_bmi323_driver_api_get_acc_feature_mask(dev, val);

			break;

		default:
			ret = -ENODEV;

			break;
		}

		break;

	case SENSOR_CHAN_GYRO_XYZ:
		switch (attr)
		{
		case SENSOR_ATTR_SAMPLING_FREQUENCY:
			// ret = bosch_bmi323_driver_api_get_gyro_odr(dev, val);

			break;

		case SENSOR_ATTR_FULL_SCALE:
			// ret = bosch_bmi323_driver_api_get_gyro_full_scale(dev, val);

			break;

		case SENSOR_ATTR_FEATURE_MASK:
			// ret = bosch_bmi323_driver_api_get_gyro_feature_mask(dev, val);

			break;

		default:
			ret = -ENODEV;

			break;
		}

		break;

	default:
		ret = -ENODEV;
		break;
	}

	k_mutex_unlock(&data->lock);

	return ret;
}

static int bosch_bmi323_driver_api_trigger_set(const struct device *dev,
											   const struct sensor_trigger *trig,
											   sensor_trigger_handler_t handler)
{
	struct bosch_bmi323_data *data = (struct bosch_bmi323_data *)dev->data;
	int ret = -ENODEV;

	k_mutex_lock(&data->lock, K_FOREVER);

	data->trigger = trig;
	data->trigger_handler = handler;

	switch (trig->chan)
	{
	case SENSOR_CHAN_ACCEL_XYZ:
		switch (trig->type)
		{
		case SENSOR_TRIG_DATA_READY:
			// ret = bosch_bmi323_driver_api_trigger_set_acc_drdy(dev);

			break;

		case SENSOR_TRIG_MOTION:
			// ret = bosch_bmi323_driver_api_trigger_set_acc_motion(dev);

			break;

		default:
			break;
		}

		break;

	default:
		break;
	}

	k_mutex_unlock(&data->lock);

	return ret;
}

static int bosch_bmi323_driver_api_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	struct bosch_bmi323_data *data = (struct bosch_bmi323_data *)dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	switch (chan)
	{
	case SENSOR_CHAN_ACCEL_XYZ:
		// ret = bosch_bmi323_driver_api_fetch_acc_samples(dev);

		break;

	case SENSOR_CHAN_GYRO_XYZ:
		// ret = bosch_bmi323_driver_api_fetch_gyro_samples(dev);

		break;

	case SENSOR_CHAN_DIE_TEMP:
		// ret = bosch_bmi323_driver_api_fetch_temperature(dev);

		break;

	case SENSOR_CHAN_ALL:
		// ret = bosch_bmi323_driver_api_fetch_acc_samples(dev);

		if (ret < 0)
		{
			break;
		}

		// ret = bosch_bmi323_driver_api_fetch_gyro_samples(dev);

		if (ret < 0)
		{
			break;
		}

		// ret = bosch_bmi323_driver_api_fetch_temperature(dev);

		break;

	default:
		ret = -ENODEV;

		break;
	}

	k_mutex_unlock(&data->lock);

	return ret;
}

static int bosch_bmi323_driver_api_channel_get(const struct device *dev, enum sensor_channel chan,
											   struct sensor_value *val)
{
	struct bosch_bmi323_data *data = (struct bosch_bmi323_data *)dev->data;
	int ret = 0;

	k_mutex_lock(&data->lock, K_FOREVER);

	switch (chan)
	{
	case SENSOR_CHAN_ACCEL_XYZ:
		if (data->acc_samples_valid == false)
		{
			ret = -ENODATA;

			break;
		}

		memcpy(val, data->acc_samples, sizeof(data->acc_samples));

		break;

	case SENSOR_CHAN_GYRO_XYZ:
		if (data->gyro_samples_valid == false)
		{
			ret = -ENODATA;

			break;
		}

		memcpy(val, data->gyro_samples, sizeof(data->gyro_samples));

		break;

	case SENSOR_CHAN_DIE_TEMP:
		if (data->temperature_valid == false)
		{
			ret = -ENODATA;

			break;
		}

		(*val) = data->temperature;

		break;

	default:
		ret = -ENOTSUP;

		break;
	}

	k_mutex_unlock(&data->lock);

	return ret;
}

static const struct sensor_driver_api bosch_bmi323_api = {
	.attr_set = bosch_bmi323_driver_api_attr_set,
	.attr_get = bosch_bmi323_driver_api_attr_get,
	.trigger_set = bosch_bmi323_driver_api_trigger_set,
	.sample_fetch = bosch_bmi323_driver_api_sample_fetch,
	.channel_get = bosch_bmi323_driver_api_channel_get,
};

static void bosch_bmi323_irq_callback(const struct device *dev)
{
	struct bosch_bmi323_data *data = (struct bosch_bmi323_data *)dev->data;

	k_work_submit(&data->callback_work);
}

static int bosch_bmi323_init_irq(const struct device *dev)
{
	struct bosch_bmi323_data *data = (struct bosch_bmi323_data *)dev->data;
	struct bmi323_config *config = (struct bmi323_config *)dev->config;
	int ret;

	ret = gpio_pin_configure_dt(&config->int_gpio, GPIO_INPUT);

	if (ret < 0)
	{
		return ret;
	}

	gpio_init_callback(&data->gpio_callback, config->int_gpio_callback,
					   BIT(config->int_gpio.pin));

	ret = gpio_add_callback(config->int_gpio.port, &data->gpio_callback);

	if (ret < 0)
	{
		return ret;
	}

	return gpio_pin_interrupt_configure_dt(&config->int_gpio, GPIO_INT_DISABLE);
}

static void bosch_bmi323_irq_callback_handler(struct k_work *item)
{
	struct bosch_bmi323_data *data =
		CONTAINER_OF(item, struct bosch_bmi323_data, callback_work);

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->trigger_handler != NULL)
	{
		data->trigger_handler(data->dev, data->trigger);
	}

	k_mutex_unlock(&data->lock);
}

static int bosch_bmi323_pm_resume(const struct device *dev)
{
	const struct bmi323_config *config = (const struct bmi323_config *)dev->config;
	int ret;

	// ret = bosch_bmi323_bus_init(dev);

	if (ret < 0)
	{
		LOG_WRN("Failed to validate chip id");

		return ret;
	}

	// ret = bosch_bmi323_soft_reset(dev);

	if (ret < 0)
	{
		LOG_WRN("Failed to soft reset chip");

		return ret;
	}

	/*ret = bosch_bmi323_bus_init(dev);

	if (ret < 0)
	{
		LOG_WRN("Failed to re-init bus");

		return ret;
	}*/

	// ret = bosch_bmi323_enable_feature_engine(dev);

	if (ret < 0)
	{
		LOG_WRN("Failed to enable feature engine");

		return ret;
	}

	// ret = bosch_bmi323_init_int1(dev);

	if (ret < 0)
	{
		LOG_WRN("Failed to enable INT1");
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&config->int_gpio, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret < 0)
	{
		LOG_WRN("Failed to configure int");
	}

	return ret;
}

#ifdef CONFIG_PM_DEVICE
static int bosch_bmi323_pm_suspend(const struct device *dev)
{
	const struct bmi323_config *config = (const struct bmi323_config *)dev->config;
	int ret;

	ret = gpio_pin_interrupt_configure_dt(&config->int_gpio, GPIO_INT_DISABLE);
	if (ret < 0)
	{
		LOG_WRN("Failed to disable int");
	}

	/* Soft reset device to put it into suspend */
	return 0; // bosch_bmi323_soft_reset(dev);
}
#endif /* CONFIG_PM_DEVICE */

#ifdef CONFIG_PM_DEVICE
static int bosch_bmi323_pm_action(const struct device *dev, enum pm_device_action action)
{
	struct bosch_bmi323_data *data = (struct bosch_bmi323_data *)dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	switch (action)
	{
	case PM_DEVICE_ACTION_RESUME:
		ret = bosch_bmi323_pm_resume(dev);

		break;

	case PM_DEVICE_ACTION_SUSPEND:
		ret = bosch_bmi323_pm_suspend(dev);

		break;

	default:
		ret = -ENOTSUP;

		break;
	}

	k_mutex_unlock(&data->lock);

	return ret;
}
#endif /* CONFIG_PM_DEVICE */

static int bosch_bmi323_init(const struct device *dev)
{
	struct bosch_bmi323_data *data = (struct bosch_bmi323_data *)dev->data;
	const struct bmi323_config *config = (const struct bmi323_config *)dev->config;
	int ret;

	LOG_INF("HPI BMI323 init");

	data->dev = dev;

	if (!device_is_ready(config->bus.bus))
	{
		LOG_ERR("Bus device is not ready");
		return -ENODEV;
	}

	ret = bmi323_get_chip_id(dev);

	if (ret < 0)
	{
		LOG_ERR("Failed to validate chip id");
		return ret;
	}

	ret = bmi323_enable_feature_engine(dev);

	if (ret < 0)
	{
		LOG_ERR("Failed to enable feature engine");
		return ret;
	}

	

	return ret;
}

#define BMI323_DEFINE(inst)                                                              \
	static struct bosch_bmi323_data bosch_bmi323_data_##inst;                            \
                                                                                         \
	static void bosch_bmi323_irq_callback##inst(const struct device *dev,                \
												struct gpio_callback *cb, uint32_t pins) \
	{                                                                                    \
		bosch_bmi323_irq_callback(DEVICE_DT_INST_GET(inst));                             \
	}                                                                                    \
                                                                                         \
	static const struct bmi323_config bmi323_config_##inst =                             \
		{                                                                                \
			.bus = I2C_DT_SPEC_INST_GET(inst),                                           \
			.int_gpio = GPIO_DT_SPEC_INST_GET(inst, int_gpios),                          \
			.int_gpio_callback = bosch_bmi323_irq_callback##inst,                        \
	};                                                                                   \
                                                                                         \
	PM_DEVICE_DT_INST_DEFINE(inst, bosch_bmi323_pm_action);                              \
                                                                                         \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, bosch_bmi323_init, PM_DEVICE_DT_INST_GET(inst),   \
								 &bosch_bmi323_data_##inst, &bmi323_config_##inst,       \
								 POST_KERNEL, 99, &bosch_bmi323_api);

DT_INST_FOREACH_STATUS_OKAY(BMI323_DEFINE)
