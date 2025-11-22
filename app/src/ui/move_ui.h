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

// Modern AMOLED-optimized color palette
#define COLOR_SURFACE_DARK    0x1C1C1E
#define COLOR_SURFACE_MEDIUM  0x2C2C2E
#define COLOR_SURFACE_LIGHT   0x3C3C3E
#define COLOR_PRIMARY_BLUE    0x007AFF
#define COLOR_SUCCESS_GREEN   0x34C759
#define COLOR_WARNING_AMBER   0xFF9500
#define COLOR_CRITICAL_RED    0xFF3B30
#define COLOR_TEXT_SECONDARY  0xE5E5E7

#define DISP_WINDOW_SIZE_EDA 250
#define PPG_DISP_WINDOW_SIZE 256 // To be verified
#define HRV_DISP_WINDOW_SIZE 128
#define ECG_DISP_WINDOW_SIZE 512 // SAMPLE_RATE * 8 - Increased for more ECG history
#define GSR_DISP_WINDOW_SIZE 256

#define BPT_DISP_WINDOW_SIZE 256
#define SPO2_DISP_WINDOW_SIZE_FI 128
#define SPO2_DISP_WINDOW_SIZE_WR 64

#define HPI_DISP_TIME_REFR_INT 1000
#define HPI_DISP_BATT_REFR_INT 1000

// Battery level thresholds for display symbols
#define HPI_BATTERY_LEVEL_FULL     90
#define HPI_BATTERY_LEVEL_HIGH     65
#define HPI_BATTERY_LEVEL_MEDIUM   35
#define HPI_BATTERY_LEVEL_LOW      15
#define HPI_BATTERY_LEVEL_CRITICAL 10

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
#if defined(CONFIG_HPI_TODAY_SCREEN)
    SCR_TODAY,
#endif
    SCR_HR,
    SCR_SPO2,
    SCR_ECG,
    SCR_TEMP,
    SCR_BPT,

    //SCR_SPL_PLOT_HRV,
    SCR_HRV,
    SCR_HRV_PPG_PLOT,
    SCR_GSR,

    SCR_LIST_END,
    // Should not go here
    
};

// Special screens
enum hpi_disp_spl_screens
{
    SCR_SPL_LIST_START = 50,

    SCR_SPL_BOOT,
    SCR_SPL_PULLDOWN,
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

   
    SCR_SPL_HRV_FREQUENCY,
    SCR_SPL_SPO2_SELECT,
    SCR_SPL_SPO2_SCR2,
    SCR_SPL_SPO2_MEASURE,
    SCR_SPL_SPO2_COMPLETE,
    SCR_SPL_SPO2_TIMEOUT,
    SCR_SPL_SPO2_CANCELLED,
    SCR_SPL_HR_SCR2,
    SCR_SPL_PLOT_GSR,
    SCR_SPL_GSR_COMPLETE,

    SCR_SPL_PROGRESS,
    SCR_SPL_LOW_BATTERY,
    SCR_SPL_DEVICE_USER_SETTINGS,
    SCR_SPL_HEIGHT_SELECT,
    SCR_SPL_WEIGHT_SELECT,
    SCR_SPL_HAND_WORN_SELECT,
    SCR_SPL_TIME_FORMAT_SELECT,
    SCR_SPL_TEMP_UNIT_SELECT,
    SCR_SPL_SLEEP_TIMEOUT_SELECT,
    //SCR_SPL_HRV_PLOT,
    SCR_SPL_LIST_END,
    SCR_SPL_PLOT_HRV,

    SCR_SPL_BLE,
};

enum hpi_disp_subscreens
{
    SUBSCR_BPT_CALIBRATE,
    SUBSCR_BPT_MEASURE,
};

// Images used in the UI
LV_IMG_DECLARE(img_heart_48px); // assets/heart2.png
LV_IMG_DECLARE(img_steps_48);
LV_IMG_DECLARE(img_calories_48);
LV_IMG_DECLARE(img_timer_48);
LV_IMG_DECLARE(ecg_70);
LV_IMG_DECLARE(ecg_45);        // 45x45 ECG icon for circular display
LV_IMG_DECLARE(bp_70);
LV_IMG_DECLARE(icon_spo2_30x35);

