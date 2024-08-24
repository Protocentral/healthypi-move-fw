/**
 * Copyright 2024 Protocentral Electronics
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CHSC5816 Capacitive Touch Panel driver
 */

#define DT_DRV_COMPAT chipsemi_chsc5816

#include <zephyr/sys/byteorder.h>
#include <zephyr/input/input.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

struct chsc5816_config
{
	struct i2c_dt_spec i2c;
	const struct gpio_dt_spec int_gpio;
	const struct gpio_dt_spec rst_gpio;
};

struct chsc5816_data
{
	const struct device *dev;
	struct k_work work;
	struct gpio_callback int_gpio_cb;
};

#define CHSC5816_REG_CMD_BUFF (0x20000000U)
#define CHSC5816_REG_RSP_BUFF (0x20000000U)
#define CHSC5816_REG_IMG_HEAD (0x20000014U)
#define CHSC5816_REG_POINT (0x2000002CU)
#define CHSC5816_REG_WR_BUFF (0x20002000U)
#define CHSC5816_REG_RD_BUFF (0x20002400U)
#define CHSC5816_REG_HOLD_MCU (0x40007000U)
#define CHSC5816_REG_AUTO_FEED (0x40007010U)
#define CHSC5816_REG_REMAP_MCU (0x40007000U)
#define CHSC5816_REG_RELEASE_MCU (0x40007000U)
#define CHSC5816_REG_BOOT_STATE (0x20000018U)

#define CHSC5816_HOLD_MCU_VAL (0x12044000U)
#define CHSC5816_AUTO_FEED_VAL (0x0000925aU)
#define CHSC5816_REMAP_MCU_VAL (0x12044002U)
#define CHSC5816_RELEASE_MCU_VAL (0x12044003U)

#define CHSC5816_REG_VID_PID_BACKUP (40 * 1024 + 0x10U)

#define CHSC5816_SIG_VALUE (0x43534843U)
/*ctp work staus*/
#define CHSC5816_POINTING_WORK (0x00000000U)
#define CHSC5816_READY_UPGRADE (1 << 1)
#define CHSC5816_UPGRAD_RUNING (1 << 2)
#define CHSC5816_SLFTEST_RUNING (1 << 3)
#define CHSC5816_SUSPEND_GATE (1 << 16)
#define CHSC5816_GUESTURE_GATE (1 << 17)
#define CHSC5816_PROXIMITY_GATE (1 << 18)
#define CHSC5816_GLOVE_GATE (1 << 19)
#define CHSC5816_ORIENTATION_GATE (1 << 20)

union CHSC5816_rpt_point_t
{
	struct
	{
		uint8_t status;
		uint8_t fingerNumber;
		uint8_t x_l8;
		uint8_t y_l8;
		uint8_t z;
		uint8_t x_h4 : 4;
		uint8_t y_h4 : 4;
		uint8_t id : 4;
		uint8_t event : 4;
		uint8_t p2;
	} rp;
	unsigned char data[8];
} CHSC5816_rpt_point;

LOG_MODULE_REGISTER(chsc5816, CONFIG_INPUT_LOG_LEVEL);

static int chsc5816_write_reg4(const struct device *dev, uint32_t reg, uint8_t *val, uint32_t val_len)
{
	const struct chsc5816_config *cfg = dev->config;

	uint8_t wr_buf[4];
	int ret;

	sys_put_be32(reg, wr_buf);
	memcpy(wr_buf, val, val_len);

	ret = i2c_burst_write_dt(&cfg->i2c, CHSC5816_REG_BOOT_STATE, wr_buf, (4 + val_len));
	if (ret < 0)
	{
		LOG_ERR("Could not write data: %i", ret);
		return -ENODATA;
	}

	return 0;
}

static int chsc5816_read_reg4(const struct device *dev, uint32_t reg, uint8_t *val, uint32_t val_len)
{
	const struct chsc5816_config *cfg = dev->config;
	uint8_t wr_buf[4];
	int ret;

	wr_buf[0] = (reg >> 24) & 0xFF;
	wr_buf[1] = (reg >> 16) & 0xFF;
	wr_buf[2] = (reg >> 8) & 0xFF;
	wr_buf[3] = reg & 0xFF;

	ret = i2c_write_read_dt(&cfg->i2c, wr_buf, 4, val, val_len);
	if (ret < 0)
	{
		LOG_ERR("Could not write data: %i", ret);
		return -ENODATA;
	}

	return 0;
}

