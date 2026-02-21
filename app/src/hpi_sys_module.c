/*
 * HealthyPi Move
 * 
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Author: Ashwin Whitchurch, Protocentral Electronics
 * Contact: ashwin@protocentral.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <stdio.h>

#include <time.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>
#include <zephyr/posix/time.h>
#include <zephyr/sys/timeutil.h>
#include <zephyr/drivers/rtc.h>

#include "hpi_common_types.h"
#include "hpi_sys.h"
#include "hw_module.h"
#include "hpi_measurement_settings.h"

LOG_MODULE_REGISTER(hpi_sys_module, LOG_LEVEL_DBG);

// External device references (declared extern in hw_module.c)
extern const struct device *rtc_dev;

// Time synchronization variables
static int64_t last_rtc_sync_uptime = 0;
static int64_t rtc_to_uptime_offset = 0;

static struct tm m_sys_sys_time;

K_MUTEX_DEFINE(mutex_time_sync);

// Time sync configuration
#define RTC_SYNC_INTERVAL_MS (30 * 60 * 1000) // Sync every 30 minutes
#define RTC_SYNC_DRIFT_THRESHOLD_S 5          // Sync if drift > 5 seconds
#define RTC_FAST_SYNC_INTERVAL_MS (60 * 1000) // Fast sync every minute after boot

struct mgmt_callback dfu_callback;

enum mgmt_cb_return dfu_callback_func(uint32_t event, enum mgmt_cb_return prev_status,
                                      int32_t *rc, uint16_t *group, bool *abort_more,
                                      void *data, size_t data_size)
{

    LOG_DBG("DFU callback event: %d, prev_status: %d, rc: %d, group: %d, abort_more: %d",
            event, prev_status, *rc, *group, *abort_more);

    /* Return OK status code to continue with acceptance to underlying handler */
    return MGMT_CB_OK;
}

struct mgmt_callback img_callback;

enum mgmt_cb_return img_callback_func(uint32_t event, enum mgmt_cb_return prev_status,
                                      int32_t *rc, uint16_t *group, bool *abort_more,
                                      void *data, size_t data_size)
{
    LOG_DBG("Image callback event: %d, prev_status: %d, rc: %d, group: %d, abort_more: %d",
            event, prev_status, *rc, *group, *abort_more);

    /* Return OK status code to continue with acceptance to underlying handler */
    return MGMT_CB_OK;
}


static bool is_on_skin = false;
K_MUTEX_DEFINE(mutex_on_skin);

K_MUTEX_DEFINE(mutex_sys_time);

// Externs
extern struct k_sem sem_hpi_sys_thread_start;

static struct hpi_last_update_time_t g_hpi_last_update = {
    .bp_last_update_ts = 0,
    .hr_last_update_ts = 0,
    .spo2_last_update_ts = 0,
    .ecg_last_update_ts = 0,

    .hr_last_value = 0,
    .spo2_last_value = 0,
    .bp_sys_last_value = 0,
    .bp_dia_last_value = 0,

    .temp_last_update_ts = 0,
    .temp_last_value = 0,

    .steps_last_update_ts = 0,
    .steps_last_value = 0,

    .gsr_last_update_ts = 0,
    .gsr_last_value = 0,

    .hrv_lf_hf_ratio_x100 = 0,
    .hrv_sdnn_x10 = 0,
    .hrv_rmssd_x10 = 0,
    .hrv_last_update_ts = 0,
};

K_MUTEX_DEFINE(mutex_hpi_last_update_time);


int hpi_sys_set_sys_time(struct tm *tm)
{
    int ret = 0;
    k_mutex_lock(&mutex_sys_time, K_FOREVER);
    m_sys_sys_time = *tm;
    k_mutex_unlock(&mutex_sys_time);
    return ret;
}

struct tm hpi_sys_get_sys_time(void)
{
    k_mutex_lock(&mutex_sys_time, K_FOREVER);
    struct tm sys_tm_time_copy = m_sys_sys_time;
    k_mutex_unlock(&mutex_sys_time);
    return sys_tm_time_copy;
}

