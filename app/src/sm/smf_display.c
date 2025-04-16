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

LOG_MODULE_REGISTER(smf_display, LOG_LEVEL_DBG);

#define HPI_DEFAULT_START_SCREEN SCR_HOME

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

static int32_t hpi_scr_ppg_hr_spo2_last_refresh = 0;

static const struct smf_state display_states[];

enum display_state
{
    HPI_DISPLAY_STATE_INIT,
    HPI_DISPLAY_STATE_SPLASH,
    HPI_DISPLAY_STATE_BOOT,
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
// static uint16_t m_disp_hr_max = 0;
// static uint16_t m_disp_hr_min = 0;
// static uint16_t m_disp_hr_mean = 0;
static struct tm m_disp_hr_last_update_tm;

// @brief Spo2 Screen variables
static uint8_t m_disp_spo2 = 0;
static int64_t m_disp_spo2_last_refresh_ts;

// @brief Today Screen variables
static uint32_t m_disp_steps = 0;
static uint16_t m_disp_kcals = 0;
static uint16_t m_disp_active_time_s = 0;

// @brief Temperature Screen variables
static float m_disp_temp = 0;

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

} s_disp_obj;

// Externs
extern const struct device *display_dev;
extern const struct device *touch_dev;
extern lv_obj_t *scr_bpt;

extern struct k_sem sem_disp_smf_start;
extern struct k_sem sem_disp_boot_complete;
extern struct k_msgq q_ecg_bioz_sample;
extern struct k_msgq q_ppg_wrist_sample;
extern struct k_msgq q_plot_ecg_bioz;
extern struct k_msgq q_plot_ppg_wrist;
extern struct k_msgq q_plot_hrv;

extern struct k_sem sem_crown_key_pressed;

extern struct k_sem sem_ppg_fi_show_loading;
extern struct k_sem sem_ppg_fi_hide_loading;

extern struct k_sem sem_ecg_lead_on;
extern struct k_sem sem_ecg_lead_off;

extern struct k_sem sem_ecg_cancel;
extern struct k_sem sem_stop_one_shot_spo2;

// User Profile settings

static uint16_t m_user_height = 170; // Example height in m, adjust as needed
static uint16_t m_user_weight = 70;  // Example weight in kg, adjust as needed
static double m_user_met = 3.5;      // Example MET value based speed = 1.34 m/s , adjust as needed

static uint16_t hpi_get_kcals_from_steps(uint16_t steps)
{
    // KCals = time * MET * 3.5 * weight / (200*60)

    double _m_time = (((m_user_height / 100.000) * 0.414 * steps) / 4800.000) * 60.000; // Assuming speed of 4.8 km/h
    double _m_kcals = (_m_time * m_user_met * 3.500 * m_user_weight) / 200;
    /// LOG_DBG("Calc Kcals %f", _m_kcals, steps);
    return (uint16_t)_m_kcals;
}

struct tm disp_get_hr_last_update_ts(void)
{
    return m_disp_hr_last_update_tm;
}

static void st_display_init_entry(void *o)
{
    LOG_DBG("Display SM Init Entry");

    LOG_DBG("Disp ON");

    if (!device_is_ready(display_dev))
    {
        LOG_ERR("Device not ready");
        // return;
    }

    LOG_DBG("Display device: %s", display_dev->name);

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

static lv_obj_t *obj_msgbox;

static void set_angle(void *obj, int32_t v)
{
    lv_arc_set_value(obj, v);
}

static void hpi_disp_show_loading(lv_obj_t *scr_parent, char *message)
{
    obj_msgbox = lv_msgbox_create(scr_parent, "Please wait...", NULL, NULL, false);
    lv_obj_center(obj_msgbox);
    lv_obj_set_size(obj_msgbox, 250, 250);

    /* setting's content*/
    lv_obj_t *content = lv_msgbox_get_content(obj_msgbox);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(content, -1, LV_PART_SCROLLBAR);

    lv_obj_t *lbl_msg = lv_label_create(content);
    lv_label_set_text(lbl_msg, message);

    /*Create an Arc*/
    lv_obj_t *arc = lv_arc_create(content);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);  /*Be sure the knob is not displayed*/
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE); /*To not allow adjusting by click*/
    lv_obj_set_size(arc, 100, 100);
    lv_obj_center(arc);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc);
    lv_anim_set_exec_cb(&a, set_angle);
    lv_anim_set_time(&a, 1000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE); /*Just for the demo*/
    lv_anim_set_repeat_delay(&a, 500);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_start(&a);
}

