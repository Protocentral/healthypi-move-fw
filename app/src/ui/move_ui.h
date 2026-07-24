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

// Darker button background colors (better contrast with white text on AMOLED)
#define COLOR_BTN_GREEN       0x1B5E20  // Dark green for start/action buttons
#define COLOR_BTN_RED         0xB71C1C  // Dark red for stop/danger buttons
#define COLOR_BTN_PURPLE      0x4A148C  // Dark purple for HRV buttons
#define COLOR_BTN_BLUE        0x0D47A1  // Dark blue for BP buttons

#define DISP_WINDOW_SIZE_EDA 250
#define PPG_DISP_WINDOW_SIZE 256 // To be verified
#define HRV_DISP_WINDOW_SIZE 128
#define ECG_DISP_WINDOW_SIZE 512 // SAMPLE_RATE * 8 - Increased for more ECG history

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

#define HPI_DISP_BPT_REFRESH_INT 3000
#define HPI_DISP_TEMP_REFRESH_INT 3000
#define HPI_DISP_SETTINGS_REFRESH_INT 1000

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
    SCR_HR,
    SCR_SPO2,
    SCR_ECG,
    SCR_TEMP,
    SCR_ACTIVITY,   /* P0-1: names the Activity carousel tile for sleep/wake save+restore */
    SCR_BPT,
    SCR_HRV,
    SCR_GSR,
    SCR_RECOVERY,   /* H6 readiness/recovery carousel tile (placeholder) */
    //SCR_HRV,
    SCR_LIST_END,
    // Should not go here
    
};

// Special screens
enum hpi_disp_spl_screens
{
    SCR_SPL_LIST_START = 50,

    SCR_SPL_BOOT,
    SCR_SPL_PULLDOWN,

    SCR_SPL_FI_SENS_CHECK,
    SCR_SPL_BPT_MEASURE,
    SCR_SPL_BPT_CAL_COMPLETE,
    SCR_SPL_BPT_CAL_PROGRESS,
    SCR_SPL_BPT_EST_COMPLETE,
    SCR_SPL_BPT_FAILED,
    SCR_SPL_BPT_CAL_REQUIRED,

    SCR_SPL_FI_SENS_WEAR,

    SCR_SPL_SPO2_MEASURE,
    SCR_SPL_SPO2_RESULT,   /* P6: outcome-driven result (replaces complete/timeout/cancelled) */
    SCR_SPL_GSR_COMPLETE,
    SCR_SPL_SPO2_BPT_TIMEOUT,

    SCR_SPL_PROGRESS,      /* MAX32664 hub firmware update — driven directly by
                            * st_display_progress_entry(), NOT via the screen
                            * table. Live: do not "sweep" it. */
    SCR_SPL_DFU,           /* BLE/SMP OTA modal — table-less (NULL gesture →
                            * nav-locked), driven by the display thread from
                            * hpi_dfu state. */
    SCR_SPL_LOW_BATTERY,
    SCR_SPL_HEIGHT_SELECT,
    SCR_SPL_WEIGHT_SELECT,

    SCR_SPL_SETTINGS,      /* P6 v2: scrollable settings screen */
    SCR_SPL_BLE,

    SCR_SPL_LIST_END,      /* keep last — every SPL id must sort below it */
};

enum hpi_disp_subscreens
{
    SUBSCR_BPT_CALIBRATE,
    SUBSCR_BPT_MEASURE,
};

/* P1-4: SCR_SPL_BPT_FAILED serves three distinct failure paths in
 * smf_ppg_finger.c but always read "Calibration Failed". Passed in arg2 -
 * arg1 stays the parent screen, as everywhere else. */
enum hpi_bpt_fail_kind
{
    BPT_FAIL_CAL = 0,   /* calibration did not succeed (default: arg2 omitted) */
    BPT_FAIL_EST,       /* BP estimation did not succeed */
    BPT_FAIL_SENSOR,    /* the finger sensor/hub itself failed */
};

/* Pre-v2 metric icons (ecg_70, bp_70, img_heart_*, img_temp_*, icon_spo2_*,
 * img_steps/calories/timer_48, …) were removed once the carousel switched to
 * Material Symbols — see the Phase A/B/C image cleanup. */

#if defined(CONFIG_HPI_GSR_SCREEN)
/* GSR capture is in-place on the EDA carousel tile (see hpi_eda_monitor_*); the
 * legacy SCR_SPL_PLOT_GSR full-screen plot and its helpers were removed with it.
 * Only the v2 result screen remains as a special screen. */