int64_t hw_get_sys_time_ts(void)
{
    int64_t sys_time_ts = timeutil_timegm64(&m_sys_sys_time);
    return sys_time_ts;
}

// Timestamp validation bounds (same as log_module.c)
#define HPI_TIME_MIN_TIMESTAMP 1577836800LL  // Jan 1, 2020 00:00:00 UTC
#define HPI_TIME_MAX_TIMESTAMP 1893456000LL  // Jan 1, 2030 00:00:00 UTC

bool hpi_sys_is_time_valid(void)
{
    int64_t current_ts = hw_get_sys_time_ts();
    return (current_ts >= HPI_TIME_MIN_TIMESTAMP && current_ts <= HPI_TIME_MAX_TIMESTAMP);
}

int64_t hw_get_synced_system_time(void)
{
    int64_t current_uptime = k_uptime_get();
    int64_t synced_time;

    k_mutex_lock(&mutex_time_sync, K_FOREVER);
    synced_time = rtc_to_uptime_offset + (current_uptime / 1000); // Convert to seconds
    k_mutex_unlock(&mutex_time_sync);

    return synced_time;
}

static int hpi_sys_sync_time_with_rtc(bool force_sync)
{
    struct rtc_time rtc_sys_time;
    int64_t current_uptime = k_uptime_get();
    int64_t time_since_last_sync = current_uptime - last_rtc_sync_uptime;

    // Check if sync is needed
    if (!force_sync && time_since_last_sync < RTC_SYNC_INTERVAL_MS)
    {
        return 0; // No sync needed
    }

    int ret = rtc_get_time(rtc_dev, &rtc_sys_time);
    if (ret < 0)
    {
        LOG_ERR("Failed to get RTC time for sync: %d", ret);
        return ret;
    }

    // Convert RTC time to timestamp
    struct tm rtc_tm = *rtc_time_to_tm(&rtc_sys_time);
    int64_t rtc_timestamp = timeutil_timegm64(&rtc_tm);

    k_mutex_lock(&mutex_time_sync, K_FOREVER);

    // Calculate new offset
    int64_t new_offset = rtc_timestamp - (current_uptime / 1000);

    // Check for significant drift
    if (last_rtc_sync_uptime > 0)
    {
        int64_t drift = new_offset - rtc_to_uptime_offset;
        if (abs(drift) > RTC_SYNC_DRIFT_THRESHOLD_S && !force_sync)
        {
            LOG_WRN("Time drift detected: %lld seconds", drift);
        }
    }

    rtc_to_uptime_offset = new_offset;
    last_rtc_sync_uptime = current_uptime;

    k_mutex_unlock(&mutex_time_sync);

    // Update system time
    hpi_sys_set_sys_time(&rtc_tm);

    LOG_DBG("Time synced with RTC. Offset: %lld", rtc_to_uptime_offset);
    return 0;
}

static bool hpi_sys_should_sync_rtc(void)
{
    int64_t current_uptime = k_uptime_get();
    int64_t time_since_last_sync = current_uptime - last_rtc_sync_uptime;

    // Force sync on first run
    if (last_rtc_sync_uptime == 0)
    {
        return true;
    }

    // Fast sync during first hour after boot
    if (current_uptime < (60 * 60 * 1000) && time_since_last_sync >= RTC_FAST_SYNC_INTERVAL_MS)
    {
        return true;
    }

    // Normal sync interval
    return time_since_last_sync >= RTC_SYNC_INTERVAL_MS;
}

void hpi_sys_set_rtc_time(const struct tm *time_to_set)
{
    struct rtc_time rtc_time_set;

    rtc_time_set.tm_sec = time_to_set->tm_sec;
    rtc_time_set.tm_min = time_to_set->tm_min;
    rtc_time_set.tm_hour = time_to_set->tm_hour;
    rtc_time_set.tm_mday = time_to_set->tm_mday;
    rtc_time_set.tm_mon = time_to_set->tm_mon;
    rtc_time_set.tm_year = time_to_set->tm_year;
    rtc_time_set.tm_wday = time_to_set->tm_wday;
    rtc_time_set.tm_yday = time_to_set->tm_yday;

    int ret = rtc_set_time(rtc_dev, &rtc_time_set);
    if (ret < 0)
    {
        LOG_ERR("Failed to set RTC time: %d", ret);
        return;
    }

    // Force immediate sync after setting RTC
    hpi_sys_sync_time_with_rtc(true);
    LOG_INF("RTC time updated and synced");
}

