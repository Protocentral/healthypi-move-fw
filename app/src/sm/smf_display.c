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
#include <zephyr/smf.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>
#include <zephyr/zbus/zbus.h>
#include <time.h>

#include <display_sh8601.h>
#include "hpi_common_types.h"
#include "hw_module.h"
#include "ui/move_ui.h"
#include "max32664_updater.h"
#include "hpi_sys.h"
#include "hpi_user_settings_api.h"

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
K_SEM_DEFINE(sem_ecg_complete, 0, 1);
K_SEM_DEFINE(sem_ecg_complete_reset, 0, 1);
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

static int last_hr_trend_refresh = 0;
static int last_spo2_trend_refresh = 0;
static int last_today_trend_refresh = 0;
static int last_temp_trend_refresh = 0;

static int scr_to_change = SCR_HOME;
K_SEM_DEFINE(sem_change_screen, 0, 1);

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

// @brief Today Screen variables
static uint32_t m_disp_steps = 0;
static uint16_t m_disp_kcals = 0;
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
static bool m_lead_on_off = false;

// @brief GSR Screen variables
static uint16_t m_disp_gsr_remaining = 60; // countdown timer (seconds remaining)

// @brief HRV Screen variables
extern struct k_sem sem_hrv_eval_complete;


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

static uint8_t g_scr_parent = SCR_HOME;

typedef void (*screen_draw_func_t)(enum scroll_dir, uint32_t, uint32_t, uint32_t, uint32_t);
typedef void (*screen_gesture_down_func_t)(void);

typedef struct
{
    screen_draw_func_t draw;
    screen_gesture_down_func_t gesture_down;
} screen_func_table_entry_t;

static int curr_screen = SCR_HOME;
K_MUTEX_DEFINE(mutex_curr_screen);

