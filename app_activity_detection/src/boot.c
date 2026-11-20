/*
 * Hardware bring-up + sensor presence checks. Every result is posted to the boot
 * screen through ui_boot_add(); nothing here touches LVGL directly.
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/sys/timeutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "max32664c.h"
#include "max32664d.h"
#include "boot.h"
#include "ui.h"
#include "storage.h"
#include "acc_recorder.h"

LOG_MODULE_REGISTER(boot, LOG_LEVEL_DBG);

struct boot_result g_boot;
const struct device *const g_rtc_dev = DEVICE_DT_GET(DT_ALIAS(rtc));

/* ---- Devices ---- */
static const struct device *flash_dev = DEVICE_DT_GET(DT_NODELABEL(w25q01jv));
const struct device *imu_dev = DEVICE_DT_GET(DT_NODELABEL(bmi323));
static const struct device *ecg_dev = DEVICE_DT_GET(DT_ALIAS(max30001));
static const struct device *hub_c_dev = DEVICE_DT_GET_ANY(protocentral_max32664c);
static const struct device *hub_d_dev = DEVICE_DT_GET_ANY(maxim_max32664);

static const struct device *max30208a50_dev = DEVICE_DT_GET(DT_NODELABEL(max30208a50));
static const struct device *max30208a52_dev = DEVICE_DT_GET(DT_NODELABEL(max30208a52));
static const struct device *as6221_dev = DEVICE_DT_GET(DT_NODELABEL(as6221));

static const struct device *ldsw_disp = DEVICE_DT_GET(DT_NODELABEL(npm_pmic_ldo1));
static const struct device *ldsw_fi = DEVICE_DT_GET(DT_NODELABEL(npm_pmic_ldo2));
static const struct device *sensor_rail = DEVICE_DT_GET(DT_NODELABEL(npm_pmic_buck2));
static const struct gpio_dt_spec dcdc_5v_en =
	GPIO_DT_SPEC_GET(DT_NODELABEL(sensor_dcdc_en), gpios);

#define SENSOR_RAIL_OFF_MS    200
#define SENSOR_RAIL_SETTLE_MS 50
#define LED_BOOST_SETTLE_MS   50

/* An nRF reboot leaves BUCK2 + the 5 V boost powered, so a stuck sensor hub stays
 * stuck. Power-cycle both before touching any sensor (same as the base app). */
static void sensor_rail_power_cycle(void)
{
	if (!device_is_ready(sensor_rail)) {
		LOG_ERR("Sensor rail (BUCK2) not ready");
		return;
	}

	gpio_pin_configure_dt(&dcdc_5v_en, GPIO_OUTPUT_INACTIVE);

	if (regulator_disable(sensor_rail) < 0) {
		LOG_ERR("Sensor rail disable failed");
		return;
	}
	k_msleep(SENSOR_RAIL_OFF_MS);

	for (int i = 0; i < 5; i++) {
		if (regulator_enable(sensor_rail) == 0) {
			break;
		}
		k_msleep(10);
	}
	k_msleep(SENSOR_RAIL_SETTLE_MS);

	/* Hold the MAX32664C in reset with MFIO high until its driver inits */
	const struct gpio_dt_spec hub_rst =
		GPIO_DT_SPEC_GET(DT_NODELABEL(max32664c), reset_gpios);
	const struct gpio_dt_spec hub_mfio =
		GPIO_DT_SPEC_GET(DT_NODELABEL(max32664c), mfio_gpios);

	gpio_pin_configure_dt(&hub_mfio, GPIO_OUTPUT_ACTIVE);
	gpio_pin_configure_dt(&hub_rst, GPIO_OUTPUT_INACTIVE);

	gpio_pin_set_dt(&dcdc_5v_en, 1);
	k_msleep(LED_BOOST_SETTLE_MS);
}

static bool try_init(const struct device *dev)
{
	if (dev == NULL) {
		return false;
	}
	(void)device_init(dev);
	k_msleep(100);
	return device_is_ready(dev);
}

static bool probe(const struct device *dev, const char *name, bool critical)
{
	bool ok = try_init(dev);

	if (ok) {
		LOG_INF("%s present", name);
	} else if (critical) {
		LOG_ERR("%s NOT present", name);
	} else {
		LOG_WRN("%s not present", name);
	}

	ui_boot_add(name, ok, true);
	return ok;
}

/* MAX30208 (0x50 or 0x52) on older boards, AS6221 (0x48) on newer ones */
static bool probe_temp_sensor(void)
{
	if (try_init(max30208a50_dev)) {
		LOG_INF("Temp sensor: MAX30208 @0x50");
		ui_boot_add("MAX30208", true, true);
		return true;
	}
	if (try_init(max30208a52_dev)) {
		LOG_INF("Temp sensor: MAX30208 @0x52");
		ui_boot_add("MAX30208", true, true);
		return true;
	}
	if (try_init(as6221_dev)) {
		LOG_INF("Temp sensor: AS6221 @0x48");
		ui_boot_add("AS6221", true, true);
		return true;
	}

	LOG_ERR("No temperature sensor found (MAX30208/AS6221)");
	ui_boot_add("Temp sensor", false, true);
	return false;
}

static void post_version(const char *tag, const struct device *dev, int attr)
{
	struct sensor_value ver;
	char buf[32];

	if (sensor_attr_get(dev, SENSOR_CHAN_ALL, attr, &ver) == 0) {
		LOG_INF("%s App Version: %d.%d", tag, ver.val1, ver.val2);
		snprintf(buf, sizeof(buf), "\t v%d.%d", ver.val1, ver.val2);
		ui_boot_add(buf, true, false);
	}
}