int hpi_sys_sync_time_if_needed(void)
{
    if (hpi_sys_should_sync_rtc())
    {
        return hpi_sys_sync_time_with_rtc(false);
    }
    return 0;
}

int hpi_sys_force_time_sync(void)
{
    return hpi_sys_sync_time_with_rtc(true);
}

struct tm hpi_sys_get_current_time(void)
{
    int64_t current_timestamp = hw_get_synced_system_time();
    struct tm current_time;
    gmtime_r(&current_timestamp, &current_time);
    return current_time;
}

bool hpi_sys_get_device_on_skin(void)
{
    bool on_skin = false;
    k_mutex_lock(&mutex_on_skin, K_FOREVER);
    on_skin = is_on_skin;
    k_mutex_unlock(&mutex_on_skin);

    return on_skin;
}

void hpi_sys_set_device_on_skin(bool on_skin)
{
    k_mutex_lock(&mutex_on_skin, K_FOREVER);
    is_on_skin = on_skin;
    k_mutex_unlock(&mutex_on_skin);
}

int hpi_helper_get_relative_time_str(int64_t in_ts, char *out_str, size_t out_str_size)
{
    if (in_ts == 0)
    {
        snprintf(out_str, out_str_size, "Never");
        return 0;
    }

    int64_t now = hw_get_sys_time_ts();
    int64_t diff = now - in_ts;

    if (diff < 60)
    {
        snprintf(out_str, out_str_size, "Just now");
    }
    else if (diff < 3600)
    {
        int mins = diff / 60;
        snprintf(out_str, out_str_size, "%d minute%s ago", mins, mins == 1 ? "" : "s");
    }
    else if (diff < 86400)
    {
        int hours = diff / 3600;
        snprintf(out_str, out_str_size, "%d hour%s ago", hours, hours == 1 ? "" : "s");
    }
    else if (diff < 172800)
    {
        snprintf(out_str, out_str_size, "Yesterday");
    }
    else
    {
        struct tm *tm_info = localtime(&in_ts);
        if (tm_info != NULL) {
            snprintf(out_str, out_str_size, "%02d-%02d-%04d",
                     tm_info->tm_mday, tm_info->tm_mon + 1, tm_info->tm_year + 1900);
        } else {
            // Fallback if localtime fails
            snprintf(out_str, out_str_size, "Long ago");
        }
    }
    return 0;
}


void hpi_sys_set_last_hr_update(uint16_t hr_last_value, int64_t hr_last_update_ts)
{
    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    g_hpi_last_update.hr_last_value = hr_last_value;
    g_hpi_last_update.hr_last_update_ts = hr_last_update_ts;
    k_mutex_unlock(&mutex_hpi_last_update_time);

    /* Persist via settings subsystem */
    hpi_meas_save_hr(hr_last_value, hr_last_update_ts);
}

void hpi_sys_set_last_spo2_update(uint8_t spo2_last_value, int64_t spo2_last_update_ts)
{
    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    g_hpi_last_update.spo2_last_value = spo2_last_value;
    g_hpi_last_update.spo2_last_update_ts = spo2_last_update_ts;
    k_mutex_unlock(&mutex_hpi_last_update_time);

    /* Persist via settings subsystem */
    hpi_meas_save_spo2(spo2_last_value, spo2_last_update_ts);
}

void hpi_sys_set_last_bp_update(uint16_t bp_sys_last_value, uint16_t bp_dia_last_value, int64_t bp_last_update_ts)
{
    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    g_hpi_last_update.bp_sys_last_value = bp_sys_last_value;
    g_hpi_last_update.bp_dia_last_value = bp_dia_last_value;
    g_hpi_last_update.bp_last_update_ts = bp_last_update_ts;
    k_mutex_unlock(&mutex_hpi_last_update_time);

    /* Persist via settings subsystem */
    hpi_meas_save_bp((uint8_t)bp_sys_last_value, (uint8_t)bp_dia_last_value, bp_last_update_ts);
}

