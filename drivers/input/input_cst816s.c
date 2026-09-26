/**
 * Copyright 2024 Protocentral Electronics
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CST816S Capacitive Touch Panel driver
 */

#define DT_DRV_COMPAT hynitron_cst816s

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/input/input.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>

#include "../../app/src/hpi_sys.h"

LOG_MODULE_REGISTER(cst816s, CONFIG_INPUT_LOG_LEVEL);

struct cst816s_config
{
	struct i2c_dt_spec i2c;
	const struct gpio_dt_spec int_gpio;
	const struct gpio_dt_spec reset_gpio;
};

/* Touch report, registers 0x02..0x06 (5 bytes), matches vendor cst8xx reference:
 *   [0] finger_num  0 = up, 1 = single touch (chip is effectively single-touch;
 *                   values >1 are treated as invalid/ignored, per vendor code)
 *   [1] x_high (bits 3:0) | x_low in [2]
 *   [3] y_high (bits 3:0) | y_low in [4]
 */
union cst816s_rpt_point_t
{
	struct
	{
		uint8_t finger_num;
		uint8_t x_h4;
		uint8_t x_l8;
		uint8_t y_h4;
		uint8_t y_l8;
	} rp;
	uint8_t data[5];
};

/*
 * Touch reports are serviced on a driver-owned work queue, not the system one,
 * for the same reason as the chsc5816 driver: keep touch servicing off a queue
 * shared with multi-ms sensor I2C transfers, and use a pending counter so an
 * edge arriving while the work item is already queued is never dropped.
 */
#define CST816S_WQ_STACK_SIZE 1024
#define CST816S_WQ_PRIORITY   3

/* If the panel goes quiet this long after a press without reporting the
 * release, re-read it ourselves and synthesize one, so LVGL never latches
 * pressed with no way to clear it. */
#define CST816S_RELEASE_WATCHDOG_MS 120

struct cst816s_data
{
	const struct device *dev;
	struct k_work work;
	struct k_work_delayable release_work;
	struct gpio_callback int_gpio_cb;
	atomic_t pending;
	bool pressed;
	union cst816s_rpt_point_t report;
};

/* Registers - 1-byte address, per vendor cst816s reference code */
#define CST816S_REG_TOUCH_DATA (0x02U) /* 5 bytes: finger_num, x_h, x_l, y_h, y_l */
#define CST816S_REG_CHIP_ID    (0xA9U) /* 1 byte */
#define CST816S_REG_SLEEP      (0xE5U) /* write 0x03 to enter sleep */

#define CST816S_SLEEP_VAL 0x03U

static K_THREAD_STACK_DEFINE(cst816s_wq_stack, CST816S_WQ_STACK_SIZE);
static struct k_work_q cst816s_work_q;
static bool cst816s_wq_started;

static int cst816s_read_reg(const struct device *dev, uint8_t reg, uint8_t *val, size_t val_len)
{
	const struct cst816s_config *cfg = dev->config;
	int ret;

	ret = i2c_write_read_dt(&cfg->i2c, &reg, 1, val, val_len);
	if (ret < 0)
	{
		LOG_ERR("Could not read reg 0x%02x: %d", reg, ret);
		return ret;
	}

	return 0;
}

static int cst816s_write_reg(const struct device *dev, uint8_t reg, uint8_t val)
{
	const struct cst816s_config *cfg = dev->config;
	uint8_t buf[2] = {reg, val};
	int ret;

	ret = i2c_write_dt(&cfg->i2c, buf, sizeof(buf));
	if (ret < 0)
	{
		LOG_ERR("Could not write reg 0x%02x: %d", reg, ret);
		return ret;
	}

	return 0;
}

static int cst816s_enter_sleep(const struct device *dev)
{
	return cst816s_write_reg(dev, CST816S_REG_SLEEP, CST816S_SLEEP_VAL);
}

/* Report a release exactly once, and stand the watchdog down. */
static void cst816s_report_release(const struct device *dev)
{
	struct cst816s_data *data = dev->data;

	k_work_cancel_delayable(&data->release_work);
	if (!data->pressed)
	{
		return;
	}
	data->pressed = false;
	input_report_key(dev, INPUT_BTN_TOUCH, 0, true, K_FOREVER);
	LOG_DBG("Touch released");
}

