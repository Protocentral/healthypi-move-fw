#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <stdio.h>

#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <time.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>

#include <time.h>
#include <zephyr/posix/time.h>
#include <zephyr/sys/timeutil.h>
#include <zephyr/drivers/rtc.h>

#include "hpi_common_types.h"
#include "hpi_sys.h"
#include "hw_module.h"

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

const char hpi_sys_update_time_file[] = "/lfs/sys/hpi_sys_update_time";

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
};

K_MUTEX_DEFINE(mutex_hpi_last_update_time);

static int hpi_sys_store_update_time(void)
{
    int ret = 0;

    LOG_DBG("Storing last update time");

    struct fs_file_t m_file;
    fs_file_t_init(&m_file);

    ret = fs_open(&m_file, hpi_sys_update_time_file, FS_O_CREATE | FS_O_WRITE);
    if (ret != 0)
    {
        LOG_ERR("Error opening file %d", ret);
        return ret;
    }

    ret = fs_write(&m_file, &g_hpi_last_update, sizeof(g_hpi_last_update));
    if (ret < 0)
    {
        LOG_ERR("Error writing file %d", ret);
        return ret;
    }

    ret = fs_close(&m_file);
    if (ret != 0)
    {
        LOG_ERR("Error closing file %d", ret);
        return ret;
    }

    return ret;
}

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
        snprintf(out_str, out_str_size, "%02d-%02d-%04d",
                 tm_info->tm_mday, tm_info->tm_mon + 1, tm_info->tm_year + 1900);
    }
    return 0;
}

static int hpi_sys_load_update_time(void)
{
    int ret = 0;

    struct hpi_last_update_time_t m_hpi_last_update_time = {
        .bp_last_update_ts = 0,
        .hr_last_update_ts = 0,
        .spo2_last_update_ts = 0,
        .ecg_last_update_ts = 0,

        .hr_last_value = 0,
        .spo2_last_value = 0,
        .bp_sys_last_value = 0,
        .bp_dia_last_value = 0,
        .ecg_last_hr = 0,
    };

    struct fs_file_t m_file;
    fs_file_t_init(&m_file);

    ret = fs_open(&m_file, hpi_sys_update_time_file, FS_O_READ);
    if (ret != 0)
    {
        LOG_ERR("Error opening file %d", ret);
        return ret;
    }

    ret = fs_read(&m_file, &m_hpi_last_update_time, sizeof(m_hpi_last_update_time));
    if (ret < 0)
    {
        LOG_ERR("Error reading file %d", ret);
        return ret;
    }

    ret = fs_close(&m_file);
    if (ret != 0)
    {
        LOG_ERR("Error closing file %d", ret);
        return ret;
    }

    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    g_hpi_last_update.bp_last_update_ts = m_hpi_last_update_time.bp_last_update_ts;
    g_hpi_last_update.bp_sys_last_value = m_hpi_last_update_time.bp_sys_last_value;
    g_hpi_last_update.bp_dia_last_value = m_hpi_last_update_time.bp_dia_last_value;

    g_hpi_last_update.hr_last_update_ts = m_hpi_last_update_time.hr_last_update_ts;
    g_hpi_last_update.spo2_last_update_ts = m_hpi_last_update_time.spo2_last_update_ts;
    g_hpi_last_update.ecg_last_update_ts = m_hpi_last_update_time.ecg_last_update_ts;
    g_hpi_last_update.hr_last_value = m_hpi_last_update_time.hr_last_value;
    g_hpi_last_update.spo2_last_value = m_hpi_last_update_time.spo2_last_value;

    g_hpi_last_update.ecg_last_hr = m_hpi_last_update_time.ecg_last_hr;

    g_hpi_last_update.temp_last_update_ts = m_hpi_last_update_time.temp_last_update_ts;
    g_hpi_last_update.temp_last_value = m_hpi_last_update_time.temp_last_value;

    g_hpi_last_update.steps_last_update_ts = m_hpi_last_update_time.steps_last_update_ts;
    g_hpi_last_update.steps_last_value = m_hpi_last_update_time.steps_last_value;
    k_mutex_unlock(&mutex_hpi_last_update_time);

    return ret;
}

void hpi_sys_set_last_hr_update(uint16_t hr_last_value, int64_t hr_last_update_ts)
{
    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    g_hpi_last_update.hr_last_value = hr_last_value;
    g_hpi_last_update.hr_last_update_ts = hr_last_update_ts;
    k_mutex_unlock(&mutex_hpi_last_update_time);
}

void hpi_sys_set_last_spo2_update(uint8_t spo2_last_value, int64_t spo2_last_update_ts)
{
    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    g_hpi_last_update.spo2_last_value = spo2_last_value;
    g_hpi_last_update.spo2_last_update_ts = spo2_last_update_ts;
    k_mutex_unlock(&mutex_hpi_last_update_time);
}

void hpi_sys_set_last_bp_update(uint16_t bp_sys_last_value, uint16_t bp_dia_last_value, int64_t bp_last_update_ts)
{
    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    g_hpi_last_update.bp_sys_last_value = bp_sys_last_value;
    g_hpi_last_update.bp_dia_last_value = bp_dia_last_value;
    g_hpi_last_update.bp_last_update_ts = bp_last_update_ts;
    k_mutex_unlock(&mutex_hpi_last_update_time);
}

void hpi_sys_set_last_ecg_update(int64_t ecg_last_update_ts)
{
    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    g_hpi_last_update.ecg_last_update_ts = ecg_last_update_ts;
    k_mutex_unlock(&mutex_hpi_last_update_time);
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

    // Load last update time
    ret = hpi_sys_load_update_time();
    if (ret != 0)
    {
        LOG_ERR("Error loading last update time %d", ret);
    }
    else
    {
        LOG_DBG("Last update time loaded");

        if (is_timestamp_today(g_hpi_last_update.steps_last_update_ts))
        {
            today_init_steps(g_hpi_last_update.steps_last_value);
            // LOG_DBG("Today's steps initialized: %d", g_hpi_last_update.steps_last_value);
        }
        else
        {
            today_init_steps(0);
            // LOG_DBG("Today's steps initialized to 0");
        }
    }

    while (1)
    {
        k_sleep(K_SECONDS(60));
        hpi_sys_store_update_time();
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
    g_hpi_last_update.temp_last_value = (hpi_temp->temp_f * 100);
    g_hpi_last_update.temp_last_update_ts = hw_get_sys_time_ts();
}
ZBUS_LISTENER_DEFINE(sys_temp_lis, sys_temp_list);

static void sys_steps_list(const struct zbus_channel *chan)
{
    const struct hpi_steps_t *hpi_steps = zbus_chan_const_msg(chan);
    g_hpi_last_update.steps_last_value = hpi_steps->steps;
    g_hpi_last_update.steps_last_update_ts = hw_get_sys_time_ts();
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
    g_hpi_last_update.ecg_last_update_ts = hw_get_sys_time_ts();
    g_hpi_last_update.ecg_last_hr = hpi_ecg->hr;
}
ZBUS_LISTENER_DEFINE(sys_ecg_stat_lis, sys_ecg_stat_list);

#define HPI_SYS_THREAD_STACKSIZE 2048
#define HPI_SYS_THREAD_PRIORITY 5

K_THREAD_DEFINE(hpi_sys_thread_id, HPI_SYS_THREAD_STACKSIZE, hpi_sys_thread, NULL, NULL, NULL, HPI_SYS_THREAD_PRIORITY, 0, 0);