void hpi_sys_set_last_ecg_update(int64_t ecg_last_update_ts)
{
    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    g_hpi_last_update.ecg_last_update_ts = ecg_last_update_ts;
    k_mutex_unlock(&mutex_hpi_last_update_time);

    /* Persist via settings subsystem - ECG HR saved from ZBus listener */
}

void hpi_sys_set_last_hrv_update(uint16_t lf_hf_ratio_x100, uint16_t sdnn_x10,
                                  uint16_t rmssd_x10, int64_t hrv_last_update_ts)
{
    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    g_hpi_last_update.hrv_lf_hf_ratio_x100 = lf_hf_ratio_x100;
    g_hpi_last_update.hrv_sdnn_x10 = sdnn_x10;
    g_hpi_last_update.hrv_rmssd_x10 = rmssd_x10;
    g_hpi_last_update.hrv_last_update_ts = hrv_last_update_ts;
    k_mutex_unlock(&mutex_hpi_last_update_time);

    /* Persist via settings subsystem */
    hpi_meas_save_hrv(lf_hf_ratio_x100, sdnn_x10, rmssd_x10, hrv_last_update_ts);
}

int hpi_sys_get_last_hrv_update(uint16_t *lf_hf_ratio_x100, uint16_t *sdnn_x10,
                                 uint16_t *rmssd_x10, int64_t *hrv_last_update_ts)
{
    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    *lf_hf_ratio_x100 = g_hpi_last_update.hrv_lf_hf_ratio_x100;
    *sdnn_x10 = g_hpi_last_update.hrv_sdnn_x10;
    *rmssd_x10 = g_hpi_last_update.hrv_rmssd_x10;
    *hrv_last_update_ts = g_hpi_last_update.hrv_last_update_ts;
    k_mutex_unlock(&mutex_hpi_last_update_time);

    return 0;
}

int hpi_sys_get_last_hr_update(uint16_t *hr_last_value, int64_t *hr_last_update_ts)
{
    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    *hr_last_value = g_hpi_last_update.hr_last_value;
    *hr_last_update_ts = g_hpi_last_update.hr_last_update_ts;
    k_mutex_unlock(&mutex_hpi_last_update_time);

    return 0;
}

int hpi_sys_get_last_spo2_update(uint8_t *spo2_last_value, int64_t *spo2_last_update_ts)
{
    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    *spo2_last_value = g_hpi_last_update.spo2_last_value;
    *spo2_last_update_ts = g_hpi_last_update.spo2_last_update_ts;
    k_mutex_unlock(&mutex_hpi_last_update_time);

    return 0;
}

int hpi_sys_get_last_bp_update(uint8_t *bp_sys_last_value, uint8_t *bp_dia_last_value, int64_t *bp_last_update_ts)
{
    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    *bp_sys_last_value = g_hpi_last_update.bp_sys_last_value;
    *bp_dia_last_value = g_hpi_last_update.bp_dia_last_value;
    *bp_last_update_ts = g_hpi_last_update.bp_last_update_ts;
    k_mutex_unlock(&mutex_hpi_last_update_time);

    return 0;
}

int hpi_sys_get_last_ecg_update(uint8_t *ecg_hr, int64_t *ecg_last_update_ts)
{
    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    *ecg_hr = g_hpi_last_update.ecg_last_hr;
    *ecg_last_update_ts = g_hpi_last_update.ecg_last_update_ts;
    k_mutex_unlock(&mutex_hpi_last_update_time);

    return 0;
}

int hpi_sys_get_last_temp_update(uint16_t *temp_last_value_x100, int64_t *temp_last_update_ts)
{
    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    *temp_last_update_ts = g_hpi_last_update.temp_last_update_ts;
    *temp_last_value_x100 = g_hpi_last_update.temp_last_value;
    k_mutex_unlock(&mutex_hpi_last_update_time);
    return 0;
}

