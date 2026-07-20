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
#include "hpi_evt.h"
#include <zephyr/smf.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>
#include <zephyr/zbus/zbus.h>
#include <time.h>

#include <display_sh8601.h>
#include "hpi_common_types.h"
#include "hw_module.h"
#include "hpi_dfu.h"
#include "ui/move_ui.h"
#include "ui/hpi_r0_theme.h"
#include "max32664_updater.h"
#include "hpi_sys.h"
#include "health/hpi_health_store.h"
#include "hpi_user_settings_api.h"
#include "hpi_watchdog.h"
#include "ui/hpi_ui_subjects.h"

LOG_MODULE_REGISTER(smf_display, LOG_LEVEL_DBG);

#define HPI_DEFAULT_START_SCREEN SCR_HOME

/**
 * @brief Get the current sleep timeout in milliseconds based on user settings
 * @return Sleep timeout in milliseconds, or default if auto sleep is disabled
 */
static uint32_t get_sleep_timeout_ms(void)
{
    if (!hpi_user_settings_get_auto_sleep_enabled())
    {
        return UINT32_MAX; // Never sleep if auto sleep is disabled
    }

    uint8_t sleep_timeout_seconds = hpi_user_settings_get_sleep_timeout();
    uint32_t timeout_ms = sleep_timeout_seconds * 1000;

    // Log the current sleep timeout occasionally for debugging
    static uint32_t last_log_time = 0;
    uint32_t now = k_uptime_get_32();
    if (now - last_log_time > 60000)
    { // Log every minute
        LOG_DBG("Sleep timeout: %d seconds (%d ms)", sleep_timeout_seconds, timeout_ms);
        last_log_time = now;
    }

    return timeout_ms;
}

K_MSGQ_DEFINE(q_plot_ecg, sizeof(struct hpi_ecg_bioz_sensor_data_t), 128, 1);
K_MSGQ_DEFINE(q_plot_ppg_wrist, sizeof(struct hpi_ppg_wr_data_t), 32, 1);
K_MSGQ_DEFINE(q_plot_ppg_fi, sizeof(struct hpi_ppg_fi_data_t), 32, 1);
K_MSGQ_DEFINE(q_plot_hrv, sizeof(struct hpi_computed_hrv_t), 16, 1);
K_MSGQ_DEFINE(q_plot_gsr, sizeof(struct hpi_gsr_sensor_data_t), 128, 1);
K_MSGQ_DEFINE(q_disp_boot_msg, sizeof(struct hpi_boot_msg_t), 4, 1);

K_SEM_DEFINE(sem_disp_ready, 0, 1);
K_SEM_DEFINE(sem_touch_wakeup, 0, 1);  // Kept for wakeup signaling

/**
 * @brief Signal touch wakeup from sleep state
 * Called by input drivers (touch controller) when touch is detected.
 * Uses LVGL's activity tracking as the source of truth, but provides
 * explicit wakeup signaling for sleep state.
 */
void hpi_display_signal_touch_wakeup(void)
{
    // Trigger LVGL activity tracking
    lv_disp_trig_activity(NULL);
    
    // Signal the display state machine to wake up
    k_sem_give(&sem_touch_wakeup);
}

static bool hpi_boot_all_passed = true;
static int last_batt_refresh = 0;

static int last_time_refresh = 0;
static int last_settings_refresh = 0;

static int last_temp_trend_refresh = 0;

/* True while sleep state is showing the AOD dim face (vs full panel blank). */
static bool s_sleep_is_aod;
/* True while the display SMF is in the SLEEP state (any sleep flavour). Used to
 * wake the panel if a BLE OTA starts while asleep. */
static bool s_display_asleep;
/* True when HW AODMON is active (vs soft dim fallback). */
static bool s_sleep_aod_hw;

/* Screen-load inbox.
 *
 * This used to be a set of unlocked globals plus a binary semaphore: a caller
 * wrote g_screen/g_arg1..4 from its own thread and gave the sem. Two things went
 * wrong with that, and BLE pairing hit both. (1) The write was not atomic w.r.t.
 * the display thread's read, so a request landing mid-draw could be rendered as a
 * MIX of two requests -- e.g. the PAIR_CANCELLED event with the PAIR_REQUEST
 * passkey still in arg2, i.e. a stale passkey shown as if live. (2) The sem is
 * binary, so give;give;take coalesced silently and invisibly.
 *
 * Now the request travels as one unit through a msgq, so it can never tear, and
 * the g_* state below is written ONLY by the display thread (which also lets the
 * sleep-save read it without racing a producer). Coalescing still happens -- the
 * newest request wins, as before -- but it is now deliberate and logged rather
 * than an accident of the semaphore's depth. */
struct hpi_scr_load_req_t
{
    int32_t screen;
    enum scroll_dir scroll_dir;
    uint32_t arg1;
    uint32_t arg2;
    uint32_t arg3;
    uint32_t arg4;
};
K_MSGQ_DEFINE(q_scr_load, sizeof(struct hpi_scr_load_req_t), 8, 4);

static const struct smf_state display_states[];

enum display_state
{
    HPI_DISPLAY_STATE_INIT,
    HPI_DISPLAY_STATE_SPLASH,
    HPI_DISPLAY_STATE_BOOT,
    HPI_DISPLAY_STATE_SCR_PROGRESS,
    HPI_DISPLAY_STATE_ACTIVE,
    HPI_DISPLAY_STATE_TRANSITION,  // NEW: Blocks all updates during screen changes
    HPI_DISPLAY_STATE_SLEEP,
    HPI_DISPLAY_STATE_ON,
    HPI_DISPLAY_STATE_OFF,
};

// Global flag to suspend ALL screen updates during screen transitions
// This prevents race conditions where update functions try to access objects
// that are being deleted/created during screen changes
// NOTE: Not static because it's declared extern in move_ui.h
volatile bool screen_transition_in_progress = false;

// Display screen variables
static uint8_t m_disp_batt_level = 0;
static bool m_disp_batt_charging = false;
/* S3: whether the low-battery screen is currently the one we put up. The battery
 * state itself is owned by battery_module (hw_is_low_battery()); this tracks what
 * the display has reflected so we only load/dismiss on the edge. */
static bool m_low_batt_shown = false;
static struct tm m_disp_sys_time;

// Battery change detection - only update UI when data actually changes
static uint8_t last_displayed_batt_level = 255; // Initialize to invalid value to force first update
static bool last_displayed_batt_charging = false;

static uint32_t splash_scr_start_time = 0;

// HR Screen variables
static uint16_t m_disp_hr = 0;
static int64_t m_disp_hr_updated_ts = 0;

// @brief Spo2 Screen variables
static uint8_t m_disp_spo2 = 0;
static int64_t m_disp_spo2_last_refresh_ts;
/* Uptime of the last SpO2 display update — age fallback when RTC is not yet
 * VALID (health store drops those samples, but the hero still shows the value). */
static uint32_t m_disp_spo2_last_uptime_ms;

// @brief Today Screen variables
static uint32_t m_disp_steps = 0;
static uint16_t m_disp_active_time_s = 0;

// @brief Temperature Screen variables
static float m_disp_temp = 0;
static int64_t m_disp_temp_updated_ts = 0;

static uint16_t m_disp_bp_sys = 0;
static uint16_t m_disp_bp_dia = 0;
static uint32_t m_disp_bp_last_refresh = 0;
static uint8_t m_disp_bpt_status = 0;
static uint8_t m_disp_bpt_progress = 0;

// @brief ECG Screen variables
static int m_disp_ecg_timer = 0;
static uint16_t m_disp_ecg_hr = 0;
static int64_t m_disp_ecg_hr_ts;          /* UTC of last non-zero ECG HR latch */
static uint32_t m_disp_ecg_hr_uptime_ms;  /* uptime stamp for age fallback    */
/* Latest ECG SMF status + progress (set by disp_ecg_stat_listener, publisher
 * context) and the last values rendered into the inline monitor (display
 * thread). A change in either drives hpi_ecg_monitor_update() from the LVGL
 * thread so the monitor tracks the SMF phase (wait/stabilize/record/idle). */
static atomic_t m_disp_ecg_status = ATOMIC_INIT(HPI_ECG_STATUS_IDLE);
static int m_disp_ecg_status_synced = -1;
/* Separate tracker for the sleep guard: the ECG status is flipped from the ECG
 * SMF thread, so the end-of-measurement inactivity reset has to happen in the
 * same place the sleep decision is taken, not in the render pass that follows it. */
static int m_disp_ecg_sleep_synced = -1;
static int m_disp_ecg_timer_synced = -1;
static bool m_lead_on_off = false;

// @brief GSR Screen variables
static uint16_t m_disp_gsr_remaining = 30; // countdown timer (seconds remaining)
static float m_disp_gsr_us = 0.0f;
/* GSR SMF phase, mirroring the ECG monitor plumbing: the zbus listener stores it,
 * the display thread renders the inline EDA monitor from the change edge and takes
 * the sleep decision from it. */
static atomic_t m_disp_gsr_status = ATOMIC_INIT(HPI_GSR_STATUS_IDLE);
static atomic_t m_disp_gsr_contact = ATOMIC_INIT(0);
static int m_disp_gsr_status_synced = -1;
static int m_disp_gsr_remaining_synced = -1;
static int m_disp_gsr_contact_synced = -1;
static int m_disp_gsr_sleep_synced = -1;

// @brief HRV Screen variables
static int m_disp_hrv_timer = 0;




struct s_disp_object
{
    struct smf_ctx ctx;
    char title[100];
    char subtitle[100];

} s_disp_obj;

static int g_screen = SCR_HOME;
static enum scroll_dir g_scroll_dir = SCROLL_NONE;
static uint32_t g_arg1 = 0;
static uint32_t g_arg2 = 0;
static uint32_t g_arg3 = 0;
static uint32_t g_arg4 = 0;

/* (g_scr_parent removed: it was assigned from arg1 on every load and never read
 * once -- each screen tracks its own parent. Screens that need one take it in
 * arg1, e.g. the pulldown shade's m_pulldown_parent.) */

typedef void (*screen_draw_func_t)(enum scroll_dir, uint32_t, uint32_t, uint32_t, uint32_t);
typedef void (*screen_gesture_down_func_t)(void);

typedef struct
{
    screen_draw_func_t draw;
    screen_gesture_down_func_t gesture_down;
} screen_func_table_entry_t;

static int curr_screen = SCR_HOME;
K_MUTEX_DEFINE(mutex_curr_screen);

/* The carousel is the one screen whose draw takes only (scroll_dir): the tile is
 * chosen from carousel_cur_tile, not from args. Every other entry is 5-arg, so it
 * was stored uncast and called through the 5-arg pointer type -- formally UB, and
 * it forced -Wno-error=incompatible-pointer-types over the whole library, which
 * suppressed the same real diagnostic everywhere else. This shim adapts the arity
 * instead, so the table is type-correct and the flag is gone. */
static void draw_scr_carousel_entry(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2,
                                    uint32_t arg3, uint32_t arg4)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    ARG_UNUSED(arg4);

    draw_scr_carousel(m_scroll_dir);
}