/* If the RTC has never been set (or holds a pre-2025 date), seed it with the
 * firmware build time so the home screen shows something sensible. A valid time
 * left by the base firmware (set over BLE) is kept untouched. */
static void rtc_seed_from_build_time(void)
{
	static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
	int mon = 0;

	for (int i = 0; i < 12; i++) {
		if (strncmp(__DATE__, months + 3 * i, 3) == 0) {
			mon = i;
			break;
		}
	}

	struct tm tmv = {
		.tm_sec = atoi(__TIME__ + 6),
		.tm_min = atoi(__TIME__ + 3),
		.tm_hour = atoi(__TIME__),
		.tm_mday = atoi(__DATE__ + 4),
		.tm_mon = mon,
		.tm_year = atoi(__DATE__ + 7) - 1900,
	};
	time_t t = timeutil_timegm(&tmv);

	gmtime_r(&t, &tmv); /* fills tm_wday / tm_yday */

	struct rtc_time rt = {
		.tm_sec = tmv.tm_sec,
		.tm_min = tmv.tm_min,
		.tm_hour = tmv.tm_hour,
		.tm_mday = tmv.tm_mday,
		.tm_mon = tmv.tm_mon,
		.tm_year = tmv.tm_year,
		.tm_wday = tmv.tm_wday,
		.tm_yday = tmv.tm_yday,
		.tm_isdst = -1,
		.tm_nsec = 0,
	};

	int rc = rtc_set_time(g_rtc_dev, &rt);

	LOG_WRN("RTC was not set - seeded with build time %s %s (rc %d)", __DATE__, __TIME__, rc);
}

bool boot_run(void)
{
	char buf[32];

#if defined(NRF_TWIM2) && defined(NRF_TWIM1)
	/* nRF5340 anomaly 47 workaround (same as the base firmware) */
	NRF_TWIM2->FREQUENCY = 0x06200000;
	NRF_TWIM1->FREQUENCY = 0x06200000;
#endif

	sensor_rail_power_cycle();

	/* Both LDSWs are boot-off in DT. Hold them off long enough to discharge so
	 * the display is really power-cycled after a warm reboot, then switch on. */
	k_msleep(100);
	if (device_is_ready(ldsw_disp)) {
		regulator_enable(ldsw_disp);
		k_msleep(500);
	} else {
		LOG_ERR("Display LDO not ready");
	}

	/* Start the UI thread and wait (bounded) for the boot screen, so a display
	 * hang cannot block boot. Messages are queued either way. */
	ui_start();
	if (!ui_wait_boot_screen(K_SECONDS(10))) {
		LOG_ERR("Display not ready within 10 s - continuing boot without it");
	}

	/* ---- External flash + LittleFS (recording target) ---- */
	g_boot.flash_ok = device_is_ready(flash_dev);
	ui_boot_add("Flash W25Q", g_boot.flash_ok, true);

	g_boot.fs_ok = g_boot.flash_ok && (storage_init() == 0);

	/* ---- Sensors ---- */
	g_boot.imu_ok = probe(imu_dev, "BMI323", true);
	g_boot.ecg_ok = probe(ecg_dev, "MAX30001", false);

	g_boot.hub_ok = probe(hub_c_dev, "MAX32664C", false);
	if (g_boot.hub_ok) {
		struct sensor_value ids;

		post_version("MAX32664C", hub_c_dev, MAX32664C_ATTR_APP_VER);

		if (sensor_attr_get(hub_c_dev, SENSOR_CHAN_ALL, MAX32664C_ATTR_SENSOR_IDS,
				    &ids) == 0) {
			bool afe = (ids.val1 == MAX32664C_AFE_ID);
			bool acc = (ids.val2 == MAX32664C_ACC_ID);

			ui_boot_add("\t AFE", afe, true);
			ui_boot_add("\t Acc", acc, true);
			g_boot.hub_ok = afe && acc;
		}
	}

	if (device_is_ready(ldsw_fi)) {
		regulator_enable(ldsw_fi);
		k_msleep(100);

		g_boot.fi_ok = probe(hub_d_dev, "MAX32664D", false);
		if (g_boot.fi_ok) {
			post_version("MAX32664D", hub_d_dev, MAX32664D_ATTR_APP_VER);
		}
		regulator_disable(ldsw_fi);
	} else {
		LOG_ERR("Finger sensor regulator (LDO2) not ready");
		ui_boot_add("MAX32664D", false, true);
	}

	g_boot.temp_ok = probe_temp_sensor();

	/* ---- RTC ---- */
	g_boot.rtc_ok = probe(g_rtc_dev, "RTC", true);
	if (g_boot.rtc_ok) {
		struct rtc_time now;

		if (rtc_get_time(g_rtc_dev, &now) != 0 || (now.tm_year + 1900) < 2025) {
			rtc_seed_from_build_time();
		}
	}

	/* ---- Confirm the MCUboot image so it is not reverted on the next reset ---- */
	if (!boot_is_img_confirmed()) {
		if (boot_write_img_confirmed()) {
			LOG_ERR("Failed to confirm image");
		} else {
			LOG_INF("Marked image as OK");
		}
	}

	bool ok = g_boot.imu_ok && g_boot.fs_ok;

	ui_boot_add(ok ? "Boot complete !!" : "Recording unavailable", ok, false);
	ui_boot_complete();
    k_msleep(3000);
   // acc_rec_dump_file("/lfs/acc/acc_0016.csv");
	return ok;
}