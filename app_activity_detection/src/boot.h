#ifndef BOOT_H
#define BOOT_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <stdbool.h>

struct boot_result {
	bool flash_ok; /* external W25Q flash */
	bool fs_ok;    /* LittleFS mounted - required for recording */
	bool imu_ok;   /* BMI323 - required for recording */
	bool ecg_ok;   /* MAX30001 */
	bool hub_ok;   /* MAX32664C wrist hub (+ AFE/accel IDs) */
	bool fi_ok;    /* MAX32664D finger hub */
	bool temp_ok;  /* MAX30208 / AS6221 */
	bool rtc_ok;   /* RTC - home screen time */
};

extern struct boot_result g_boot;
extern const struct device *const g_rtc_dev;

/* Rails, display, sensor checks, filesystem. Blocks until finished; the UI
 * thread then switches to the home screen on its own. Returns true when
 * everything needed for recording (IMU + LittleFS) is present. */
bool boot_run(void);

#endif