void hpi_sys_set_last_gsr_update(uint16_t gsr_last_value, int64_t gsr_last_update_ts)
{
    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    g_hpi_last_update.gsr_last_value = gsr_last_value;
    g_hpi_last_update.gsr_last_update_ts = gsr_last_update_ts;
    k_mutex_unlock(&mutex_hpi_last_update_time);

    /* Note: GSR stress data saved via hpi_sys_set_last_gsr_stress() */
}

void hpi_sys_set_last_gsr_stress(uint8_t stress_level, uint16_t tonic_x100, uint8_t peaks_per_min, int64_t update_ts)
{
    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    g_hpi_last_update.gsr_stress_level = stress_level;
    g_hpi_last_update.gsr_tonic_level_x100 = tonic_x100;
    g_hpi_last_update.gsr_peaks_per_minute = peaks_per_min;
    g_hpi_last_update.gsr_last_update_ts = update_ts;
    k_mutex_unlock(&mutex_hpi_last_update_time);

    /* Persist via settings subsystem */
    hpi_meas_save_gsr_stress(stress_level, tonic_x100, peaks_per_min, update_ts);
}

int hpi_sys_get_last_gsr_update(uint16_t *gsr_last_value, int64_t *gsr_last_update_ts)
{
    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    *gsr_last_value = g_hpi_last_update.gsr_last_value;
    *gsr_last_update_ts = g_hpi_last_update.gsr_last_update_ts;
    k_mutex_unlock(&mutex_hpi_last_update_time);
    return 0;
}

int hpi_sys_get_last_gsr_stress(uint8_t *stress_level, uint16_t *tonic_x100, uint8_t *peaks_per_min, int64_t *update_ts)
{
    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    *stress_level = g_hpi_last_update.gsr_stress_level;
    *tonic_x100 = g_hpi_last_update.gsr_tonic_level_x100;
    *peaks_per_min = g_hpi_last_update.gsr_peaks_per_minute;
    *update_ts = g_hpi_last_update.gsr_last_update_ts;
    k_mutex_unlock(&mutex_hpi_last_update_time);
    return 0;
}

static bool is_timestamp_today(int64_t ts)
{
    if (ts == 0)
    {
        return false;
    }

    // Get current system time as int64_t
    int64_t now_ts = hw_get_sys_time_ts();

    struct tm today_time_tm = *gmtime(&now_ts);
    today_time_tm.tm_hour = 0;
    today_time_tm.tm_min = 0;
    today_time_tm.tm_sec = 0;
    int64_t today_ts = timeutil_timegm64(&today_time_tm);

    LOG_DBG("Checking if timestamp %lld is today. Today's start: %lld", ts, today_ts);

    // Check if ts is within today's range
    if (ts >= today_ts && ts < (today_ts + 86400))
    {
        return true;
    }
    return false;
}