static void hpi_disp_hide_loading(void)
{
    if (obj_msgbox != NULL)
    {
        lv_msgbox_close(obj_msgbox);
        obj_msgbox = NULL;
    }
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

    // draw_scr_splash();
}

static void st_display_boot_run(void *o)
{
    struct hpi_boot_msg_t boot_msg;

    if (k_msgq_get(&q_disp_boot_msg, &boot_msg, K_NO_WAIT) == 0)
    {
        if (boot_msg.status == false)
        {
            hpi_boot_all_passed = false;
        }
        scr_boot_add_status(boot_msg.msg, boot_msg.status);
    }

    // Stay in this state until the boot is complete
    if (k_sem_take(&sem_disp_boot_complete, K_NO_WAIT) == 0)
    {
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
    // LOG_DBG("Display SM Boot Run");
}

static void st_display_boot_exit(void *o)
{
    LOG_DBG("Display SM Boot Exit");
    lv_disp_trig_activity(NULL);
}

static void hpi_disp_process_ppg_fi_data(struct hpi_ppg_fi_data_t ppg_sensor_sample)
{
    if (hpi_disp_get_curr_screen() == SCR_SPL_PLOT_BPT_PPG)
    {
        hpi_disp_bpt_draw_plotPPG(ppg_sensor_sample);

        if (k_uptime_get_32() - m_disp_bp_last_refresh > 1000)
        {
            m_disp_bp_last_refresh = k_uptime_get_32();
            hpi_disp_bpt_update_progress(ppg_sensor_sample.bpt_progress);
        }
    }
}

static void hpi_disp_process_ppg_wr_data(struct hpi_ppg_wr_data_t ppg_sensor_sample)
{
    if (hpi_disp_get_curr_screen() == SCR_SPL_RAW_PPG)
    {
        hpi_disp_ppg_draw_plotPPG(ppg_sensor_sample);

        if (k_uptime_get_32() - hpi_scr_ppg_hr_spo2_last_refresh > 1000)
        {
            hpi_scr_ppg_hr_spo2_last_refresh = k_uptime_get_32();
            hpi_ppg_disp_update_hr(ppg_sensor_sample.hr);
        }

        lv_disp_trig_activity(NULL);
    }
    else if (hpi_disp_get_curr_screen() == SCR_SPL_SPO2_SCR3)
    {
        hpi_disp_spo2_plotPPG(ppg_sensor_sample);
        hpi_disp_spo2_update_progress(ppg_sensor_sample.spo2_valid_percent_complete, ppg_sensor_sample.spo2_state, ppg_sensor_sample.spo2, ppg_sensor_sample.hr);
    }

    else if ((hpi_disp_get_curr_screen() == SCR_HOME)) // || (hpi_disp_get_curr_screen() == SCR_CLOCK_SMALL))
    {
    }
    /*else if (hpi_disp_get_curr_screen() == SCR_PLOT_HRV)
    {
        if (ppg_sensor_sample.rtor != 0) // && ppg_sensor_sample.rtor != prev_rtor)
        {
            // printk("RTOR: %d | SCD: %d", ppg_sensor_sample.rtor, ppg_sensor_sample.scd_state);
            hpi_disp_hrv_draw_plot_rtor((float)((ppg_sensor_sample.rtor)));
            hpi_disp_hrv_update_rtor(ppg_sensor_sample.rtor);
            // prev_rtor = ppg_sensor_sample.rtor;
        }
    }
    else if (hpi_disp_get_curr_screen() == SCR_PLOT_HRV_SCATTER)
    {
        if (ppg_sensor_sample.rtor != 0) //&& ppg_sensor_sample.rtor != prev_rtor)
        {
            // printk("RTOR: %d | SCD: %d", ppg_sensor_sample.rtor, ppg_sensor_sample.scd_state);
            // hpi_disp_hrv_scatter_draw_plot_rtor((float)((ppg_sensor_sample.rtor)), (float)prev_rtor);
            // hpi_disp_hrv_scatter_update_rtor(ppg_sensor_sample.rtor);
            // prev_rtor = ppg_sensor_sample.rtor;
        }
    }*/

    /*
    if (hpi_disp_get_curr_screen() == SUBSCR_BPT_MEASURE)
    {
    if (k_msgq_get(&q_plot_ppg_wrist, &ppg_sensor_sample, K_NO_WAIT) == 0)
    {
        if (bpt_meas_started == true)
        {
            hpi_disp_draw_plotPPG((float)((ppg_sensor_sample.raw_red * 1.0000)));

            if (bpt_meas_done_flag == false)
            {
                if (bpt_meas_last_status != ppg_sensor_sample.bpt_status)
                {
                    bpt_meas_last_status = ppg_sensor_sample.bpt_status;
                    printk("BPT Status: %d", ppg_sensor_sample.bpt_status);
                }
                if (bpt_meas_last_progress != ppg_sensor_sample.bpt_progress)
                {
                    hpi_disp_bpt_update_progress(ppg_sensor_sample.bpt_progress);
                    bpt_meas_last_progress = ppg_sensor_sample.bpt_progress;
                }
                if (bpt_meas_last_progress >= 100)
                {
                    printk("BPT Meas progress 100");

                    global_bp_dia = ppg_sensor_sample.bp_dia;
                    global_bp_sys = ppg_sensor_sample.bp_sys;

                    hw_bpt_stop();
                    ppg_data_stop();

                    printk("BPT Done: %d / %d", global_bp_sys, global_bp_dia);
                    bpt_meas_done_flag = true;
                    bpt_meas_started = false;
                    hpi_disp_update_bp(global_bp_sys, global_bp_dia);

                    if (hpi_disp_get_curr_screen() == SUBSCR_BPT_MEASURE)
                    {
                        lv_obj_clear_flag(label_bp_val, LV_OBJ_FLAG_HIDDEN);
                        lv_obj_clear_flag(label_bp_sys_sub, LV_OBJ_FLAG_HIDDEN);
                        lv_obj_clear_flag(label_bp_sys_cap, LV_OBJ_FLAG_HIDDEN);
                        lv_obj_add_flag(chart1, LV_OBJ_FLAG_HIDDEN);

                        lv_obj_add_flag(btn_bpt_measure_start, LV_OBJ_FLAG_HIDDEN);
                        lv_obj_clear_flag(btn_bpt_measure_exit, LV_OBJ_FLAG_HIDDEN);
                    }
                    hpi_disp_bpt_update_progress(ppg_sensor_sample.bpt_progress);
                }
            }
            lv_disp_trig_activity(NULL);
        }
        */
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
        hpi_move_load_screen(HPI_DEFAULT_START_SCREEN, SCROLL_NONE);
    }
    /*else
    {
        hpi_move_load_screen(hpi_disp_get_curr_screen(), SCROLL_NONE);
    }*/
}

