#include <zephyr/kernel.h>
#include <zephyr/smf.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>
#include <zephyr/zbus/zbus.h>

#include <display_sh8601.h>
#include "sampling_module.h"
#include "hw_module.h"
#include "ui/move_ui.h"

LOG_MODULE_REGISTER(smf_display, LOG_LEVEL_DBG);

K_MSGQ_DEFINE(q_plot_ecg_bioz, sizeof(struct hpi_ecg_bioz_sensor_data_t), 64, 1);
K_MSGQ_DEFINE(q_plot_ppg, sizeof(struct hpi_ppg_sensor_data_t), 64, 1);
K_MSGQ_DEFINE(q_plot_hrv, sizeof(struct hpi_computed_hrv_t), 64, 1);

K_MSGQ_DEFINE(q_disp_boot_msg, sizeof(struct hpi_boot_msg_t), 4, 1);
static bool hpi_boot_all_passed = true;

static int last_batt_refresh = 0;
static int last_hr_refresh = 0;
static int last_time_refresh = 0;

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

static uint8_t m_disp_batt_level = 0;
static bool m_disp_batt_charging = false;

static struct rtc_time m_disp_sys_time;
static uint8_t m_disp_hr = 0;

extern int curr_screen;
int scr_ppg_hr_spo2_refresh_counter = 0;

// extern uint16_t disp_thread_refresh_int_ms;

// int batt_refresh_counter = 0;
// int hr_refresh_counter = 0;
// int time_refresh_counter = 0;
int m_disp_inact_refresh_counter = 0;

extern struct k_sem sem_crown_key_pressed;

static uint32_t splash_scr_start_time = 0;

// Externs
extern const struct device *display_dev;
extern const struct device *touch_dev;

extern struct k_sem sem_display_on;
extern struct k_sem sem_disp_boot_complete;
extern struct k_msgq q_ecg_bioz_sample;
extern struct k_msgq q_ppg_sample;
extern struct k_msgq q_plot_ecg_bioz;
extern struct k_msgq q_plot_ppg;
extern struct k_msgq q_plot_hrv;

K_SEM_DEFINE(sem_disp_ready, 0, 1);

struct s_disp_object
{
    struct smf_ctx ctx;

} s_disp_obj;

