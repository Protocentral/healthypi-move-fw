#pragma once

#include <lvgl.h>
#include <zephyr/drivers/rtc.h>

#include "hpi_common_types.h"

// Settings

#define SAMPLE_RATE 125
#define SCREEN_TRANS_TIME 00
#define HPI_DEFAULT_DISP_THREAD_REFRESH_INT_MS 2

#define DISP_SLEEP_TIME_MS 10000
#define DISPLAY_DEFAULT_BRIGHTNESS 50

#define DISP_WINDOW_SIZE_EDA 250
#define PPG_DISP_WINDOW_SIZE 256 // To be verified
#define HRV_DISP_WINDOW_SIZE 128
#define ECG_DISP_WINDOW_SIZE 256 // SAMPLE_RATE * 4

#define BPT_DISP_WINDOW_SIZE 256
#define SPO2_DISP_WINDOW_SIZE 128

#define HPI_DISP_TIME_REFR_INT 1000
#define HPI_DISP_BATT_REFR_INT 1000

#define HPI_DISP_TODAY_REFRESH_INT 3000
#define HPI_DISP_BPT_REFRESH_INT 3000
#define HPI_DISP_TEMP_REFRESH_INT 3000
#define HPI_DISP_SETTINGS_REFRESH_INT 1000
#define HPI_DISP_TRENDS_REFRESH_INT 20000

struct hpi_boot_msg_t
{
    char msg[25];
    bool status;
    bool show_status;
    bool show_progress;
    uint8_t progress;
};

enum scroll_dir
{
    SCROLL_UP,
    SCROLL_DOWN,
    SCROLL_LEFT,
    SCROLL_RIGHT,
    SCROLL_NONE,
};

enum hpi_disp_screens
{
    SCR_LIST_START,

    SCR_HOME,
    SCR_TODAY,
    SCR_HR,
    SCR_SPO2,
    SCR_ECG,
    SCR_TEMP,
    SCR_BPT,

    SCR_LIST_END,
    // Should not go here
    
};

// Special screens
enum hpi_disp_spl_screens
{
    SCR_SPL_LIST_START = 50,

    SCR_SPL_BOOT,
    SCR_SPL_SETTINGS,
    SCR_SPL_RAW_PPG,
    SCR_SPL_ECG_SCR2,
    SCR_SPL_ECG_COMPLETE,
    SCR_SPL_PLOT_ECG,

    SCR_SPL_FI_SENS_CHECK,
    SCR_SPL_BPT_MEASURE,
    SCR_SPL_BPT_CAL_COMPLETE,
    SCR_SPL_BPT_CAL_PROGRESS,
    SCR_SPL_BPT_EST_COMPLETE,
    SCR_SPL_BPT_FAILED,
    SCR_SPL_BPT_CAL_REQUIRED,

    SCR_SPL_FI_SENS_WEAR,

    SCR_SPL_PLOT_HRV,
    SCR_SPL_SPO2_SELECT,
    SCR_SPL_SPO2_SCR2,
    SCR_SPL_SPO2_MEASURE,
    SCR_SPL_SPO2_COMPLETE,
    SCR_SPL_SPO2_TIMEOUT,
    SCR_SPL_HR_SCR2,

    SCR_SPL_PROGRESS,

    SCR_SPL_LIST_END,

    SCR_SPL_BLE,
};

enum hpi_disp_subscreens
{
    SUBSCR_BPT_CALIBRATE,
    SUBSCR_BPT_MEASURE,
};

// Images used in the UI
LV_IMG_DECLARE(img_heart_48px); // assets/heart2.png
LV_IMG_DECLARE(img_heart_35);
LV_IMG_DECLARE(img_steps_48);
LV_IMG_DECLARE(img_calories_48);
LV_IMG_DECLARE(img_timer_48);
LV_IMG_DECLARE(ecg_70);
LV_IMG_DECLARE(bp_70);
//LV_IMG_DECLARE(icon_spo2_30x35);
LV_IMG_DECLARE(img_heart_70);
LV_IMG_DECLARE(img_temp_100);
LV_IMG_DECLARE(icon_spo2_100);
LV_IMG_DECLARE(img_spo2_hand);

