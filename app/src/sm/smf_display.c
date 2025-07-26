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
    if (!hpi_user_settings_get_auto_sleep_enabled()) {
        return UINT32_MAX; // Never sleep if auto sleep is disabled
    }
    
    uint8_t sleep_timeout_seconds = hpi_user_settings_get_sleep_timeout();
    uint32_t timeout_ms = sleep_timeout_seconds * 1000;
    
    // Log the current sleep timeout occasionally for debugging
    static uint32_t last_log_time = 0;
    uint32_t now = k_uptime_get_32();
    if (now - last_log_time > 60000) { // Log every minute
        LOG_DBG("Sleep timeout: %d seconds (%d ms)", sleep_timeout_seconds, timeout_ms);
        last_log_time = now;
    }
    
    return timeout_ms;
}

K_MSGQ_DEFINE(q_plot_ecg_bioz, sizeof(struct hpi_ecg_bioz_sensor_data_t), 64, 1);
K_MSGQ_DEFINE(q_plot_ppg_wrist, sizeof(struct hpi_ppg_wr_data_t), 64, 1);
K_MSGQ_DEFINE(q_plot_ppg_fi, sizeof(struct hpi_ppg_fi_data_t), 64, 1);
K_MSGQ_DEFINE(q_plot_hrv, sizeof(struct hpi_computed_hrv_t), 64, 1);
K_MSGQ_DEFINE(q_disp_boot_msg, sizeof(struct hpi_boot_msg_t), 4, 1);

K_SEM_DEFINE(sem_disp_ready, 0, 1);
K_SEM_DEFINE(sem_ecg_complete, 0, 1);
K_SEM_DEFINE(sem_ecg_complete_reset, 0, 1);

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
    HPI_DISPLAY_STATE_SLEEP,
    HPI_DISPLAY_STATE_ON,
    HPI_DISPLAY_STATE_OFF,
};