static int chsc5816_process(const struct device *dev)
{
	int ret;
	uint16_t col = 0;
	uint16_t row = 0;

	ret = chsc5816_read_reg4(dev, CHSC5816_REG_POINT, CHSC5816_rpt_point.data, 8);
	if (ret < 0)
	{
		LOG_ERR("Could not read data: %i", ret);
		return -ENODATA;
	}
	if (CHSC5816_rpt_point.rp.status == 0xFF)
	{
		if (CHSC5816_rpt_point.rp.fingerNumber == 0)
		{
			input_report_key(dev, INPUT_BTN_TOUCH, 0, true, K_FOREVER);
		}
		else
		{
			row = (CHSC5816_rpt_point.rp.x_h4 << 8) | CHSC5816_rpt_point.rp.x_l8;
			col = (CHSC5816_rpt_point.rp.y_h4 << 8) | CHSC5816_rpt_point.rp.y_l8;

			input_report_abs(dev, INPUT_ABS_X, col, false, K_FOREVER);
			input_report_abs(dev, INPUT_ABS_Y, row, false, K_FOREVER);
			input_report_key(dev, INPUT_BTN_TOUCH, 1, true, K_FOREVER);

			//LOG_INF("Touch at %d, %d", col, row);
		}
	}

	else
	{
		LOG_INF("No touch");
		return -ENODATA;
	}

	return 0;
}

static void chsc5816_work_handler(struct k_work *work)
{
	struct chsc5816_data *data = CONTAINER_OF(work, struct chsc5816_data, work);

	chsc5816_process(data->dev);
}

static void chsc5816_isr_handler(const struct device *dev, struct gpio_callback *cb, uint32_t mask)
{
	struct chsc5816_data *data = CONTAINER_OF(cb, struct chsc5816_data, int_gpio_cb);

	k_work_submit(&data->work);
}

static void chsc5816_chip_reset(const struct device *dev)
{
	const struct chsc5816_config *config = dev->config;
	int ret;

	if (gpio_is_ready_dt(&config->rst_gpio))
	{
		ret = gpio_pin_configure_dt(&config->rst_gpio, GPIO_OUTPUT);
		if (ret < 0)
		{
			LOG_ERR("Could not configure reset GPIO pin");
			return;
		}
		gpio_pin_set_dt(&config->rst_gpio, 1);
		k_msleep(50);
		gpio_pin_set_dt(&config->rst_gpio, 0);
		k_msleep(50);
	}
}

static int chsc5816_chip_init(const struct device *dev)
{
	const struct chsc5816_config *cfg = dev->config;

	if (!i2c_is_ready_dt(&cfg->i2c))
	{
		LOG_ERR("I2C bus %s not ready", cfg->i2c.bus->name);
		return -ENODEV;
	}

	chsc5816_chip_reset(dev);

	uint8_t val[4] = {0x00, 0x00, 0x00, 0x00};
	// chsc5816_write_reg4(dev, CHSC5816_REG_BOOT_STATE, val, 4);
	//  Read FW version
	chsc5816_read_reg4(dev, CHSC5816_REG_IMG_HEAD, val, 4);
	LOG_INF("FW version: %d.%d.%d.%d", val[0], val[1], val[2], val[3]);

	chsc5816_read_reg4(dev, CHSC5816_REG_BOOT_STATE, val, 4);
	LOG_INF("Boot state: %d.%d.%d.%d", val[0], val[1], val[2], val[3]);

	return 0;
}

static int chsc5816_init(const struct device *dev)
{
	struct chsc5816_data *data = dev->data;
	int ret;

	data->dev = dev;

	k_work_init(&data->work, chsc5816_work_handler);

	const struct chsc5816_config *config = dev->config;

	k_msleep(50);

	if (!gpio_is_ready_dt(&config->int_gpio))
	{
		LOG_ERR("GPIO port %s not ready", config->int_gpio.port->name);
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&config->int_gpio, GPIO_INPUT);
	if (ret < 0)
	{
		LOG_ERR("Could not configure interrupt GPIO pin: %d", ret);
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&config->int_gpio, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret < 0)
	{
		LOG_ERR("Could not configure interrupt GPIO interrupt: %d", ret);
		return ret;
	}

	gpio_init_callback(&data->int_gpio_cb, chsc5816_isr_handler, BIT(config->int_gpio.pin));

	ret = gpio_add_callback(config->int_gpio.port, &data->int_gpio_cb);
	if (ret < 0)
	{
		LOG_ERR("Could not set gpio callback: %d", ret);
		return ret;
	}

	return chsc5816_chip_init(dev);
};

#define CHSC5816_DEFINE(index)                                                               \
	static const struct chsc5816_config chsc5816_config_##index = {                          \
		.i2c = I2C_DT_SPEC_INST_GET(index),                                                  \
		.int_gpio = GPIO_DT_SPEC_INST_GET(index, irq_gpios),                                 \
	};                                                                                       \
	static struct chsc5816_data chsc5816_data_##index;                                       \
	DEVICE_DT_INST_DEFINE(index, chsc5816_init, NULL, &chsc5816_data_##index,                \
						  &chsc5816_config_##index, POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY, \
						  NULL);

DT_INST_FOREACH_STATUS_OKAY(CHSC5816_DEFINE)