static int cst816s_process(const struct device *dev)
{
	struct cst816s_data *data = dev->data;
	int ret;
	uint16_t x, y;

	ret = cst816s_read_reg(dev, CST816S_REG_TOUCH_DATA, data->report.data, sizeof(data->report.data));
	if (ret < 0)
	{
		return -ENODATA;
	}

	/* 0 fingers = up. Chip is effectively single-touch; treat anything else
	 * abnormal (>1) the same as the vendor reference code does - ignore it. */
	if (data->report.rp.finger_num == 0)
	{
		cst816s_report_release(dev);
		return 0;
	}

	if (data->report.rp.finger_num > 1)
	{
		LOG_DBG("Unexpected finger_num %d, ignoring", data->report.rp.finger_num);
		return 0;
	}

	y = (((uint16_t)(data->report.rp.x_h4 & 0x0F)) << 8) | data->report.rp.x_l8;
	x = (((uint16_t)(data->report.rp.y_h4 & 0x0F)) << 8) | data->report.rp.y_l8;

	input_report_abs(dev, INPUT_ABS_X, x, false, K_FOREVER);
	input_report_abs(dev, INPUT_ABS_Y, y, false, K_FOREVER);
	input_report_key(dev, INPUT_BTN_TOUCH, 1, true, K_FOREVER);
	data->pressed = true;

	/* (Re)arm the release watchdog in case the release edge is ever missed. */
	k_work_reschedule(&data->release_work, K_MSEC(CST816S_RELEASE_WATCHDOG_MS));

	/* Signal display wakeup - uses LVGL activity tracking internally */
	hpi_display_signal_touch_wakeup();

	LOG_DBG("Touch at %d, %d", x, y);

	return 0;
}

static void cst816s_work_handler(struct k_work *work)
{
	struct cst816s_data *data = CONTAINER_OF(work, struct cst816s_data, work);

	/* Drain every edge counted since the last pass - see chsc5816 driver for
	 * why a plain k_work_submit() alone would lose edges here. */
	while (atomic_set(&data->pending, 0) != 0)
	{
		cst816s_process(data->dev);
	}
}

/* Watchdog: no further edge since the last press. Ask the panel directly and,
 * if the finger is gone, synthesize the release. */
static void cst816s_release_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct cst816s_data *data = CONTAINER_OF(dwork, struct cst816s_data, release_work);

	if (!data->pressed)
	{
		return;
	}

	if (cst816s_read_reg(data->dev, CST816S_REG_TOUCH_DATA, data->report.data,
			    sizeof(data->report.data)) == 0 &&
	    data->report.rp.finger_num != 0)
	{
		/* Still held - keep watching. */
		k_work_reschedule(&data->release_work, K_MSEC(CST816S_RELEASE_WATCHDOG_MS));
		return;
	}

	LOG_DBG("Release edge missed - synthesizing release");
	data->pressed = false;
	input_report_key(data->dev, INPUT_BTN_TOUCH, 0, true, K_FOREVER);
}

static void cst816s_isr_handler(const struct device *dev, struct gpio_callback *cb, uint32_t mask)
{
	struct cst816s_data *data = CONTAINER_OF(cb, struct cst816s_data, int_gpio_cb);

	atomic_inc(&data->pending);
	k_work_submit_to_queue(&cst816s_work_q, &data->work);
}

static void cst816s_chip_reset(const struct device *dev)
{
	const struct cst816s_config *cfg = dev->config;
	int ret;

	if (!gpio_is_ready_dt(&cfg->reset_gpio))
	{
		return;
	}

	ret = gpio_pin_configure_dt(&cfg->reset_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret < 0)
	{
		LOG_ERR("Could not configure reset GPIO pin: %d", ret);
		return;
	}

	/* Reset pulse per vendor reference (hctp_reset_ic): assert 20ms, release,
	 * settle 10ms. GPIO_ACTIVE_LOW in devicetree means "set active" here
	 * drives the physical reset line low. */
	gpio_pin_set_dt(&cfg->reset_gpio, 1);
	k_msleep(20);
	gpio_pin_set_dt(&cfg->reset_gpio, 0);
	k_msleep(10);
}

