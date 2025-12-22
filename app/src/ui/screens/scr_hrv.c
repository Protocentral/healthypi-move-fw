/*
 * HealthyPi Move HRV Home Screen
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Main HRV screen showing last measurement with balance indicator
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <lvgl.h>
#include <stdio.h>
#include <math.h>
#include <zephyr/smf.h>
#include <app_version.h>
#include "hpi_common_types.h"
#include "ui/move_ui.h"
#include "hpi_sys.h"
#include "stdlib.h"
#include "hrv_algos.h"

LOG_MODULE_REGISTER(hpi_disp_scr_hrv, LOG_LEVEL_DBG);

static int m_hrv_ratio_int = 0;
static int m_hrv_ratio_dec = 0;
static int64_t m_hrv_last_update = 0;

extern struct k_sem sem_ecg_start;

// GUI Objects
lv_obj_t *scr_hrv;
static lv_obj_t *btn_hrv_measure;
static lv_obj_t *label_hrv_value;
static lv_obj_t *label_hrv_status;
static lv_obj_t *label_balance_state;
static lv_obj_t *bar_balance_bg;
static lv_obj_t *indicator_balance;

// Externs - Modern style system
extern lv_style_t style_body_medium;
extern lv_style_t style_numeric_medium;
extern lv_style_t style_caption;

// Balance bar dimensions (smaller for home screen)
#define HRV_HOME_BAR_WIDTH   220
#define HRV_HOME_BAR_HEIGHT  12
#define HRV_HOME_INDICATOR_SIZE 16

// Color definitions for balance gradient
#define COLOR_PARA_DOMINANT  0x4CAF50  // Green - parasympathetic (recovery)
#define COLOR_BALANCED       0xFFEB3B  // Yellow - balanced
#define COLOR_SYMP_DOMINANT  0xF44336  // Red - sympathetic (stress/alert)

/**
 * @brief Convert LF/HF ratio (x100) to balance position (0-100)
 */
static int get_balance_position_from_stored(uint16_t lf_hf_x100)
{
    if (lf_hf_x100 == 0) return 50;  // No data - center

    float lf_hf = lf_hf_x100 / 100.0f;

    // Clamp ratio to reasonable range
    if (lf_hf < 0.25f) lf_hf = 0.25f;
    if (lf_hf > 4.0f) lf_hf = 4.0f;

    // log2 scaling centered at 1.0
    float log_ratio = log2f(lf_hf);  // Range: -2 to +2

    // Map to 0-100 (center at 50)
    int position = (int)(50.0f + (log_ratio * 25.0f));

    // Clamp to valid range
    if (position < 0) position = 0;
    if (position > 100) position = 100;

    return position;
}

/**
 * @brief Get balance state description based on stored LF/HF ratio (x100)
 */
static const char* get_balance_state_from_stored(uint16_t lf_hf_x100)
{
    if (lf_hf_x100 == 0) return "";

    float lf_hf = lf_hf_x100 / 100.0f;

    if (lf_hf < 0.5f)       return "Deep Recovery";
    else if (lf_hf < 0.8f)  return "Relaxed";
    else if (lf_hf < 1.2f)  return "Balanced";
    else if (lf_hf < 1.8f)  return "Mildly Active";
    else if (lf_hf < 2.5f)  return "Alert";
    else if (lf_hf < 3.5f)  return "Stressed";
    else                    return "High Stress";
}

/**
 * @brief Get color for balance state indicator
 */
static lv_color_t get_balance_indicator_color(int position)
{
    if (position < 35) {
        return lv_color_hex(COLOR_PARA_DOMINANT);
    } else if (position < 65) {
        return lv_color_hex(COLOR_BALANCED);
    } else {
        return lv_color_hex(COLOR_SYMP_DOMINANT);
    }
}

/**
 * @brief Button event handler for starting HRV evaluation
 */
static void scr_hrv_btn_start_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        LOG_INF("HRV Evaluation started by user");

        // Display progress screen immediately
        hpi_load_scr_spl(SCR_SPL_HRV_EVAL_PROGRESS, SCROLL_UP, 0, 0, 0, 0);

        // Signal state machine to start HRV evaluation
        extern struct k_sem sem_hrv_eval_start;
        k_sem_give(&sem_hrv_eval_start);
    }
}

static void hrv_update_display(void);

