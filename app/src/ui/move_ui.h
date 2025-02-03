#pragma once

#include <lvgl.h>
#include <zephyr/drivers/rtc.h>

// Settings

#define SAMPLE_RATE 125
#define SCREEN_TRANS_TIME 300
#define HPI_DEFAULT_DISP_THREAD_REFRESH_INT_MS 2

#define DISP_SLEEP_TIME_MS 60000
#define DISPLAY_DEFAULT_BRIGHTNESS 50

#define DISP_WINDOW_SIZE_EDA 250
#define PPG_DISP_WINDOW_SIZE 128 // To be verified
#define HRV_DISP_WINDOW_SIZE 128
#define ECG_DISP_WINDOW_SIZE 512 // SAMPLE_RATE * 4

#define HPI_DISP_TIME_REFR_INT 1000
#define HPI_DISP_BATT_REFR_INT 1000
#define HPI_DISP_HR_REFR_INT 1000

struct hpi_boot_msg_t
{
    char msg[15];
    bool status;
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
    SCR_PLOT_PPG,
    SCR_PLOT_ECG,
    SCR_TEMP,
    SCR_PLOT_EDA,
    SCR_BPT,

    SCR_PLOT_HRV,
    SCR_PLOT_HRV_SCATTER,

    SCR_LIST_END,
    // Should not go here
    SCR_CLOCK,
    SCR_VITALS,
    // SCR_BPT_HOME,
};

// Special screens
#define SCR_SPL_SETTINGS 20
#define SCR_SPL_BOOT 21

enum hpi_disp_subscreens
{
    SUBSCR_BPT_CALIBRATE,
    SUBSCR_BPT_MEASURE,
};

void display_init_styles(void);

void hpi_display_sleep_off(void);
void hpi_display_sleep_on(void);

/******** UI Function Prototypes ********/

void draw_scr_boot(void);
void scr_boot_add_status(char *dev_label, bool status);
void scr_boot_add_final(bool status);

void draw_scr_splash(void);

// Clock Screen functions
void draw_scr_clockface(enum scroll_dir m_scroll_dir);
void draw_scr_clock_small(enum scroll_dir m_scroll_dir);

// Home Screen functions
void draw_scr_home(enum scroll_dir m_scroll_dir);
void ui_home_time_display_update(struct rtc_time in_time);
void hpi_home_hr_update(int hr);

void ui_time_display_update(uint8_t hour, uint8_t min, bool small);
void ui_date_display_update(uint8_t day, uint8_t month, uint16_t year);
void ui_battery_update(uint8_t percent);

// Today Screen functions
void draw_scr_today(enum scroll_dir m_scroll_dir);

// HR Screen functions
void draw_scr_hr(enum scroll_dir m_scroll_dir);
void hpi_hr_disp_update_hr(uint16_t hr, uint16_t min, uint16_t max, uint16_t hr_mean);

// ECG Screen functions
void draw_scr_ecg(enum scroll_dir m_scroll_dir);
void hpi_ecg_disp_draw_plotECG(int32_t *data_ecg, int num_samples, bool ecg_lead_off);
void hpi_ecg_disp_update_hr(int hr);

// PPG screen functions
void hpi_disp_ppg_draw_plotPPG(struct hpi_ppg_sensor_data_t ppg_sensor_sample);
void hpi_ppg_disp_update_hr(int hr);
void hpi_ppg_disp_update_spo2(int spo2);
void hpi_ppg_disp_update_status(uint8_t status);

// EDA screen functions
void draw_scr_eda(enum scroll_dir m_scroll_dir);
void hpi_eda_disp_draw_plotEDA(int32_t *data_eda, int num_samples, bool eda_lead_off);

// BPT screen functions
// void draw_scr_bpt_calibrate(void);
void draw_scr_bpt(enum scroll_dir m_scroll_dir);
// void draw_scr_bpt_measure(void);
void hpi_disp_bpt_draw_plotPPG(int32_t *data_ppg, int num_samples);
void hpi_disp_bpt_update_progress(int progress);

// HRV screen functions
void draw_scr_hrv(enum scroll_dir m_scroll_dir);
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
void draw_header_minimal(lv_obj_t *parent, int top_offset);
void hpi_move_load_screen(enum hpi_disp_screens m_screen, enum scroll_dir m_scroll_dir);
void hpi_move_load_scr_settings(enum scroll_dir m_scroll_dir);

void hpi_disp_set_curr_screen(int screen);
int hpi_disp_get_curr_screen(void);

void hpi_disp_set_brightness(uint8_t brightness_percent);
uint8_t hpi_disp_get_brightness(void);

// Component objects
lv_obj_t *ui_hr_button_create(lv_obj_t *comp_parent);
void ui_hr_button_update(uint8_t hr_bpm);

lv_obj_t *ui_settings_button_create(lv_obj_t *comp_parent, int y_offset);

lv_obj_t *ui_spo2_button_create(lv_obj_t *comp_parent);
void ui_spo2_button_update(uint8_t spo2);

lv_obj_t *ui_dailymissiongroup_create(lv_obj_t *comp_parent);
void ui_dailymissiongroup_update(uint32_t steps_walk, uint32_t steps_run);

lv_obj_t *ui_steps_button_create(lv_obj_t *comp_parent);
void ui_steps_button_update(uint16_t steps);

void draw_bg(lv_obj_t *parent);

void draw_scr_ppg(enum scroll_dir m_scroll_dir);
void draw_scr_temp(enum scroll_dir m_scroll_dir);

void draw_scr_vitals_home(enum scroll_dir m_scroll_dir);

void hpi_disp_update_batt_level(int batt_level, bool charging);
void hpi_temp_disp_update_temp_f(float temp_f);

void hpi_show_screen(lv_obj_t *parent, enum scroll_dir m_scroll_dir);
void disp_screen_event(lv_event_t *e);

// LV_IMG_DECLARE(ui_img_flash_png);
// LV_IMG_DECLARE(ui_img_heart2_png);        // assets/heart2.png
// LV_IMG_DECLARE(ui_img_daily_mission_png); // assets/daily_mission.png

LV_IMG_DECLARE(img_heart_48px); // assets/heart2.png
LV_IMG_DECLARE(img_steps_48px);
LV_IMG_DECLARE(img_calories_48px);
LV_IMG_DECLARE(img_clock_48px);

// LV_FONT_DECLARE( ui_font_H1);
LV_FONT_DECLARE(ui_font_Number_big);
LV_FONT_DECLARE( ui_font_Number_extra);
// LV_FONT_DECLARE( ui_font_Subtitle);
// LV_FONT_DECLARE( ui_font_Title);