static int cst816s_chip_init(const struct device *dev)
{
	const struct cst816s_config *cfg = dev->config;
	uint8_t chip_id = 0;
	int ret;

	if (!i2c_is_ready_dt(&cfg->i2c))
	{
		LOG_ERR("I2C bus %s not ready", cfg->i2c.bus->name);
		return -ENODEV;
	}

	cst816s_chip_reset(dev);

	/* Vendor reference waits 200ms after reset before first I2C access. */
	k_msleep(200);

	ret = cst816s_read_reg(dev, CST816S_REG_CHIP_ID, &chip_id, 1);
	if (ret < 0)
	{
		LOG_ERR("Touch not ready: %d", ret);

		for (int i = 0; i < 4; i++)
		{
			LOG_ERR("Retry %d", i);
			cst816s_chip_reset(dev);
			k_msleep(200);
			ret = cst816s_read_reg(dev, CST816S_REG_CHIP_ID, &chip_id, 1);
			if (ret == 0)
			{
				break;
			}
		}
	}

	if (ret == 0)
	{
		LOG_INF("CST816S chip id/version: 0x%02x", chip_id);
	}

	return ret;
}

static int cst816s_init(const struct device *dev)
{
	struct cst816s_data *data = dev->data;
	const struct cst816s_config *cfg = dev->config;
	int ret;

	data->dev = dev;

	if (!cst816s_wq_started)
	{
		k_work_queue_init(&cst816s_work_q);
		k_work_queue_start(&cst816s_work_q, cst816s_wq_stack,
				   K_THREAD_STACK_SIZEOF(cst816s_wq_stack),
				   CST816S_WQ_PRIORITY, NULL);
		k_thread_name_set(&cst816s_work_q.thread, "cst816s");
		cst816s_wq_started = true;
	}

	k_work_init(&data->work, cst816s_work_handler);
	k_work_init_delayable(&data->release_work, cst816s_release_work_handler);
	atomic_set(&data->pending, 0);
	data->pressed = false;

	if (!gpio_is_ready_dt(&cfg->int_gpio))
	{
		LOG_ERR("GPIO port %s not ready", cfg->int_gpio.port->name);
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&cfg->int_gpio, GPIO_INPUT);
	if (ret < 0)
	{
		LOG_ERR("Could not configure interrupt GPIO pin: %d", ret);
		return ret;
	}

	ret = cst816s_chip_init(dev);
	if (ret < 0)
	{
		LOG_ERR("CST816S init failed: %d", ret);
		/* Continue to register the ISR anyway - matches chsc5816 driver's
		 * behavior of not hard-failing device init on a bring-up hiccup. */
	}

	ret = gpio_pin_interrupt_configure_dt(&cfg->int_gpio, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret < 0)
	{
		LOG_ERR("Could not configure interrupt GPIO interrupt: %d", ret);
		return ret;
	}

	gpio_init_callback(&data->int_gpio_cb, cst816s_isr_handler, BIT(cfg->int_gpio.pin));

	ret = gpio_add_callback(cfg->int_gpio.port, &data->int_gpio_cb);
	if (ret < 0)
	{
		LOG_ERR("Could not set gpio callback: %d", ret);
		return ret;
	}

	return 0;
}

#ifdef CONFIG_PM_DEVICE

static int cst816s_pm_action(const struct device *dev, enum pm_device_action action)
{
	switch (action)
	{
	case PM_DEVICE_ACTION_SUSPEND:
		return cst816s_enter_sleep(dev);
	case PM_DEVICE_ACTION_RESUME:
		/* Chip has no documented soft wake in the vendor reference; a full
		 * reset + re-init brings it back reliably. */
		return cst816s_chip_init(dev);
	default:
		return -ENOTSUP;
	}
}

#endif /* CONFIG_PM_DEVICE */

#define CST816S_DEFINE(index)                                                          \
	static const struct cst816s_config cst816s_config_##index = {                      \
		.i2c = I2C_DT_SPEC_INST_GET(index),                                          \
		.int_gpio = GPIO_DT_SPEC_INST_GET(index, irq_gpios),                         \
		.reset_gpio = GPIO_DT_SPEC_INST_GET(index, reset_gpios),                     \
	};                                                                                \
	PM_DEVICE_DT_INST_DEFINE(index, cst816s_pm_action);                               \
	static struct cst816s_data cst816s_data_##index;                                   \
	DEVICE_DT_INST_DEFINE(index, cst816s_init, PM_DEVICE_DT_INST_GET(index),          \
						  &cst816s_data_##index, &cst816s_config_##index, POST_KERNEL, \
						  CONFIG_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(CST816S_DEFINE)