static void st_disp_do_bpt_stuff(void)
{
    if (k_sem_take(&sem_ppg_fi_show_loading, K_NO_WAIT) == 0)
    {
        hpi_disp_show_loading(scr_bpt, "Starting estimation");
    }

    if (k_sem_take(&sem_ppg_fi_hide_loading, K_NO_WAIT) == 0)
    {
        hpi_disp_hide_loading();
    }
}

static void st_display_active_run(void *o)
{
    struct hpi_ecg_bioz_sensor_data_t ecg_bioz_sensor_sample;
    struct hpi_ppg_wr_data_t ppg_sensor_sample;
    struct hpi_ppg_fi_data_t ppg_fi_sensor_sample;
    // LOG_DBG("Display SM Active Run");

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
            hpi_temp_disp_update_temp_f((double)m_disp_temp);
            last_temp_trend_refresh = k_uptime_get_32();
        }
        break;
    case SCR_HR:
        hpi_disp_hr_update_hr(m_disp_hr, m_disp_hr_last_update_tm);
        break;
    case SCR_SPL_HR_SCR2:
        if ((k_uptime_get_32() - last_hr_trend_refresh) > HPI_DISP_TRENDS_REFRESH_INT)
        {
            //hpi_disp_hr_load_trend();
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
        // st_disp_do_bpt_stuff();
        break;
    case SCR_SPL_ECG_SCR2:
        hpi_ecg_disp_update_hr(m_disp_ecg_hr);
        hpi_ecg_disp_update_timer(m_disp_ecg_timer);
        if (k_sem_take(&sem_ecg_complete, K_NO_WAIT) == 0)
        {
            hpi_move_load_scr_spl(SCR_SPL_ECG_COMPLETE, SCROLL_DOWN, SCR_SPL_PLOT_ECG);
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
            hpi_move_load_screen(SCR_ECG, SCROLL_UP);
        }
        break;
    case SCR_TODAY:
        if ((k_uptime_get_32() - last_today_trend_refresh) > HPI_DISP_TODAY_REFRESH_INT)
        {
            hpi_scr_today_update_all(m_disp_steps, m_disp_kcals, m_disp_active_time_s);
            last_today_trend_refresh = k_uptime_get_32();
        }
        break;
    case SCR_SPL_SETTINGS:
        if (k_uptime_get_32() - last_settings_refresh > HPI_DISP_SETTINGS_REFRESH_INT)
        {
            // hpi_disp_settings_update_time_date(m_disp_sys_time);
            last_settings_refresh = k_uptime_get_32();
        }
        break;
    default:
        break;
    }

    if (k_uptime_get_32() - last_batt_refresh > HPI_DISP_BATT_REFR_INT)
    {
        if ((hpi_disp_get_curr_screen() == SCR_HOME))
        {
            // hpi_disp_update_batt_level(m_disp_batt_level, m_disp_batt_charging);
            hpi_disp_home_update_batt_level(m_disp_batt_level, m_disp_batt_charging);
        }
        else if (hpi_disp_get_curr_screen() == SCR_SPL_SETTINGS)
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
        else
        {
            // hdr_time_display_update(m_disp_sys_time);
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
            k_sem_give(&sem_ecg_cancel);
            hpi_move_load_screen(SCR_HOME, SCROLL_NONE);
        }
        else if (hpi_disp_get_curr_screen() == SCR_SPL_SPO2_SCR3)
        {
            k_sem_give(&sem_stop_one_shot_spo2);
            hpi_move_load_screen(SCR_HOME, SCROLL_NONE);
        }
        else
        {
            hpi_move_load_screen(SCR_HOME, SCROLL_NONE);
        }
    }

    int inactivity_time = lv_disp_get_inactive_time(NULL);
    // LOG_DBG("Inactivity Time: %d", inactivity_time);
    if (inactivity_time > DISP_SLEEP_TIME_MS)
    {
        // hpi_display_sleep_on();
        smf_set_state(SMF_CTX(&s_disp_obj), &display_states[HPI_DISPLAY_STATE_SLEEP]);
    }
    else
    {
        // hpi_display_sleep_off();
    }
    //}
}