// Array of function pointers for screen drawing functions
static const screen_func_table_entry_t screen_func_table[] = {
    [SCR_HOME] = {draw_scr_home, NULL},
    [SCR_HR] = {draw_scr_hr, NULL},
    [SCR_SPO2] = {draw_scr_spo2, NULL},
    [SCR_ECG] = {draw_scr_ecg, NULL},
    [SCR_TEMP] = {draw_scr_temp, NULL},
    [SCR_BPT] = {draw_scr_bpt, NULL},
    [SCR_GSR] = {draw_scr_gsr, NULL},
    [SCR_HRV] = {draw_scr_hrv, NULL},
    [SCR_SPL_RAW_PPG] = {draw_scr_spl_raw_ppg, gesture_down_scr_spl_raw_ppg},
    [SCR_SPL_ECG_SCR2] = {draw_scr_ecg_scr2, gesture_down_scr_ecg_2},
    [SCR_SPL_FI_SENS_WEAR] = {draw_scr_fi_sens_wear, gesture_down_scr_fi_sens_wear},
    [SCR_SPL_FI_SENS_CHECK] = {draw_scr_fi_sens_check, gesture_down_scr_fi_sens_check},
    [SCR_SPL_BPT_MEASURE] = {draw_scr_bpt_measure, gesture_down_scr_bpt_measure},
    [SCR_SPL_BPT_CAL_COMPLETE] = {draw_scr_bpt_cal_complete, gesture_down_scr_bpt_cal_complete},
    [SCR_SPL_ECG_COMPLETE] = {draw_scr_ecg_complete, gesture_down_scr_ecg_complete},

  //  [SCR_SPL_PLOT_HRV] = {draw_scr_hrv, NULL},
    [SCR_SPL_HRV_EVAL_PROGRESS] = {draw_scr_spl_hrv_eval_progress, gesture_down_scr_spl_hrv_eval_progress},
    [SCR_SPL_HRV_COMPLETE] = {draw_scr_spl_hrv_complete, gesture_down_scr_spl_hrv_complete},

    //[SCR_SPL_HR_SCR2] = { draw_scr_hr_scr2, gesture_down_scr_hr_scr2 },
    [SCR_SPL_SPO2_SCR2] = {draw_scr_spo2_scr2, gesture_down_scr_spo2_scr2},
    [SCR_SPL_SPO2_MEASURE] = {draw_scr_spo2_measure, gesture_down_scr_spo2_measure},
    [SCR_SPL_SPO2_COMPLETE] = {draw_scr_spl_spo2_complete, gesture_down_scr_spl_spo2_complete},
    [SCR_SPL_SPO2_TIMEOUT] = {draw_scr_spl_spo2_timeout, gesture_down_scr_spl_spo2_timeout},
    [SCR_SPL_SPO2_CANCELLED] = {draw_scr_spl_spo2_cancelled, gesture_down_scr_spl_spo2_cancelled},
    [SCR_SPL_PLOT_GSR] = {draw_scr_gsr_plot, unload_scr_gsr_plot},
    [SCR_SPL_GSR_COMPLETE] = {draw_scr_gsr_complete, unload_scr_gsr_complete},
    [SCR_SPL_LOW_BATTERY] = {draw_scr_spl_low_battery, gesture_down_scr_spl_low_battery},
    [SCR_SPL_SPO2_SELECT] = {draw_scr_spo2_select, gesture_down_scr_spo2_select},

    [SCR_SPL_BPT_CAL_PROGRESS] = {draw_scr_bpt_cal_progress, gesture_down_scr_bpt_cal_progress},
    [SCR_SPL_BPT_FAILED] = {draw_scr_bpt_cal_failed, gesture_down_scr_bpt_cal_failed},
    [SCR_SPL_BPT_EST_COMPLETE] = {draw_scr_bpt_est_complete, gesture_down_scr_bpt_est_complete},
    [SCR_SPL_BPT_CAL_REQUIRED] = {draw_scr_bpt_cal_required, gesture_down_scr_bpt_cal_required},

    [SCR_SPL_BLE] = {draw_scr_ble, NULL},
    [SCR_SPL_PULLDOWN] = {draw_scr_pulldown, gesture_down_scr_pulldown},
    [SCR_SPL_DEVICE_USER_SETTINGS] = {draw_scr_device_user_settings, gesture_down_scr_device_user_settings},
    [SCR_SPL_HEIGHT_SELECT] = {draw_scr_height_select, gesture_down_scr_height_select},
    [SCR_SPL_WEIGHT_SELECT] = {draw_scr_weight_select, gesture_down_scr_weight_select},
    [SCR_SPL_HAND_WORN_SELECT] = {draw_scr_hand_worn_select, gesture_down_scr_hand_worn_select},
    [SCR_SPL_TIME_FORMAT_SELECT] = {draw_scr_time_format_select, gesture_down_scr_time_format_select},
    [SCR_SPL_TEMP_UNIT_SELECT] = {draw_scr_temp_unit_select, gesture_down_scr_temp_unit_select},
    [SCR_SPL_SLEEP_TIMEOUT_SELECT] = {draw_scr_sleep_timeout_select, gesture_down_scr_sleep_timeout_select},
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
    m_disp_kcals = 0;
    m_disp_active_time_s = 0;
    m_disp_temp = 0;
    m_disp_bp_sys = 0;
    m_disp_bp_dia = 0;
    m_disp_ecg_hr = 0;
    m_disp_ecg_timer = 0;

    m_disp_hr_updated_ts = 0;
    m_disp_spo2_last_refresh_ts = 0;
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

    screen_sleep_state.saved_screen = hpi_disp_get_curr_screen();
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
        // Regular screens: SCR_LIST_START < screen < SCR_LIST_END (e.g., SCR_HOME, SCR_TODAY, etc.)
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

void disp_screen_event(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    // lv_obj_t *target = lv_event_get_target(e);

    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_LEFT)
    {
        lv_indev_wait_release(lv_indev_get_act());
        printf("Left at %d\n", curr_screen);

        if ((curr_screen + 1) == SCR_LIST_END)
        {
            // Wrap around: go from last carousel screen back to HOME
            printk("End of list, wrapping to HOME\n");
            hpi_load_screen(SCR_HOME, SCROLL_LEFT);
        }
        else
        {
            printk("Loading screen %d\n", curr_screen + 1);
            hpi_load_screen(curr_screen + 1, SCROLL_LEFT);
        }
    }

    else if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT)
    {
        lv_indev_wait_release(lv_indev_get_act());
        printf("Right at %d\n", curr_screen);

        if (hpi_disp_get_curr_screen() == SCR_SPL_HR_SCR2)
        {
            hpi_load_screen(SCR_HR, SCROLL_LEFT);
            return;
        }

        if (hpi_disp_get_curr_screen() == SCR_SPL_SPO2_MEASURE)
        {
            hpi_load_screen(SCR_SPO2, SCROLL_LEFT);
            return;
        }

        if (hpi_disp_get_curr_screen() == SCR_SPL_DEVICE_USER_SETTINGS)
        {
            // If we are in the device user settings screen, go back to the pull down screen
            hpi_load_screen(SCR_HOME, SCROLL_RIGHT);
            return;
        }
        if ((curr_screen - 1) == SCR_LIST_START)
        {
            // Wrap around: go from HOME back to last carousel screen (SCR_HRV or SCR_GSR)
            printk("Start of list, wrapping to last screen\n");
            // Find the last regular screen before SCR_LIST_END
            int last_screen = SCR_LIST_END - 1;
            hpi_load_screen(last_screen, SCROLL_RIGHT);
        }
        else
        {
            printk("Loading screen %d\n", curr_screen - 1);
            hpi_load_screen(curr_screen - 1, SCROLL_RIGHT);
        }
    }
    else if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_BOTTOM)
    {
        lv_indev_wait_release(lv_indev_get_act());
        printk("Down at %d\n", curr_screen);

        int screen = hpi_disp_get_curr_screen();

        if (screen == SCR_HOME)
        {
            // If we are on the home screen, load the settings screen
            // hpi_load_screen(SCR_SPL_PULLDOWN, SCROLL_DOWN);
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
        else if (curr_screen == SCR_SPL_DEVICE_USER_SETTINGS)
        {
            hpi_load_scr_spl(SCR_SPL_PULLDOWN, SCROLL_UP, 0, 0, 0, 0);
        }
        /*else if (hpi_disp_get_curr_screen() == SCR_HR)
        {
            hpi_load_scr_spl(SCR_SPL_HR_SCR2, SCROLL_DOWN, SCR_HR, 0, 0, 0);
        }*/
    }
}