// Array of function pointers for screen drawing functions
static const screen_func_table_entry_t screen_func_table[] = {
    [SCR_HOME] = {draw_scr_carousel_entry, NULL},   /* P6: home is now the tileview carousel (tile 0) */
    /* P6: metric overviews are carousel tiles; route their table entries to it. */
    [SCR_HR] = {draw_scr_carousel_entry, NULL},
    [SCR_SPO2] = {draw_scr_carousel_entry, NULL},
    [SCR_ECG] = {draw_scr_carousel_entry, NULL},
    [SCR_TEMP] = {draw_scr_carousel_entry, NULL},
    [SCR_BPT] = {draw_scr_carousel_entry, NULL},
    [SCR_GSR] = {draw_scr_carousel_entry, NULL},
    [SCR_HRV] = {draw_scr_carousel_entry, NULL},
    [SCR_SPL_FI_SENS_WEAR] = {draw_scr_fi_sens_wear, gesture_down_scr_fi_sens_wear},
    [SCR_SPL_FI_SENS_CHECK] = {draw_scr_fi_sens_check, gesture_down_scr_fi_sens_check},
    [SCR_SPL_BPT_MEASURE] = {draw_scr_bpt_measure, gesture_down_scr_bpt_measure},
    [SCR_SPL_BPT_CAL_COMPLETE] = {draw_scr_bpt_cal_complete, gesture_down_scr_bpt_cal_complete},
    [SCR_SPL_HRV_EVAL_PROGRESS] = {draw_scr_spl_hrv_eval_progress, gesture_down_scr_spl_hrv_eval_progress},
    [SCR_SPL_HRV_COMPLETE] = {draw_scr_spl_hrv_complete, gesture_down_scr_spl_hrv_complete},
    [SCR_SPL_SPO2_MEASURE] = {draw_scr_spo2_measure, gesture_down_scr_spo2_measure},
    [SCR_SPL_SPO2_RESULT] = {draw_scr_spo2_result, gesture_down_scr_spo2_result},   /* P6 pilot */
    [SCR_SPL_GSR_COMPLETE] = {draw_scr_gsr_complete, gesture_down_scr_gsr_complete},
    [SCR_SPL_LOW_BATTERY] = {draw_scr_spl_low_battery, gesture_down_scr_spl_low_battery},
    [SCR_SPL_SPO2_BPT_TIMEOUT] = {draw_scr_timeout, gesture_down_scr_timeout},
    [SCR_SPL_BPT_CAL_PROGRESS] = {draw_scr_bpt_cal_progress, gesture_down_scr_bpt_cal_progress},
    [SCR_SPL_BPT_FAILED] = {draw_scr_bpt_cal_failed, gesture_down_scr_bpt_cal_failed},
    [SCR_SPL_BPT_EST_COMPLETE] = {draw_scr_bpt_est_complete, gesture_down_scr_bpt_est_complete},
    [SCR_SPL_BPT_CAL_REQUIRED] = {draw_scr_bpt_cal_required, gesture_down_scr_bpt_cal_required},

    [SCR_SPL_BLE] = {draw_scr_ble, gesture_down_scr_ble},   /* P1-3 */
    [SCR_SPL_PULLDOWN] = {draw_scr_pulldown, gesture_down_scr_pulldown},
    /* Old device-user-settings menu + redundant selects removed (fields moved to
     * the v2 settings screen). Height/Weight keep their roller pickers. */
    [SCR_SPL_HEIGHT_SELECT] = {draw_scr_height_select, gesture_down_scr_height_select},
    [SCR_SPL_WEIGHT_SELECT] = {draw_scr_weight_select, gesture_down_scr_weight_select},
    [SCR_SPL_SETTINGS] = {draw_scr_settings, gesture_down_scr_settings},         /* P6 v2 */

};

// Screen state persistence for sleep/wake cycles
static struct
{
    int saved_screen;
    enum scroll_dir saved_scroll_dir;
    uint32_t saved_arg1;
    uint32_t saved_arg2;
    uint32_t saved_arg3;
    uint32_t saved_arg4;
    bool state_saved;
} screen_sleep_state = {
    .saved_screen = SCR_HOME,
    .saved_scroll_dir = SCROLL_NONE,
    .saved_arg1 = 0,
    .saved_arg2 = 0,
    .saved_arg3 = 0,
    .saved_arg4 = 0,
    .state_saved = false};

K_MUTEX_DEFINE(mutex_screen_sleep_state);

// Function declarations for screen state management
static void hpi_disp_save_screen_state(void);
static void hpi_disp_restore_screen_state(void);
static void hpi_disp_clear_saved_state(void);

void hpi_disp_set_curr_screen(int screen)
{
    k_mutex_lock(&mutex_curr_screen, K_FOREVER);
    curr_screen = screen;
    k_mutex_unlock(&mutex_curr_screen);
}

int hpi_disp_get_curr_screen(void)
{
    k_mutex_lock(&mutex_curr_screen, K_FOREVER);
    int screen = curr_screen;
    k_mutex_unlock(&mutex_curr_screen);
    return screen;
}

int hpi_disp_reset_all_last_updated(void)
{
    m_disp_hr = 0;
    m_disp_spo2 = 0;
    m_disp_steps = 0;
    m_disp_active_time_s = 0;
    m_disp_temp = 0;
    m_disp_bp_sys = 0;
    m_disp_bp_dia = 0;
    m_disp_ecg_hr = 0;
    m_disp_ecg_hr_ts = 0;
    m_disp_ecg_hr_uptime_ms = 0;
    m_disp_ecg_timer = 0;
    m_lead_on_off = false;  // Reset to "leads ON" state for fresh measurement start

    m_disp_hr_updated_ts = 0;
    m_disp_spo2_last_refresh_ts = 0;
    m_disp_spo2_last_uptime_ms = 0;
    m_disp_temp_updated_ts = 0;
    m_disp_bp_last_refresh = 0;
    m_disp_bpt_status = 0;
    m_disp_bpt_progress = 0;

    return 0;
}

/**
 * @brief Save the current screen state before entering sleep mode
 *
 * This function captures the current screen, scroll direction, and arguments
 * so they can be restored when waking from sleep.
 */
static void hpi_disp_save_screen_state(void)
{
    k_mutex_lock(&mutex_screen_sleep_state, K_FOREVER);

    /* The carousel is a single LVGL screen that pins curr_screen to SCR_HOME, so
     * ask it which tile is actually showing - otherwise sleeping on any metric
     * tile wakes back onto home. */
    int cur = hpi_disp_get_curr_screen();
    /* Never persist the transient low-battery warning as the restore target - on
     * wake it is re-derived from the battery state (reconcile). Save home so a
     * recovered wake lands somewhere sane. */
    if (cur == SCR_SPL_LOW_BATTERY)
    {
        cur = SCR_HOME;
    }
    screen_sleep_state.saved_screen = (cur == SCR_HOME) ? hpi_carousel_curr_screen() : cur;
    screen_sleep_state.saved_scroll_dir = g_scroll_dir;
    screen_sleep_state.saved_arg1 = g_arg1;
    screen_sleep_state.saved_arg2 = g_arg2;
    screen_sleep_state.saved_arg3 = g_arg3;
    screen_sleep_state.saved_arg4 = g_arg4;
    screen_sleep_state.state_saved = true;

    k_mutex_unlock(&mutex_screen_sleep_state);

    LOG_DBG("Screen state saved: screen=%d, scroll_dir=%d",
            screen_sleep_state.saved_screen, screen_sleep_state.saved_scroll_dir);
}

/**
 * @brief Restore the screen state after waking from sleep mode
 *
 * This function restores the previously saved screen state, including
 * the screen ID, scroll direction, and arguments.
 */
static void hpi_disp_restore_screen_state(void)
{
    k_mutex_lock(&mutex_screen_sleep_state, K_FOREVER);

    if (screen_sleep_state.state_saved)
    {
        // Use the saved state to restore the screen
        int saved_screen = screen_sleep_state.saved_screen;
        enum scroll_dir saved_scroll = screen_sleep_state.saved_scroll_dir;
        uint32_t saved_arg1 = screen_sleep_state.saved_arg1;
        uint32_t saved_arg2 = screen_sleep_state.saved_arg2;
        uint32_t saved_arg3 = screen_sleep_state.saved_arg3;
        uint32_t saved_arg4 = screen_sleep_state.saved_arg4;

        k_mutex_unlock(&mutex_screen_sleep_state);

        LOG_DBG("Restoring screen state: screen=%d, scroll_dir=%d",
                saved_screen, saved_scroll);

        // Check if this is a special screen (SCR_SPL_*) or a regular screen
        // Regular screens: SCR_LIST_START < screen < SCR_LIST_END (e.g., SCR_HOME, SCR_HR, …)
        // Special screens: SCR_SPL_LIST_START <= screen (e.g., SCR_SPL_BOOT, SCR_SPL_PULLDOWN, etc.)
        if (saved_screen >= SCR_SPL_LIST_START && 
            saved_screen < ARRAY_SIZE(screen_func_table) &&
            screen_func_table[saved_screen].draw != NULL)
        {
            // Use the special screen loading function for complex screens with arguments
            hpi_load_scr_spl(saved_screen, saved_scroll, saved_arg1, saved_arg2, saved_arg3, saved_arg4);
        }
        else if (saved_screen > SCR_LIST_START && saved_screen < SCR_LIST_END)
        {
            // Regular screen - use standard loading function
            hpi_load_screen(saved_screen, saved_scroll);
        }
        else
        {
            // Invalid screen ID - fall back to home screen
            LOG_WRN("Invalid saved screen %d, loading home screen", saved_screen);
            hpi_load_screen(SCR_HOME, SCROLL_NONE);
        }
    }
    else
    {
        k_mutex_unlock(&mutex_screen_sleep_state);
        LOG_DBG("No saved screen state, loading current screen");
        // No saved state, just reload the current screen
        hpi_load_screen(hpi_disp_get_curr_screen(), SCROLL_NONE);
    }
}

/**
 * @brief Clear the saved screen state
 *
 * This function clears the saved screen state, typically called
 * after successfully restoring the state or when resetting.
 */
static void hpi_disp_clear_saved_state(void)
{
    k_mutex_lock(&mutex_screen_sleep_state, K_FOREVER);

    screen_sleep_state.state_saved = false;
    screen_sleep_state.saved_screen = SCR_HOME;
    screen_sleep_state.saved_scroll_dir = SCROLL_NONE;
    screen_sleep_state.saved_arg1 = 0;
    screen_sleep_state.saved_arg2 = 0;
    screen_sleep_state.saved_arg3 = 0;
    screen_sleep_state.saved_arg4 = 0;

    k_mutex_unlock(&mutex_screen_sleep_state);

    LOG_DBG("Saved screen state cleared");
}

// Toast notification support
static lv_obj_t *toast_obj = NULL;
static lv_timer_t *toast_timer = NULL;

/**
 * @brief Timer callback to hide and delete the toast notification
 */
static void toast_hide_timer_cb(lv_timer_t *timer)
{
    if (toast_obj != NULL && lv_obj_is_valid(toast_obj))
    {
        lv_obj_del(toast_obj);
        toast_obj = NULL;
    }
    if (toast_timer != NULL)
    {
        lv_timer_del(toast_timer);
        toast_timer = NULL;
    }
}

/**
 * @brief Display a toast notification message
 *
 * Creates a small overlay message at the center of the screen that
 * auto-dismisses after the specified duration. The toast appears on top
 * of the current screen and doesn't block user interaction.
 *
 * @param message The message text to display
 * @param duration_ms How long to show the toast (in milliseconds)
 */
