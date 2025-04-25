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
LOG_MODULE_REGISTER(bosch_bmi323hpi, CONFIG_BMI323_HPI_LOG_LEVEL);

#define DT_DRV_COMPAT bosch_bmi323hpi

/* Value taken from BMI323 Datasheet section 5.8.1 */
#define IMU_BOSCH_FEATURE_ENGINE_STARTUP_CONFIG (0x012C)

#define IMU_BOSCH_DIE_TEMP_OFFSET_MICRO_DEG_CELCIUS (23000000LL)
#define IMU_BOSCH_DIE_TEMP_MICRO_DEG_CELCIUS_LSB (1953L)

#define BMI3_SET_BITS(reg_data, bitname, data) \
	((reg_data & ~(bitname##_MASK)) |          \
	 ((data << bitname##_POS) & bitname##_MASK))

#define BMI3_GET_BITS(reg_data, bitname) \
	((reg_data & (bitname##_MASK)) >>    \
	 (bitname##_POS))

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

	bool feature_engine_enabled;
	bool feature_step_counter_enabled;
	bool feature_step_detector_enabled;
	bool feature_tilt_enabled;
	bool feature_orientation_enabled;
	bool feature_flat_enabled;
	bool feature_double_tap_enabled;
	bool feature_single_tap_enabled;
	bool feature_any_motion_enabled;

	uint32_t step_counter;
};

static int bmi323_write_step_counter_config(const struct device *dev, bool reset_counter);

static int bmi323_read_reg_16(const struct device *dev, uint8_t addr, uint16_t *data)
{
	const struct bmi323_config *config = (const struct bmi323_config *)dev->config;

	uint8_t wr_buf[1] = {addr};
	uint8_t rd_buf[4] = {0x00};
	int ret;

	ret = i2c_write_read_dt(&config->bus, wr_buf, sizeof(wr_buf), rd_buf, sizeof(rd_buf));

	if (ret < 0)
	{
		LOG_ERR("Error reading %d", ret);
		return ret;
	}

	// LOG_DBG("Reg Read: %x | %x %x %x %x", addr, rd_buf[0], rd_buf[1], rd_buf[2], rd_buf[3]);

	*data = (rd_buf[2] | rd_buf[3] << 8);
	return 0;
}

static int bmi323_write_reg_16(const struct device *dev, uint8_t addr, uint16_t data)
{
	const struct bmi323_config *config = (const struct bmi323_config *)dev->config;
	uint8_t wr_buf[3] = {addr, data & 0xFF, data >> 8};

	// LOG_DBG("Reg Write: %x | %x %x", addr, wr_buf[1], wr_buf[2]);

	return i2c_write_dt(&config->bus, wr_buf, sizeof(wr_buf));
}

static int bmi323_write_regs_16_n(const struct device *dev, uint8_t addr, uint16_t *data, uint16_t len)
{
	const struct bmi323_config *config = (const struct bmi323_config *)dev->config;
	uint8_t wr_buf[25] = {0};
	len = MIN(len, 12);

	wr_buf[0] = addr;

	for (int i = 0; i < len; i++)
	{
		wr_buf[1 + (i * 2)] = data[i] & 0xFF;
		wr_buf[2 + (i * 2)] = data[i] >> 8;

		// LOG_DBG("Reg Write: %x | %x %x", addr, wr_buf[1 + (i * 2)], wr_buf[2 + (i * 2)]);
	}

	return i2c_write_dt(&config->bus, wr_buf, sizeof(wr_buf));
}

static int bmi323_get_chip_id(const struct device *dev)
{
	uint16_t chip_id;
	int ret;

	ret = bmi323_read_reg_16(dev, BMI3_REG_CHIP_ID, &chip_id);

	chip_id = (chip_id & 0xFF);

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

static int bmi323_get_status(const struct device *dev)
{
	uint16_t status;
	int ret;

	ret = bmi323_read_reg_16(dev, BMI3_REG_STATUS, &status);

	if (ret < 0)
	{
		LOG_ERR("Error reading status %d", ret);
		return ret;
	}

	LOG_DBG("Status: 0x%x", status);

	return 0;
}

static int bmi323_enable_feature_engine(const struct device *dev)
{
	struct bosch_bmi323_data *data = (struct bosch_bmi323_data *)dev->data;
	int ret;
	uint16_t tmp_data = 0;

	ret = bmi323_get_status(dev);

	if (ret < 0)
	{
		LOG_ERR("Failed to read status");
		return ret;
	}

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

	ret = bmi323_read_reg_16(dev, BMI3_REG_FEATURE_CTRL, &tmp_data);
	if (ret < 0)
	{
		LOG_ERR("Error enabling feature engine %d", ret);
		return ret;
	}

	data->feature_engine_enabled = true;

	LOG_DBG("Feature engine enabled");

	// bmi323_set_feature_io0(dev);

	return 0;
}

static int bmi323_enable_acc(const struct device *dev)
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

	LOG_DBG("Accel enabled");

	return 0;
}

static int bmi323_enable_step_counter(const struct device *dev)
{
	struct bosch_bmi323_data *data = (struct bosch_bmi323_data *)dev->data;
	int ret;

	ret = bmi323_enable_acc(dev);
	if (ret < 0)
	{
		LOG_ERR("Error enabling acc %d", ret);
		return ret;
	}

	// Write step counter config
	// ret = bmi323_write_step_counter_config(dev, false);
	/*if (ret < 0)
	{
		LOG_ERR("Error writing step counter config %d", ret);
		return ret;
	}*/

	// Reset Feature register
	ret = bmi323_write_reg_16(dev, BMI3_REG_FEATURE_IO0, 0x0000);
	if (ret < 0)
	{
		LOG_ERR("Error resetting feature register %d", ret);
		return ret;
	}

	// Enable Step Counter
	ret = bmi323_write_reg_16(dev, BMI3_REG_FEATURE_IO0, 0x0200);

	if (ret < 0)
	{
		LOG_ERR("Error setting feature IO0 %d", ret);
		return ret;
	}

	LOG_DBG("Feature IO-0 set");

	// Write IO status
	ret = bmi323_write_reg_16(dev, BMI3_REG_FEATURE_IO_STATUS, 0x0001);
	if (ret < 0)
	{
		LOG_ERR("Error setting feature IO status %d", ret);
		return ret;
	}
	LOG_DBG("Feature IO status set");

	data->feature_step_counter_enabled = true;

	LOG_DBG("Step Counter enabled");

	return 0;
}

static uint32_t bmi323_fetch_step_counter(const struct device *dev)
{
	struct bosch_bmi323_data *data = (struct bosch_bmi323_data *)dev->data;

	int ret;
	uint16_t step_counter0, step_counter1;
	uint32_t steps;

	ret = bmi323_read_reg_16(dev, BMI3_REG_FEATURE_IO2, &step_counter0);
	ret = bmi323_read_reg_16(dev, BMI3_REG_FEATURE_IO3, &step_counter1);

	steps = (step_counter1 << 16) | step_counter0;

	// LOG_DBG("Step Counter: %d", steps);
	data->step_counter = steps;

	return 0;
}

static int bmi323_write_step_counter_config(const struct device *dev, bool reset_counter)
{
	uint16_t step_counter_base_addr = (uint16_t)(BMI3_BASE_ADDR_STEP_CNT);
	int ret;

	// Write step counter base address
	ret = bmi323_write_reg_16(dev, BMI3_REG_FEATURE_DATA_ADDR, step_counter_base_addr);

	if (ret < 0)
	{
		LOG_ERR("Error writing step counter base address %d", ret);
		return ret;
	}

	// Write step counter config
	uint16_t step_config = 0;

	if (reset_counter == true)
	{
		step_config = 0x0401; // SC1 Reset
	}
	else
	{
		step_config = 0x0001; // SC1 No Reset
	}

	bmi323_write_reg_16(dev, BMI3_REG_FEATURE_DATA_TX, step_config);
}

static int bmi323_reset_step_counter(const struct device *dev)
{
	struct bosch_bmi323_data *data = (struct bosch_bmi323_data *)dev->data;
	int ret;

	ret = bmi323_write_step_counter_config(dev, true);
	data->step_counter = 0;

	LOG_DBG("Step Counter reset");

	return 0;
}

static uint32_t bmi323_soft_reset(const struct device *dev)
{
	int ret;

	ret = bmi323_write_reg_16(dev, BMI3_REG_CMD, 0xDEAF);
	if (ret < 0)
	{
		LOG_ERR("Error resetting device %d", ret);
		return ret;
	}

	return 0;
}

static int bosch_bmi323_driver_api_attr_set(const struct device *dev, enum sensor_channel chan,
											enum sensor_attribute attr,
											const struct sensor_value *val)
{
	struct bosch_bmi323_data *data = (struct bosch_bmi323_data *)dev->data;
	int ret = 0;

	k_mutex_lock(&data->lock, K_FOREVER);

	switch (chan)
	{
	case SENSOR_CHAN_ACCEL_XYZ:
		switch (attr)
		{
		case BMI323_HPI_ATTR_EN_FEATURE_ENGINE:
			break;
		case BMI323_HPI_ATTR_EN_STEP_COUNTER:
			break;
		case BMI323_HPI_ATTR_RESET_STEP_COUNTER:
			ret = bmi323_reset_step_counter(dev);
			break;
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

static int bmi323_trigger_set_acc_drdy(const struct device *dev)
{
	// struct bosch_bmi323_data *data = (struct bosch_bmi323_data *)dev->data;
	int ret;

	// ret = bmi323_write_reg_16(dev, BMI3_REG_ACC_INT_CONF_0, 0x0001);

	if (ret < 0)
	{
		LOG_ERR("Error setting acc drdy trigger %d", ret);
		return ret;
	}

	return 0;
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
			ret = bmi323_trigger_set_acc_drdy(dev);
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
	case SENSOR_CHAN_ACCEL_X:
		ret = bmi323_fetch_step_counter(dev);
		break;
	case SENSOR_CHAN_ALL:
		ret = bmi323_fetch_step_counter(dev);
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
	case SENSOR_CHAN_ACCEL_X:
		val->val1 = data->step_counter;
		val->val2 = 0;
		break;
	default:
		ret = -ENOTSUP;
		break;
	}

	k_mutex_unlock(&data->lock);

	return ret;
}

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
	int ret = 0;

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

static const struct sensor_driver_api bosch_bmi323_api = {
	.attr_set = bosch_bmi323_driver_api_attr_set,
	.attr_get = bosch_bmi323_driver_api_attr_get,
	.trigger_set = bosch_bmi323_driver_api_trigger_set,
	.sample_fetch = bosch_bmi323_driver_api_sample_fetch,
	.channel_get = bosch_bmi323_driver_api_channel_get,
};

static int bosch_bmi323_init(const struct device *dev)
{
	struct bosch_bmi323_data *data = (struct bosch_bmi323_data *)dev->data;
	const struct bmi323_config *config = (const struct bmi323_config *)dev->config;
	int ret;

	LOG_DBG("HPI BMI323 init");

	data->dev = dev;

	k_mutex_init(&data->lock);

	k_work_init(&data->callback_work, bosch_bmi323_irq_callback_handler);

	ret = bosch_bmi323_init_irq(dev);

	if (ret < 0)
	{
		LOG_ERR("Failed to init IRQ");
		return ret;
	}

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

	ret = bmi323_soft_reset(dev);

	if (ret < 0)
	{
		LOG_WRN("Failed to soft reset chip");

		return ret;
	}

	k_msleep(100);

	ret = bmi323_get_status(dev);

	if (ret < 0)
	{
		LOG_ERR("Failed to read status");
		return ret;
	}

	ret = bmi323_enable_feature_engine(dev);

	if (ret < 0)
	{
		LOG_ERR("Failed to enable feature engine");
		return ret;
	}

	ret = bmi323_enable_step_counter(dev);

	if (ret < 0)
	{
		LOG_ERR("Failed to enable step counter");
		return ret;
	}

	ret = bmi323_get_status(dev);

	if (ret < 0)
	{
		LOG_ERR("Failed to read status");
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