static void st_display_init_entry(void *o)
{
    LOG_DBG("Display SM Init Entry");

    printk("Disp ON");

    if (!device_is_ready(display_dev))
    {
        LOG_ERR("Device not ready");
        // return;
    }

    LOG_DBG("Display device: %s", display_dev->name);

    sh8601_reinit(display_dev);
    k_msleep(500);
    device_init(touch_dev);

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

static void st_display_active_entry(void *o)
{
    LOG_DBG("Display SM Active Entry");
    draw_scr_home(SCROLL_RIGHT);
}

static void hpi_disp_process_ppg_data(struct hpi_ppg_sensor_data_t ppg_sensor_sample)
{
    if (curr_screen == SCR_PLOT_PPG)
    {
        hpi_disp_ppg_draw_plotPPG(ppg_sensor_sample);
        hpi_ppg_disp_update_status(ppg_sensor_sample.scd_state);

        if (k_uptime_get_32() - hpi_scr_ppg_hr_spo2_last_refresh > 1000)
        {
            hpi_scr_ppg_hr_spo2_last_refresh = k_uptime_get_32();
            hpi_disp_update_batt_level(m_disp_batt_level, m_disp_batt_charging);
            // hpi_disp_update_hr(m_disp_hr);
            hpi_ppg_disp_update_hr(ppg_sensor_sample.hr);
            hpi_ppg_disp_update_spo2(ppg_sensor_sample.spo2);
        }
    }
    else if ((curr_screen == SCR_HOME)) // || (curr_screen == SCR_CLOCK_SMALL))
    {
    }
    else if (curr_screen == SCR_PLOT_HRV)
    {
        if (ppg_sensor_sample.rtor != 0) // && ppg_sensor_sample.rtor != prev_rtor)
        {
            // printk("RTOR: %d | SCD: %d", ppg_sensor_sample.rtor, ppg_sensor_sample.scd_state);
            hpi_disp_hrv_draw_plot_rtor((float)((ppg_sensor_sample.rtor)));
            hpi_disp_hrv_update_rtor(ppg_sensor_sample.rtor);
            // prev_rtor = ppg_sensor_sample.rtor;
        }
    }
    else if (curr_screen == SCR_PLOT_HRV_SCATTER)
    {
        if (ppg_sensor_sample.rtor != 0) //&& ppg_sensor_sample.rtor != prev_rtor)
        {
            // printk("RTOR: %d | SCD: %d", ppg_sensor_sample.rtor, ppg_sensor_sample.scd_state);
            // hpi_disp_hrv_scatter_draw_plot_rtor((float)((ppg_sensor_sample.rtor)), (float)prev_rtor);
            // hpi_disp_hrv_scatter_update_rtor(ppg_sensor_sample.rtor);
            // prev_rtor = ppg_sensor_sample.rtor;
        }
    }
    else if (curr_screen == SUBSCR_BPT_CALIBRATE)
    {

        // hpi_disp_bpt_draw_plotPPG(ppg_sensor_sample.raw_red, ppg_sensor_sample.bpt_status, ppg_sensor_sample.bpt_progress);
        //  hpi_disp_draw_plotPPG((float)(ppg_sensor_sample.raw_red * 1.0000));
        /*if (bpt_cal_done_flag == false)
        {
            if (bpt_cal_last_status != ppg_sensor_sample.bpt_status)
            {
                bpt_cal_last_status = ppg_sensor_sample.bpt_status;
                printk("BPT Status: %d", ppg_sensor_sample.bpt_status);
            }
            if (bpt_cal_last_progress != ppg_sensor_sample.bpt_progress)
            {
                bpt_cal_last_progress = ppg_sensor_sample.bpt_progress;
                hpi_disp_bpt_update_progress(ppg_sensor_sample.bpt_progress);
            }
            if (ppg_sensor_sample.bpt_progress == 100)
            {
                hw_bpt_stop();

                if (ppg_sensor_sample.bpt_status == 2)
                {
                    printk("Calibration done");
                }
                bpt_cal_done_flag = true;

                hw_bpt_get_calib();

                // ppg_data_stop();
            }
            hpi_disp_bpt_update_progress(ppg_sensor_sample.bpt_progress);
            lv_disp_trig_activity(NULL);
        }*/
    }
    /*
    if (curr_screen == SUBSCR_BPT_MEASURE)
    {
    if (k_msgq_get(&q_plot_ppg, &ppg_sensor_sample, K_NO_WAIT) == 0)
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

                    if (curr_screen == SUBSCR_BPT_MEASURE)
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
    if (curr_screen == SCR_PLOT_ECG)
    {
        //((float)((ecg_bioz_sensor_sample.ecg_sample / 1000.0000)), ecg_bioz_sensor_sample.ecg_lead_off);
        hpi_ecg_disp_draw_plotECG(ecg_bioz_sensor_sample.ecg_samples, ecg_bioz_sensor_sample.ecg_num_samples, ecg_bioz_sensor_sample.ecg_lead_off);
        hpi_ecg_disp_update_hr(ecg_bioz_sensor_sample.hr);
    }
    else if (curr_screen == SCR_PLOT_EDA)
    {
        hpi_eda_disp_draw_plotEDA(ecg_bioz_sensor_sample.bioz_sample, ecg_bioz_sensor_sample.bioz_num_samples, ecg_bioz_sensor_sample.bioz_lead_off);
    }
}

static void st_display_active_run(void *o)
{
    struct hpi_ecg_bioz_sensor_data_t ecg_bioz_sensor_sample;
    struct hpi_ppg_sensor_data_t ppg_sensor_sample;
    // LOG_DBG("Display SM Active Run");

    if (k_msgq_get(&q_plot_ppg, &ppg_sensor_sample, K_NO_WAIT) == 0)
    {
        hpi_disp_process_ppg_data(ppg_sensor_sample);
    }

    /*if (k_msgq_get(&q_plot_hrv, &hrv_sample, K_NO_WAIT) == 0)
    {
        if (curr_screen == SCR_PLOT_HRV)
        {
            hpi_disp_hrv_update_sdnn(hrv_sample.rmssd);
        }
        else if (curr_screen == SCR_PLOT_HRV_SCATTER)
        {
            hpi_disp_hrv_scatter_update_sdnn(hrv_sample.rmssd);
        }
    }*/

    if (k_msgq_get(&q_plot_ecg_bioz, &ecg_bioz_sensor_sample, K_NO_WAIT) == 0)
    {
        hpi_disp_process_ecg_bioz_data(ecg_bioz_sensor_sample);
    }

    if (curr_screen == SCR_HOME)
    {
        // ui_hr_button_update(m_disp_hr);

        // if (time_refresh_counter >= (1000 / disp_thread_refresh_int_ms))

        if (k_uptime_get_32() - last_time_refresh > HPI_DISP_TIME_REFR_INT)
        {
            ui_home_time_display_update(m_disp_sys_time);
            ui_hr_button_update(m_disp_hr);
        }
    }

    // if (batt_refresh_counter >= (1000 / disp_thread_refresh_int_ms))
    //{

    if (k_uptime_get_32() - last_batt_refresh > HPI_DISP_BATT_REFR_INT)
    {
        hpi_disp_update_batt_level(m_disp_batt_level, m_disp_batt_charging);
        last_batt_refresh = k_uptime_get_32();
    }

    // hpi_disp_update_batt_level(m_disp_batt_level, m_disp_batt_charging);
    //     batt_refresh_counter = 0;
    // }

    /*else
    {
        batt_refresh_counter++;
    }*/

    // Add button handlers
    /*if (k_sem_take(&sem_crown_key_pressed, K_NO_WAIT) == 0)
    {
        if (m_display_active == false)
        {
            lv_disp_trig_activity(NULL);
        }
        else
        {
            if (curr_screen == SCR_HOME)
            {
                hpi_display_sleep_on();
            }
            else
            {
                hpi_move_load_screen(SCR_HOME, SCROLL_NONE);
            }
        }
    }*/

    int inactivity_time = lv_disp_get_inactive_time(NULL);
    LOG_DBG("Inactivity Time: %d", inactivity_time);
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

    k_sem_take(&sem_display_on, K_FOREVER);

    LOG_DBG("Display SMF Thread Started");

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
        // k_msleep(100);
    }
}