void hpi_disp_show_toast(const char *message, uint32_t duration_ms)
{
    // Clean up any existing toast
    if (toast_obj != NULL && lv_obj_is_valid(toast_obj))
    {
        lv_obj_del(toast_obj);
        toast_obj = NULL;
    }
    if (toast_timer != NULL)
    {
        lv_timer_del(toast_timer);
        toast_timer = NULL;
    }

    // Create toast container on the active screen
    lv_obj_t *active_scr = lv_scr_act();
    if (active_scr == NULL)
    {
        LOG_WRN("No active screen for toast");
        return;
    }

    toast_obj = lv_obj_create(active_scr);
    lv_obj_set_size(toast_obj, 320, 70);
    lv_obj_align(toast_obj, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(toast_obj, lv_color_make(40, 40, 40), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(toast_obj, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_radius(toast_obj, 15, LV_PART_MAIN);
    lv_obj_set_style_border_width(toast_obj, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(toast_obj, 20, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(toast_obj, LV_OPA_30, LV_PART_MAIN);
    lv_obj_clear_flag(toast_obj, LV_OBJ_FLAG_SCROLLABLE);

    // Create message label
    lv_obj_t *label = lv_label_create(toast_obj);
    lv_label_set_text(label, message);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, 300);
    lv_obj_center(label);

    // Create timer to auto-hide the toast
    toast_timer = lv_timer_create(toast_hide_timer_cb, duration_ms, NULL);
    lv_timer_set_repeat_count(toast_timer, 1);

    LOG_INF("Toast displayed: %s (duration: %d ms)", message, duration_ms);
}

void disp_screen_event(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    // lv_obj_t *target = lv_event_get_target(e);

    /* P6: on the tileview carousel (curr_screen == SCR_HOME), a L/R swipe steps
     * one tile INSTANTLY (native scroll+snap is disabled - too slow/jagged on
     * this SPI panel). Swipe left = next tile, right = previous. Vertical (down)
     * still falls through to open the pulldown. */
    if (event_code == LV_EVENT_GESTURE && curr_screen == SCR_HOME) {
        lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (d == LV_DIR_LEFT || d == LV_DIR_RIGHT) {
            lv_indev_wait_release(lv_indev_get_act());
            hpi_carousel_step(d == LV_DIR_LEFT ? +1 : -1);
            return;
        }
    }

    /* P0-4: L/R only steps the carousel, and only on SCR_HOME (handled above).
     * On every other screen L/R is inert. The old handlers below walked screens
     * by raw enum arithmetic (`hpi_load_screen(curr_screen ± 1)`), which on a
     * special screen (id >= 50) jumped into unrelated SCR_SPL_* screens, and on
     * a metric tile fought the tileview. To leave a metric/special screen, use
     * swipe-down (its registered gesture_down handler, below). The one real back
     * gesture that lived here - SCR_SPL_SPO2_MEASURE right-swipe -> SCR_SPO2 - is
     * already covered by that screen's swipe-down (gesture_down_scr_spo2_measure). */
    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_BOTTOM)
    {
        lv_indev_wait_release(lv_indev_get_act());
        printk("Down at %d\n", curr_screen);

        int screen = hpi_disp_get_curr_screen();

        if (screen == SCR_HOME)
        {
            /* leaving the carousel to the shade also cancels an in-progress ECG */
            hpi_ecg_monitor_leave();
            hpi_load_scr_spl(SCR_SPL_PULLDOWN, SCROLL_DOWN, SCR_HOME, 0, 0, 0);
            return;
        }

        if (screen >= 0 && screen < ARRAY_SIZE(screen_func_table) && screen_func_table[screen].gesture_down)
        {
            screen_func_table[screen].gesture_down();
        }
        else
        {
            // Default handler or nothing
        }
    }
    else if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_TOP)
    {
        lv_indev_wait_release(lv_indev_get_act());
        printk("Up at %d\n", curr_screen);

        if (curr_screen == SCR_SPL_PULLDOWN)
        {
            hpi_load_screen(SCR_HOME, SCROLL_UP);
        }
    }
}

typedef void (*screen_static_draw_func_t)(enum scroll_dir, uint32_t, uint32_t, uint32_t, uint32_t);

static int max32664_update_progress = 0;
static int max32664_update_status = MAX32664_UPDATER_STATUS_IDLE;

// Externs
extern const struct device *display_dev;
extern const struct device *touch_dev;
extern lv_obj_t *scr_bpt;

extern struct k_sem sem_disp_smf_start;

extern struct k_sem sem_disp_boot_complete;
extern struct k_sem sem_boot_update_req;

extern struct k_msgq q_ecg_sample;
extern struct k_msgq q_ppg_wrist_sample;
extern struct k_msgq q_plot_ecg;
extern struct k_msgq q_plot_ppg_wrist;
extern struct k_msgq q_plot_hrv;
extern struct k_msgq q_plot_gsr;

/* R2 new-design waveforms (carousel monitors), fed real samples here. The SpO2
 * tile has no waveform: its spot check runs on SCR_SPL_SPO2_MEASURE, which owns
 * the only PPG plot in that flow and is fed via hpi_disp_spo2_plot_*. */
extern lv_obj_t *g_hr_wave, *g_ecg_wave, *g_gsr_wave;
extern bool g_ecg_active, g_gsr_active;   /* spot checks: gated on Start/Stop */
void hpi_wave_monitor_push_eda(lv_obj_t *wm, int32_t raw);
void hpi_wave_monitor_push_auto(lv_obj_t *wm, int32_t raw);
void hpi_wave_monitor_push_ecg(lv_obj_t *wm, int32_t raw);

extern struct k_sem sem_crown_key_pressed;


extern struct k_sem sem_spo2_complete;

// Note: sem_bpt_sensor_found was removed from smf_ppg_finger.c - extern removed




static void st_display_init_entry(void *o)
{
    LOG_DBG("Display SM Init Entry");

    // LOG_DBG("Disp ON");

    if (!device_is_ready(display_dev))
    {
        LOG_ERR("Device not ready");
        // return;
    }

    sh8601_reinit(display_dev);
    k_msleep(500);

    device_init(touch_dev);
    k_msleep(50);

    // Init all styles globally
    display_init_styles();

    display_blanking_off(display_dev);

    uint8_t brightness = hpi_disp_get_brightness();
    hpi_disp_set_brightness(brightness);

    smf_set_state(SMF_CTX(&s_disp_obj), &display_states[HPI_DISPLAY_STATE_SPLASH]);
}

static void st_display_splash_entry(void *o)
{
    LOG_DBG("Display SM Splash Entry");
    draw_scr_splash();
    splash_scr_start_time = k_uptime_get_32();
}

static enum smf_state_result st_display_splash_run(void *o)
{
    // Stay in this state for 2 seconds
    if ((k_uptime_get_32() - splash_scr_start_time) > 2000)
    {
        smf_set_state(SMF_CTX(&s_disp_obj), &display_states[HPI_DISPLAY_STATE_BOOT]);
    }
    return SMF_EVENT_HANDLED;
}

static void st_display_boot_entry(void *o)
{
    LOG_DBG("Display SM Boot Entry");
    draw_scr_boot();

    // Signal that the display is ready
    k_sem_give(&sem_disp_ready);
}

static enum smf_state_result st_display_boot_run(void *o)
{
    struct s_disp_object *s = (struct s_disp_object *)o;

    struct hpi_boot_msg_t boot_msg;

    if (k_msgq_get(&q_disp_boot_msg, &boot_msg, K_NO_WAIT) == 0)
    {
        if (boot_msg.status == false)
        {
            hpi_boot_all_passed = false;
        }
        scr_boot_add_status(boot_msg.msg, boot_msg.status, boot_msg.show_status);
    }

    // Stay in this state until the boot is complete
    if (k_sem_take(&sem_disp_boot_complete, K_NO_WAIT) == 0)
    {
        k_msleep(2000);
        if (hpi_boot_all_passed)
        {
            smf_set_state(SMF_CTX(&s_disp_obj), &display_states[HPI_DISPLAY_STATE_ACTIVE]);
        }
        else
        {
            smf_set_state(SMF_CTX(&s_disp_obj), &display_states[HPI_DISPLAY_STATE_ACTIVE]);
            // smf_set_state(SMF_CTX(&s_disp_obj), &display_states[HPI_DISPLAY_STATE_SLEEP]);
        }
    }

    if (k_sem_take(&sem_boot_update_req, K_NO_WAIT) == 0)
    {
        // Get the current device type to show the correct title
        enum max32664_updater_device_type device_type = max32664_get_current_update_device_type();
        const char *msg;

        if (device_type == MAX32664_UPDATER_DEV_TYPE_MAX32664C)
        {
            msg = "MAX32664C \n FW Update Required";
        }
        else
        {
            msg = "MAX32664D \n FW Update Required";
        }

        // Copy the appropriate message
        strcpy(s->title, msg);
        smf_set_state(SMF_CTX(&s_disp_obj), &display_states[HPI_DISPLAY_STATE_SCR_PROGRESS]);
    }

    // LOG_DBG("Display SM Boot Run");
    return SMF_EVENT_HANDLED;
}

static void st_display_boot_exit(void *o)
{
    LOG_DBG("Display SM Boot Exit");
    lv_disp_trig_activity(NULL);
}

static void hpi_max32664_update_progress(int progress, int status)
{
    LOG_DBG("MAX32664 Update Progress: %d%%, Status: %d", progress, status);
    max32664_update_progress = progress;
    max32664_update_status = status;
}

static void st_display_progress_entry(void *o)
{
    struct s_disp_object *s = (struct s_disp_object *)o;

    LOG_DBG("Display SM Progress Entry");
    draw_scr_progress(s->title, "Please wait...");

    // Reset progress screen to normal state
    hpi_disp_scr_reset_progress();

    max32664_set_progress_callback(hpi_max32664_update_progress);
    max32664_update_progress = 0;
    max32664_update_status = MAX32664_UPDATER_STATUS_IDLE;
    hpi_disp_scr_update_progress(max32664_update_progress, "Starting...");
}

static enum smf_state_result st_display_progress_run(void *o)
{
    if (max32664_update_status == MAX32664_UPDATER_STATUS_IN_PROGRESS)
    {
        // Provide detailed status messages based on progress
        const char *status_msg = "Updating...";
        if (max32664_update_progress <= 5)
        {
            status_msg = "Checking filesystem...";
        }
        else if (max32664_update_progress <= 10)
        {
            status_msg = "Entering bootloader...";
        }
        else if (max32664_update_progress <= 15)
        {
            status_msg = "Loading firmware file...";
        }
        else if (max32664_update_progress <= 25)
        {
            status_msg = "Setting up bootloader...";
        }
        else if (max32664_update_progress <= 35)
        {
            status_msg = "Erasing flash...";
        }
        else if (max32664_update_progress < 95)
        {
            status_msg = "Writing firmware...";
        }
        else
        {
            status_msg = "Finalizing update...";
        }

        hpi_disp_scr_update_progress(max32664_update_progress, status_msg);
    }
    else if (max32664_update_status == MAX32664_UPDATER_STATUS_SUCCESS)
    {
        hpi_disp_scr_update_progress(100, "Update Complete!");
        k_msleep(2000);
        smf_set_state(SMF_CTX(&s_disp_obj), &display_states[HPI_DISPLAY_STATE_BOOT]);
    }
    else if (max32664_update_status == MAX32664_UPDATER_STATUS_FILE_NOT_FOUND)
    {
        // Provide specific error message based on when the error occurred
        const char *error_msg = "Firmware File Not Found!";
        if (max32664_update_progress <= 5)
        {
            error_msg = "No Firmware Files in LFS!";
        }
        hpi_disp_scr_update_progress(max32664_update_progress, error_msg);
        // Also show the error display for better visual feedback
        hpi_disp_scr_show_error(error_msg);
        LOG_ERR("MAX32664 firmware file missing from LFS filesystem");
        k_msleep(3000);
        smf_set_state(SMF_CTX(&s_disp_obj), &display_states[HPI_DISPLAY_STATE_ACTIVE]);
    }
    else if (max32664_update_status == MAX32664_UPDATER_STATUS_FAILED)
    {
        const char *error_msg = "Update Failed!";
        hpi_disp_scr_update_progress(max32664_update_progress, error_msg);
        // Also show the error display for better visual feedback
        hpi_disp_scr_show_error(error_msg);
        LOG_ERR("MAX32664 firmware update failed");
        k_msleep(2000);
        smf_set_state(SMF_CTX(&s_disp_obj), &display_states[HPI_DISPLAY_STATE_ACTIVE]);
    }
    return SMF_EVENT_HANDLED;
}

static void st_display_progress_exit(void *o)
{
    LOG_DBG("Display SM Progress Exit");
    // Clear the progress callback when exiting the progress state
    max32664_set_progress_callback(NULL);
    lv_disp_trig_activity(NULL);
}

static void hpi_disp_process_ppg_fi_data(struct hpi_ppg_fi_data_t ppg_sensor_sample)
{
    if (hpi_disp_get_curr_screen() == SCR_SPL_BPT_MEASURE)
    {
        hpi_disp_bpt_draw_plotPPG(ppg_sensor_sample);

        if (k_uptime_get_32() - m_disp_bp_last_refresh > 1000)
        {
            m_disp_bp_last_refresh = k_uptime_get_32();
            hpi_disp_bpt_update_progress(ppg_sensor_sample.bpt_progress);
        }

        lv_disp_trig_activity(NULL);
    }
    else if (hpi_disp_get_curr_screen() == SCR_SPL_BPT_CAL_PROGRESS)
    {
        /* Cal screen shares the measure chrome: live PPG trace + ring/HR pill,
         * plus POINT n/3 from the finger SMF's current cal index. */
        hpi_disp_bpt_cal_draw_plotPPG(ppg_sensor_sample);

        if (k_uptime_get_32() - m_disp_bp_last_refresh > 1000)
        {
            m_disp_bp_last_refresh = k_uptime_get_32();
            uint8_t st = 0, prog = 0, idx = 0;
            bool run = false;
            hpi_bpt_cal_status(&st, &prog, &idx, &run);
            hpi_disp_bpt_cal_update_progress(idx, ppg_sensor_sample.bpt_progress);
            LOG_INF("BPT Cal Progress: point %d, %d%%", idx, ppg_sensor_sample.bpt_progress);
        }
        lv_disp_trig_activity(NULL);
    }
    else if (hpi_disp_get_curr_screen() == SCR_SPL_SPO2_MEASURE)
    {
     
        hpi_disp_spo2_plot_fi_ppg(ppg_sensor_sample);
        hpi_disp_spo2_update_progress(ppg_sensor_sample.spo2_valid_percent_complete, ppg_sensor_sample.spo2_state, ppg_sensor_sample.spo2, ppg_sensor_sample.hr, ppg_sensor_sample.spo2_confidence);
        lv_disp_trig_activity(NULL);
    }
}

static void hpi_disp_process_ppg_wr_data(struct hpi_ppg_wr_data_t ppg_sensor_sample)
{
    /* Feed the HR tile's monitor (auto-scaled raw wrist PPG). It is gated: P3
     * put the HR waveform behind the tile's LIVE toggle - pushing samples into a
     * hidden canvas would burn the autoscaler + an invalidate per sample for
     * nothing. */
    bool hr_live = hpi_hr_wave_is_live();
    if (g_hr_wave && hr_live) {
        for (int i = 0; i < ppg_sensor_sample.ppg_num_samples; i++) {
            hpi_wave_monitor_push_auto(g_hr_wave, (int32_t)ppg_sensor_sample.raw_green[i]);
        }
    }

    if (hpi_disp_get_curr_screen() == SCR_SPL_SPO2_MEASURE )
    {
        lv_disp_trig_activity(NULL);
        hpi_disp_spo2_plot_wrist_ppg(ppg_sensor_sample);
        hpi_disp_spo2_update_progress(ppg_sensor_sample.spo2_valid_percent_complete, ppg_sensor_sample.spo2_state, ppg_sensor_sample.spo2, ppg_sensor_sample.hr, ppg_sensor_sample.spo2_confidence);
    }
}

static void hpi_disp_process_ecg_data(struct hpi_ecg_bioz_sensor_data_t ecg_sensor_sample)
{
    /* Feed the new-design ECG monitor while its spot check is running. */
    if (g_ecg_wave && g_ecg_active) {
        for (int i = 0; i < ecg_sensor_sample.ecg_num_samples; i++) {
            hpi_wave_monitor_push_ecg(g_ecg_wave, ecg_sensor_sample.ecg_samples[i]);
        }
    }

    if (hpi_disp_get_curr_screen() == SCR_SPL_HRV_EVAL_PROGRESS)
    {
        hpi_ecg_disp_draw_plotECG_hrv(ecg_sensor_sample.ecg_samples, ecg_sensor_sample.ecg_num_samples, ecg_sensor_sample.ecg_lead_off);
    }
    /*else if (hpi_disp_get_curr_screen() == SCR_PLOT_EDA)
    {
        hpi_eda_disp_draw_plotEDA(ecg_bioz_sensor_sample.bioz_sample, ecg_bioz_sensor_sample.bioz_num_samples, ecg_bioz_sensor_sample.bioz_lead_off);
    }*/
}
float hpi_data_get_last_converted_us(void)
{
    return m_disp_gsr_us;
}
static inline int32_t bioz_extract_24bit(int32_t raw)
{
    raw >>= 8;              // remove unused bits
    if (raw & 0x00800000)   // sign bit (bit 23)
        raw |= 0xFF000000; // sign extend to 32-bit
    return raw;
}

/**
 * @brief Convert driver-provided BioZ sample to conductance in µS
 *
 * The MAX30001 driver now performs the ADC-to-conductance conversion internally
 * using the datasheet formula and outputs values as fixed-point (µS × 100).
 * This function simply converts from fixed-point to float.
 *
 * Driver conversion (in max30001.h):
 *   Z (Ω) = ADC × VREF / (2^19 × CGMAG × GAIN)
 *   Conductance (µS) = 1/Z × 10^6
 */
static float convert_raw_sample_to_uS(int32_t raw)
{
    /* Driver outputs conductance as fixed-point: µS × 100
     * Divide by 100 to get actual µS value */
    return (float)raw / 100.0f;
}

static void hpi_disp_process_gsr_data(struct hpi_gsr_sensor_data_t gsr_sensor_sample)
{
    /* Feed the inline EDA monitor while its spot check is running. Ungated by
     * screen, exactly like the ECG trace: the tile owns the plot now, so gating
     * on the old plot screen would starve it. Uses the EDA scaler - push_auto's
     * envelope is tuned for pulsatile PPG and collapses on a slow tonic level. */
    if (g_gsr_wave && g_gsr_active) {
        for (int i = 0; i < gsr_sensor_sample.bioz_num_samples; i++) {
            hpi_wave_monitor_push_eda(g_gsr_wave, gsr_sensor_sample.bioz_samples[i]);
        }
    }

    if (gsr_sensor_sample.bioz_num_samples > 0)
    {
        /* Use latest sample (same as ECG uses latest RR) */
        int32_t raw = gsr_sensor_sample.bioz_samples[gsr_sensor_sample.bioz_num_samples - 1];

        m_disp_gsr_us = convert_raw_sample_to_uS(raw);
    }
}

static void st_display_active_entry(void *o)
{
    LOG_DBG("Display SM Active Entry");

    int scr = hpi_disp_get_curr_screen();

    /* ACTIVE means the SMF is handing control back to the user, so whatever is on
     * the panel has to be something they can LEAVE. Force Home if it isn't.
     *
     * This was a real dead end, not a hypothetical: the MAX32664 hub-update
     * failure paths (FILE_NOT_FOUND / FAILED, above) dwell on the error then
     * transition straight to ACTIVE with curr_screen still SCR_SPL_PROGRESS. That
     * screen is drawn *directly* by st_display_progress_entry(), is deliberately
     * absent from screen_func_table (so its gesture_down is a NULL hole), and
     * scr_progress.c has no button -- so "Update Failed" was unescapable until the
     * sleep timeout. FILE_NOT_FOUND fires whenever the hub firmware is missing
     * from LittleFS, i.e. on a fresh or erased external flash.
     *
     * Testing gesture_down rather than special-casing SCR_SPL_PROGRESS also hardens
     * every future table-less screen. SCR_HOME is exempt: the carousel pins
     * curr_screen to it and its gestures are handled specially. SCR_SPL_BOOT has no
     * table entry either, so it still lands here exactly as before.
     * The error dwell (k_msleep) already ran, so the user has seen the message. */
    bool navigable = (scr == SCR_HOME) ||
                     /* DFU modal is intentionally unescapable while an OTA runs;
                      * hpi_disp_dfu_tick() returns to Home once it ends. */
                     (scr == SCR_SPL_DFU && hpi_dfu_get_state() != HPI_DFU_IDLE) ||
                     (scr >= 0 && scr < (int)ARRAY_SIZE(screen_func_table) &&
                      screen_func_table[scr].gesture_down != NULL);

    if (!navigable)
    {
        LOG_DBG("Active entry on a screen with no way out (%d) - loading home", scr);
        hpi_load_screen(HPI_DEFAULT_START_SCREEN, SCROLL_NONE);
    }
}

static void hpi_disp_update_screens(void)
{
    // CRITICAL: Do not update ANY screen if a transition is in progress
    // This prevents race conditions during screen creation/deletion
    if (screen_transition_in_progress) {
        return;
    }
    
    switch (hpi_disp_get_curr_screen())
    {
    /* SCR_HOME/HR/SpO2/ECG/Temp/BPT/HRV/GSR overview refresh removed in P6:
     * those are now carousel tiles bound to lv_subjects (updated in
     * hpi_disp_push_subjects), so no per-screen periodic refresh is needed. */
    /* SCR_SPL_HR_SCR2 / SCR_SPO2 periodic trend-refresh cases removed with the
     * legacy trend store (bodies were already no-ops). */
    case  SCR_SPL_BPT_MEASURE:
        if(hpi_evt_consume(&fi_evt, EVT_FI_CONTACT_TIMEOUT))
        {
            LOG_INF("DISPLAY THREAD: Finger contact timeout - returning to BPT home screen");
            hpi_load_screen(SCR_BPT, SCROLL_DOWN);
            hpi_disp_show_toast("Measurement cancelled\nNo finger detected", 3000);
        }
        break;
    case SCR_SPL_BPT_CAL_PROGRESS:
         if(hpi_evt_consume(&fi_evt, EVT_FI_BPT_CAL_CANCEL))
         {
            hpi_bpt_abort();
            hpi_load_screen(SCR_BPT, SCROLL_NONE);
            return;
         }
        lv_disp_trig_activity(NULL);
        break;
    case SCR_SPL_SPO2_MEASURE:

         if(hpi_evt_consume(&fi_evt, EVT_FI_CONTACT_TIMEOUT))
         {
            LOG_INF("DISPLAY THREAD: Finger contact timeout - returning to SpO2 home screen");
            hpi_load_screen(SCR_SPO2, SCROLL_DOWN);
            hpi_disp_show_toast("Measurement cancelled\nNo finger detected", 3000);
         }        
        lv_disp_trig_activity(NULL);
        break;
    case SCR_SPL_HRV_EVAL_PROGRESS:
        hpi_hrv_disp_update_timer(m_disp_ecg_timer);

        // Check for lead placement timeout - return to HRV home screen
        // (signaled by ECG SMF when user doesn't place leads within timeout)
        if (hpi_evt_consume(&ecg_evt, EVT_ECG_LEAD_TIMEOUT))
        {
            LOG_INF("DISPLAY THREAD: Lead placement timeout - returning to HRV home screen");
            unload_scr_hrv_eval_progress();
            hpi_load_screen(SCR_HRV, SCROLL_DOWN);
            hpi_disp_show_toast("Measurement cancelled\nNo leads detected", 3000);
            break;
        }

        // Check for HRV evaluation complete - show completion screen
        if (hpi_evt_consume(&ecg_evt, EVT_HRV_COMPLETE))
        {
            LOG_INF("DISPLAY THREAD: HRV evaluation complete");
            hpi_load_scr_spl(SCR_SPL_HRV_COMPLETE, SCROLL_UP, 0, 0, 0, 0);
            break;
        }

        // Handle lead ON/OFF UI updates (signaled by ECG SMF)
        // Note: State transitions are handled by the SMF, display just updates UI
        if (hpi_evt_consume(&ecg_evt, EVT_ECG_LEAD_ON))
        {
            LOG_INF("DISPLAY THREAD: HRV Lead ON - updating UI");
            scr_hrv_lead_on_off_handler(false); // false = leads ON
            m_lead_on_off = false;
        }

        if (hpi_evt_consume(&ecg_evt, EVT_ECG_LEAD_OFF))
        {
            LOG_INF("DISPLAY THREAD: HRV Lead OFF - updating UI");
            scr_hrv_lead_on_off_handler(true); // true = leads OFF
            m_lead_on_off = true;
        }

        lv_disp_trig_activity(NULL);
        break;

    /* SCR_SPL_ECG_SCR2 and SCR_SPL_RAW_PPG removed (legacy full-screen plots). */
    /*case SCR_SPL_FI_SENS_CHECK:
        if (k_sem_take(&sem_bpt_sensor_found, K_NO_WAIT) == 0)
        {
            LOG_DBG("Loading BPT SCR4");

        }
        if
        break;*/
    case SCR_SPL_PULLDOWN:
        if (k_uptime_get_32() - last_settings_refresh > HPI_DISP_SETTINGS_REFRESH_INT)
        {
            // hpi_disp_settings_update_time_date(m_disp_sys_time);
            last_settings_refresh = k_uptime_get_32();
        }
        break;
    default:
        break;
    }
}

/* Callable from any thread (BLE, the sensor SMFs, LVGL callbacks). Deferred: the
 * draw happens later on the display thread. Accepts special screens *and*
 * carousel metric ids (SCR_BPT, SCR_HR, …) — the drain routes the latter through
 * hpi_carousel_show() so they land on the right tile (P0-3). NOT interchangeable
 * with hpi_load_screen(), which draws synchronously in the caller's context and
 * is therefore only safe from the display thread. */
void hpi_load_scr_spl(int m_screen, enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    LOG_DBG("Loading screen %d", m_screen);

    if (m_screen < 0 || m_screen >= ARRAY_SIZE(screen_func_table) ||
        screen_func_table[m_screen].draw == NULL)
    {
        LOG_ERR("Invalid screen: %d", m_screen);
        return;
    }

    struct hpi_scr_load_req_t req = {
        .screen = m_screen,
        .scroll_dir = m_scroll_dir,
        .arg1 = arg1,
        .arg2 = arg2,
        .arg3 = arg3,
        .arg4 = arg4,
    };

    /* Depth 8 against one consumer that drains every tick; a full queue means the
     * display thread is wedged, which is the watchdog's problem, not ours. Never
     * block -- this is called from the BLE thread and from SMF run functions. */
    if (k_msgq_put(&q_scr_load, &req, K_NO_WAIT) != 0)
    {
        LOG_ERR("Screen load queue full - dropped request for screen %d", m_screen);
    }
}

/* S3: the low-battery screen is owned by the display thread (LVGL is single-
 * threaded). battery_module latches the state on hw_thread; here - in display
 * context - we put the warning up or take it down on the edge. Returns the
 * current low state. Safe to call every active tick and from the sleep-exit
 * wake path. */
static bool hpi_disp_reconcile_low_battery(void)
{
    bool low = hw_is_low_battery();
    if (low == m_low_batt_shown)
    {
        return low;
    }
    m_low_batt_shown = low;

    if (low)
    {
        hpi_load_scr_spl(SCR_SPL_LOW_BATTERY, SCROLL_NONE, m_disp_batt_level,
                         m_disp_batt_charging, 0, 0);
    }
    else
    {
        /* Recovered (SoC rebounded, with or without a charger) - drop the
         * warning and return home. */
        hpi_disp_low_battery_cleanup();
        hpi_load_screen(SCR_HOME, SCROLL_NONE);
    }
    return low;
}

static enum smf_state_result st_display_active_run(void *o)
{
    struct hpi_ecg_bioz_sensor_data_t ecg_sensor_sample;
    struct hpi_gsr_sensor_data_t gsr_sensor_sample;
    struct hpi_ppg_wr_data_t ppg_sensor_sample;
    struct hpi_ppg_fi_data_t ppg_fi_sensor_sample;

    /* S3: show/dismiss the low-battery warning on the edge (display context). */
    hpi_disp_reconcile_low_battery();

    if (k_msgq_get(&q_plot_ppg_wrist, &ppg_sensor_sample, K_NO_WAIT) == 0)
    {
        hpi_disp_process_ppg_wr_data(ppg_sensor_sample);
    }

    // Process multiple ECG samples per cycle to prevent queue backups
    int ecg_processed_count = 0;
    while (k_msgq_get(&q_plot_ecg, &ecg_sensor_sample, K_NO_WAIT) == 0)
    {
        hpi_disp_process_ecg_data(ecg_sensor_sample);
        ecg_processed_count++;

        if (ecg_processed_count >= 8)
            break; // Prevent blocking other processing
    }

    // Process GSR queue data (allow multiple batches per cycle to keep up with producer)
    int gsr_processed_count = 0;
    while (k_msgq_get(&q_plot_gsr, &gsr_sensor_sample, K_NO_WAIT) == 0)
    {
        /* No lv_disp_trig_activity() here: arriving sensor data is not user
         * activity. The GSR sleep guard (st_display_active_run) keeps the screen
         * up for the capture instead. */
        hpi_disp_process_gsr_data(gsr_sensor_sample);
        gsr_processed_count++;

        if (gsr_processed_count >= 8)
            break; // Prevent blocking other processing
    }

    if (k_msgq_get(&q_plot_ppg_fi, &ppg_fi_sensor_sample, K_NO_WAIT) == 0)
    {
        hpi_disp_process_ppg_fi_data(ppg_fi_sensor_sample);
    }

    // Do screen specific updates
    hpi_disp_update_screens();

    // Update battery display only when data actually changes
    // This optimization reduces unnecessary UI updates since battery data only arrives every 5 seconds
    // while the display refresh runs every second
    if (k_uptime_get_32() - last_batt_refresh > HPI_DISP_BATT_REFR_INT)
    {
        // Only update UI if battery level or charging state has changed
        if (m_disp_batt_level != last_displayed_batt_level || 
            m_disp_batt_charging != last_displayed_batt_charging)
        {
            // Track if any screen was actually updated
            bool ui_updated = false;
            
            /* SCR_HOME battery is a subject-bound label now (subj_batt). */
            if (hpi_disp_get_curr_screen() == SCR_SPL_PULLDOWN)
            {
                hpi_disp_settings_update_batt_level(m_disp_batt_level, m_disp_batt_charging);
                ui_updated = true;
            }
            else if (hpi_disp_get_curr_screen() == SCR_SPL_LOW_BATTERY)
            {
                hpi_disp_low_battery_update(m_disp_batt_level, m_disp_batt_charging);
                ui_updated = true;
            }
            
            // Only update tracking variables if UI was actually updated
            // This ensures that when user returns to home screen, it will show updated value
            if (ui_updated)
            {
                last_displayed_batt_level = m_disp_batt_level;
                last_displayed_batt_charging = m_disp_batt_charging;
            }
        }
        last_batt_refresh = k_uptime_get_32();
    }

    /* Time is subject-bound now (subj_time / subj_date) - no SCR_HOME refresh. */


    // Add button handlers
    if (k_sem_take(&sem_crown_key_pressed, K_NO_WAIT) == 0)
    {
        lv_disp_trig_activity(NULL);
        if (hpi_disp_get_curr_screen() == SCR_HOME)
        {
            // hpi_display_sleep_on();
        }
        else if (hpi_disp_get_curr_screen() == SCR_SPL_SPO2_MEASURE)
        {
            gesture_down_scr_spo2_measure();
        }
        else if(hpi_disp_get_curr_screen() == SCR_SPL_BPT_MEASURE)
        {
            gesture_down_scr_bpt_measure();
        }
        else if(hpi_disp_get_curr_screen() == SCR_SPL_HRV_EVAL_PROGRESS)
        {
            gesture_down_scr_spl_hrv_eval_progress();
        }
        else
        {
            hpi_load_screen(SCR_HOME, SCROLL_NONE);
        }
    }

    /* Drain the screen-load inbox. Only the newest request is drawn -- rebuilding a
     * screen just to replace it in the same tick is wasted LVGL churn, and the older
     * request is superseded by definition. This matches what the old binary sem did
     * by accident; the difference is that each request is now internally consistent
     * and a superseded one is counted rather than vanishing. */
    struct hpi_scr_load_req_t req;
    uint32_t superseded = 0;
    bool have_req = false;

    while (k_msgq_get(&q_scr_load, &req, K_NO_WAIT) == 0)
    {
        if (have_req)
        {
            superseded++;
        }
        have_req = true;
    }

    if (have_req)
    {
        if (superseded > 0)
        {
            LOG_DBG("Change Screen: %u superseded request(s) skipped", superseded);
        }
        LOG_DBG("Change Screen: %d", req.screen);

        // CRITICAL: Set transition flag to block all screen updates
        screen_transition_in_progress = true;

        /* Publish to the display-thread-owned copy before drawing: the sleep-save
         * path reads these to restore the screen on wake. */
        g_screen = req.screen;
        g_scroll_dir = req.scroll_dir;
        g_arg1 = req.arg1;
        g_arg2 = req.arg2;
        g_arg3 = req.arg3;
        g_arg4 = req.arg4;

        /* Carousel metric ids (SCR_HOME / SCR_HR / SCR_BPT / …) must land on the
         * matching tile. Their table entries are draw_scr_carousel_entry(), which
         * takes only scroll_dir and redraws whatever carousel_cur_tile already
         * was — so a deferred hpi_load_scr_spl(SCR_BPT, …) from the finger SMF
         * (P0-3, smf_ppg_finger.c:686) would wake the wrong tile. Route those
         * through hpi_carousel_show() on this display thread instead: deferred
         * *and* correctly targeted. Specials still go through the table.
         * Do NOT call hpi_load_screen() from the finger SMF — that draws
         * synchronously in the caller's context (cross-thread LVGL). */
        if (g_screen > SCR_LIST_START && g_screen < SCR_LIST_END)
        {
            hpi_carousel_show(g_screen, g_scroll_dir);
        }
        else if (g_screen >= 0 && g_screen < (int)ARRAY_SIZE(screen_func_table) &&
                 screen_func_table[g_screen].draw)
        {
            screen_func_table[g_screen].draw(g_scroll_dir, g_arg1, g_arg2, g_arg3, g_arg4);
        }
        else
        {
            LOG_ERR("Change Screen: no draw for screen %d; staying put", g_screen);
        }

        // CRITICAL: Clear transition flag after screen is loaded
        screen_transition_in_progress = false;

        lv_disp_trig_activity(NULL);
    }

    int inactivity_time = lv_disp_get_inactive_time(NULL);
    // LOG_DBG("Inactivity Time: %d", inactivity_time);

    // Get current sleep timeout based on user settings
    uint32_t sleep_timeout_ms = get_sleep_timeout_ms();

    // Prevent sleep during active recording, an in-progress ECG measurement
    // (STREAMING = wait/stabilize/record - the user is holding the electrodes
    // and not touching the screen), the COMPLETE confirmation card, or if auto
    // sleep is disabled.
    // S4: low battery no longer inhibits sleep - pinning the AMOLED on while low
    // just accelerated the drain. The display sleeps normally; the warning is
    // re-shown on wake, and hw_thread still hits the 3.0 V shutdown while asleep.
    int ecg_status = (int)atomic_get(&m_disp_ecg_status);

    /* A spot check runs hands-off, so inactivity keeps climbing for its whole
     * duration while the guard below holds sleep off. Restart the timer as the
     * measurement leaves STREAMING, and again when the COMPLETE card closes, so
     * neither the confirmation nor the idle view it returns to is dropped into
     * an already expired timeout. This must precede the check: the status is set
     * from the ECG SMF thread, so a reset in a later pass loses the race. */
    if ((m_disp_ecg_sleep_synced == HPI_ECG_STATUS_STREAMING ||
         m_disp_ecg_sleep_synced == HPI_ECG_STATUS_COMPLETE) &&
        ecg_status != m_disp_ecg_sleep_synced) {
        lv_disp_trig_activity(NULL);
        inactivity_time = 0;
    }
    m_disp_ecg_sleep_synced = ecg_status;

    /* Same story for the GSR spot check: fingers on the electrodes, nobody
     * touching the screen. (Until now this was masked by the GSR plot queue
     * calling lv_disp_trig_activity() on every sample batch.) */
    int gsr_status = (int)atomic_get(&m_disp_gsr_status);
    if (m_disp_gsr_sleep_synced == HPI_GSR_STATUS_STREAMING &&
        gsr_status != m_disp_gsr_sleep_synced) {
        lv_disp_trig_activity(NULL);
        inactivity_time = 0;
    }
    m_disp_gsr_sleep_synced = gsr_status;

    bool ecg_measuring = (ecg_status == HPI_ECG_STATUS_STREAMING ||
                          ecg_status == HPI_ECG_STATUS_COMPLETE);
    bool gsr_measuring = (gsr_status == HPI_GSR_STATUS_STREAMING);
    if (sleep_timeout_ms != UINT32_MAX &&
        inactivity_time > sleep_timeout_ms &&
        !ecg_measuring &&
        !gsr_measuring)
    {
        smf_set_state(SMF_CTX(&s_disp_obj), &display_states[HPI_DISPLAY_STATE_SLEEP]);
    }
    return SMF_EVENT_HANDLED;
}

static void st_display_active_exit(void *o)
{
    LOG_DBG("Display SM Active Exit");
}

/*
 * AOD brightness (0–255). Soft path writes normal-mode 0x51; HW path writes
 * AOD bank 0x4A then AODMON. Not persisted into user brightness settings.
 * Tunable after PPK — start conservative for readable dim glyphs.
 */
#define HPI_AOD_BRIGHTNESS_RAW  18

/* Host loop sleep while AOD is showing (ms). Active UI uses 20 ms. */
#define HPI_AOD_HOST_SLEEP_MS   200

static void st_display_sleep_entry(void *o)
{
    LOG_DBG("Display SM Sleep Entry");

    s_display_asleep = true;

    // Save the current screen state before going to sleep
    hpi_disp_save_screen_state();

    /*
     * Sleep modes from Settings "Always-on" (hpi_v2_aod_get / hpiui/aod):
     *
     *  AOD ON + CONFIG_HPI_SH8601_HW_AOD:
     *    paint face → flush GRAM → sh8601_aod_enter (0x4A + AODMON 0x49).
     *  AOD ON + HW AOD disabled or enter fails:
     *    software dim of normal mode (display_set_brightness low).
     *  AOD OFF:
     *    full sleep — brightness 0 + DISPOFF + SLPIN (touch rail stays on).
     */
    s_sleep_is_aod = hpi_v2_aod_get() != 0;
    s_sleep_aod_hw = false;

    if (s_sleep_is_aod) {
        LOG_INF("Sleep: entering AOD face");
        hpi_v2_aod_enter();
        /* Push the face into GRAM before switching panel power mode. */
        lv_task_handler();

        if (display_dev && device_is_ready(display_dev)) {
#if IS_ENABLED(CONFIG_HPI_SH8601_HW_AOD)
            int aod_rc = sh8601_aod_enter(display_dev, HPI_AOD_BRIGHTNESS_RAW);

            if (aod_rc == 0) {
                s_sleep_aod_hw = true;
                LOG_INF("Sleep: SH8601 HW AOD active");
            } else {
                LOG_WRN("Sleep: HW AOD enter failed (%d); soft dim fallback",
                        aod_rc);
                display_set_brightness(display_dev, HPI_AOD_BRIGHTNESS_RAW);
            }
#else
            display_set_brightness(display_dev, HPI_AOD_BRIGHTNESS_RAW);
            LOG_INF("Sleep: soft AOD (CONFIG_HPI_SH8601_HW_AOD=n)");
#endif
        }
        return;
    }

    display_set_brightness(display_dev, 0);
    if (display_dev && device_is_ready(display_dev))
    {
        /* Use the display blanking API which maps to sh8601_display_blanking_on */
        display_blanking_on(display_dev);
        /* Also request panel sleep to reduce power inside the panel */
        sh8601_transmit_cmd(display_dev, SH8601_C_SLPIN, NULL, 0);
    }
    else
    {
        LOG_WRN("Display device not ready; skipping blanking");
    }
}

static enum smf_state_result st_display_sleep_run(void *o)
{
    // Check for crown button wakeup
    if (k_sem_take(&sem_crown_key_pressed, K_NO_WAIT) == 0)
    {
        LOG_DBG("Crown key pressed in sleep state");
        smf_set_state(SMF_CTX(&s_disp_obj), &display_states[HPI_DISPLAY_STATE_ACTIVE]);
        return SMF_EVENT_HANDLED;
    }

    // Check for touch wakeup signaled by LVGL input event callback
    if (k_sem_take(&sem_touch_wakeup, K_NO_WAIT) == 0)
    {
        LOG_DBG("Touch detected via LVGL event - waking up");
        smf_set_state(SMF_CTX(&s_disp_obj), &display_states[HPI_DISPLAY_STATE_ACTIVE]);
        return SMF_EVENT_HANDLED;
    }
    return SMF_EVENT_HANDLED;
}

static void st_display_sleep_exit(void *o)
{
    LOG_DBG("Display SM Sleep Exit (aod=%d hw=%d)",
            (int)s_sleep_is_aod, (int)s_sleep_aod_hw);
    s_display_asleep = false;
    /* Ensure the display power rail is enabled (no-op if already on) */
    hw_pwr_display_enable(true);

    if (s_sleep_is_aod) {
        /* Leave panel AOD mode before rebuilding UI at full brightness. */
#if IS_ENABLED(CONFIG_HPI_SH8601_HW_AOD)
        if (s_sleep_aod_hw && display_dev && device_is_ready(display_dev)) {
            (void)sh8601_aod_exit(display_dev);
        }
#endif
        s_sleep_aod_hw = false;

        /* Design: tap-to-wake → Home. Load Home first, then free AOD face. */
        hpi_disp_set_brightness(hpi_disp_get_brightness());

        m_low_batt_shown = false;
        hpi_disp_clear_saved_state();
        hpi_carousel_show(SCR_HOME, SCROLL_NONE);
        hpi_v2_aod_exit();
        hpi_disp_reconcile_low_battery();

        s_sleep_is_aod = false;
        lv_task_handler();
        k_msleep(5);
        lv_disp_trig_activity(NULL);
        return;
    }

    /* Full blank sleep: bring the panel out of SLPIN and reinit the driver.
     * Also clear any leftover AOD state if something left aod_active set. */
    if (display_dev && device_is_ready(display_dev))
    {
#if IS_ENABLED(CONFIG_HPI_SH8601_HW_AOD)
        if (sh8601_aod_is_active(display_dev)) {
            (void)sh8601_aod_exit(display_dev);
        }
#endif
        sh8601_transmit_cmd(display_dev, SH8601_C_SLPOUT, NULL, 0);
        k_msleep(10);
        sh8601_reinit(display_dev);
        k_msleep(10);
        /* Turn display back on and restore brightness */
        display_blanking_off(display_dev);
    }

    hpi_disp_set_brightness(hpi_disp_get_brightness());

    /* Re-init touch in case its driver needs re-attachment (safe no-op)
     * This keeps the existing wake path behavior. */
    device_init(touch_dev);
    k_msleep(10);

    /* S4: the display now sleeps even while low battery, so re-derive the
     * low-battery screen on wake. Reset the tracker first so reconcile re-fires;
     * if still low it re-shows the warning, otherwise the restored screen stays. */
    m_low_batt_shown = false;

    // Restore the saved screen state
    hpi_disp_restore_screen_state();

    // Clear the saved state after successful restoration
    hpi_disp_clear_saved_state();

    hpi_disp_reconcile_low_battery();

    // CRITICAL: Process LVGL tasks to ensure screen is fully rendered
    // This prevents race conditions where updates try to run before rendering completes
    lv_task_handler();
    k_msleep(5);  // Small delay to ensure LVGL finishes processing

    // Trigger LVGL activity to reset the inactivity timer
    lv_disp_trig_activity(NULL);
}

static void st_display_on_entry(void *o)
{
    LOG_DBG("Display SM On Entry");
}

// ============================================================================
// TRANSITION State: Blocks all screen updates during screen loading
// ============================================================================

static void st_display_transition_entry(void *o)
{
    LOG_DBG("Display SM Transition Entry - Updates suspended");
    // State machine automatically blocks updates - no run function defined
}

static void st_display_transition_exit(void *o)
{
    LOG_DBG("Display SM Transition Exit - Updates resumed");
}

// ============================================================================

static const struct smf_state display_states[] = {
    [HPI_DISPLAY_STATE_INIT] = SMF_CREATE_STATE(st_display_init_entry, NULL, NULL, NULL, NULL),
    [HPI_DISPLAY_STATE_SPLASH] = SMF_CREATE_STATE(st_display_splash_entry, st_display_splash_run, NULL, NULL, NULL),
    [HPI_DISPLAY_STATE_BOOT] = SMF_CREATE_STATE(st_display_boot_entry, st_display_boot_run, st_display_boot_exit, NULL, NULL),

    [HPI_DISPLAY_STATE_SCR_PROGRESS] = SMF_CREATE_STATE(st_display_progress_entry, st_display_progress_run, st_display_progress_exit, NULL, NULL),
    [HPI_DISPLAY_STATE_ACTIVE] = SMF_CREATE_STATE(st_display_active_entry, st_display_active_run, st_display_active_exit, NULL, NULL),
    [HPI_DISPLAY_STATE_TRANSITION] = SMF_CREATE_STATE(st_display_transition_entry, NULL, st_display_transition_exit, NULL, NULL),
    [HPI_DISPLAY_STATE_SLEEP] = SMF_CREATE_STATE(st_display_sleep_entry, st_display_sleep_run, st_display_sleep_exit, NULL, NULL),
    [HPI_DISPLAY_STATE_ON] = SMF_CREATE_STATE(st_display_on_entry, NULL, NULL, NULL, NULL),
};

/* One-time boot restore of last-known values from the health store's persisted
 * latest-per-type snapshot, so screens show the previous reading immediately
 * after a reboot (instead of "--" until a fresh measurement). Live values from
 * the disp_*_lis listeners override these as soon as they arrive. Fires once the
 * store's durable snapshot is loaded (after the FS mount); a no-op on a device
 * with no prior data. */
static void hpi_disp_restore_last_from_store(void)
{
    struct hpi_hs_sample s, dia;
    if (hpi_hs_get_latest(HPI_HS_T_HR, &s))         { m_disp_hr = s.value; }
    if (hpi_hs_get_latest(HPI_HS_T_SPO2, &s)) {
        m_disp_spo2 = s.value;
        m_disp_spo2_last_refresh_ts = s.ts_utc;
        /* uptime left 0 — age formats from UTC when the clock is valid */
    }
    /* stored skin_temp is degC*100; m_disp_temp drives the (degF) hero, so convert */
    if (hpi_hs_get_latest(HPI_HS_T_SKIN_TEMP, &s))  { m_disp_temp = (s.value / 100.0f) * 1.8f + 32.0f; }
    if (hpi_hs_get_latest(HPI_HS_T_ECG_HR, &s)) {
        m_disp_ecg_hr = (uint16_t)s.value;
        m_disp_ecg_hr_ts = s.ts_utc;
        /* uptime 0 — age formats from UTC when the clock is valid */
        hpi_ui_subj_set_ecg_hr(s.value);
    }
    if (hpi_hs_get_latest(HPI_HS_T_BP_SYS, &s) &&
        hpi_hs_get_latest(HPI_HS_T_BP_DIA, &dia))   { hpi_ui_subj_set_bp(s.value, dia.value); }
    if (hpi_hs_get_latest(HPI_HS_T_HRV_SDNN, &s))   { hpi_ui_subj_set_hrv_sdnn(s.value / 10); }
    if (hpi_hs_get_latest(HPI_HS_T_EDA_SCR_RATE, &s)) { hpi_ui_subj_set_gsr(s.value); }
    if (hpi_hs_get_latest(HPI_HS_T_STRESS, &s))     { hpi_ui_subj_set_stress(s.value); }
}

/* P6 step A: push the latest metric values into the UI subjects. Runs on the
 * display (LVGL) thread only. The subjects notify their observers (bound labels)
 * only when a value actually changed, so calling this every loop is cheap.
 * During AOD sleep only time + HR are pushed (AOD face bindings). */
/* One-shot: surface a recovered crash as a toast a few seconds after boot, so
 * the fault reason/thread is visible without the console (which shares USB lines
 * with the finger sensor). Display-thread only. */
static void hpi_disp_maybe_report_crash(void)
{
    static bool crash_shown = false;
    if (crash_shown || k_uptime_get() < 3000 || s_sleep_is_aod) {
        return;
    }
    crash_shown = true;

    uint32_t reason = 0, count = 0;
    char thr[16] = {0};
    if (hpi_crash_get_last(&reason, thr, sizeof(thr), &count)) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Recovered fault\nreason=%u thr=%s x%u",
                 reason, thr, count);
        LOG_ERR("%s", msg);
        hpi_disp_show_toast(msg, 8000);
    }
}