// Display screen variables
static uint8_t m_disp_batt_level = 0;
static bool m_disp_batt_charging = false;
static struct tm m_disp_sys_time;

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
    [SCR_SPL_RAW_PPG] = {draw_scr_spl_raw_ppg, NULL},
    [SCR_SPL_ECG_SCR2] = {draw_scr_ecg_scr2, gesture_down_scr_ecg_2},
    [SCR_SPL_FI_SENS_WEAR] = {draw_scr_fi_sens_wear, gesture_down_scr_fi_sens_wear},
    [SCR_SPL_FI_SENS_CHECK] = {draw_scr_fi_sens_check, gesture_down_scr_fi_sens_check},
    [SCR_SPL_BPT_MEASURE] = {draw_scr_bpt_measure, gesture_down_scr_bpt_measure},
    [SCR_SPL_BPT_CAL_COMPLETE] = {draw_scr_bpt_cal_complete, gesture_down_scr_bpt_cal_complete},
    [SCR_SPL_ECG_COMPLETE] = {draw_scr_ecg_complete, gesture_down_scr_ecg_complete},
    [SCR_SPL_PLOT_HRV] = {draw_scr_hrv, NULL},
    //[SCR_SPL_HR_SCR2] = { draw_scr_hr_scr2, gesture_down_scr_hr_scr2 },
    [SCR_SPL_SPO2_SCR2] = {draw_scr_spo2_scr2, gesture_down_scr_spo2_scr2},
    [SCR_SPL_SPO2_MEASURE] = {draw_scr_spo2_measure, gesture_down_scr_spo2_measure},
    [SCR_SPL_SPO2_COMPLETE] = {draw_scr_spl_spo2_complete, gesture_down_scr_spl_spo2_complete},
    [SCR_SPL_SPO2_TIMEOUT] = {draw_scr_spl_spo2_timeout, gesture_down_scr_spl_spo2_timeout},
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

        // Check if the saved screen has a valid draw function
        if (saved_screen >= 0 && saved_screen < ARRAY_SIZE(screen_func_table) &&
            screen_func_table[saved_screen].draw != NULL)
        {
            // Use the special screen loading function for complex screens
            hpi_load_scr_spl(saved_screen, saved_scroll, saved_arg1, saved_arg2, saved_arg3, saved_arg4);
        }
        else
        {
            // Fall back to regular screen loading for basic screens
            hpi_load_screen(saved_screen, SCROLL_NONE);
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
            printk("End of list\n");
            return;
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

        if( hpi_disp_get_curr_screen() == SCR_SPL_DEVICE_USER_SETTINGS)
        {
            // If we are in the device user settings screen, go back to the pull down screen
            hpi_load_screen(SCR_HOME, SCROLL_RIGHT);
            return;
        }
        if ((curr_screen - 1) == SCR_LIST_START)
        {
            printk("Start of list\n");
            return;
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

extern struct k_sem sem_disp_smf_start;

extern struct k_sem sem_disp_boot_complete;
extern struct k_sem sem_boot_update_req;

extern struct k_msgq q_ecg_bioz_sample;
extern struct k_msgq q_ppg_wrist_sample;
extern struct k_msgq q_plot_ecg_bioz;
extern struct k_msgq q_plot_ppg_wrist;
extern struct k_msgq q_plot_hrv;

extern struct k_sem sem_crown_key_pressed;

extern struct k_sem sem_ecg_lead_on;
extern struct k_sem sem_ecg_lead_off;

extern struct k_sem sem_stop_one_shot_spo2;
extern struct k_sem sem_spo2_complete;

extern struct k_sem sem_bpt_sensor_found;

// User Profile settings

uint16_t m_user_height = 170; // Example height in m, adjust as needed
uint16_t m_user_weight = 70;  // Example weight in kg, adjust as needed
static double m_user_met = 3.5;      // Example MET value based speed = 1.34 m/s , adjust as needed

static uint16_t hpi_get_kcals_from_steps(uint16_t steps)
{
    // KCals = time * MET * 3.5 * weight / (200*60)

    double _m_time = (((m_user_height / 100.000) * 0.414 * steps) / 4800.000) * 60.000; // Assuming speed of 4.8 km/h
    double _m_kcals = (_m_time * m_user_met * 3.500 * m_user_weight) / 200;
    /// LOG_DBG("Calc Kcals %f", _m_kcals, steps);
    return (uint16_t)_m_kcals;
}

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
        const char msg[] = "MAX32664D \n FW Update Required";
        memcpy(s->title, msg, sizeof(msg));
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
    LOG_DBG("MAX32664 Update Progress: %d", progress);
    max32664_update_progress = progress;
    max32664_update_status = status;
}

static void st_display_progress_entry(void *o)
{
    struct s_disp_object *s = (struct s_disp_object *)o;

    LOG_DBG("Display SM Progress Entry");
    draw_scr_progress(s->title, "Please wait...");
    max32664_set_progress_callback(hpi_max32664_update_progress);
    max32664_update_progress = 0;
    hpi_disp_scr_update_progress(max32664_update_progress, "Starting...");
}

static void st_display_progress_run(void *o)
{
    if (max32664_update_status == MAX32664_UPDATER_STATUS_IN_PROGRESS)
    {
        hpi_disp_scr_update_progress(max32664_update_progress, "Updating...");
    }
    else if (max32664_update_status == MAX32664_UPDATER_STATUS_SUCCESS)
    {
        hpi_disp_scr_update_progress(max32664_update_progress, "Update Success");
        k_msleep(2000);
        smf_set_state(SMF_CTX(&s_disp_obj), &display_states[HPI_DISPLAY_STATE_BOOT]);
    }
    else if (max32664_update_status == MAX32664_UPDATER_STATUS_FAILED)
    {
        hpi_disp_scr_update_progress(max32664_update_progress, "Update Failed");
        k_msleep(2000);
        smf_set_state(SMF_CTX(&s_disp_obj), &display_states[HPI_DISPLAY_STATE_ACTIVE]);
    }
}

static void st_display_progress_exit(void *o)
{
    LOG_DBG("Display SM Progress Exit");
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
}

static void hpi_disp_process_ecg_bioz_data(struct hpi_ecg_bioz_sensor_data_t ecg_bioz_sensor_sample)
{
    if (hpi_disp_get_curr_screen() == SCR_SPL_ECG_SCR2)
    {
        hpi_ecg_disp_draw_plotECG(ecg_bioz_sensor_sample.ecg_samples, ecg_bioz_sensor_sample.ecg_num_samples, ecg_bioz_sensor_sample.ecg_lead_off);
    }
    else
    {
    }
    /*else if (hpi_disp_get_curr_screen() == SCR_PLOT_EDA)
    {
        hpi_eda_disp_draw_plotEDA(ecg_bioz_sensor_sample.bioz_sample, ecg_bioz_sensor_sample.bioz_num_samples, ecg_bioz_sensor_sample.bioz_lead_off);
    }*/
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
    switch (hpi_disp_get_curr_screen())
    {
    case SCR_HOME:
        if (k_uptime_get_32() - last_time_refresh > HPI_DISP_TIME_REFR_INT)
        {
            // ui_home_time_display_update(m_disp_sys_time);
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
    case SCR_SPL_ECG_SCR2:
        hpi_ecg_disp_update_hr(m_disp_ecg_hr);
        hpi_ecg_disp_update_timer(m_disp_ecg_timer);
        if (k_sem_take(&sem_ecg_complete, K_NO_WAIT) == 0)
        {
            hpi_load_scr_spl(SCR_SPL_ECG_COMPLETE, SCROLL_DOWN, SCR_SPL_PLOT_ECG, 0, 0, 0);
        }
        if (k_sem_take(&sem_ecg_lead_on, K_NO_WAIT) == 0)
        {
            scr_ecg_lead_on_off_handler(true);
            m_lead_on_off = true;
        }
        if (k_sem_take(&sem_ecg_lead_off, K_NO_WAIT) == 0)
        {
            scr_ecg_lead_on_off_handler(false);
            m_lead_on_off = false;
        }

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
    case SCR_TODAY:
        if ((k_uptime_get_32() - last_today_trend_refresh) > HPI_DISP_TODAY_REFRESH_INT)
        {
            hpi_scr_today_update_all(m_disp_steps, m_disp_kcals, m_disp_active_time_s);
            last_today_trend_refresh = k_uptime_get_32();
        }
        break;
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
    struct hpi_ecg_bioz_sensor_data_t ecg_bioz_sensor_sample;
    struct hpi_ppg_wr_data_t ppg_sensor_sample;
    struct hpi_ppg_fi_data_t ppg_fi_sensor_sample;

    if (k_msgq_get(&q_plot_ppg_wrist, &ppg_sensor_sample, K_NO_WAIT) == 0)
    {
        hpi_disp_process_ppg_wr_data(ppg_sensor_sample);
    }

    if (k_msgq_get(&q_plot_ecg_bioz, &ecg_bioz_sensor_sample, K_NO_WAIT) == 0)
    {
        hpi_disp_process_ecg_bioz_data(ecg_bioz_sensor_sample);
    }

    if (k_msgq_get(&q_plot_ppg_fi, &ppg_fi_sensor_sample, K_NO_WAIT) == 0)
    {
        hpi_disp_process_ppg_fi_data(ppg_fi_sensor_sample);
    }

    // Do screen specific updates
    hpi_disp_update_screens();

    if (k_uptime_get_32() - last_batt_refresh > HPI_DISP_BATT_REFR_INT)
    {
        if ((hpi_disp_get_curr_screen() == SCR_HOME))
        {
            hpi_disp_home_update_batt_level(m_disp_batt_level, m_disp_batt_charging);
        }
        else if (hpi_disp_get_curr_screen() == SCR_SPL_PULLDOWN)
        {
            hpi_disp_settings_update_batt_level(m_disp_batt_level, m_disp_batt_charging);
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
            k_sem_give(&sem_stop_one_shot_spo2);
            hpi_load_screen(SCR_HOME, SCROLL_NONE);
        }
        else
        {
            hpi_load_screen(SCR_HOME, SCROLL_NONE);
        }
    }

    if (k_sem_take(&sem_change_screen, K_NO_WAIT) == 0)
    {
        LOG_DBG("Change Screen: %d", scr_to_change);
        if (screen_func_table[g_screen].draw)
        {
            screen_func_table[g_screen].draw(g_scroll_dir, g_arg1, g_arg2, g_arg3, g_arg4);
        }
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

    display_set_brightness(display_dev, 0);
    hw_pwr_display_enable(false);
}

static void st_display_sleep_run(void *o)
{
    int inactivity_time = lv_disp_get_inactive_time(NULL);
    // LOG_DBG("Inactivity Time: %d", inactivity_time);
    
    uint32_t sleep_timeout_ms = get_sleep_timeout_ms();
    
    if (sleep_timeout_ms == UINT32_MAX || inactivity_time < sleep_timeout_ms)
    {
        // hpi_display_sleep_on();
        smf_set_state(SMF_CTX(&s_disp_obj), &display_states[HPI_DISPLAY_STATE_ACTIVE]);
    }
    else
    {
        // If we are in sleep state, we can still process some events
        // For example, if the user presses the crown button
        if (k_sem_take(&sem_crown_key_pressed, K_NO_WAIT) == 0)
        {
            LOG_DBG("Crown key pressed in sleep state");
            smf_set_state(SMF_CTX(&s_disp_obj), &display_states[HPI_DISPLAY_STATE_ACTIVE]);
        }
    }
}

static void st_display_sleep_exit(void *o)
{
    LOG_DBG("Display SM Sleep Exit");
    hw_pwr_display_enable(true);

    sh8601_reinit(display_dev);
    k_msleep(10);

    hpi_disp_set_brightness(hpi_disp_get_brightness());

    device_init(touch_dev);
    k_msleep(10);

    // Restore the saved screen state
    hpi_disp_restore_screen_state();

    // Clear the saved state after successful restoration
    hpi_disp_clear_saved_state();
}

static void st_display_on_entry(void *o)
{
    LOG_DBG("Display SM On Entry");
}

static const struct smf_state display_states[] = {
    [HPI_DISPLAY_STATE_INIT] = SMF_CREATE_STATE(st_display_init_entry, NULL, NULL, NULL, NULL),
    [HPI_DISPLAY_STATE_SPLASH] = SMF_CREATE_STATE(st_display_splash_entry, st_display_splash_run, NULL, NULL, NULL),
    [HPI_DISPLAY_STATE_BOOT] = SMF_CREATE_STATE(st_display_boot_entry, st_display_boot_run, st_display_boot_exit, NULL, NULL),

    [HPI_DISPLAY_STATE_SCR_PROGRESS] = SMF_CREATE_STATE(st_display_progress_entry, st_display_progress_run, st_display_progress_exit, NULL, NULL),
    [HPI_DISPLAY_STATE_ACTIVE] = SMF_CREATE_STATE(st_display_active_entry, st_display_active_run, st_display_active_exit, NULL, NULL),
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
        k_msleep(100);
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

#define SMF_DISPLAY_THREAD_STACK_SIZE 32768
#define SMF_DISPLAY_THREAD_PRIORITY 5

K_THREAD_DEFINE(smf_display_thread_id, SMF_DISPLAY_THREAD_STACK_SIZE, smf_display_thread, NULL, NULL, NULL, SMF_DISPLAY_THREAD_PRIORITY, 0, 0);