#if defined(CONFIG_HPI_GSR_SCREEN)
// GSR screens and helpers
void draw_scr_gsr(enum scroll_dir m_scroll_dir);
void hpi_gsr_disp_update_gsr_int(uint16_t gsr_value_x100, int64_t gsr_last_update);
// Special GSR plot screen
void draw_scr_gsr_plot(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void unload_scr_gsr_plot(void);
// GSR complete screen
void draw_scr_gsr_complete(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void unload_scr_gsr_complete(void);
void hpi_gsr_complete_update_results(const struct hpi_gsr_stress_index_t *results);
// Plot update helper called from sensor path
void hpi_gsr_disp_plot_add_sample(uint16_t gsr_value_x100);
void hpi_gsr_disp_draw_plotGSR(int32_t *data_gsr, int num_samples, bool gsr_lead_off);
void hpi_gsr_disp_update_timer(uint16_t remaining_s);
#else
// Stubs when GSR is disabled
static inline void draw_scr_gsr(enum scroll_dir m_scroll_dir) { ARG_UNUSED(m_scroll_dir); }
static inline void hpi_gsr_disp_update_gsr_int(uint16_t a, int64_t b) { ARG_UNUSED(a); ARG_UNUSED(b); }
static inline void hpi_gsr_process_bioz_sample(int32_t s) { ARG_UNUSED(s); }
static inline void draw_scr_gsr_plot(enum scroll_dir m_scroll_dir, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4) {
    ARG_UNUSED(m_scroll_dir); ARG_UNUSED(a1); ARG_UNUSED(a2); ARG_UNUSED(a3); ARG_UNUSED(a4);
}
static inline void draw_scr_gsr_complete(enum scroll_dir m_scroll_dir, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4) {
    ARG_UNUSED(m_scroll_dir); ARG_UNUSED(a1); ARG_UNUSED(a2); ARG_UNUSED(a3); ARG_UNUSED(a4);
}
static inline void unload_scr_gsr_complete(void) { }
static inline void hpi_gsr_complete_update_results(const struct hpi_gsr_stress_index_t *r) { ARG_UNUSED(r); }
static inline void hpi_gsr_disp_plot_add_sample(uint16_t v) { ARG_UNUSED(v); }
#endif
LV_IMG_DECLARE(img_heart_70);
LV_IMG_DECLARE(hpi_logo_90x92);
LV_IMG_DECLARE(img_temp_100);
LV_IMG_DECLARE(img_temp_45);   // 45x45 temperature icon for circular display
LV_IMG_DECLARE(img_battery_charged);
LV_IMG_DECLARE(img_battery_discharging);
LV_IMG_DECLARE(icon_spo2_100);
LV_IMG_DECLARE(img_spo2_hand);

LV_IMG_DECLARE(img_complete_85);
LV_IMG_DECLARE(img_failed_80);
LV_IMG_DECLARE(img_bpt_finger_90);
LV_IMG_DECLARE(img_bpt_finger_45);
LV_IMG_DECLARE(img_wrist_45);
LV_IMG_DECLARE(bck_heart_2_180);
LV_IMG_DECLARE(low_batt_100);

LV_FONT_DECLARE(oxanium_90);
LV_FONT_DECLARE(ui_font_number_big);

/* Modern Google Fonts for AMOLED Display */
// Core System Fonts
LV_FONT_DECLARE(inter_semibold_24);       // General UI text (upgraded to semibold for better readability)
LV_FONT_DECLARE(inter_semibold_18);       // Legacy 18px font - kept for compatibility but styles now use 24px minimum
LV_FONT_DECLARE(inter_semibold_80_time);  // Large minimalist time display (80px, digits only)
//LV_FONT_DECLARE(jetbrains_mono_regular_16); // Time display, sensor readings
// Additional Sizes
LV_FONT_DECLARE(inter_regular_16);        // Secondary size for specific contexts  
LV_FONT_DECLARE(inter_semibold_24); // Large time display

/* Modern style declarations */
extern lv_style_t style_health_arc;
extern lv_style_t style_health_arc_bg;
extern lv_style_t style_headline;
extern lv_style_t style_body_large;
extern lv_style_t style_body_medium;
extern lv_style_t style_caption;
/* Additional specialized styles */
extern lv_style_t style_numeric_large;  // For large numeric displays (time, main values)
extern lv_style_t style_numeric_medium; // For medium numeric displays
extern lv_style_t style_status_small;   // For small status text


/******** UI Function Prototypes ********/
void display_init_styles(void);
void hpi_ui_styles_init(void);
lv_obj_t *hpi_btn_create(lv_obj_t *parent);

/* Modern button creation helpers */
lv_obj_t *hpi_btn_create_primary(lv_obj_t *parent);
lv_obj_t *hpi_btn_create_secondary(lv_obj_t *parent);
lv_obj_t *hpi_btn_create_icon(lv_obj_t *parent);

// Boot Screen functions
void draw_scr_splash(void);
void draw_scr_boot(void);
void scr_boot_add_status(const char *dev_label, bool status, bool show_status);
void scr_boot_add_final(bool status);

// Progress Screen functions
void draw_scr_progress(const char *title, const char *message);
void hpi_disp_scr_update_progress(int progress, const char *status);
void hpi_disp_scr_show_error(const char *error_message);
void hpi_disp_scr_reset_progress(void);
void hpi_disp_scr_debug_status(void);

// Clock Screen functions
void draw_scr_clockface(enum scroll_dir m_scroll_dir);
void draw_scr_clock_small(enum scroll_dir m_scroll_dir);

// Home Screen functions
void draw_scr_home(enum scroll_dir m_scroll_dir);
void hpi_scr_home_update_time_date(struct tm in_time);
void hpi_home_hr_update(int hr);
void hpi_home_steps_update(int steps);

#if defined(CONFIG_HPI_TODAY_SCREEN)
// Today Screen functions
void draw_scr_today(enum scroll_dir m_scroll_dir);
void hpi_scr_today_update_all(uint16_t steps, uint16_t kcals, uint16_t active_time_s);
#endif

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
void draw_scr_spl_spo2_cancelled(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void draw_scr_spl_low_battery(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void draw_scr_spo2_measure(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);

// ECG Screen functions
void draw_scr_ecg(enum scroll_dir m_scroll_dir);
void hpi_ecg_disp_draw_plotECG(int32_t *data_ecg, int num_samples, bool ecg_lead_off);
void hpi_ecg_disp_update_hr(int hr);
void hpi_ecg_disp_update_timer(int timer);
void draw_scr_ecg_complete(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void draw_scr_ecg_scr2(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void scr_ecg_lead_on_off_handler(bool lead_on_off);

// ECG Timer control functions for lead-based automatic start/stop
void hpi_ecg_timer_start(void);
void hpi_ecg_timer_pause(void);
void hpi_ecg_timer_reset(void);
bool hpi_ecg_timer_is_running(void);
void hpi_ecg_reset_countdown_timer(void);

void gesture_down_scr_spl_raw_ppg(void);
void gesture_down_scr_ecg_2(void);
void gesture_down_scr_gsr_plot(void);
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
void gesture_down_scr_spl_spo2_cancelled(void);
void gesture_down_scr_spl_low_battery(void);
void gesture_down_scr_spo2_select(void);
void gesture_down_scr_bpt_cal_progress(void);
void gesture_down_scr_bpt_cal_failed(void);
void gesture_down_scr_bpt_est_complete(void);
void gesture_down_scr_ble(void);
void gesture_down_scr_pulldown(void);
void gesture_down_scr_bpt_cal_required(void);

// PPG screen functions
void hpi_disp_ppg_draw_plotPPG(struct hpi_ppg_wr_data_t ppg_sensor_sample);
void hpi_ppg_disp_update_hr(int hr);
void hpi_ppg_check_signal_timeout(void);  // Check for signal timeout periodically

/* Shared autoscale helper for PPG/LVGL charts.
 * chart: LVGL chart object
 * y_min_ppg, y_max_ppg: pointers to tracked min/max values
 * gx: pointer to sample counter used to decide when to rescale
 * disp_window_size: window size constant used to determine threshold
 */
void hpi_ppg_disp_do_set_scale_shared(lv_obj_t *chart, float *y_min_ppg, float *y_max_ppg, float *gx, int disp_window_size);

/* Reset autoscale state - call when initializing a screen that uses autoscaling */
void hpi_ppg_autoscale_reset(void);

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
void draw_scr_hrv(enum scroll_dir m_scroll_dir);
void hpi_disp_hrv_draw_plot_rtor(float rtor);
void hpi_disp_hrv_update_rtor(int rtor);
void hpi_disp_hrv_update_sdnn(int sdnn);

// HRV Scatter screen functions
void draw_scr_hrv_scatter(enum scroll_dir m_scroll_dir);
void hpi_disp_hrv_scatter_draw_plot_rtor(float rtor, float prev_rtor);
void hpi_disp_hrv_scatter_update_rtor(int rtor);
void hpi_disp_hrv_scatter_update_sdnn(int sdnn);

// HRV Summary screen functions
void draw_scr_hrv_summary(enum scroll_dir m_scroll_dir);
void hpi_hrv_summary_update_metrics(float sdnn, float rmssd, float pnn50, float mean_rr);
void hpi_hrv_summary_draw_rr_plot(float rr_interval);
void hpi_hrv_summary_set_update_enabled(bool enabled);

// HRV Frequency Analysis screen functions
void draw_scr_hrv_frequency(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void hpi_hrv_frequency_update_spectrum(float *rr_intervals, int num_intervals);
void hpi_hrv_frequency_update_display(void);

// HRV Frequency Compact screen functions (optimized for small round displays)
void draw_scr_hrv_frequency_compact(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
//void hpi_hrv_frequency_compact_update_spectrum(float *rr_intervals, int num_intervals);
void hpi_hrv_frequency_compact_update_display(void);

// Settings screen functions
void draw_scr_pulldown(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void draw_scr_device_user_settings(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void gesture_down_scr_device_user_settings(void);
void draw_scr_height_select(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void gesture_down_scr_height_select(void);
void draw_scr_weight_select(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void gesture_down_scr_weight_select(void);
void draw_scr_hand_worn_select(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void gesture_down_scr_hand_worn_select(void);
void draw_scr_time_format_select(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void gesture_down_scr_time_format_select(void);
void draw_scr_temp_unit_select(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void gesture_down_scr_temp_unit_select(void);
void draw_scr_sleep_timeout_select(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void gesture_down_scr_sleep_timeout_select(void);
void gesture_right_scr_sleep_timeout_select(void);
void hpi_update_height_weight_labels(void);
void hpi_update_setting_labels(void);

// Global flag to suspend screen updates during transitions
extern volatile bool screen_transition_in_progress;

// Helper objects
void draw_scr_common(lv_obj_t *parent);
void hpi_load_screen(int m_screen, enum scroll_dir m_scroll_dir);
void hpi_load_scr_spl(int m_screen, enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);

void hpi_move_load_scr_pulldown(enum scroll_dir m_scroll_dir);

void hpi_disp_set_curr_screen(int screen);
int hpi_disp_get_curr_screen(void);

void hpi_disp_set_brightness(uint8_t brightness_percent);
uint8_t hpi_disp_get_brightness(void);

// Battery display helper functions
const char* hpi_get_battery_symbol(uint8_t level, bool charging);
lv_color_t hpi_get_battery_color(uint8_t level, bool charging);

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

// GSR Screen functions
void draw_scr_gsr(enum scroll_dir m_scroll_dir);
void hpi_gsr_disp_update_gsr_int(uint16_t gsr_value_x100, int64_t gsr_last_update);
void hpi_gsr_process_bioz_sample(int32_t bioz_sample);

void hpi_disp_home_update_batt_level(int batt_level, bool charging);
void hpi_disp_settings_update_batt_level(int batt_level, bool charging);

void hpi_temp_disp_update_temp_f(double temp_f, int64_t temp_f_last_update);

void hpi_show_screen(lv_obj_t *parent, enum scroll_dir m_scroll_dir);
void hpi_show_screen_spl(lv_obj_t *m_screen, enum scroll_dir m_scroll_dir);

void draw_scr_bpt_cal_complete(enum scroll_dir dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void draw_scr_bpt_cal_progress(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void scr_bpt_cal_progress_update_text(char *text);
void draw_scr_bpt_cal_failed(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void draw_scr_bpt_est_complete(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);

void draw_scr_ble(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void disp_screen_event(lv_event_t *e);

// User settings variables
extern uint16_t m_user_height;
extern uint16_t m_user_weight;

/*HRV SCREEN FUNCTIONS*/
void hpi_hrv_frequency_compact_update_spectrum(uint16_t *rr_intervals, int num_intervals, float sdnn, float rmssd);
void gesture_down_scr_spl_raw_ppg_hrv(void);
void hpi_ppg_disp_update_hr_hrv(int hr);
static void hpi_ppg_disp_add_samples_hrv(int num_samples);
static void hpi_ppg_disp_do_set_scale_hrv(int disp_window_size);
static void hpi_ppg_disp_do_set_scale_hrv(int disp_window_size);
static void hpi_ppg_update_signal_status_hrv(enum hpi_ppg_status scd_state);
void hpi_disp_ppg_draw_plotPPG_hrv(struct hpi_ppg_wr_data_t ppg_sensor_sample);
void hpi_ppg_check_signal_timeout_hrv(void);
void draw_scr_spl_raw_ppg_hrv(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void hpi_hrv_disp_update_timer(int time_left);
void hpi_hrv_timer_start(void);
void hpi_hrv_timer_start(void);
void hpi_hrv_timer_pause(void);
void hpi_hrv_timer_reset(void);
bool hpi_hrv_timer_is_running(void);
float hpi_get_lf_hf_ratio(void);
void gesture_down_scr_spl_ppg_for_hrv(void);
void gesture_handler_for_ppg(lv_event_t *e);
void scr_hrv_measure_btn_event_handler(lv_event_t *e);
void hrv_measure_timer_handler(struct k_timer *timer_id);
void gesture_down_scr_spl_hrv(void);
void hpi_ppg_disp_update_rr_interval_hrv(int rr_interval);
void hrv_check_and_transition(void);
static void hrv_update_display(void);
static void hrv_check_and_transition_work(struct k_work *work);
static void hrv_check_timer_handler(struct k_timer *timer);
void hpi_ecg_disp_draw_plotECG_for_hrv(int32_t *data_ecg, int num_samples, bool ecg_lead_off);
void scr_ecg_lead_on_off_handler_for_hrv(bool lead_on_off);


void gesture_down_scr_ecg_hrv(void);
void scr_ecg_lead_on_off_handler_hrv(bool lead_on_off);
void hpi_ecg_disp_draw_plotECG_hrv(int32_t *data_ecg, int num_samples, bool ecg_lead_off);
bool hpi_ecg_timer_is_running_hrv(void);
void hpi_ecg_timer_reset_hrv(void);
void hpi_ecg_timer_pause_hrv(void);
void hpi_ecg_timer_start_hrv(void);
void hpi_ecg_disp_update_timer_hrv(int time_left);
void hpi_ecg_disp_update_hr_hrv(int hr);
void hpi_ecg_disp_add_samples_hrv(int num_samples);
void hpi_ecg_disp_do_set_scale_hrv(int disp_window_size);
static void ecg_chart_reset_performance_counters_hrv(void);
static void ecg_chart_enable_performance_mode_hrv(bool enable);
void draw_scr_ecg_hrv(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);