static void hpi_disp_push_subjects(void)
{
    hpi_disp_maybe_report_crash();

    /* Restore persisted last-known values once, as soon as the store snapshot is
     * available (has any HR/ECG history). */
    static bool restored = false;
    if (!restored) {
        struct hpi_hs_sample tmp;
        if (hpi_hs_get_latest(HPI_HS_T_HR, &tmp) || hpi_hs_get_latest(HPI_HS_T_ECG_HR, &tmp)) {
            hpi_disp_restore_last_from_store();
            restored = true;
        }
    }

    if (s_sleep_is_aod) {
        /* AOD face: time, date (via subj_time path), HR only — skip trends. */
        hpi_ui_subj_set_hr(m_disp_hr);
        hpi_ui_subj_set_time(m_disp_sys_time);
        return;
    }

    hpi_ui_subj_set_hr(m_disp_hr);
    hpi_ui_subj_set_spo2(m_disp_spo2);
    hpi_ui_subj_set_ecg_hr(m_disp_ecg_hr);
    hpi_ui_subj_set_steps((int)m_disp_steps);
    hpi_ui_subj_set_activity((int)m_disp_steps);
    hpi_ui_subj_set_batt(m_disp_batt_level, m_disp_batt_charging);
    /* m_disp_temp is °F from the sensor path; convert for user unit (0=°C). */
    {
        float t_disp = m_disp_temp;
        if (hpi_user_settings_get_temp_unit() == 0) {
            t_disp = (t_disp - 32.0f) * (5.0f / 9.0f);
        }
        hpi_ui_subj_set_temp_x100((int)(t_disp * 100.0f));
    }
    hpi_ui_subj_set_time(m_disp_sys_time);

    /* Derived (H2) metrics from the health-store summary cache (cheap: a locked
     * struct copy, no I/O). Static buffer avoids growing the display-thread
     * stack frame (already large with LVGL work). */
    static struct hpi_hs_summary summ;
    if (hpi_hs_summary(&summ) == 0) {
        hpi_ui_subj_set_hr_resting(summ.hr_resting_valid ? summ.hr_resting : 0);
        hpi_ui_subj_set_hr_min(summ.hr_min);
        hpi_ui_subj_set_hr_max(summ.hr_max);
        hpi_ui_subj_set_temp_dev_x100(summ.temp_dev_x100, summ.temp_dev_valid);
        if (summ.hrv_rmssd_x10 > 0) {
            /* Subject is ms integer; store holds ×10. */
            hpi_ui_subj_set_hrv_sdnn(summ.hrv_rmssd_x10 / 10);
        } else if (summ.hrv_sdnn_x10 > 0) {
            hpi_ui_subj_set_hrv_sdnn(summ.hrv_sdnn_x10 / 10);
        }
        if (summ.stress_hrv_valid) {
            hpi_ui_subj_set_stress(summ.stress_hrv);
        } else if (summ.stress_valid) {
            hpi_ui_subj_set_stress(summ.stress_last);
        }
        hpi_ui_subj_set_recovery(summ.readiness, summ.readiness_valid);
    }

    /* P3: trend cache paint — early-outs if tile widgets are not mounted. */
    hpi_hr_trend_refresh();
    hpi_temp_trend_refresh();
    hpi_spo2_trend_refresh();
    hpi_ecg_trend_refresh();
    hpi_stress_hrv_trend_refresh();
}