typedef void (*screen_static_draw_func_t)(enum scroll_dir, uint32_t, uint32_t, uint32_t, uint32_t);

static int max32664_update_progress = 0;
static int max32664_update_status = MAX32664_UPDATER_STATUS_IDLE;

// Externs
extern const struct device *display_dev;
extern const struct device *touch_dev;
extern lv_obj_t *scr_bpt;
#if defined(CONFIG_HPI_TODAY_SCREEN)
extern lv_obj_t *scr_today;
#endif

extern struct k_sem sem_disp_smf_start;

extern struct k_sem sem_disp_boot_complete;
extern struct k_sem sem_boot_update_req;

extern struct k_msgq q_ecg_sample;
extern struct k_msgq q_ppg_wrist_sample;
extern struct k_msgq q_plot_ecg;
extern struct k_msgq q_plot_ppg_wrist;
extern struct k_msgq q_plot_hrv;
extern struct k_msgq q_plot_gsr;

extern struct k_sem sem_crown_key_pressed;

extern struct k_sem sem_ecg_lead_on;
extern struct k_sem sem_ecg_lead_off;
extern struct k_sem sem_ecg_lead_on_stabilize;

extern struct k_sem sem_stop_one_shot_spo2;
extern struct k_sem sem_spo2_complete;
extern struct k_sem sem_spo2_cancel;

extern struct k_sem sem_bpt_sensor_found;

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
    hpi_disp_set_brightness(50);

    smf_set_state(SMF_CTX(&s_disp_obj), &display_states[HPI_DISPLAY_STATE_SPLASH]);
}

static void st_display_splash_entry(void *o)
{
    LOG_DBG("Display SM Splash Entry");
    draw_scr_splash();
    splash_scr_start_time = k_uptime_get_32();
}

static void st_display_splash_run(void *o)
{
    // Stay in this state for 2 seconds
    if ((k_uptime_get_32() - splash_scr_start_time) > 2000)
    {
        smf_set_state(SMF_CTX(&s_disp_obj), &display_states[HPI_DISPLAY_STATE_BOOT]);
    }
}

static void st_display_boot_entry(void *o)
{
    LOG_DBG("Display SM Boot Entry");
    draw_scr_boot();

    // Signal that the display is ready
    k_sem_give(&sem_disp_ready);
}

static void st_display_boot_run(void *o)
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

