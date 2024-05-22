/**
 * @file microcrystal_rv_cal.c
 * @author Brian Bradley (brian.bradley.p@gmail.com)
 * @brief
 * @date 2022-11-22
 *
 * @copyright Copyright (C) 2022 Brian Bradley
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/sys/timeutil.h>

#define DT_DRV_COMPAT microcrystal_rv8263

// The 7-bit I2C address of the RV8263
#define RV8263_ADDR 0x32

#define RV8263_CONTROL1_REG 0x00
#define RV8263_CONTROL2_REG 0x01
#define RV8263_OFFSET_REG 0x02
#define RV8263_RAM_REG 0x03
#define RV8263_SECOND_REG 0x04
#define RV8263_MINUTE_REG 0x05
#define RV8263_HOUR_REG 0x06
#define RV8263_DAY_REG 0x07
#define RV8263_WEEKDAY_REG 0x08
#define RV8263_MONTH_REG 0x09
#define RV8263_YEAR_REG 0x0A
#define RV8263_ALARM_MINUTE_REG 0x0B
#define RV8263_ALARM_HOUR_REG 0x0C
#define RV8263_ALARM_DAY_REG 0x0D
#define RV8263_ALARM_WEEKDAY_REG 0x0E
#define RV8263_TIMER_TMR_VAL_REG 0x10
#define RV8263_TIMER_TMR_MODE_REG 0x11

LOG_MODULE_REGISTER(rv8263);

struct rv8263_config
{
	const struct i2c_dt_spec i2c;
};

struct rv8263_data {

};

static int rv8263_settime(const struct device *dev, const struct rtc_time *timeptr)
{
	// struct rv8263_data *data = dev->data;

	const struct rv8263_config *config = dev->config;

	uint8_t buf[7];
	int ret;

	if (timeptr == NULL)
	{
		return -EINVAL;
	}

	buf[0] = bin2bcd(timeptr->tm_sec);
	buf[1] = bin2bcd(timeptr->tm_min);
	buf[2] = bin2bcd(timeptr->tm_hour);
	buf[3] = bin2bcd(timeptr->tm_mday);
	buf[4] = bin2bcd(timeptr->tm_wday);
	buf[5] = bin2bcd(timeptr->tm_mon );
	buf[6] = bin2bcd(timeptr->tm_year );

	ret = i2c_burst_write_dt(&config->i2c, RV8263_SECOND_REG, buf, sizeof(buf));

	if (ret)
	{
		LOG_ERR("Failed to write to RV8263! (err %i)", ret);
		return -EIO;
	}

	return 0;
}

static int rv8263_gettime(const struct device *dev, struct rtc_time *timeptr)
{
	const struct rv8263_config *config = dev->config;
	uint8_t buf[7]={0};
	int ret=0;

	ret = i2c_burst_read_dt(&config->i2c, RV8263_SECOND_REG, buf, sizeof(buf));

	if (ret)
	{
		LOG_ERR("Failed to read from RV8263! (err %i)", ret);
		return -EIO;
	}

	/*if(buf[0] & 0x80)
	{
		LOG_ERR("RV8263 clock integrity fail!");
		return -EIO;
	}*/

	//printk("RV8263 time: %02x:%02x:%02x %02x/%02x/%02x\n", buf[2] & 0x3f, buf[1] & 0x7f, buf[0] & 0x7f, buf[3] & 0x3f, buf[5] & 0x1f, buf[6] & 0xff);

	timeptr->tm_sec = bcd2bin(buf[0] & 0x7f);
	timeptr->tm_min = bcd2bin(buf[1] & 0x7f);
	timeptr->tm_hour = bcd2bin(buf[2] & 0x3f);
	timeptr->tm_mday = bcd2bin(buf[3] & 0x3f);
	timeptr->tm_wday = bcd2bin(buf[4] & 0x07);
	timeptr->tm_mon = bcd2bin(buf[5] & 0x1f);
	timeptr->tm_year = bcd2bin(buf[6] & 0xff); 

	timeptr->tm_yday=-1;

	return 0;

}

static int rv8263_init(const struct device *dev)
{
	const struct rv8263_config *config = dev->config;
	int ret;
	uint8_t reg=0;

	if (!device_is_ready(config->i2c.bus))
	{
		LOG_ERR("I2C bus %s is not ready", config->i2c.bus->name);
		return -ENODEV;
	}

	/* Check if it's alive. */
	ret = i2c_reg_read_byte_dt(&config->i2c, RV8263_CONTROL1_REG, &reg);
	if (ret)
	{
		LOG_ERR("Failed to read from PCF85063! (err %i)", ret);
		return -EIO;
	}

	//printk("%s is initialized!", dev->name);

	return 0;
}

static const struct rtc_driver_api rv8263_driver_api = {
	.set_time = rv8263_settime,
	.get_time = rv8263_gettime,
};

#define RV8263_INIT(inst)                                                          \
	static const struct rv8263_config rv8263_config_##inst = {                     \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                                         \
	};                                                                             \
                                                                                   \
	static struct rv8263_data rv8263_data_##inst;                                  \
                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, &rv8263_init, NULL,                                \
						  &rv8263_data_##inst, &rv8263_config_##inst, POST_KERNEL, \
						  CONFIG_RTC_INIT_PRIORITY, &rv8263_driver_api);

DT_INST_FOREACH_STATUS_OKAY(RV8263_INIT)