/* Fail an upload that goes quiet this long — the recovery path for the app
 * dropping off mid-transfer (a raw BLE disconnect emits no DFU_STOPPED). During
 * a healthy upload chunks arrive many times a second. Finalizing gets a longer
 * grace: the app still has to send `os reset` to trigger the reboot/swap. */
#define HPI_DFU_STALL_MS          10000
#define HPI_DFU_FINALIZE_STALL_MS 30000

/* Drive the DFU modal from hpi_dfu state (display/LVGL thread only). Runs every
 * loop; a no-op in the common IDLE case. Owns the DFU screen lifecycle: shows it
 * on ACTIVE, keeps the panel awake, paints the terminal phases, and returns Home
 * when the update ends. Waking a sleeping panel is handled via sem_touch_wakeup. */
static void hpi_disp_dfu_tick(void)
{
    static enum hpi_dfu_state last = HPI_DFU_IDLE;
    static int64_t terminal_ts;
    enum hpi_dfu_state st = hpi_dfu_get_state();

    /* An OTA that begins while asleep must wake the panel to show progress. */
    if (st != HPI_DFU_IDLE && last == HPI_DFU_IDLE && s_display_asleep) {
        k_sem_give(&sem_touch_wakeup);
    }

    switch (st) {
    case HPI_DFU_ACTIVE:
        /* App dropped off mid-transfer? Fail out instead of hanging forever. */
        if (hpi_dfu_ms_since_activity() > HPI_DFU_STALL_MS) {
            LOG_WRN("DFU stalled %u ms (app dropped off) - failing",
                    hpi_dfu_ms_since_activity());
            hpi_dfu_set_state(HPI_DFU_FAILED);
            break;
        }
        if (hpi_disp_get_curr_screen() != SCR_SPL_DFU) {
            draw_scr_dfu();
        }
        hpi_disp_dfu_update(hpi_dfu_get_progress());
        lv_disp_trig_activity(NULL);   /* never sleep mid-upload */
        break;

    case HPI_DFU_FINALIZING:
        /* Upload done but no reset arrived (app dropped before `os reset`) —
         * recover to normal rather than a permanent "Restarting...". */
        if (hpi_dfu_ms_since_activity() > HPI_DFU_FINALIZE_STALL_MS) {
            LOG_WRN("DFU finalize stalled - returning to normal");
            hpi_dfu_set_state(HPI_DFU_FAILED);
            break;
        }
        if (hpi_disp_get_curr_screen() != SCR_SPL_DFU) {
            draw_scr_dfu();
        }
        hpi_disp_dfu_update(100);
        hpi_disp_dfu_set_phase("UPDATE COMPLETE", "Restarting...", V2_GREEN);
        lv_disp_trig_activity(NULL);   /* hold until MCUboot swaps on reboot */
        break;

    case HPI_DFU_LOW_BATTERY:
        if (hpi_disp_get_curr_screen() != SCR_SPL_DFU) {
            draw_scr_dfu();
        }
        hpi_disp_dfu_set_phase("CHARGE TO UPDATE",
                               "Battery too low.\nConnect the charger.", R0_WARNING);
        if (last != HPI_DFU_LOW_BATTERY) {
            terminal_ts = k_uptime_get();
        }
        lv_disp_trig_activity(NULL);
        if (k_uptime_get() - terminal_ts > 5000) {
            hpi_dfu_set_state(HPI_DFU_IDLE);
        }
        break;

    case HPI_DFU_FAILED:
        if (last != HPI_DFU_FAILED) {
            terminal_ts = k_uptime_get();
            if (hpi_disp_get_curr_screen() == SCR_SPL_DFU) {
                hpi_disp_dfu_set_phase("UPDATE FAILED", "Please try again.", R0_ERROR);
            }
        }
        lv_disp_trig_activity(NULL);
        if (k_uptime_get() - terminal_ts > 4000) {
            hpi_dfu_set_state(HPI_DFU_IDLE);
        }
        break;

    case HPI_DFU_IDLE:
    default:
        if (last != HPI_DFU_IDLE && hpi_disp_get_curr_screen() == SCR_SPL_DFU) {
            hpi_load_screen(HPI_DEFAULT_START_SCREEN, SCROLL_NONE);
        }
        break;
    }

    last = st;
}

