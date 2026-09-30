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
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <stdio.h>

#include <time.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>
#include <zephyr/mgmt/mcumgr/grp/os_mgmt/os_mgmt_callbacks.h>
#include <zephyr/mgmt/mcumgr/grp/img_mgmt/img_mgmt.h>
#include <zephyr/mgmt/mcumgr/grp/img_mgmt/img_mgmt_callbacks.h>
#include <zephyr/mgmt/mcumgr/grp/fs_mgmt/fs_mgmt_callbacks.h>
#include <string.h>
#include <zephyr/sys/timeutil.h>
#include <zephyr/drivers/rtc.h>

#include "hpi_common_types.h"
#include "hpi_sys.h"
#include "hw_module.h"
#include "hpi_user_settings_api.h"  /* utc offset get/set + persistence */
#include "hpi_dfu.h"      /* DFU system-mode state + progress */
#include "hpi_storage_migrate.h" /* one-shot pre-3.0 storage purge */
#include "ui/move_ui.h"  /* hpi_disp_restore_brightness() */
#include "max32664_updater.h" /* MSBL file paths */

/* Refuse to start an OTA below this SoC% unless on charger — overwrite-only DFU
 * has no revert, so a brown-out during the post-reboot MCUboot swap can brick. */
#define HPI_DFU_MIN_BATTERY_PCT 30

LOG_MODULE_REGISTER(hpi_sys_module, LOG_LEVEL_DBG);

// External device references (declared extern in hw_module.c)
extern const struct device *rtc_dev;

// Time synchronization variables
static int64_t rtc_to_uptime_offset = 0;

// Display time cache (updated every 5s via ZBus for UI display only)
static struct tm m_sys_sys_time;

/* UTC offset in SECONDS east of UTC (local = UTC + offset), DST-inclusive.
 * Set by the phone via HPI_HS SET_TZ and persisted; loaded at boot. int32 so
 * the full -12h..+14h range fits (int16 overflowed past ~9.1h). The RTC holds
 * UTC; this offset is applied only at the display / local-calendar edge. */
int32_t timezone_offset_sec = 0;

K_MUTEX_DEFINE(mutex_time_sync);

struct mgmt_callback dfu_callback;