static void st_display_active_exit(void *o)
{
    LOG_DBG("Display SM Active Exit");
}

static void st_display_sleep_entry(void *o)
{
    LOG_DBG("Display SM Sleep Entry");
    hpi_display_sleep_on();
}

static void st_display_sleep_run(void *o)
{
    // LOG_DBG("Display SM Sleep Run");
    int inactivity_time = lv_disp_get_inactive_time(NULL);
    // LOG_DBG("Inactivity Time: %d", inactivity_time);
    if (inactivity_time < DISP_SLEEP_TIME_MS)
    {
        // hpi_display_sleep_on();
        smf_set_state(SMF_CTX(&s_disp_obj), &display_states[HPI_DISPLAY_STATE_ACTIVE]);
    }
    else
    {
        // hpi_display_sleep_off();
    }
}

static void st_display_sleep_exit(void *o)
{
    LOG_DBG("Display SM Sleep Exit");
    hpi_display_sleep_off();
}

static void st_display_on_entry(void *o)
{
    LOG_DBG("Display SM On Entry");
}

static const struct smf_state display_states[] = {
    [HPI_DISPLAY_STATE_INIT] = SMF_CREATE_STATE(st_display_init_entry, NULL, NULL, NULL, NULL),
    [HPI_DISPLAY_STATE_SPLASH] = SMF_CREATE_STATE(st_display_splash_entry, st_display_splash_run, NULL, NULL, NULL),
    [HPI_DISPLAY_STATE_BOOT] = SMF_CREATE_STATE(st_display_boot_entry, st_display_boot_run, st_display_boot_exit, NULL, NULL),
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
        k_msleep(lv_task_handler());
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
    m_disp_hr_last_update_tm = hpi_hr->time_tm;
    LOG_INF("ZB HR: %d at %02d:%02d", hpi_hr->hr, hpi_hr->time_tm.tm_hour, hpi_hr->time_tm.tm_min);
}
ZBUS_LISTENER_DEFINE(disp_hr_lis, disp_hr_listener);