void smf_display_thread(void)
{
    int ret;

    k_sem_take(&sem_disp_smf_start, K_FOREVER);

    LOG_INF("Display SMF Thread Started");

    hpi_ui_subjects_init();   /* P6 step A: create UI subjects before any screen binds */

    smf_set_initial(SMF_CTX(&s_disp_obj), &display_states[HPI_DISPLAY_STATE_INIT]);

    /* Stall detection: a frozen UI is the most visible hang. Timeout is generous
     * (10s) to cover the longest bounded display iterations (boot/progress
     * screens). Registration is lazy - it succeeds once the watchdog is
     * initialised after boot. (Sensor SMF threads are added in P2, once their
     * run handlers no longer block internally.) */
    int wdt_ch = -1;

    for (;;)
    {
        if (wdt_ch < 0)
        {
            wdt_ch = hpi_watchdog_register("smf_display", 10000);
        }

        ret = smf_run_state(SMF_CTX(&s_disp_obj));
        if (ret != 0)
        {
            LOG_ERR("SMF Run error: %d", ret);
            break;
        }

        hpi_disp_dfu_tick();        /* firmware-update modal (LVGL thread) */

        hpi_disp_push_subjects();   /* P6 step A: refresh UI subjects (LVGL thread) */

        /* Skip active-only monitors while AOD face is up (power + no widgets). */
        if (!s_sleep_is_aod) {
            /* Render the inline ECG monitor to the SMF phase on every change edge
             * (LVGL thread - safe to touch widgets here). Tracks both status and the
             * progress countdown so the monitor follows wait -> stabilize -> record
             * and never drifts out of sync with the SMF. */
            {
                int ecg_status = (int)atomic_get(&m_disp_ecg_status);
                int ecg_timer = m_disp_ecg_timer;
                if (ecg_status != m_disp_ecg_status_synced ||
                    ecg_timer != m_disp_ecg_timer_synced) {
                    m_disp_ecg_status_synced = ecg_status;
                    m_disp_ecg_timer_synced = ecg_timer;
                    hpi_ecg_monitor_update(ecg_status, ecg_timer);
                }
            }

#if defined(CONFIG_HPI_GSR_SCREEN)
            /* Same for the inline EDA monitor: render the GSR SMF phase on every
             * status / countdown / contact edge. */
            {
                int gsr_status = (int)atomic_get(&m_disp_gsr_status);
                int gsr_contact = (int)atomic_get(&m_disp_gsr_contact);
                int gsr_remaining = m_disp_gsr_remaining;
                if (gsr_status != m_disp_gsr_status_synced ||
                    gsr_remaining != m_disp_gsr_remaining_synced ||
                    gsr_contact != m_disp_gsr_contact_synced) {
                    m_disp_gsr_status_synced = gsr_status;
                    m_disp_gsr_remaining_synced = gsr_remaining;
                    m_disp_gsr_contact_synced = gsr_contact;
                    hpi_eda_monitor_update(gsr_status, gsr_remaining, gsr_contact != 0);
                }
            }

            /* GSR capture finished -> the v2 results screen owns the result view.
             * Routed here rather than from the removed SCR_SPL_PLOT_GSR case, so an
             * in-place capture on the carousel tile still lands on it. */
            if (hpi_evt_consume(&ecg_evt, EVT_GSR_RESET)) {
                hpi_load_scr_spl(SCR_SPL_GSR_COMPLETE, SCROLL_DOWN, 0, 0, 0, 0);
            }
#endif

            /* Transient "leads off" warning when a spot check is aborted because
             * contact was lost mid-measurement (SMF -> IDLE). */
            if (hpi_evt_consume(&ecg_evt, EVT_ECG_LEADOFF_ABORT)) {
                hpi_disp_show_toast("ECG leads off\nMeasurement cancelled", 2500);
            }
        }

        lv_task_handler();
        hpi_watchdog_feed(wdt_ch);
        /* Throttle host loop during AOD to cut SPI/LVGL churn. */
        k_msleep(s_sleep_is_aod ? HPI_AOD_HOST_SLEEP_MS : 20);
    }
}