void hpi_sys_thread(void)
{
    int ret = 0;

    k_sem_take(&sem_hpi_sys_thread_start, K_FOREVER);
    LOG_INF("HPI Sys Thread starting");

    dfu_callback.callback = dfu_callback_func;
    dfu_callback.event_id = (MGMT_EVT_OP_IMG_MGMT_DFU_STOPPED | MGMT_EVT_OP_IMG_MGMT_DFU_STARTED | MGMT_EVT_OP_IMG_MGMT_DFU_PENDING | MGMT_EVT_OP_IMG_MGMT_DFU_CONFIRMED);
    mgmt_callback_register(&dfu_callback);

    // img_callback.callback = img_callback_func;
    // img_callback.event_id = (MGMT_EVT_OP_IMG_MGMT_DFU_CHUNK);
    // mgmt_callback_register(&img_callback);
    LOG_DBG("DFU callback registered");

    // Initialize measurement settings (uses Zephyr settings subsystem)
    ret = hpi_measurement_settings_init();
    if (ret != 0)
    {
        LOG_ERR("Error initializing measurement settings: %d", ret);
    }

    hpi_disp_restore_brightness();

    // Load steps from settings and initialize if from today
    uint16_t saved_steps = 0;
    int64_t saved_steps_ts = 0;
    if (hpi_meas_load_steps(&saved_steps, &saved_steps_ts) == 0 && saved_steps_ts > 0)
    {
        if (is_timestamp_today(saved_steps_ts))
        {
            today_init_steps(saved_steps);
            LOG_DBG("Steps initialized from settings: %u", saved_steps);
        }
        else
        {
            today_init_steps(0);
        }
    }
    else
    {
        today_init_steps(0);
    }

    // Load other cached values from settings into RAM cache
    uint16_t hr_val;
    int64_t hr_ts;
    if (hpi_meas_load_hr(&hr_val, &hr_ts) == 0 && hr_ts > 0)
    {
        k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
        g_hpi_last_update.hr_last_value = hr_val;
        g_hpi_last_update.hr_last_update_ts = hr_ts;
        k_mutex_unlock(&mutex_hpi_last_update_time);
    }

    uint8_t spo2_val;
    int64_t spo2_ts;
    if (hpi_meas_load_spo2(&spo2_val, &spo2_ts) == 0 && spo2_ts > 0)
    {
        k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
        g_hpi_last_update.spo2_last_value = spo2_val;
        g_hpi_last_update.spo2_last_update_ts = spo2_ts;
        k_mutex_unlock(&mutex_hpi_last_update_time);
    }

    uint8_t bp_sys, bp_dia;
    int64_t bp_ts;
    if (hpi_meas_load_bp(&bp_sys, &bp_dia, &bp_ts) == 0 && bp_ts > 0)
    {
        k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
        g_hpi_last_update.bp_sys_last_value = bp_sys;
        g_hpi_last_update.bp_dia_last_value = bp_dia;
        g_hpi_last_update.bp_last_update_ts = bp_ts;
        k_mutex_unlock(&mutex_hpi_last_update_time);
    }

    uint8_t ecg_hr;
    int64_t ecg_ts;
    if (hpi_meas_load_ecg(&ecg_hr, &ecg_ts) == 0 && ecg_ts > 0)
    {
        k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
        g_hpi_last_update.ecg_last_hr = ecg_hr;
        g_hpi_last_update.ecg_last_update_ts = ecg_ts;
        k_mutex_unlock(&mutex_hpi_last_update_time);
    }

    uint16_t temp_val;
    int64_t temp_ts;
    if (hpi_meas_load_temp(&temp_val, &temp_ts) == 0 && temp_ts > 0)
    {
        k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
        g_hpi_last_update.temp_last_value = temp_val;
        g_hpi_last_update.temp_last_update_ts = temp_ts;
        k_mutex_unlock(&mutex_hpi_last_update_time);
    }

    uint8_t gsr_stress, gsr_peaks;
    uint16_t gsr_tonic;
    int64_t gsr_ts;
    if (hpi_meas_load_gsr_stress(&gsr_stress, &gsr_tonic, &gsr_peaks, &gsr_ts) == 0 && gsr_ts > 0)
    {
        k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
        g_hpi_last_update.gsr_stress_level = gsr_stress;
        g_hpi_last_update.gsr_tonic_level_x100 = gsr_tonic;
        g_hpi_last_update.gsr_peaks_per_minute = gsr_peaks;
        g_hpi_last_update.gsr_last_update_ts = gsr_ts;
        k_mutex_unlock(&mutex_hpi_last_update_time);
    }

    uint16_t hrv_lf_hf, hrv_sdnn, hrv_rmssd;
    int64_t hrv_ts;
    if (hpi_meas_load_hrv(&hrv_lf_hf, &hrv_sdnn, &hrv_rmssd, &hrv_ts) == 0 && hrv_ts > 0)
    {
        k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
        g_hpi_last_update.hrv_lf_hf_ratio_x100 = hrv_lf_hf;
        g_hpi_last_update.hrv_sdnn_x10 = hrv_sdnn;
        g_hpi_last_update.hrv_rmssd_x10 = hrv_rmssd;
        g_hpi_last_update.hrv_last_update_ts = hrv_ts;
        k_mutex_unlock(&mutex_hpi_last_update_time);
    }

    LOG_INF("Measurement settings loaded from persistent storage");

    // Thread now just sleeps - all saves happen immediately via settings subsystem
    while (1)
    {
        k_sleep(K_FOREVER);
    }
}