static void disp_batt_status_listener(const struct zbus_channel *chan)
{
    const struct batt_status *batt_s = zbus_chan_const_msg(chan);

    // LOG_DBG("Ch Batt: %d, Charge: %d", batt_s->batt_level, batt_s->batt_charging);
    m_disp_batt_level = batt_s->batt_level;
    m_disp_batt_charging = batt_s->batt_charging;
}

ZBUS_LISTENER_DEFINE(disp_batt_lis, disp_batt_status_listener);

static void disp_sys_time_listener(const struct zbus_channel *chan)
{
    const struct rtc_time *sys_time = zbus_chan_const_msg(chan);
    m_disp_sys_time = *sys_time;
}
ZBUS_LISTENER_DEFINE(disp_sys_time_lis, disp_sys_time_listener);

static void disp_hr_listener(const struct zbus_channel *chan)
{
    const struct hpi_hr_t *hpi_hr = zbus_chan_const_msg(chan);
    m_disp_hr = hpi_hr->hr;
}
ZBUS_LISTENER_DEFINE(disp_hr_lis, disp_hr_listener);

static void disp_steps_listener(const struct zbus_channel *chan)
{
    const struct hpi_steps_t *hpi_steps = zbus_chan_const_msg(chan);
    if (curr_screen == SCR_HOME)
    {
        // ui_steps_button_update(hpi_steps->steps_walk);
    }
    // ui_steps_button_update(hpi_steps->steps_walk);
}
ZBUS_LISTENER_DEFINE(disp_steps_lis, disp_steps_listener);

static void disp_temp_listener(const struct zbus_channel *chan)
{
    const struct hpi_temp_t *hpi_temp = zbus_chan_const_msg(chan);
    // printk("ZB Temp: %.2f\n", hpi_temp->temp_f);
}
ZBUS_LISTENER_DEFINE(disp_temp_lis, disp_temp_listener);

#define SMF_DISPLAY_THREAD_STACK_SIZE 32768
#define SMF_DISPLAY_THREAD_PRIORITY 5

K_THREAD_DEFINE(smf_display_thread_id, SMF_DISPLAY_THREAD_STACK_SIZE, smf_display_thread, NULL, NULL, NULL, SMF_DISPLAY_THREAD_PRIORITY, 0, 0);