/* Touch controller auto-detect - same logic as the base firmware's hw_module.c */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>
#include "touch.h"

LOG_MODULE_REGISTER(touch, LOG_LEVEL_DBG);

static const struct i2c_dt_spec touch_chsc5816_i2c = I2C_DT_SPEC_GET(DT_NODELABEL(chsc5816));
static const struct i2c_dt_spec touch_cst816s_i2c = I2C_DT_SPEC_GET(DT_NODELABEL(cst816s));
static const struct gpio_dt_spec touch_rst =
	GPIO_DT_SPEC_GET(DT_NODELABEL(chsc5816), rst_gpios);
static enum hpi_touch_ctrl touch_ctrl = HPI_TOUCH_NONE;
static bool touch_detected;

static bool touch_probe(const struct i2c_dt_spec *spec)
{
	uint8_t dummy;

	return i2c_read_dt(spec, &dummy, 1) == 0;
}

enum hpi_touch_ctrl hpi_touch_detect(void)
{
	if (touch_detected) {
		return touch_ctrl;
	}
	touch_detected = true;

	if (!i2c_is_ready_dt(&touch_chsc5816_i2c)) {
		LOG_ERR("Touch I2C bus not ready");
		return touch_ctrl;
	}

	/* Pulse the shared touch reset: after a warm reboot the controller may be
	 * in its own low-power mode, where the CST816S does not ACK. */
	if (gpio_is_ready_dt(&touch_rst) &&
	    gpio_pin_configure_dt(&touch_rst, GPIO_OUTPUT_ACTIVE) == 0) {
		k_msleep(20);
		gpio_pin_set_dt(&touch_rst, 0);
		k_msleep(200);
	}

	if (touch_probe(&touch_chsc5816_i2c)) {
		touch_ctrl = HPI_TOUCH_CHSC5816;
		LOG_INF("Touch: CHSC5816 (display module V1)");
	} else if (touch_probe(&touch_cst816s_i2c)) {
		touch_ctrl = HPI_TOUCH_CST816S;
		LOG_INF("Touch: CST816S (display module V2)");
	} else {
		LOG_ERR("No touch controller detected; assuming display module V1, touch disabled");
	}

	return touch_ctrl;
}
void hpi_display_signal_touch_wakeup(void)
{
    // Trigger LVGL activity tracking
    lv_disp_trig_activity(NULL);

}
const struct device *hpi_touch_get_dev(void)
{
	switch (hpi_touch_detect()) {
	case HPI_TOUCH_CHSC5816:
		return DEVICE_DT_GET(DT_NODELABEL(chsc5816));
	case HPI_TOUCH_CST816S:
		return DEVICE_DT_GET(DT_NODELABEL(cst816s));
	default:
		return NULL;
	}
}