/**
 * @brief Draw HRV home screen with measurement controls and balance bar
 */
void draw_scr_hrv(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_hrv = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_hrv, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_hrv, LV_OBJ_FLAG_SCROLLABLE);

    /*
     * SPACED LAYOUT FOR 390x390 ROUND AMOLED
     * ======================================
     * Layout with balance bar and proper spacing (24px font for value):
     *   y=55:  Title "HRV"
     *   y=90:  HRV icon (35px) -> ends at y=125
     *   y=135: LF/HF value with unit (35px container, 24px font) -> ends at y=170
     *   y=180: Balance state text
     *   y=215: Balance gradient bar with indicator
     *   y=260: Last update time
     *   y=305: Start button
     */

    // TOP: Title at y=55
    lv_obj_t *label_title = lv_label_create(scr_hrv);
    lv_label_set_text(label_title, "HRV");
    lv_obj_set_pos(label_title, 0, 55);
    lv_obj_set_width(label_title, 390);
    lv_obj_add_style(label_title, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);

    // UPPER: HRV icon at y=90 (35px)
    lv_obj_t *img_hrv = lv_img_create(scr_hrv);
    lv_img_set_src(img_hrv, &img_heart_35);
    lv_obj_set_pos(img_hrv, (390 - 35) / 2, 90);
    lv_obj_set_style_img_recolor(img_hrv, lv_color_hex(0x8000FF), LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(img_hrv, LV_OPA_COVER, LV_PART_MAIN);

    // CENTER: LF/HF value with inline unit at y=135 (35px container for 24px font)
    lv_obj_t *cont_value = lv_obj_create(scr_hrv);
    lv_obj_remove_style_all(cont_value);
    lv_obj_set_size(cont_value, 390, 35);
    lv_obj_set_pos(cont_value, 0, 135);
    lv_obj_set_style_bg_opa(cont_value, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont_value, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_value, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);

    label_hrv_value = lv_label_create(cont_value);
    lv_label_set_text(label_hrv_value, "--");
    lv_obj_set_style_text_color(label_hrv_value, lv_color_white(), LV_PART_MAIN);
    lv_obj_add_style(label_hrv_value, &style_numeric_medium, LV_PART_MAIN);

    // Inline "LF/HF" unit
    lv_obj_t *label_hrv_unit = lv_label_create(cont_value);
    lv_label_set_text(label_hrv_unit, " LF/HF");
    lv_obj_set_style_text_color(label_hrv_unit, lv_color_hex(0x8000FF), LV_PART_MAIN);
    lv_obj_add_style(label_hrv_unit, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(label_hrv_unit, 6, LV_PART_MAIN);

    // Balance state text at y=180
    label_balance_state = lv_label_create(scr_hrv);
    lv_label_set_text(label_balance_state, "");
    lv_obj_set_pos(label_balance_state, 0, 180);
    lv_obj_set_width(label_balance_state, 390);
    lv_obj_add_style(label_balance_state, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_balance_state, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_balance_state, lv_color_hex(COLOR_BALANCED), LV_PART_MAIN);

    // Balance gradient bar at y=215
    bar_balance_bg = lv_obj_create(scr_hrv);
    lv_obj_remove_style_all(bar_balance_bg);
    lv_obj_set_size(bar_balance_bg, HRV_HOME_BAR_WIDTH, HRV_HOME_BAR_HEIGHT);
    lv_obj_set_pos(bar_balance_bg, (390 - HRV_HOME_BAR_WIDTH) / 2, 215);
    lv_obj_set_style_radius(bar_balance_bg, HRV_HOME_BAR_HEIGHT / 2, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar_balance_bg, LV_OPA_COVER, LV_PART_MAIN);

    // Create gradient using three colored segments
    // Left segment (green - parasympathetic/recovery)
    lv_obj_t *seg_left = lv_obj_create(bar_balance_bg);
    lv_obj_remove_style_all(seg_left);
    lv_obj_set_size(seg_left, HRV_HOME_BAR_WIDTH / 3, HRV_HOME_BAR_HEIGHT);
    lv_obj_set_pos(seg_left, 0, 0);
    lv_obj_set_style_bg_color(seg_left, lv_color_hex(COLOR_PARA_DOMINANT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(seg_left, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(seg_left, HRV_HOME_BAR_HEIGHT / 2, LV_PART_MAIN);

    // Center segment (yellow - balanced)
    lv_obj_t *seg_center = lv_obj_create(bar_balance_bg);
    lv_obj_remove_style_all(seg_center);
    lv_obj_set_size(seg_center, HRV_HOME_BAR_WIDTH / 3 + 10, HRV_HOME_BAR_HEIGHT);
    lv_obj_set_pos(seg_center, HRV_HOME_BAR_WIDTH / 3 - 5, 0);
    lv_obj_set_style_bg_color(seg_center, lv_color_hex(COLOR_BALANCED), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(seg_center, LV_OPA_COVER, LV_PART_MAIN);

    // Right segment (red - sympathetic/stress)
    lv_obj_t *seg_right = lv_obj_create(bar_balance_bg);
    lv_obj_remove_style_all(seg_right);
    lv_obj_set_size(seg_right, HRV_HOME_BAR_WIDTH / 3, HRV_HOME_BAR_HEIGHT);
    lv_obj_set_pos(seg_right, HRV_HOME_BAR_WIDTH * 2 / 3, 0);
    lv_obj_set_style_bg_color(seg_right, lv_color_hex(COLOR_SYMP_DOMINANT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(seg_right, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(seg_right, HRV_HOME_BAR_HEIGHT / 2, LV_PART_MAIN);

    // Center marker line
    lv_obj_t *center_marker = lv_obj_create(bar_balance_bg);
    lv_obj_remove_style_all(center_marker);
    lv_obj_set_size(center_marker, 2, HRV_HOME_BAR_HEIGHT + 4);
    lv_obj_set_pos(center_marker, HRV_HOME_BAR_WIDTH / 2 - 1, -2);
    lv_obj_set_style_bg_color(center_marker, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(center_marker, LV_OPA_70, LV_PART_MAIN);

    // Balance indicator (circle) - positioned below bar at y=215
    indicator_balance = lv_obj_create(scr_hrv);
    lv_obj_remove_style_all(indicator_balance);
    lv_obj_set_size(indicator_balance, HRV_HOME_INDICATOR_SIZE, HRV_HOME_INDICATOR_SIZE);
    int initial_x = (390 - HRV_HOME_BAR_WIDTH) / 2 + (HRV_HOME_BAR_WIDTH / 2) - (HRV_HOME_INDICATOR_SIZE / 2);
    lv_obj_set_pos(indicator_balance, initial_x, 215 + HRV_HOME_BAR_HEIGHT + 2);
    lv_obj_set_style_bg_color(indicator_balance, lv_color_hex(COLOR_BALANCED), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(indicator_balance, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(indicator_balance, HRV_HOME_INDICATOR_SIZE / 2, LV_PART_MAIN);
    lv_obj_set_style_border_width(indicator_balance, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(indicator_balance, lv_color_white(), LV_PART_MAIN);

    // Hide balance bar initially (shown when data available)
    lv_obj_add_flag(bar_balance_bg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(indicator_balance, LV_OBJ_FLAG_HIDDEN);

    // Last measurement time at y=260
    label_hrv_status = lv_label_create(scr_hrv);
    lv_label_set_text(label_hrv_status, "No measurement yet");
    lv_obj_set_pos(label_hrv_status, 0, 260);
    lv_obj_set_width(label_hrv_status, 390);
    lv_obj_set_style_text_color(label_hrv_status, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_add_style(label_hrv_status, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_hrv_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // Start HRV button at y=305
    const int btn_width = 180;
    const int btn_height = 50;
    const int btn_y = 305;

    btn_hrv_measure = hpi_btn_create_primary(scr_hrv);
    lv_obj_set_size(btn_hrv_measure, btn_width, btn_height);
    lv_obj_set_pos(btn_hrv_measure, (390 - btn_width) / 2, btn_y);
    lv_obj_set_style_radius(btn_hrv_measure, 28, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn_hrv_measure, lv_color_hex(COLOR_BTN_PURPLE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_hrv_measure, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_hrv_measure, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn_hrv_measure, 0, LV_PART_MAIN);

    lv_obj_t *label_btn = lv_label_create(btn_hrv_measure);
    lv_label_set_text(label_btn, LV_SYMBOL_PLAY " Start HRV");
    lv_obj_center(label_btn);
    lv_obj_set_style_text_color(label_btn, lv_color_white(), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_hrv_measure, scr_hrv_btn_start_handler, LV_EVENT_CLICKED, NULL);

    hrv_update_display();
    hpi_disp_set_curr_screen(SCR_HRV);
    hpi_show_screen(scr_hrv, m_scroll_dir);
}

static void hrv_update_display(void)
{
    if (label_hrv_value == NULL || label_hrv_status == NULL) {
        return;
    }

    char last_meas_str[25];
    uint16_t m_hrv_lf_hf = 0, m_hrv_sdnn = 0, m_hrv_rmssd = 0;

    // Retrieve stored HRV values from settings
    hpi_sys_get_last_hrv_update(&m_hrv_lf_hf, &m_hrv_sdnn, &m_hrv_rmssd, &m_hrv_last_update);

    LOG_DBG("HRV display update: LF/HF=%u (raw), SDNN=%u, RMSSD=%u, ts=%lld",
            m_hrv_lf_hf, m_hrv_sdnn, m_hrv_rmssd, m_hrv_last_update);

    ARG_UNUSED(m_hrv_sdnn);
    ARG_UNUSED(m_hrv_rmssd);
    hpi_helper_get_relative_time_str(m_hrv_last_update, last_meas_str, sizeof(last_meas_str));

    if (m_hrv_lf_hf == 0 || m_hrv_last_update == 0) {
        m_hrv_ratio_int = 0;
        m_hrv_ratio_dec = 0;

        lv_label_set_text(label_hrv_value, "--");
        lv_label_set_text(label_hrv_status, "No measurement yet");
        lv_label_set_text(label_balance_state, "");

        // Hide balance bar when no data
        if (bar_balance_bg) lv_obj_add_flag(bar_balance_bg, LV_OBJ_FLAG_HIDDEN);
        if (indicator_balance) lv_obj_add_flag(indicator_balance, LV_OBJ_FLAG_HIDDEN);
    } else {
        m_hrv_ratio_int = m_hrv_lf_hf / 100;
        m_hrv_ratio_dec = m_hrv_lf_hf % 100;

        LOG_INF("HRV displaying: %d.%02d LF/HF", m_hrv_ratio_int, m_hrv_ratio_dec);
        lv_label_set_text_fmt(label_hrv_value, "%d.%02d", m_hrv_ratio_int, m_hrv_ratio_dec);
        lv_label_set_text(label_hrv_status, last_meas_str);

        // Update balance state text
        const char* state_text = get_balance_state_from_stored(m_hrv_lf_hf);
        if (label_balance_state) {
            lv_label_set_text(label_balance_state, state_text);
            int balance_pos = get_balance_position_from_stored(m_hrv_lf_hf);
            lv_obj_set_style_text_color(label_balance_state,
                                         get_balance_indicator_color(balance_pos), LV_PART_MAIN);
        }

        // Show and update balance bar
        if (bar_balance_bg) lv_obj_clear_flag(bar_balance_bg, LV_OBJ_FLAG_HIDDEN);
        if (indicator_balance) {
            lv_obj_clear_flag(indicator_balance, LV_OBJ_FLAG_HIDDEN);

            // Update indicator position
            int balance_pos = get_balance_position_from_stored(m_hrv_lf_hf);
            int bar_start_x = (390 - HRV_HOME_BAR_WIDTH) / 2;
            int indicator_x = bar_start_x + (balance_pos * HRV_HOME_BAR_WIDTH / 100) - (HRV_HOME_INDICATOR_SIZE / 2);

            // Clamp to bar bounds
            if (indicator_x < bar_start_x - HRV_HOME_INDICATOR_SIZE / 2) {
                indicator_x = bar_start_x - HRV_HOME_INDICATOR_SIZE / 2;
            }
            if (indicator_x > bar_start_x + HRV_HOME_BAR_WIDTH - HRV_HOME_INDICATOR_SIZE / 2) {
                indicator_x = bar_start_x + HRV_HOME_BAR_WIDTH - HRV_HOME_INDICATOR_SIZE / 2;
            }

            lv_obj_set_pos(indicator_balance, indicator_x, 215 + HRV_HOME_BAR_HEIGHT + 2);
            lv_obj_set_style_bg_color(indicator_balance,
                                       get_balance_indicator_color(balance_pos), LV_PART_MAIN);
        }
    }
}