enum mgmt_cb_return dfu_callback_func(uint32_t event, enum mgmt_cb_return prev_status,
                                      int32_t *rc, uint16_t *group, bool *abort_more,
                                      void *data, size_t data_size)
{

    switch (event) {
    case MGMT_EVT_OP_IMG_MGMT_DFU_STARTED:
        LOG_INF("DFU started");
        hpi_dfu_set_progress(0);
        hpi_dfu_set_state(HPI_DFU_ACTIVE);
        break;

    case MGMT_EVT_OP_IMG_MGMT_DFU_CHUNK: {
        /* Upload-check hook (CONFIG_MCUMGR_GRP_IMG_UPLOAD_CHECK_HOOK): authorise
         * each chunk and report progress. Battery-gate on the first chunk so the
         * upload is refused before any flash is written. */
        const struct img_mgmt_upload_check *chk = data;

        if (chk != NULL && chk->req != NULL && chk->req->off == 0) {
            if (hw_get_current_battery_level() < HPI_DFU_MIN_BATTERY_PCT &&
                !hw_is_vbus_connected()) {
                LOG_WRN("DFU refused: battery %u%% < %d%% and not charging",
                        hw_get_current_battery_level(), HPI_DFU_MIN_BATTERY_PCT);
                hpi_dfu_set_state(HPI_DFU_LOW_BATTERY);
                *rc = MGMT_ERR_EBADSTATE;
                *group = MGMT_GROUP_ID_IMAGE;
                return MGMT_CB_ERROR_RC;
            }
        }

        if (chk != NULL && chk->req != NULL && chk->action != NULL &&
            chk->action->size > 0) {
            uint64_t off = (uint64_t)chk->req->off;
            uint64_t total = (uint64_t)chk->action->size;
            hpi_dfu_set_progress((int)((off * 100U) / total));
        }
        hpi_dfu_set_state(HPI_DFU_ACTIVE);
        break;
    }

    case MGMT_EVT_OP_IMG_MGMT_DFU_PENDING:
        LOG_INF("DFU upload complete — image pending");
        hpi_dfu_set_progress(100);
        hpi_dfu_set_state(HPI_DFU_FINALIZING);
        break;

    case MGMT_EVT_OP_IMG_MGMT_DFU_STOPPED:
        /* Only a genuine abort/failure of a running upload — don't clobber a
         * successful FINALIZING or the LOW_BATTERY rejection. */
        if (hpi_dfu_get_state() == HPI_DFU_ACTIVE) {
            LOG_WRN("DFU stopped/failed");
            hpi_dfu_set_state(HPI_DFU_FAILED);
        }
        break;

    default:
        break;
    }

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

#if defined(CONFIG_MCUMGR_GRP_FS_FILE_ACCESS_HOOK)
static struct mgmt_callback fs_access_callback;

/* The MCUmgr FS group is enabled only so the MAX32664C/D MSBL images can be
 * uploaded. Allow writes (plus status/hash, to verify an upload) to exactly
 * those two paths; refuse every read and every other path. Exact string match,
 * so "..", trailing slashes etc. can't reach anything else. */
static enum mgmt_cb_return fs_access_callback_func(uint32_t event, enum mgmt_cb_return prev_status,
                                                   int32_t *rc, uint16_t *group, bool *abort_more,
                                                   void *data, size_t data_size)
{
    ARG_UNUSED(prev_status);
    ARG_UNUSED(group);
    ARG_UNUSED(abort_more);

    if (event != MGMT_EVT_OP_FS_MGMT_FILE_ACCESS) {
        return MGMT_CB_OK;
    }

    const struct fs_mgmt_file_access *fa = data;

    if (fa == NULL || data_size < sizeof(*fa) || fa->filename == NULL) {
        *rc = MGMT_ERR_EINVAL;
        return MGMT_CB_ERROR_RC;
    }

    bool msbl_path = (strcmp(fa->filename, MAX32664C_FW_PATH) == 0) ||
                     (strcmp(fa->filename, MAX32664D_FW_PATH) == 0);

    if (msbl_path && fa->access != FS_MGMT_FILE_ACCESS_READ) {
        if (fa->access == FS_MGMT_FILE_ACCESS_WRITE) {
            LOG_INF("MCUmgr MSBL upload: %s", fa->filename);
        }
        return MGMT_CB_OK;
    }

    LOG_WRN("MCUmgr file access denied (type %d): %s", fa->access, fa->filename);
    *rc = MGMT_ERR_EACCESSDENIED;
    return MGMT_CB_ERROR_RC;
}

/* Registered at SYS_INIT, not from hpi_sys_thread: with no callback registered
 * the FS group allows everything, so the filter must be in place before BLE is up. */
static int fs_access_hook_init(void)
{
    fs_access_callback.callback = fs_access_callback_func;
    fs_access_callback.event_id = MGMT_EVT_OP_FS_MGMT_FILE_ACCESS;
    mgmt_callback_register(&fs_access_callback);
    return 0;
}
SYS_INIT(fs_access_hook_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
#endif

#if defined(CONFIG_MCUMGR_GRP_OS_DATETIME_HOOK)
struct mgmt_callback datetime_callback;

/* MCUmgr OS datetime SET (group 0 / cmd 4). Payload is struct rtc_time. */
enum mgmt_cb_return datetime_callback_func(uint32_t event, enum mgmt_cb_return prev_status,
                                           int32_t *rc, uint16_t *group, bool *abort_more,
                                           void *data, size_t data_size)
{
    ARG_UNUSED(prev_status);
    ARG_UNUSED(group);
    ARG_UNUSED(abort_more);

    if (event != MGMT_EVT_OP_OS_MGMT_DATETIME_SET) {
        return MGMT_CB_OK;
    }
    if (data == NULL || data_size < sizeof(struct rtc_time) || rc == NULL) {
        if (rc) {
            *rc = MGMT_ERR_EINVAL;
        }
        return MGMT_CB_ERROR_RC;
    }

    const struct rtc_time *rt = data;
    struct tm tm_set = {
        .tm_sec = rt->tm_sec,
        .tm_min = rt->tm_min,
        .tm_hour = rt->tm_hour,
        .tm_mday = rt->tm_mday,
        .tm_mon = rt->tm_mon,
        .tm_year = rt->tm_year,
        .tm_wday = rt->tm_wday,
        .tm_yday = rt->tm_yday,
        .tm_isdst = rt->tm_isdst,
    };

    if (rtc_dev != NULL && !device_is_ready(rtc_dev)) {
        (void)device_init(rtc_dev);
    }

    /* Writes RV8263 via rtc_set_time and refreshes rtc_to_uptime_offset. */
    hpi_sys_set_rtc_time(&tm_set);

    LOG_INF("MCUmgr datetime SET applied: %04d-%02d-%02d %02d:%02d:%02d",
            tm_set.tm_year + 1900, tm_set.tm_mon + 1, tm_set.tm_mday,
            tm_set.tm_hour, tm_set.tm_min, tm_set.tm_sec);

    /* Skip Zephyr's second rtc_set_time in os_mgmt_datetime_write. */
    *rc = MGMT_ERR_EOK;
    return MGMT_CB_ERROR_RC;
}
#endif


static bool is_on_skin = false;
K_MUTEX_DEFINE(mutex_on_skin);

K_MUTEX_DEFINE(mutex_sys_time);

// Externs
extern struct k_sem sem_hpi_sys_thread_start;




int hpi_sys_set_sys_time(struct tm *tm)
{
    int ret = 0;
    k_mutex_lock(&mutex_sys_time, K_FOREVER);
    m_sys_sys_time = *tm;
    k_mutex_unlock(&mutex_sys_time);
    return ret;
}

int64_t hw_get_sys_time_ts(void)
{
    return hw_get_synced_system_time();
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

// Sync offset with RTC hardware. Called only at boot and after BLE time set.
// Between syncs, hw_get_synced_system_time() interpolates using k_uptime_get().
// Crystal drift between nRF5340 LFXO (~20ppm) and RV8263 (~2ppm) is <2s/day,
// so periodic re-sync is unnecessary — the phone app re-syncs on each connect.
static int hpi_sys_sync_time_with_rtc(void)
{
    struct rtc_time rtc_sys_time;

    int ret = rtc_get_time(rtc_dev, &rtc_sys_time);
    if (ret < 0)
    {
        LOG_ERR("Failed to get RTC time for sync: %d", ret);
        return ret;
    }

    /* RTC holds UTC (the phone sends UTC via MCUmgr os-datetime), so timegm() of
     * it IS the canonical UTC epoch. The timezone offset is NOT applied here — it
     * belongs only at the display / local-calendar edge (get_current_time,
     * ts_is_today). Keeping storage/logs in UTC is what makes samples comparable
     * across DST changes and travel. */
    struct tm rtc_tm = *rtc_time_to_tm(&rtc_sys_time);
    int64_t rtc_timestamp = timeutil_timegm64(&rtc_tm); // RTC (UTC) -> UTC epoch
    int64_t current_uptime = k_uptime_get();

    k_mutex_lock(&mutex_time_sync, K_FOREVER);
    rtc_to_uptime_offset = rtc_timestamp - (current_uptime / 1000);
    k_mutex_unlock(&mutex_time_sync);

    // Update display time cache
    hpi_sys_set_sys_time(&rtc_tm);

    LOG_INF("Time synced with RTC. Offset: %lld", rtc_to_uptime_offset);
    return 0;
}

void hpi_sys_set_rtc_time(const struct tm *time_to_set)
{
    struct rtc_time rtc_time_set;

    if (time_to_set == NULL || rtc_dev == NULL) {
        return;
    }
    if (!device_is_ready(rtc_dev)) {
        int r = device_init(rtc_dev);
        if (r < 0) {
            LOG_ERR("RTC not ready for set (%d)", r);
            return;
        }
    }

    rtc_time_set.tm_sec = time_to_set->tm_sec;
    rtc_time_set.tm_min = time_to_set->tm_min;
    rtc_time_set.tm_hour = time_to_set->tm_hour;
    rtc_time_set.tm_mday = time_to_set->tm_mday;
    rtc_time_set.tm_mon = time_to_set->tm_mon;
    rtc_time_set.tm_year = time_to_set->tm_year;
    rtc_time_set.tm_wday = time_to_set->tm_wday;
    rtc_time_set.tm_yday = time_to_set->tm_yday;
    rtc_time_set.tm_nsec = 0;

    int ret = rtc_set_time(rtc_dev, &rtc_time_set);
    if (ret < 0)
    {
        LOG_ERR("Failed to set RTC time: %d", ret);
        return;
    }

    // Sync offset immediately after writing RTC
    hpi_sys_sync_time_with_rtc();
    LOG_INF("RTC time updated and synced");
}

int hpi_sys_force_time_sync(void)
{
    return hpi_sys_sync_time_with_rtc();
}

/* Set + persist the UTC offset (seconds east of UTC), then re-derive the display
 * clock so it updates immediately. Called from the HPI_HS SET_TZ command. The RTC
 * (UTC) is not touched — only the display/local-calendar view shifts. */
void hpi_sys_set_utc_offset(int32_t offset_sec)
{
    if (hpi_user_settings_set_utc_offset(offset_sec) != 0) {
        LOG_WRN("UTC offset %d out of range, ignored", offset_sec);
        return;
    }
    timezone_offset_sec = offset_sec;
    LOG_INF("UTC offset set: %d sec", offset_sec);
    hpi_sys_sync_time_with_rtc();   /* refresh display cache + offset now */
}

int32_t hpi_sys_get_utc_offset(void)
{
    return timezone_offset_sec;
}

struct tm hpi_sys_get_current_time(void)
{
    int64_t current_timestamp = hw_get_synced_system_time();
    current_timestamp += timezone_offset_sec; // Add offset back for local time display purposes
    struct tm current_time;
    gmtime_r(&current_timestamp, &current_time);
    return current_time;
}

// True if a stored UTC timestamp falls on the current *local* calendar day.
// Used to decide whether a persisted daily total (e.g. steps) still applies
// after a reboot, or belongs to a previous day and should start fresh.
bool hpi_sys_ts_is_today(int64_t ts_utc)
{
    if (ts_utc < HPI_TIME_MIN_TIMESTAMP || ts_utc > HPI_TIME_MAX_TIMESTAMP)
    {
        return false;
    }
    if (!hpi_sys_is_time_valid())
    {
        return false;
    }
    struct tm now = hpi_sys_get_current_time();
    int64_t local_ts = ts_utc + timezone_offset_sec; // to local-as-UTC, same basis as above
    struct tm then;
    gmtime_r(&local_ts, &then);
    return (then.tm_year == now.tm_year && then.tm_yday == now.tm_yday);
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


/* Last-value store removed — the health store owns this now (greenfield). */

/* is_timestamp_today removed (was only used by the steps last-value restore) */

void hpi_sys_thread(void)
{
    int ret = 0;

    k_sem_take(&sem_hpi_sys_thread_start, K_FOREVER);
    LOG_INF("HPI Sys Thread starting");

    /* Restore the persisted UTC offset so the clock reads local before the phone
     * next connects. Settings are loaded by hw init before this thread runs. */
    timezone_offset_sec = hpi_user_settings_get_utc_offset();
    LOG_INF("UTC offset restored: %d sec", timezone_offset_sec);

    dfu_callback.callback = dfu_callback_func;
    dfu_callback.event_id = (MGMT_EVT_OP_IMG_MGMT_DFU_STOPPED | MGMT_EVT_OP_IMG_MGMT_DFU_STARTED | MGMT_EVT_OP_IMG_MGMT_DFU_PENDING | MGMT_EVT_OP_IMG_MGMT_DFU_CONFIRMED | MGMT_EVT_OP_IMG_MGMT_DFU_CHUNK);
    mgmt_callback_register(&dfu_callback);

#if defined(CONFIG_MCUMGR_GRP_OS_DATETIME_HOOK)
    /* MCUmgr os datetime write calls rtc_set_time() on DT_ALIAS(rtc) but does
     * not touch our software RTC↔uptime offset. Take ownership of SET: write
     * the RV8263 via hpi_sys_set_rtc_time() (which re-syncs the offset) and
     * return EOK with MGMT_CB_ERROR_RC so the default second rtc_set_time is
     * skipped. GET is left to the default handler (reads RTC hardware). */
    datetime_callback.callback = datetime_callback_func;
    datetime_callback.event_id = MGMT_EVT_OP_OS_MGMT_DATETIME_SET;
    mgmt_callback_register(&datetime_callback);
    LOG_DBG("MCUmgr datetime SET hook registered");
#endif

    LOG_DBG("DFU callback registered");

    hpi_disp_restore_brightness();
    /* step total is owned + initialized by hw_thread (restores today's count
     * from the health store, or starts at 0); no reset here (would race it). */

    /* One-shot storage migration (drops the pre-3.0 trend/recording tree on a
     * unit upgraded from 2.x). Deliberately here and not in fs_module_init():
     * that runs on the boot critical path, and unlinking thousands of files on
     * the external QSPI die takes seconds. No-op once stamped. */
    ret = hpi_storage_migrate_run();
    if (ret == -EAGAIN)
    {
        LOG_INF("Storage migration deferred (DFU active); will retry next boot");
    }

    /* Settings saves happen immediately via the settings subsystem, so there is
     * no periodic work here. The thread stays alive as the storage-maintenance
     * worker: hpi_storage_service() blocks on the erase request semaphore, so an
     * idle system costs exactly what k_sleep(K_FOREVER) used to. See
     * hpi_storage_migrate.c for why the erase lands on this thread rather than a
     * dedicated workqueue or the (cooperative, shared) system workqueue. */
    while (1)
    {
        hpi_storage_service(K_FOREVER);
    }
}


static void sys_sys_time_list(const struct zbus_channel *chan)
{
    const struct tm *sys_time = zbus_chan_const_msg(chan);
    hpi_sys_set_sys_time(sys_time);
}
ZBUS_LISTENER_DEFINE(sys_sys_time_lis, sys_sys_time_list);


/* 3 KB, not 2 KB: this thread now does filesystem work -- the one-shot pre-3.0
 * purge at boot and every user-requested erase -- and the littlefs call chain
 * behind fs_opendir/fs_readdir/fs_unlink dominates its stack use. 2 KB was sized
 * for the init work above (mgmt callback registration, a couple of snprintf'd
 * date strings) and is not a safe budget for that.
 *
 * The number is a starting point, not a measurement: hpi_storage_service() logs
 * k_thread_stack_space_get() straight after the deepest FS work it ever does, so
 * tune this from that log on a unit that actually has files to delete. Costs
 * 1 KB of the ~11 KB of app-core RAM still free -- which is also why the erase
 * reuses this thread instead of getting a workqueue and a second stack. */
#define HPI_SYS_THREAD_STACKSIZE 3072
#define HPI_SYS_THREAD_PRIORITY 5

K_THREAD_DEFINE(hpi_sys_thread_id, HPI_SYS_THREAD_STACKSIZE, hpi_sys_thread, NULL, NULL, NULL, HPI_SYS_THREAD_PRIORITY, 0, 0);