static void st_display_progress_run(void *o)
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
        // Update calibration progress text
        if (k_uptime_get_32() - m_disp_bp_last_refresh > 1000)
        {
            m_disp_bp_last_refresh = k_uptime_get_32();
            char progress_str[32];
            snprintf(progress_str, sizeof(progress_str), "Calibrating... %d%%", ppg_sensor_sample.bpt_progress);
            scr_bpt_cal_progress_update_text(progress_str);
            LOG_INF("BPT Cal Progress: %d%%", ppg_sensor_sample.bpt_progress);
        }
        lv_disp_trig_activity(NULL);
    }
    else if (hpi_disp_get_curr_screen() == SCR_SPL_SPO2_MEASURE)
    {
        lv_disp_trig_activity(NULL);
        hpi_disp_spo2_plot_fi_ppg(ppg_sensor_sample);
        hpi_disp_spo2_update_progress(ppg_sensor_sample.spo2_valid_percent_complete, ppg_sensor_sample.spo2_state, ppg_sensor_sample.spo2, ppg_sensor_sample.hr);
    }
}

static void hpi_disp_process_ppg_wr_data(struct hpi_ppg_wr_data_t ppg_sensor_sample)
{
    if (hpi_disp_get_curr_screen() == SCR_SPL_SPO2_MEASURE)
    {
        lv_disp_trig_activity(NULL);
        hpi_disp_spo2_plot_wrist_ppg(ppg_sensor_sample);
        hpi_disp_spo2_update_progress(ppg_sensor_sample.spo2_valid_percent_complete, ppg_sensor_sample.spo2_state, ppg_sensor_sample.spo2, ppg_sensor_sample.hr);
    }
    else if (hpi_disp_get_curr_screen() == SCR_SPL_RAW_PPG)
    {
        /* Forward samples to the raw PPG screen plotting function */
        lv_disp_trig_activity(NULL);
        hpi_disp_ppg_draw_plotPPG(ppg_sensor_sample);
        /* Update the HR label on raw PPG screen if available */
        hpi_ppg_disp_update_hr(ppg_sensor_sample.hr);
    }
}

static void hpi_disp_process_ecg_data(struct hpi_ecg_bioz_sensor_data_t ecg_sensor_sample)
{
    if (hpi_disp_get_curr_screen() == SCR_SPL_ECG_SCR2)
    {
        hpi_ecg_disp_draw_plotECG(ecg_sensor_sample.ecg_samples, ecg_sensor_sample.ecg_num_samples, ecg_sensor_sample.ecg_lead_off);
    }
    else if (hpi_disp_get_curr_screen() == SCR_SPL_HRV_EVAL_PROGRESS)
    {
        hpi_ecg_disp_draw_plotECG_hrv(ecg_sensor_sample.ecg_samples, ecg_sensor_sample.ecg_num_samples, ecg_sensor_sample.ecg_lead_off);
    }
    /*else if (hpi_disp_get_curr_screen() == SCR_PLOT_EDA)
    {
        hpi_eda_disp_draw_plotEDA(ecg_bioz_sensor_sample.bioz_sample, ecg_bioz_sensor_sample.bioz_num_samples, ecg_bioz_sensor_sample.bioz_lead_off);
    }*/
}

static void hpi_disp_process_gsr_data(struct hpi_gsr_sensor_data_t gsr_sensor_sample)
{
    if (hpi_disp_get_curr_screen() == SCR_SPL_PLOT_GSR)
    {
        // Call batched GSR plot function (bioz_samples contains multiple samples)
        hpi_gsr_disp_draw_plotGSR(gsr_sensor_sample.bioz_samples, gsr_sensor_sample.bioz_num_samples, gsr_sensor_sample.bioz_lead_off != 0);
    }
}