LV_IMG_DECLARE(img_complete_85);
LV_IMG_DECLARE(img_failed_80);

LV_IMG_DECLARE(img_bpt_finger_90);

LV_IMG_DECLARE(img_bpt_finger_45);
LV_IMG_DECLARE(img_wrist_45);

//LV_IMG_DECLARE(pc_move_bg_200);
LV_IMG_DECLARE(bck_heart_200);

// LV_FONT_DECLARE( ui_font_H1);
LV_FONT_DECLARE(oxanium_90);
//LV_FONT_DECLARE(fredoka_28);
LV_FONT_DECLARE(ui_font_Number_big);
LV_FONT_DECLARE(ui_font_Number_extra);
//LV_FONT_DECLARE(orbitron_90px);

/******** UI Function Prototypes ********/
void display_init_styles(void);

// Boot Screen functions
void draw_scr_splash(void);
void draw_scr_boot(void);
void scr_boot_add_status(char *dev_label, bool status, bool show_status);
void scr_boot_add_final(bool status);

// Progress Screen functions
void draw_scr_progress(char *title, char *message);
void hpi_disp_scr_update_progress(int progress, char *status);

// Clock Screen functions
void draw_scr_clockface(enum scroll_dir m_scroll_dir);
void draw_scr_clock_small(enum scroll_dir m_scroll_dir);

// Home Screen functions
void draw_scr_home(enum scroll_dir m_scroll_dir);
void hpi_scr_home_update_time_date(struct tm in_time);
void hpi_home_hr_update(int hr);

// Today Screen functions
void draw_scr_today(enum scroll_dir m_scroll_dir);
void hpi_scr_today_update_all(uint16_t steps, uint16_t kcals, uint16_t active_time_s);

// HR Screen functions
void draw_scr_hr(enum scroll_dir m_scroll_dir);
void hpi_disp_hr_update_hr(uint16_t hr, int64_t last_update_ts);
void hpi_disp_hr_load_trend(void);
void draw_scr_hr_scr2(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);