static void disp_batt_status_listener(const struct zbus_channel *chan)
{
    const struct hpi_batt_status_t *batt_s = zbus_chan_const_msg(chan);

    // LOG_DBG("Ch Batt: %d, Charge: %d", batt_s->batt_level, batt_s->batt_charging);
    m_disp_batt_level = batt_s->batt_level;
    m_disp_batt_charging = batt_s->batt_charging;
}

ZBUS_LISTENER_DEFINE(disp_batt_lis, disp_batt_status_listener);

static void data_mod_sys_time_listener(const struct zbus_channel *chan)
{
    const struct tm *sys_time = zbus_chan_const_msg(chan);
    m_disp_sys_time = *sys_time;

    // rtc_time_to_tm
}
ZBUS_LISTENER_DEFINE(disp_sys_time_lis, data_mod_sys_time_listener);

static void disp_hr_listener(const struct zbus_channel *chan)
{
    const struct hpi_hr_t *hpi_hr = zbus_chan_const_msg(chan);
    m_disp_hr = hpi_hr->hr;
    m_disp_hr_updated_ts = hpi_hr->timestamp;
    // LOG_DBG("ZB HR: %d at %02d:%02d", hpi_hr->hr, hpi_hr->time_tm.tm_hour, hpi_hr->time_tm.tm_min);
}
ZBUS_LISTENER_DEFINE(disp_hr_lis, disp_hr_listener);