static void st_display_active_entry(void *o)
{
    LOG_DBG("Display SM Active Entry");

    if (hpi_disp_get_curr_screen() == SCR_SPL_BOOT)
    {
        hpi_load_screen(HPI_DEFAULT_START_SCREEN, SCROLL_NONE);
    }
    /*else
    {
        hpi_load_screen(hpi_disp_get_curr_screen(), SCROLL_NONE);
    }*/
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
    case SCR_HOME:
        if (k_uptime_get_32() - last_time_refresh > HPI_DISP_TIME_REFR_INT)
        {
            // Update home screen arcs with actual ZBus data
            hpi_home_hr_update(m_disp_hr);
            hpi_home_steps_update(m_disp_steps);
            // Also update quick action buttons for consistency
            ui_hr_button_update(m_disp_hr);
            ui_steps_button_update(m_disp_steps);
        }
        break;
    case SCR_TEMP:
        if (k_uptime_get_32() - last_temp_trend_refresh > HPI_DISP_TEMP_REFRESH_INT)
        {
            if (m_disp_temp > 0)
            {
                hpi_temp_disp_update_temp_f((double)m_disp_temp, m_disp_temp_updated_ts);
            }
            last_temp_trend_refresh = k_uptime_get_32();
        }
        break;
    case SCR_GSR:
#if defined(CONFIG_HPI_GSR_SCREEN)
        if (k_uptime_get_32() - last_temp_trend_refresh > HPI_DISP_TEMP_REFRESH_INT)
        {
            uint16_t gsr_value = 0;
            int64_t gsr_last_update = 0;
            if (hpi_sys_get_last_gsr_update(&gsr_value, &gsr_last_update) == 0 && gsr_value > 0)
            {
                // Use integer-only function for flash optimization
                hpi_gsr_disp_update_gsr_int(gsr_value, gsr_last_update);
            }
            last_temp_trend_refresh = k_uptime_get_32();
        }
#endif
        break;
    case SCR_HR:
        if (m_disp_hr > 0)
        {
            hpi_disp_hr_update_hr(m_disp_hr, m_disp_hr_updated_ts);
        }
        last_hr_trend_refresh = k_uptime_get_32();
        break;
    case SCR_SPL_HR_SCR2:
        if ((k_uptime_get_32() - last_hr_trend_refresh) > HPI_DISP_TRENDS_REFRESH_INT)
        {
            // hpi_disp_hr_load_trend();
            last_hr_trend_refresh = k_uptime_get_32();
        }
        break;
    case SCR_SPO2:
        if ((k_uptime_get_32() - last_spo2_trend_refresh) > HPI_DISP_TRENDS_REFRESH_INT)
        {
            // hpi_disp_update_spo2(m_disp_spo2, m_disp_spo2_last_refresh_tm);
            // hpi_disp_spo2_load_trend();
            last_spo2_trend_refresh = k_uptime_get_32();
        }
        break;
    case SCR_BPT:

        break;
    case SCR_SPL_BPT_CAL_PROGRESS:
        lv_disp_trig_activity(NULL);
        break;
    case SCR_SPL_HRV_EVAL_PROGRESS:
         if(k_sem_take(&sem_hrv_eval_complete, K_NO_WAIT) == 0)
         {
            hpi_load_scr_spl(SCR_SPL_HRV_COMPLETE, SCROLL_UP, 0, 0, 0, 0);
         }
         if (k_sem_take(&sem_ecg_lead_on, K_NO_WAIT) == 0)
         {
            LOG_INF("HRV Screen: Lead ON detected ");
            scr_hrv_lead_on_off_handler(false);  
         }
         if (k_sem_take(&sem_ecg_lead_off, K_NO_WAIT) == 0)
         {
            LOG_INF("HRV Screen: Lead OFF detected");
            scr_hrv_lead_on_off_handler(true);   
         }
         lv_disp_trig_activity(NULL);
        break;

    case SCR_SPL_ECG_SCR2:
        hpi_ecg_disp_update_hr(m_disp_ecg_hr);
        hpi_ecg_disp_update_timer(m_disp_ecg_timer);
        if (k_sem_take(&sem_ecg_complete, K_NO_WAIT) == 0)
        {
            hpi_load_scr_spl(SCR_SPL_ECG_COMPLETE, SCROLL_DOWN, SCR_SPL_PLOT_ECG, 0, 0, 0);
        }
        if (k_sem_take(&sem_ecg_lead_on, K_NO_WAIT) == 0)
        {
            LOG_INF("DISPLAY THREAD: Processing ECG Lead ON semaphore - calling UI handler");
            scr_ecg_lead_on_off_handler(false); // false = leads ON
            
            // Only trigger stabilization if this is a reconnection (previous state was leads OFF)
            bool is_ecg_active = hpi_data_is_ecg_record_active();
            bool was_lead_off = m_lead_on_off;  // Previous state before this update
            
            m_lead_on_off = false;              // Update to leads ON
            
            LOG_INF("DISPLAY THREAD: ECG active=%s, was_lead_off=%s", 
                    is_ecg_active ? "true" : "false", 
                    was_lead_off ? "true" : "false");
            
            // Only trigger re-stabilization if:
            // 1. Recording is active AND
            // 2. This is a reconnection (previous state was lead off)
            if (is_ecg_active && was_lead_off)
            {
                LOG_INF("DISPLAY THREAD: Lead reconnected - triggering stabilization phase");
                
                // Signal state machine to enter stabilization before resuming recording
                k_sem_give(&sem_ecg_lead_on_stabilize);
            }
            // Start timer if this is first lead-on (not a reconnection)
            else if (is_ecg_active && !was_lead_off)
            {
                LOG_INF("DISPLAY THREAD: Leads already on - starting timer");
                hpi_ecg_timer_start();
            }
        }
        if (k_sem_take(&sem_ecg_lead_off, K_NO_WAIT) == 0)
        {
            LOG_INF("DISPLAY THREAD: Processing ECG Lead OFF semaphore - calling UI handler");
            scr_ecg_lead_on_off_handler(true); // true = leads OFF
            m_lead_on_off = true;              // true = leads OFF

            // Reset recording to ensure continuous 30s data when leads come back on
            bool is_ecg_active = hpi_data_is_ecg_record_active();
            LOG_INF("DISPLAY THREAD: ECG record active = %s", is_ecg_active ? "true" : "false");
            if (is_ecg_active)
            {
                LOG_INF("DISPLAY THREAD: Lead disconnected - resetting recording buffer for continuous capture");
                
                // Reset the recording buffer without saving incomplete data
                hpi_data_reset_ecg_record_buffer();
                
                // Reset UI timer state
                LOG_INF("DISPLAY THREAD: Resetting UI timer state");
                hpi_ecg_timer_reset();
                
                // Reset ECG SMF countdown to 30s
                LOG_INF("DISPLAY THREAD: Resetting ECG SMF countdown to 30s");
                hpi_ecg_reset_countdown_timer();
            }
        }

        lv_disp_trig_activity(NULL);

        break;
    case SCR_SPL_PLOT_GSR:
#if defined(CONFIG_HPI_GSR_SCREEN)
        // Update GSR countdown timer display (mirrors ECG pattern)
        hpi_gsr_disp_update_timer(m_disp_gsr_remaining);
#endif
        lv_disp_trig_activity(NULL);
        break;
    case SCR_SPL_RAW_PPG:
        // Periodically check for signal timeout to show "No Signal" message
        hpi_ppg_check_signal_timeout();
        lv_disp_trig_activity(NULL);
        break;
    case SCR_SPL_ECG_COMPLETE:
        if (k_sem_take(&sem_ecg_complete_reset, K_NO_WAIT) == 0)
        {
            hpi_load_screen(SCR_ECG, SCROLL_UP);
        }
        break;
    case SCR_SPL_SPO2_COMPLETE:
        /*if (k_sem_take(&sem_spo2_complete, K_NO_WAIT) == 0)
        {
            hpi_disp_update_spo2(m_disp_spo2, m_disp_spo2_last_refresh_ts);
        }*/
        break;
    /*case SCR_SPL_FI_SENS_CHECK:
        if (k_sem_take(&sem_bpt_sensor_found, K_NO_WAIT) == 0)
        {
            LOG_DBG("Loading BPT SCR4");

        }
        if
        break;*/
#if defined(CONFIG_HPI_TODAY_SCREEN)
    case SCR_TODAY:
        if ((k_uptime_get_32() - last_today_trend_refresh) > HPI_DISP_TODAY_REFRESH_INT)
        {
            // Only update if the screen has been properly initialized
            // This prevents crashes during sleep/wake transitions
            if (scr_today != NULL)
            {
                hpi_scr_today_update_all(m_disp_steps, m_disp_kcals, m_disp_active_time_s);
                last_today_trend_refresh = k_uptime_get_32();
            }
        }
        break;
#endif
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

void hpi_load_scr_spl(int m_screen, enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    LOG_DBG("Loading screen %d", m_screen);

    if (m_screen >= 0 && m_screen < ARRAY_SIZE(screen_func_table) && screen_func_table[m_screen].draw != NULL)
    {
        g_screen = m_screen;
        g_scroll_dir = m_scroll_dir;
        g_scr_parent = arg1;
        g_arg1 = arg1;
        g_arg2 = arg2;
        g_arg3 = arg3;
        g_arg4 = arg4;

        k_sem_give(&sem_change_screen);
    }
    else
    {
        LOG_ERR("Invalid screen: %d", m_screen);
    }
}

static void st_display_active_run(void *o)
{
    struct hpi_ecg_bioz_sensor_data_t ecg_sensor_sample;
    struct hpi_gsr_sensor_data_t gsr_sensor_sample;
    struct hpi_ppg_wr_data_t ppg_sensor_sample;
    struct hpi_ppg_fi_data_t ppg_fi_sensor_sample;

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
        hpi_disp_process_gsr_data(gsr_sensor_sample);
        lv_disp_trig_activity(NULL);
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
            
            if ((hpi_disp_get_curr_screen() == SCR_HOME))
            {
                hpi_disp_home_update_batt_level(m_disp_batt_level, m_disp_batt_charging);
                ui_updated = true;
            }
            else if (hpi_disp_get_curr_screen() == SCR_SPL_PULLDOWN)
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

    // Update Time
    if (k_uptime_get_32() - last_time_refresh > HPI_DISP_TIME_REFR_INT)
    {
        last_time_refresh = k_uptime_get_32();

        // Home screen doesn't have a header display
        if (hpi_disp_get_curr_screen() == SCR_HOME)
        {
            hpi_scr_home_update_time_date(m_disp_sys_time);
        }
    }

    // Add button handlers
    if (k_sem_take(&sem_crown_key_pressed, K_NO_WAIT) == 0)
    {
        lv_disp_trig_activity(NULL);
        if (hpi_disp_get_curr_screen() == SCR_HOME)
        {
            // hpi_display_sleep_on();
        }
        else if (hpi_disp_get_curr_screen() == SCR_SPL_ECG_SCR2)
        {
            gesture_down_scr_ecg_2();
        }
        else if (hpi_disp_get_curr_screen() == SCR_SPL_SPO2_MEASURE)
        {
            /* User cancelled measurement via crown: signal explicit cancel semaphore */
            k_sem_give(&sem_spo2_cancel);
        }
        else
        {
            hpi_load_screen(SCR_HOME, SCROLL_NONE);
        }
    }

    if (k_sem_take(&sem_change_screen, K_NO_WAIT) == 0)
    {
        LOG_DBG("Change Screen: %d", scr_to_change);
        
        // CRITICAL: Set transition flag to block all screen updates
        screen_transition_in_progress = true;
        
        if (screen_func_table[g_screen].draw)
        {
            screen_func_table[g_screen].draw(g_scroll_dir, g_arg1, g_arg2, g_arg3, g_arg4);
        }
        
        // CRITICAL: Clear transition flag after screen is loaded
        screen_transition_in_progress = false;
        
        lv_disp_trig_activity(NULL);
    }

    int inactivity_time = lv_disp_get_inactive_time(NULL);
    // LOG_DBG("Inactivity Time: %d", inactivity_time);

    // Get current sleep timeout based on user settings
    uint32_t sleep_timeout_ms = get_sleep_timeout_ms();

    // Prevent sleep during low battery conditions or if auto sleep is disabled
    if (sleep_timeout_ms != UINT32_MAX &&
        inactivity_time > sleep_timeout_ms &&
        !hw_is_low_battery())
    {
        smf_set_state(SMF_CTX(&s_disp_obj), &display_states[HPI_DISPLAY_STATE_SLEEP]);
    }
}

static void st_display_active_exit(void *o)
{
    LOG_DBG("Display SM Active Exit");
}

static void st_display_sleep_entry(void *o)
{
    LOG_DBG("Display SM Sleep Entry");

    // Save the current screen state before going to sleep
    hpi_disp_save_screen_state();

    /*
     * Instead of cutting the display LDO (which may also power the touch
     * controller on some hardware revisions), request the display driver to
     * blank the panel (DISPOFF / SLPIN). This keeps the panel power rail
     * enabled so the touch controller remains powered and can still report
     * touches while the screen is off.
     */
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

static void st_display_sleep_run(void *o)
{
    // Check for crown button wakeup
    if (k_sem_take(&sem_crown_key_pressed, K_NO_WAIT) == 0)
    {
        LOG_DBG("Crown key pressed in sleep state");
        smf_set_state(SMF_CTX(&s_disp_obj), &display_states[HPI_DISPLAY_STATE_ACTIVE]);
        return;
    }

    // Check for touch wakeup signaled by LVGL input event callback
    if (k_sem_take(&sem_touch_wakeup, K_NO_WAIT) == 0)
    {
        LOG_DBG("Touch detected via LVGL event - waking up");
        smf_set_state(SMF_CTX(&s_disp_obj), &display_states[HPI_DISPLAY_STATE_ACTIVE]);
        return;
    }
}

static void st_display_sleep_exit(void *o)
{
    LOG_DBG("Display SM Sleep Exit");
    /* Ensure the display power rail is enabled (no-op if already on) */
    hw_pwr_display_enable(true);

    /* Bring the panel out of sleep and reinit the driver */
    if (display_dev && device_is_ready(display_dev))
    {
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

    // Restore the saved screen state
    hpi_disp_restore_screen_state();

    // Clear the saved state after successful restoration
    hpi_disp_clear_saved_state();
    
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

void smf_display_thread(void)
{
    int ret;

    k_sem_take(&sem_disp_smf_start, K_FOREVER);

    LOG_INF("Display SMF Thread Started");

    smf_set_initial(SMF_CTX(&s_disp_obj), &display_states[HPI_DISPLAY_STATE_INIT]);

    for (;;)
    {
        ret = smf_run_state(SMF_CTX(&s_disp_obj));
        if (ret != 0)
        {
            LOG_ERR("SMF Run error: %d", ret);
            break;
        }

        lv_task_handler();
        k_msleep(20);
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
    m_disp_spo2 = hpi_spo2->spo2;
    m_disp_spo2_last_refresh_ts = hpi_spo2->timestamp;
    // LOG_DBG("ZB Spo2: %d | Time: %lld", hpi_spo2->spo2, hpi_spo2->timestamp);
}
ZBUS_LISTENER_DEFINE(disp_spo2_lis, disp_spo2_listener);

static void disp_steps_listener(const struct zbus_channel *chan)
{
    const struct hpi_steps_t *hpi_steps = zbus_chan_const_msg(chan);
    m_disp_steps = hpi_steps->steps;
    m_disp_kcals = hpi_get_kcals_from_steps(m_disp_steps);
    // LOG_DBG("ZB Steps Walk : %d | Run: %d", hpi_steps->steps_walk, hpi_steps->steps_run);
}
ZBUS_LISTENER_DEFINE(disp_steps_lis, disp_steps_listener);

static void disp_temp_listener(const struct zbus_channel *chan)
{
    const struct hpi_temp_t *hpi_temp = zbus_chan_const_msg(chan);
    m_disp_temp = hpi_temp->temp_f;
    m_disp_temp_updated_ts = hpi_temp->timestamp;
    LOG_DBG("ZB Temp: %.2f", hpi_temp->temp_f);
}
ZBUS_LISTENER_DEFINE(disp_temp_lis, disp_temp_listener);

static void disp_bpt_listener(const struct zbus_channel *chan)
{
    const struct hpi_bpt_t *hpi_bpt = zbus_chan_const_msg(chan);
    m_disp_bp_sys = hpi_bpt->sys;
    m_disp_bp_dia = hpi_bpt->dia;
    m_disp_bpt_status = hpi_bpt->status;
    m_disp_bpt_progress = hpi_bpt->progress;

    LOG_DBG("ZB BPT Status: %d Progress: %d\n", hpi_bpt->status, hpi_bpt->progress);
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
    m_disp_ecg_hr = ecg_status->hr;
    m_disp_ecg_timer = ecg_status->progress_timer;
    // LOG_DBG("ZB ECG HR: %d", *ecg_hr);
}
ZBUS_LISTENER_DEFINE(disp_ecg_stat_lis, disp_ecg_stat_listener);

#if defined(CONFIG_HPI_GSR_STRESS_INDEX)
static void disp_gsr_stress_listener(const struct zbus_channel *chan)
{
    const struct hpi_gsr_stress_index_t *stress_data = zbus_chan_const_msg(chan);
    
    if (stress_data && stress_data->stress_data_ready) {
        // Update the GSR complete screen if it's currently displayed
        hpi_gsr_complete_update_results(stress_data);
        
        LOG_DBG("GSR Stress Index: level=%d, tonic=%d.%02d μS, peaks/min=%d", 
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
}
ZBUS_LISTENER_DEFINE(disp_gsr_status_lis, disp_gsr_status_listener);
#endif

#define SMF_DISPLAY_THREAD_STACK_SIZE 24576
#define SMF_DISPLAY_THREAD_PRIORITY 5

K_THREAD_DEFINE(smf_display_thread_id, SMF_DISPLAY_THREAD_STACK_SIZE, smf_display_thread, NULL, NULL, NULL, SMF_DISPLAY_THREAD_PRIORITY, 0, 0);