// Spo2 Screen functions
void draw_scr_spo2(enum scroll_dir m_scroll_dir);
void draw_scr_spo2_scr3(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void draw_scr_spo2_scr2(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void hpi_disp_update_spo2(uint8_t spo2, int64_t ts_last_update);
void draw_scr_spo2_select(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);

int hpi_disp_reset_all_last_updated(void);

void hpi_disp_spo2_load_trend(void);
void hpi_disp_spo2_plot_wrist_ppg(struct hpi_ppg_wr_data_t ppg_sensor_sample);
void hpi_disp_spo2_plot_fi_ppg(struct hpi_ppg_fi_data_t ppg_sensor_sample);

void hpi_disp_spo2_update_progress(int progress, enum spo2_meas_state state, int spo2, int hr);
void hpi_disp_spo2_update_hr(int hr);
void draw_scr_spl_spo2_complete(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void draw_scr_spl_spo2_timeout(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void draw_scr_spo2_measure(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);

// ECG Screen functions
void draw_scr_ecg(enum scroll_dir m_scroll_dir);
void hpi_ecg_disp_draw_plotECG(int32_t *data_ecg, int num_samples, bool ecg_lead_off);
void hpi_ecg_disp_update_hr(int hr);
void hpi_ecg_disp_update_timer(int timer);
void draw_scr_ecg_complete(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void draw_scr_ecg_scr2(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void scr_ecg_lead_on_off_handler(bool lead_on_off);

void gesture_down_scr_spl_raw_ppg(void);
void gesture_down_scr_ecg_2(void);
void gesture_down_scr_fi_sens_wear(void);
void gesture_down_scr_fi_sens_check(void);
void gesture_down_scr_bpt_measure(void);
void gesture_down_scr_bpt_cal_complete(void);
void gesture_down_scr_ecg_complete(void);
void gesture_down_scr_hrv(void);
void gesture_down_scr_hr_scr2(void);
void gesture_down_scr_spo2_scr2(void);
void gesture_down_scr_spo2_measure(void);
void gesture_down_scr_spl_spo2_complete(void);
void gesture_down_scr_spl_spo2_timeout(void);
void gesture_down_scr_spo2_select(void);
void gesture_down_scr_bpt_cal_progress(void);
void gesture_down_scr_bpt_cal_failed(void);
void gesture_down_scr_bpt_est_complete(void);
void gesture_down_scr_ble(void);
void gesture_down_scr_settings(void);
void gesture_down_scr_bpt_cal_required(void);

// PPG screen functions
void hpi_disp_ppg_draw_plotPPG(struct hpi_ppg_wr_data_t ppg_sensor_sample);
void hpi_ppg_disp_update_hr(int hr);

// EDA screen functions
void draw_scr_pre(enum scroll_dir m_scroll_dir);
void hpi_eda_disp_draw_plotEDA(int32_t *data_eda, int num_samples, bool eda_lead_off);

// BPT screen functions
// void draw_scr_bpt_calibrate(void);
void draw_scr_bpt(enum scroll_dir m_scroll_dir);
// void draw_scr_bpt_measure(void);
void hpi_disp_bpt_draw_plotPPG(struct hpi_ppg_fi_data_t ppg_sensor_sample);
void hpi_disp_bpt_update_progress(int progress);
void draw_scr_fi_sens_check(enum scroll_dir dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void draw_scr_fi_sens_wear(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void draw_scr_bpt_measure(enum scroll_dir dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void draw_scr_bpt_cal_required(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);

// HRV screen functions
void draw_scr_hrv(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void hpi_disp_hrv_draw_plot_rtor(float rtor);
void hpi_disp_hrv_update_rtor(int rtor);
void hpi_disp_hrv_update_sdnn(int sdnn);

// HRV Scatter screen functions
void draw_scr_hrv_scatter(enum scroll_dir m_scroll_dir);
void hpi_disp_hrv_scatter_draw_plot_rtor(float rtor, float prev_rtor);
void hpi_disp_hrv_scatter_update_rtor(int rtor);
void hpi_disp_hrv_scatter_update_sdnn(int sdnn);

// Settings screen functions
void draw_scr_settings(enum scroll_dir m_scroll_dir);

// Helper objects
void draw_scr_common(lv_obj_t *parent);
void hpi_load_screen(int m_screen, enum scroll_dir m_scroll_dir);
void hpi_load_scr_spl(int m_screen, enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);

void hpi_move_load_scr_settings(enum scroll_dir m_scroll_dir);

void hpi_disp_set_curr_screen(int screen);
int hpi_disp_get_curr_screen(void);

void hpi_disp_set_brightness(uint8_t brightness_percent);
uint8_t hpi_disp_get_brightness(void);

// Component objects
lv_obj_t *ui_hr_button_create(lv_obj_t *comp_parent);
void ui_hr_button_update(uint8_t hr_bpm);

lv_obj_t *ui_spo2_button_create(lv_obj_t *comp_parent);
void ui_spo2_button_update(uint8_t spo2);


lv_obj_t *ui_steps_button_create(lv_obj_t *comp_parent);
void ui_steps_button_update(uint16_t steps);

void draw_bg(lv_obj_t *parent);

// Draw special screens
void draw_scr_spl_raw_ppg(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void draw_scr_spl_plot_ecg(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);

void draw_scr_temp(enum scroll_dir m_scroll_dir);

void hpi_disp_home_update_batt_level(int batt_level, bool charging);
void hpi_disp_settings_update_batt_level(int batt_level, bool charging);

void hpi_temp_disp_update_temp_f(double temp_f, int64_t temp_f_last_update);

void hpi_show_screen(lv_obj_t *parent, enum scroll_dir m_scroll_dir);
void hpi_show_screen_spl(lv_obj_t *m_screen, enum scroll_dir m_scroll_dir);

void draw_scr_bpt_cal_complete(enum scroll_dir dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void draw_scr_bpt_cal_progress(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void draw_scr_bpt_cal_failed(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void draw_scr_bpt_est_complete(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);

void draw_scr_ble(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);

void disp_screen_event(lv_event_t *e);