static void disp_spo2_listener(const struct zbus_channel *chan)
{
    const struct hpi_spo2_point_t *hpi_spo2 = zbus_chan_const_msg(chan);
    if (hpi_spo2->spo2 > 0) {
        m_disp_spo2 = hpi_spo2->spo2;
        m_disp_spo2_last_refresh_ts = hpi_spo2->timestamp;
        m_disp_spo2_last_uptime_ms = k_uptime_get_32();
    }
    // LOG_DBG("ZB Spo2: %d | Time: %lld", hpi_spo2->spo2, hpi_spo2->timestamp);
}
ZBUS_LISTENER_DEFINE(disp_spo2_lis, disp_spo2_listener);

void hpi_disp_update_spo2(uint8_t spo2, int64_t ts_last_update)
{
    if (spo2 == 0) {
        return;
    }
    m_disp_spo2 = spo2;
    m_disp_spo2_last_refresh_ts = ts_last_update;
    m_disp_spo2_last_uptime_ms = k_uptime_get_32();
}

bool hpi_disp_get_last_spo2(uint8_t *spo2, int64_t *ts_utc, uint32_t *uptime_ms)
{
    if (m_disp_spo2 == 0) {
        return false;
    }
    if (spo2) {
        *spo2 = m_disp_spo2;
    }
    if (ts_utc) {
        *ts_utc = m_disp_spo2_last_refresh_ts;
    }
    if (uptime_ms) {
        *uptime_ms = m_disp_spo2_last_uptime_ms;
    }
    return true;
}

static void disp_steps_listener(const struct zbus_channel *chan)
{
    const struct hpi_steps_t *hpi_steps = zbus_chan_const_msg(chan);
    m_disp_steps = hpi_steps->steps;
    // LOG_DBG("ZB Steps Walk : %d | Run: %d", hpi_steps->steps_walk, hpi_steps->steps_run);
}
ZBUS_LISTENER_DEFINE(disp_steps_lis, disp_steps_listener);

static void disp_temp_listener(const struct zbus_channel *chan)
{
    const struct hpi_temp_t *hpi_temp = zbus_chan_const_msg(chan);
    m_disp_temp = hpi_temp->temp_f;
    m_disp_temp_updated_ts = hpi_temp->timestamp;
}
ZBUS_LISTENER_DEFINE(disp_temp_lis, disp_temp_listener);

static void disp_bpt_listener(const struct zbus_channel *chan)
{
    const struct hpi_bpt_t *hpi_bpt = zbus_chan_const_msg(chan);
    m_disp_bp_sys = hpi_bpt->sys;
    m_disp_bp_dia = hpi_bpt->dia;
    m_disp_bpt_status = hpi_bpt->status;
    m_disp_bpt_progress = hpi_bpt->progress;

}
ZBUS_LISTENER_DEFINE(disp_bpt_lis, disp_bpt_listener);

static void disp_ecg_timer_listener(const struct zbus_channel *chan)
{
    const struct hpi_ecg_status_t *ecg_status = zbus_chan_const_msg(chan);
    m_disp_ecg_timer = ecg_status->progress_timer;
}
ZBUS_LISTENER_DEFINE(disp_ec, disp_ecg_timer_listener);

static void disp_ecg_stat_listener(const struct zbus_channel *chan)
{
    const struct hpi_ecg_status_t *ecg_status = zbus_chan_const_msg(chan);
    /* Never clear a latched last-session HR with a 0 payload: IDLE / leads-off
     * status publishes carry hr=0, and overwriting here forced subj_ecg to
     * "--" after every successful recording. */
    if (ecg_status->hr > 0) {
        m_disp_ecg_hr = ecg_status->hr;
        m_disp_ecg_hr_uptime_ms = k_uptime_get_32();
        /* Prefer the SMF's session timestamp on COMPLETE; else wall clock. */
        if (ecg_status->status == HPI_ECG_STATUS_COMPLETE &&
            ecg_status->ts_complete > 0) {
            m_disp_ecg_hr_ts = ecg_status->ts_complete;
        } else {
            m_disp_ecg_hr_ts = hw_get_sys_time_ts();
        }
    }
    m_disp_ecg_timer = ecg_status->progress_timer;
    atomic_set(&m_disp_ecg_status, ecg_status->status);
    // LOG_DBG("ZB ECG HR: %d", *ecg_hr);
}
ZBUS_LISTENER_DEFINE(disp_ecg_stat_lis, disp_ecg_stat_listener);

bool hpi_disp_get_last_ecg_hr(uint16_t *hr, int64_t *ts_utc, uint32_t *uptime_ms)
{
    if (m_disp_ecg_hr == 0) {
        return false;
    }
    if (hr) {
        *hr = m_disp_ecg_hr;
    }
    if (ts_utc) {
        *ts_utc = m_disp_ecg_hr_ts;
    }
    if (uptime_ms) {
        *uptime_ms = m_disp_ecg_hr_uptime_ms;
    }
    return true;
}

static void disp_hrv_stat_listener(const struct zbus_channel *chan)
{
    const struct hpi_hrv_status_t *hrv_status = zbus_chan_const_msg(chan);
      m_disp_hrv_timer = hrv_status->remaining_s;
    // m_disp_hrv_timer = hrv_status->progress_timer;
}
ZBUS_LISTENER_DEFINE(disp_hrv_stat_lis, disp_hrv_stat_listener);

#if defined(CONFIG_HPI_GSR_STRESS_INDEX)
static void disp_gsr_stress_listener(const struct zbus_channel *chan)
{
    const struct hpi_gsr_stress_index_t *stress_data = zbus_chan_const_msg(chan);
    
    if (stress_data && stress_data->stress_data_ready) {
        // Update the GSR complete screen if it's currently displayed
        hpi_gsr_complete_update_results(stress_data);
        
        LOG_DBG("GSR Stress Index: level=%d, tonic=%d.%02d μS, peaks/30s=%d", 
                stress_data->stress_level,
                stress_data->tonic_level_x100 / 100,
                stress_data->tonic_level_x100 % 100,
                stress_data->peaks_per_minute);
    }
}
ZBUS_LISTENER_DEFINE(disp_gsr_stress_lis, disp_gsr_stress_listener);
#endif

#if defined(CONFIG_HPI_GSR_SCREEN)
static void disp_gsr_status_listener(const struct zbus_channel *chan)
{
    const struct hpi_gsr_status_t *status = zbus_chan_const_msg(chan);
    if (!status) return;
    // Store in display thread variable for periodic update (mirrors ECG pattern)
    m_disp_gsr_remaining = status->remaining_s;
    atomic_set(&m_disp_gsr_status, status->status);
    atomic_set(&m_disp_gsr_contact, status->contact ? 1 : 0);
}
ZBUS_LISTENER_DEFINE(disp_gsr_status_lis, disp_gsr_status_listener);
#endif


#define SMF_DISPLAY_THREAD_STACK_SIZE 24576
#define SMF_DISPLAY_THREAD_PRIORITY 5

K_THREAD_DEFINE(smf_display_thread_id, SMF_DISPLAY_THREAD_STACK_SIZE, smf_display_thread, NULL, NULL, NULL, SMF_DISPLAY_THREAD_PRIORITY, 0, 0);
