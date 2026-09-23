/*
 * native_sim stand-ins for the real-hardware modules in src/hw/.
 */

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/timeutil.h>
#include <zephyr/zbus/zbus.h>

#include "display_sh8601.h"
#include "hpi_evt.h"
#include "hpi_sys.h"
#include "hw_module.h"

/* SDL window and SDL mouse-as-touch instead of the SH8601 panel and CHSC5816 */
const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
const struct device *touch_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_touch));

/* hw_module.c gives these once hardware init/POST completes; there is no POST
 * here, so start the display state machine straight away. */
K_SEM_DEFINE(sem_ble_thread_start, 1, 1);
K_SEM_DEFINE(sem_disp_smf_start, 1, 1);
K_SEM_DEFINE(sem_disp_boot_complete, 1, 1);
K_SEM_DEFINE(sem_boot_update_req, 0, 1);
K_SEM_DEFINE(sem_crown_key_pressed, 0, 1);

/* Owned by the sensor state machines in src/sm/, which are not built here */
K_EVENT_DEFINE(ecg_evt);
K_EVENT_DEFINE(fi_evt);
K_EVENT_DEFINE(spo2_evt);

static int32_t utc_offset_sec;
static int64_t sys_time_base_sec;

void hw_module_init(void) {}
bool hpi_sys_is_time_valid(void) { return true; }
int64_t hw_get_sys_time_ts(void) { return sys_time_base_sec + k_uptime_get() / 1000; }

int hpi_sys_set_sys_time(struct tm *tm)
{
	sys_time_base_sec = timeutil_timegm64(tm) - k_uptime_get() / 1000;
	return 0;
}

int32_t hpi_sys_get_utc_offset(void) { return utc_offset_sec; }
void hpi_sys_set_utc_offset(int32_t offset_sec) { utc_offset_sec = offset_sec; }
bool hpi_sys_get_device_on_skin(void) { return true; }
int64_t hpi_hw_get_last_motion_s(void) { return 0; }

/* Stand-in for hw/hpi_sys_module.c's observer on sys_time_chan */
static void sys_sys_time_list(const struct zbus_channel *chan)
{
	const struct tm *sys_time = zbus_chan_const_msg(chan);

	hpi_sys_set_sys_time((struct tm *)sys_time);
}
ZBUS_LISTENER_DEFINE(sys_sys_time_lis, sys_sys_time_list);

void hw_pwr_display_enable(bool enable) {}
void hpi_hw_pmic_off(void) {}
bool hw_is_low_battery(void) { return false; }
uint8_t hw_get_current_battery_level(void) { return 100; }

/* SH8601 panel extensions have no SDL equivalent */
int sh8601_transmit_cmd(const struct device *dev, uint8_t cmd, const void *tx_data, size_t tx_len)
{
	return 0;
}
int sh8601_reinit(const struct device *dev) { return 0; }
int sh8601_aod_enter(const struct device *dev, uint8_t brightness) { return -ENOTSUP; }
int sh8601_aod_exit(const struct device *dev) { return 0; }
bool sh8601_aod_is_active(const struct device *dev) { return false; }

/* No PPG finger sensor, so BP measurement and calibration are unsupported */
void hpi_bpt_abort(void) {}
int hpi_bpt_cal_enter(void) { return -ENOTSUP; }
int hpi_bpt_cal_point(uint8_t sys, uint8_t dia, uint8_t idx) { return -ENOTSUP; }
int hpi_bpt_cal_end(void) { return -ENOTSUP; }
void hpi_bpt_cal_status(uint8_t *st, uint8_t *prog, uint8_t *idx, bool *run)
{
	*st = 0;
	*prog = 0;
	*idx = 0;
	*run = false;
}
uint8_t hpi_bpt_cal_points_done(void) { return 0; }
uint8_t hpi_bpt_cal_vectors(void) { return 0; }