static void disp_spo2_listener(const struct zbus_channel *chan)
{
    const struct hpi_spo2_point_t *hpi_spo2 = zbus_chan_const_msg(chan);
    m_disp_spo2 = hpi_spo2->spo2;
    m_disp_spo2_last_refresh_ts = hpi_spo2->timestamp;
    LOG_DBG("ZB Spo2: %d | Time: %d", hpi_spo2->spo2, hpi_spo2->timestamp);
}
ZBUS_LISTENER_DEFINE(disp_spo2_lis, disp_spo2_listener);

static void disp_steps_listener(const struct zbus_channel *chan)
{
    const struct hpi_steps_t *hpi_steps = zbus_chan_const_msg(chan);
    m_disp_steps = hpi_steps->steps_walk;

    m_disp_kcals = hpi_get_kcals_from_steps(m_disp_steps);

    // LOG_DBG("ZB Steps Walk : %d | Run: %d", hpi_steps->steps_walk, hpi_steps->steps_run);

    // ui_steps_button_update(hpi_steps->steps_walk);
}
ZBUS_LISTENER_DEFINE(disp_steps_lis, disp_steps_listener);

static void disp_temp_listener(const struct zbus_channel *chan)
{
    const struct hpi_temp_t *hpi_temp = zbus_chan_const_msg(chan);
    m_disp_temp = hpi_temp->temp_f;
    // printk("ZB Temp: %.2f\n", hpi_temp->temp_f);
}
ZBUS_LISTENER_DEFINE(disp_temp_lis, disp_temp_listener);

static void disp_bpt_listener(const struct zbus_channel *chan)
{
    const struct hpi_bpt_t *hpi_bpt = zbus_chan_const_msg(chan);
    m_disp_bp_sys = hpi_bpt->sys;
    m_disp_bp_dia = hpi_bpt->dia;
    m_disp_bpt_status = hpi_bpt->status;
    m_disp_bpt_progress = hpi_bpt->progress;
    printk("ZB BPT Status: %d Progress: %d\n", hpi_bpt->status, hpi_bpt->progress);
    printk("ZB BPT: %d / %d\n", hpi_bpt->sys, hpi_bpt->dia);
    // hpi_disp_update_bp(hpi_bpt->sys, hpi_bpt->dia);
}
ZBUS_LISTENER_DEFINE(disp_bpt_lis, disp_bpt_listener);

static void disp_ecg_timer_listener(const struct zbus_channel *chan)
{
    const struct hpi_ecg_timer_t *ecg_timer = zbus_chan_const_msg(chan);
    m_disp_ecg_timer = ecg_timer->timer_val;
}
ZBUS_LISTENER_DEFINE(disp_ecg_timer_lis, disp_ecg_timer_listener);

static void disp_ecg_hr_listener(const struct zbus_channel *chan)
{
    const uint16_t *ecg_hr = zbus_chan_const_msg(chan);
    m_disp_ecg_hr = *ecg_hr;
    // LOG_INF("ZB ECG HR: %d", *ecg_hr);
}
ZBUS_LISTENER_DEFINE(disp_ecg_hr_lis, disp_ecg_hr_listener);

#define SMF_DISPLAY_THREAD_STACK_SIZE 32768
#define SMF_DISPLAY_THREAD_PRIORITY 5

K_THREAD_DEFINE(smf_display_thread_id, SMF_DISPLAY_THREAD_STACK_SIZE, smf_display_thread, NULL, NULL, NULL, SMF_DISPLAY_THREAD_PRIORITY, 0, 0);