static void sys_bpt_list(const struct zbus_channel *chan)
{
    const struct hpi_bpt_t *hpi_bp = zbus_chan_const_msg(chan);
    hpi_sys_set_last_bp_update(hpi_bp->sys, hpi_bp->dia, hw_get_sys_time_ts());
}
ZBUS_LISTENER_DEFINE(sys_bpt_lis, sys_bpt_list);

static void sys_hr_list(const struct zbus_channel *chan)
{
    const struct hpi_hr_t *hpi_hr = zbus_chan_const_msg(chan);
    hpi_sys_set_last_hr_update(hpi_hr->hr, hpi_hr->timestamp);
}
ZBUS_LISTENER_DEFINE(sys_hr_lis, sys_hr_list);

static void sys_temp_list(const struct zbus_channel *chan)
{
    const struct hpi_temp_t *hpi_temp = zbus_chan_const_msg(chan);
    uint16_t temp_x100 = (uint16_t)(hpi_temp->temp_f * 100);
    int64_t ts = hw_get_sys_time_ts();

    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    g_hpi_last_update.temp_last_value = temp_x100;
    g_hpi_last_update.temp_last_update_ts = ts;
    k_mutex_unlock(&mutex_hpi_last_update_time);

    /* Persist via settings subsystem */
    hpi_meas_save_temp(temp_x100, ts);
}
ZBUS_LISTENER_DEFINE(sys_temp_lis, sys_temp_list);

static void sys_steps_list(const struct zbus_channel *chan)
{
    const struct hpi_steps_t *hpi_steps = zbus_chan_const_msg(chan);
    uint16_t steps = hpi_steps->steps;
    int64_t ts = hw_get_sys_time_ts();

    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    g_hpi_last_update.steps_last_value = steps;
    g_hpi_last_update.steps_last_update_ts = ts;
    k_mutex_unlock(&mutex_hpi_last_update_time);

    /* Persist via settings subsystem */
    hpi_meas_save_steps(steps, ts);
}
ZBUS_LISTENER_DEFINE(sys_steps_lis, sys_steps_list);

static void sys_sys_time_list(const struct zbus_channel *chan)
{
    const struct tm *sys_time = zbus_chan_const_msg(chan);
    hpi_sys_set_sys_time(sys_time);
}
ZBUS_LISTENER_DEFINE(sys_sys_time_lis, sys_sys_time_list);

static void sys_ecg_stat_list(const struct zbus_channel *chan)
{
    const struct hpi_ecg_status_t *hpi_ecg = zbus_chan_const_msg(chan);
    uint8_t hr = hpi_ecg->hr;
    int64_t ts = hw_get_sys_time_ts();

    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    g_hpi_last_update.ecg_last_update_ts = ts;
    g_hpi_last_update.ecg_last_hr = hr;
    k_mutex_unlock(&mutex_hpi_last_update_time);

    /* Persist via settings subsystem */
    hpi_meas_save_ecg(hr, ts);
}
ZBUS_LISTENER_DEFINE(sys_ecg_stat_lis, sys_ecg_stat_list);

static void sys_hrv_stat_list(const struct zbus_channel *chan)
{
    const struct hpi_hrv_status_t *hpi_hrv = zbus_chan_const_msg(chan);
    int64_t ts = hw_get_sys_time_ts();

    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    g_hpi_last_update.hrv_last_update_ts = ts;
    /* HRV value is set via hpi_sys_set_last_hrv_update() when result is available */
    k_mutex_unlock(&mutex_hpi_last_update_time);

    ARG_UNUSED(hpi_hrv);
}
ZBUS_LISTENER_DEFINE(sys_hrv_stat_lis, sys_hrv_stat_list);

#define HPI_SYS_THREAD_STACKSIZE 2048
#define HPI_SYS_THREAD_PRIORITY 5

K_THREAD_DEFINE(hpi_sys_thread_id, HPI_SYS_THREAD_STACKSIZE, hpi_sys_thread, NULL, NULL, NULL, HPI_SYS_THREAD_PRIORITY, 0, 0);