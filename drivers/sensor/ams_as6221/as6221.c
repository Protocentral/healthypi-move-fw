/* SPDX-License-Identifier: Apache-2.0
 *
 * AMS AS6221 Digital Temperature Sensor Driver
 *
 * 16-bit resolution, 0.0078125 C/LSB, I2C interface.
 *
 * Copyright (c) 2024 ProtoCentral Electronics
 */

#define DT_DRV_COMPAT ams_as6221

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(as6221, CONFIG_SENSOR_LOG_LEVEL);

/* Register addresses */
#define AS6221_REG_TEMP		0x00
#define AS6221_REG_CONFIG	0x01
#define AS6221_REG_TLOW		0x02
#define AS6221_REG_THIGH	0x03

/* Config register bits */
#define AS6221_CONFIG_AL	BIT(5)  /* Alert status (read-only) */
#define AS6221_CONFIG_CR0	BIT(6)  /* Conversion rate bit 0 */
#define AS6221_CONFIG_CR1	BIT(7)  /* Conversion rate bit 1 */
#define AS6221_CONFIG_SM	BIT(8)  /* Sleep mode */
#define AS6221_CONFIG_IM	BIT(9)  /* Interrupt mode */
#define AS6221_CONFIG_POL	BIT(10) /* Alert polarity */
#define AS6221_CONFIG_CF0	BIT(11) /* Consecutive faults bit 0 */
#define AS6221_CONFIG_CF1	BIT(12) /* Consecutive faults bit 1 */
#define AS6221_CONFIG_SS	BIT(15) /* Single-shot trigger */

/* Temperature: 16-bit signed, 0.0078125 C/LSB (1/128 C) */
#define AS6221_TEMP_SCALE_NUM	78125
#define AS6221_TEMP_SCALE_DEN	10000000

struct as6221_config {
	struct i2c_dt_spec i2c;
};

struct as6221_data {
	int16_t raw_temp;
};

static int as6221_reg_read(const struct device *dev, uint8_t reg, uint16_t *val)
{
	const struct as6221_config *cfg = dev->config;
	uint8_t buf[2];
	int ret;

	ret = i2c_burst_read_dt(&cfg->i2c, reg, buf, 2);
	if (ret < 0) {
		return ret;
	}

	*val = sys_get_be16(buf);
	return 0;
}

static int as6221_sample_fetch(const struct device *dev,
			       enum sensor_channel chan)
{
	struct as6221_data *data = dev->data;
	uint16_t raw;
	int ret;

	if (chan != SENSOR_CHAN_ALL && chan != SENSOR_CHAN_AMBIENT_TEMP) {
		return -ENOTSUP;
	}

	ret = as6221_reg_read(dev, AS6221_REG_TEMP, &raw);
	if (ret < 0) {
		LOG_ERR("Failed to read temperature: %d", ret);
		return ret;
	}

	data->raw_temp = (int16_t)raw;
	return 0;
}

static int as6221_channel_get(const struct device *dev,
			      enum sensor_channel chan,
			      struct sensor_value *val)
{
	struct as6221_data *data = dev->data;

	if (chan != SENSOR_CHAN_AMBIENT_TEMP) {
		return -ENOTSUP;
	}

	/*
	 * raw_temp is 16-bit signed, resolution = 0.0078125 C/LSB
	 * Convert to micro-degrees: raw * 7812.5 = raw * 15625 / 2
	 */
	int64_t uval = ((int64_t)data->raw_temp * 15625) / 2;

	val->val1 = (int32_t)(uval / 1000000);
	val->val2 = (int32_t)(uval % 1000000);

	if (uval < 0 && val->val2 != 0) {
		val->val1--;
		val->val2 += 1000000;
	}

	return 0;
}

static int as6221_init(const struct device *dev)
{
	const struct as6221_config *cfg = dev->config;
	uint16_t config_reg;
	int ret;

	if (!i2c_is_ready_dt(&cfg->i2c)) {
		LOG_ERR("I2C bus not ready");
		return -ENODEV;
	}

	/* Read config register to verify communication */
	ret = as6221_reg_read(dev, AS6221_REG_CONFIG, &config_reg);
	if (ret < 0) {
		LOG_ERR("Failed to read config register: %d", ret);
		return ret;
	}

	LOG_DBG("AS6221 ready (config=0x%04x)", config_reg);
	return 0;
}

static DEVICE_API(sensor, as6221_api) = {
	.sample_fetch = as6221_sample_fetch,
	.channel_get = as6221_channel_get,
};

#define AS6221_INIT(inst)						\
	static struct as6221_data as6221_data_##inst;			\
									\
	static const struct as6221_config as6221_config_##inst = {	\
		.i2c = I2C_DT_SPEC_INST_GET(inst),			\
	};								\
									\
	SENSOR_DEVICE_DT_INST_DEFINE(inst, as6221_init, NULL,		\
				     &as6221_data_##inst,		\
				     &as6221_config_##inst,		\
				     POST_KERNEL,			\
				     CONFIG_SENSOR_INIT_PRIORITY,	\
				     &as6221_api);

DT_INST_FOREACH_STATUS_OKAY(AS6221_INIT)