void draw_scr_gsr_complete(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void unload_scr_gsr_complete(void);
void hpi_gsr_complete_update_results(const struct hpi_gsr_stress_index_t *results);
#else
// Stubs when GSR is disabled
static inline void draw_scr_gsr_complete(enum scroll_dir m_scroll_dir, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4) {
    ARG_UNUSED(m_scroll_dir); ARG_UNUSED(a1); ARG_UNUSED(a2); ARG_UNUSED(a3); ARG_UNUSED(a4);
}
static inline void unload_scr_gsr_complete(void) { }
static inline void hpi_gsr_complete_update_results(const struct hpi_gsr_stress_index_t *r) { ARG_UNUSED(r); }
#endif
LV_IMG_DECLARE(img_complete_85);
LV_IMG_DECLARE(img_failed_80);
LV_IMG_DECLARE(img_bpt_finger_90);
LV_IMG_DECLARE(bck_heart_2_180);
LV_IMG_DECLARE(low_batt_100);

/* v2 type system — 4 text bins + 2 icon sizes (see hpi_r0_theme.h HPI_FONT_*).
 * Rubik = numerals, Manrope = labels; declared here too so legacy screens that
 * include only move_ui.h resolve them. */
LV_FONT_DECLARE(rubik_500_88);
LV_FONT_DECLARE(rubik_500_32);
LV_FONT_DECLARE(rubik_500_22);
LV_FONT_DECLARE(manrope_700_22);
LV_FONT_DECLARE(matsym_24);
LV_FONT_DECLARE(matsym_26);
LV_FONT_DECLARE(matsym_28);
/* Modern style declarations */
extern lv_style_t style_health_arc;
extern lv_style_t style_health_arc_bg;
extern lv_style_t style_body_medium;
extern lv_style_t style_caption;
/* Additional specialized styles */
extern lv_style_t style_numeric_large;  // For large numeric displays (time, main values)


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

void hpi_disp_restore_brightness(void);

/* Low-battery screen control (defined in screens/scr_low_battery.c). */
void hpi_disp_low_battery_update(uint8_t battery_level, bool is_charging);
void hpi_disp_low_battery_cleanup(void);
// Progress Screen functions
void draw_scr_progress(const char *title, const char *message);
void hpi_disp_scr_update_progress(int progress, const char *status);
void hpi_disp_scr_show_error(const char *error_message);
void hpi_disp_scr_reset_progress(void);
void hpi_disp_scr_debug_status(void);

// Clock Screen functions

// Home Screen functions
void draw_scr_carousel(enum scroll_dir m_scroll_dir);
void hpi_carousel_show(int scr, enum scroll_dir dir);
void hpi_carousel_step(int dir);   /* instant one-step tile change (+1 next / -1 prev) */
int  hpi_carousel_curr_screen(void);   /* screen id of the active tile (SCR_HOME if none) */
void hpi_home_set_face(int face);  /* 0 = Digital (default), 1 = Minimal; next carousel rebuild */
void hpi_carousel_rebuild(void);   /* drop the cached carousel so it rebuilds (face/accent change) */

/* v2 scrollable settings screen */
void draw_scr_settings(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void gesture_down_scr_settings(void);
void draw_scr_spo2_result(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void gesture_down_scr_spo2_result(void);
void hpi_hr_monitor_into(lv_obj_t *parent);   /* build HR monitor into a carousel tile */
/* P3 + handoff layout: 296×74 plot (24h spark default / LIVE wave) with a
 * MEASURE-style LIVE↔24H pill under it, then RESTING + MIN/MAX chips. `is_live`
 * lets the
 * display's PPG feed skip pushing into a hidden canvas. It is a VIEW toggle:
 * the wrist PPG keeps streaming either way (background HR + continuous HRV). */
bool hpi_hr_wave_is_live(void);
/* Repaint the HR sparkline from the store's cached series. Display-thread only
 * (it touches LVGL); cheap - the series itself is computed on the store thread. */
void hpi_hr_trend_refresh(void);
/* Same for the Temp tile's 7-night skin-temp sparkline. */
void hpi_temp_trend_refresh(void);
void hpi_spo2_trend_refresh(void);            /* SpO2 idle tile: last reading + age */
void hpi_ecg_trend_refresh(void);             /* ECG idle tile: BPM + age under hero */
void hpi_stress_hrv_trend_refresh(void);      /* Stress 10× HRV_RMSSD bars */
void hpi_spo2_monitor_into(lv_obj_t *parent); /* build SpO2 idle tile into a carousel tile */
/* Armed SpO2 source (SPO2_SOURCE_PPG_WR/_FI). Set by the idle tile's segmented
 * toggle (and by the measure screen from its arg2, so an SMF-driven entry stays
 * authoritative); read by the measure + result screens to render the source. */
int  hpi_spo2_source_get(void);
void hpi_spo2_source_set(int source);
void hpi_temp_monitor_into(lv_obj_t *parent); /* build Temp monitor into a carousel tile */
void hpi_ecg_monitor_into(lv_obj_t *parent);    /* build ECG monitor into a carousel tile */
void hpi_ecg_monitor_update(int status, int progress_timer); /* render monitor to ECG SMF phase (LVGL thread) */
void hpi_ecg_monitor_leave(void);   /* cancel an in-progress ECG when navigating away from the tile */
/* Last non-zero ECG-session HR known to the display path (for idle meta/age). */
bool hpi_disp_get_last_ecg_hr(uint16_t *hr, int64_t *ts_utc, uint32_t *uptime_ms);
void hpi_stress_monitor_into(lv_obj_t *parent); /* build Stress/HRV monitor into a carousel tile */
void hpi_activity_monitor_into(lv_obj_t *parent); /* build Activity monitor into a carousel tile */
void hpi_bpt_monitor_into(lv_obj_t *parent);    /* build BP idle tile into a carousel tile (v2) */
#if defined(CONFIG_HPI_GSR_SCREEN)
void hpi_eda_monitor_into(lv_obj_t *parent);    /* build EDA/GSR monitor into a carousel tile (v2) */
void hpi_eda_monitor_update(int status, int remaining_s, bool contact); /* render monitor to GSR SMF phase (LVGL thread) */
void hpi_eda_monitor_leave(void);   /* cancel an in-progress GSR when navigating away from the tile */
#else
/* GSR disabled: scr_eda_monitor.c is not compiled (see app/CMakeLists.txt). */
static inline void hpi_eda_monitor_into(lv_obj_t *parent) { ARG_UNUSED(parent); }
static inline void hpi_eda_monitor_update(int status, int remaining_s, bool contact) {
    ARG_UNUSED(status); ARG_UNUSED(remaining_s); ARG_UNUSED(contact);
}
static inline void hpi_eda_monitor_leave(void) { }
#endif
/* SpO2 result outcome (arg2 to draw_scr_spo2_result / SCR_SPL_SPO2_RESULT) */
#define HPI_SPO2_RESULT_SUCCESS   0
#define HPI_SPO2_RESULT_TIMEOUT   1
#define HPI_SPO2_RESULT_CANCELLED 2

// Spo2 Screen functions
/* Push a just-taken SpO2 into the display last-value path (hero + age). Used by
 * the result screen so the idle tile updates even if the health store drops the
 * sample (e.g. RTC not yet VALID-gated). Also fed by spo2_chan. */
void hpi_disp_update_spo2(uint8_t spo2, int64_t ts_last_update);
/* Last non-zero SpO2 known to the display path. uptime_ms is the k_uptime stamp
 * of that update (0 if only restored from the store). */
bool hpi_disp_get_last_spo2(uint8_t *spo2, int64_t *ts_utc, uint32_t *uptime_ms);

int hpi_disp_reset_all_last_updated(void);
void hpi_disp_spo2_plot_wrist_ppg(struct hpi_ppg_wr_data_t ppg_sensor_sample);
void hpi_disp_spo2_plot_fi_ppg(struct hpi_ppg_fi_data_t ppg_sensor_sample);

/* conf: the hub's SpO2 confidence for this sample. The measure screen must
 * apply the SAME >50 gate the wrist SMF uses before it stores/publishes a
 * reading (smf_ppg_wrist.c), or the result screen would show a value the rest
 * of the firmware threw away. */
void hpi_disp_spo2_update_progress(int progress, enum spo2_meas_state state, int spo2, int hr, int conf);
void draw_scr_spl_low_battery(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void draw_scr_spo2_measure(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);

// Recording Screen functions

// ECG completion screen (legacy full-screen ECG plot scr_ecg_scr2 retired)

// ECG Timer control functions for lead-based automatic start/stop
void hpi_ecg_timer_start(void);
void hpi_ecg_timer_pause(void);
void hpi_ecg_timer_reset(void);
bool hpi_ecg_timer_is_running(void);
void hpi_ecg_reset_countdown_timer(void);
void hpi_ecg_clear_lead_placement_timeout(void);

void gesture_down_scr_fi_sens_wear(void);
void gesture_down_scr_fi_sens_check(void);
void gesture_down_scr_bpt_measure(void);
void gesture_down_scr_bpt_cal_complete(void);
void gesture_down_scr_spo2_measure(void);
void gesture_down_scr_spl_low_battery(void);
void gesture_down_scr_bpt_cal_progress(void);
void gesture_down_scr_bpt_cal_failed(void);
void gesture_down_scr_bpt_est_complete(void);
void gesture_down_scr_ble(void);
void gesture_down_scr_pulldown(void);
void gesture_down_scr_bpt_cal_required(void);
#if defined(CONFIG_HPI_GSR_SCREEN)
void gesture_down_scr_gsr_complete(void);
#else
static inline void gesture_down_scr_gsr_complete(void) { }
#endif

/* The shared PPG chart autoscale helper (hpi_ppg_autoscale.c) was removed with
 * its last caller: the Raw PPG screen went first, then the SpO2 measure screen
 * moved to the R0 wave monitor, which auto-scales itself (push_auto). */

// EDA screen functions
void hpi_eda_disp_draw_plotEDA(int32_t *data_eda, int num_samples, bool eda_lead_off);

// BPT screen functions
// void draw_scr_bpt_calibrate(void);
// void draw_scr_bpt_measure(void);
void hpi_disp_bpt_draw_plotPPG(struct hpi_ppg_fi_data_t ppg_sensor_sample);
void hpi_disp_bpt_update_progress(int progress);
/* Calibration progress screen shares the measure chrome (v2); fed separately so
 * it can carry the POINT n/3 index. */
void hpi_disp_bpt_cal_draw_plotPPG(struct hpi_ppg_fi_data_t ppg_sensor_sample);
void hpi_disp_bpt_cal_update_progress(int point_idx, int progress);
/* Shared v2 BPT measure/cal chrome builders (defined in scr_bpt_measure.c). */
lv_obj_t *hpi_bpt_make_title_row(lv_obj_t *parent, const char *title);
lv_obj_t *hpi_bpt_make_status_pill(lv_obj_t *parent, lv_obj_t **out_ring,
                                   lv_obj_t **out_pct, lv_obj_t **out_hr);
lv_obj_t *hpi_bpt_make_cancel(lv_obj_t *parent, lv_event_cb_t cb);
void draw_scr_fi_sens_check(enum scroll_dir dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void draw_scr_fi_sens_wear(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void draw_scr_bpt_measure(enum scroll_dir dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void draw_scr_bpt_cal_required(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);


// HRV screen functions
void hrv_check_and_transition(void);
void gesture_handler(lv_event_t *e);

// Settings screen functions
void draw_scr_pulldown(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
/* P2: the old device-user-settings menu and its hand-worn / time-format /
 * temp-unit / sleep-timeout pickers are gone — P6 migrated those fields into the
 * v2 settings screen and left the menu unreachable. Height/Weight survive in
 * scr_height_weight_select.c (still reached from the v2 settings rows). */
void draw_scr_height_select(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void gesture_down_scr_height_select(void);
void draw_scr_weight_select(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void gesture_down_scr_weight_select(void);
/* A6: hpi_update_height_weight_labels() / hpi_update_setting_labels() declared
 * here but never defined anywhere — they belonged to the deleted P2 settings
 * menu. Same for hpi_show_screen_spl() and hpi_move_load_scr_pulldown(). */

// Global flag to suspend screen updates during transitions
extern volatile bool screen_transition_in_progress;

// Helper objects
void hpi_load_screen(int m_screen, enum scroll_dir m_scroll_dir);
void hpi_load_scr_spl(int m_screen, enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);

void hpi_disp_set_curr_screen(int screen);
int hpi_disp_get_curr_screen(void);

void hpi_disp_set_brightness(uint8_t brightness_percent);
uint8_t hpi_disp_get_brightness(void);

void draw_bg(lv_obj_t *parent);

void hpi_disp_settings_update_batt_level(int batt_level, bool charging);


void hpi_show_screen(lv_obj_t *parent, enum scroll_dir m_scroll_dir);

/* Park on a blank screen and free the screen currently on the panel, so the next
 * one is built against a reclaimed LVGL heap instead of peaking with both
 * resident. Called from hpi_load_screen(), hpi_carousel_show() and the display
 * thread's screen-load drain — every navigation path goes through one of those.
 * Display (LVGL) thread only. */
void hpi_scr_release_current(void);

// Toast notification utility
void hpi_disp_show_toast(const char *message, uint32_t duration_ms);

/* DFU (firmware-update) modal screen — driven by the display thread from hpi_dfu
 * state. draw is parameterless (table-less special screen). */
void draw_scr_dfu(void);
void hpi_disp_dfu_update(int pct);
void hpi_disp_dfu_set_phase(const char *title, const char *message, uint32_t accent);

void draw_scr_bpt_cal_complete(enum scroll_dir dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void draw_scr_bpt_cal_progress(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void draw_scr_bpt_cal_failed(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void draw_scr_bpt_est_complete(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);

void draw_scr_ble(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void disp_screen_event(lv_event_t *e);

/* P2: m_user_height / m_user_weight removed. They were globals mirroring the
 * persisted profile, and the roller pickers were their only consumer — those now
 * read/write hpi_user_settings_get/set_height|weight directly, so there is no
 * second copy to drift. */

void draw_scr_timeout(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void gesture_down_scr_timeout(void);

void scr_ppg_finger_contact_handler(bool contact);