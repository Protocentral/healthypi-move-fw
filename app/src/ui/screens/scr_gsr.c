/*
 * HealthyPi Move GSR Screen
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 *
 * GSR (Galvanic Skin Response) dashboard display
 * Shows: SCL (Tonic Level) and SCR rate (peaks per minute)
 * Note: Stress level display temporarily hidden pending validation
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <lvgl.h>
#include <stdio.h>

#include "hpi_common_types.h"
#include "ui/move_ui.h"
#include "hw_module.h"
#include "hpi_sys.h"

LOG_MODULE_REGISTER(hpi_disp_scr_gsr, LOG_LEVEL_ERR);

// Extern semaphore declarations for GSR control
extern struct k_sem sem_gsr_start;
extern struct k_sem sem_gsr_cancel;

// GUI Objects
lv_obj_t *scr_gsr;
static lv_obj_t *label_gsr_current;
static lv_obj_t *btn_gsr_measure;
static lv_obj_t *label_gsr_last_update;

// Multi-metric display labels
static lv_obj_t *label_scl_value;        // Primary: Tonic level (SCL in uS)
static lv_obj_t *label_scr_rate_value;   // Secondary: SCR rate (/min)

// GSR Data Management
static uint16_t gsr_baseline_x100 = 1000;  // 10.00 uS default baseline
static uint8_t baseline_sample_count = 0;
static bool gsr_measurement_active = false;
static int64_t gsr_measurement_start_ts = 0; // uptime (ms) when measurement started

// Function declarations
static void calculate_gsr_baseline(uint16_t new_value);
static void scr_gsr_measure_btn_event_handler(lv_event_t *e);
void hpi_gsr_disp_plot_add_sample(uint16_t gsr_value_x100);

// Externs - Modern style system
extern lv_style_t style_body_medium;
extern lv_style_t style_numeric_large;
extern lv_style_t style_caption;

// Color definitions
#define COLOR_GSR_TEAL      0x00897B  // Teal theme color

/**
 * @brief Calculate rolling baseline for GSR measurements
 * @param new_value New GSR value to include in baseline calculation
 */
static void calculate_gsr_baseline(uint16_t new_value)
{
    static uint32_t baseline_sum = 0;
    static bool baseline_initialized = false;

    if (!baseline_initialized) {
        gsr_baseline_x100 = new_value;
        baseline_sum = new_value * 20;  // Initialize with 20 samples
        baseline_initialized = true;
        baseline_sample_count = 1;
        return;
    }

    // Rolling average for baseline
    baseline_sum = baseline_sum - gsr_baseline_x100 + new_value;
    gsr_baseline_x100 = baseline_sum / 20;

    baseline_sample_count++;
}

/**
 * @brief Handle GSR measure button events
 * @param e LVGL event
 */
static void scr_gsr_measure_btn_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        hpi_load_scr_spl(SCR_SPL_PLOT_GSR, SCROLL_UP, (uint32_t)SCR_GSR, 0, 0, 0);

        // Start GSR live view via semaphore
        k_msleep(500);  // Allow screen transition
        k_sem_give(&sem_gsr_start);
    }
}


/**
 * @brief Update GSR display with new measurement data (integer-only)
 * @param gsr_value_x100 GSR value multiplied by 100
 * @param gsr_last_update_ts Timestamp of measurement
 */
void hpi_gsr_disp_update_gsr_int(uint16_t gsr_value_x100, int64_t gsr_last_update_ts)
{
    if (label_gsr_current == NULL) {
        return;
    }

    // Only update if live view is active
    if (!gsr_measurement_active) {
        return;
    }

    // Display current value for live monitoring (no calculation)
    uint16_t integer_part = gsr_value_x100 / 100;
    uint16_t decimal_part = gsr_value_x100 % 100;
    lv_label_set_text_fmt(label_gsr_current, "%u.%02u", integer_part, decimal_part);

    // Note: GSR data is sent to plot for live visualization
}

void draw_scr_gsr(enum scroll_dir m_scroll_dir)
{
    scr_gsr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_gsr, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_gsr, LV_OBJ_FLAG_SCROLLABLE);

    /*
     * GSR DASHBOARD LAYOUT FOR 390x390 ROUND AMOLED
     * ==============================================
     * Vertical stacked layout for large values (handles 5+ char values like "393.4"):
     *   y=35:  Title "GSR" (close to top)
     *   y=80:  Tonic Level label
     *   y=100: SCL value (large font, full width) + uS inline
     *   y=185: SCR Rate label
     *   y=205: SCR rate value (large font) + /min inline
     *   y=285: Last update status
     *   y=315: Start GSR button
     *
     * Note: Stress level display temporarily hidden pending validation
     */

    // Get stored GSR stress data for initial display
    uint8_t stored_stress_level = 0;
    uint16_t stored_tonic_x100 = 0;
    uint8_t stored_peaks_per_min = 0;
    int64_t gsr_last_update_stored = 0;
    hpi_sys_get_last_gsr_stress(&stored_stress_level, &stored_tonic_x100, &stored_peaks_per_min, &gsr_last_update_stored);
    bool has_data = (gsr_last_update_stored > 0);
    ARG_UNUSED(stored_stress_level);  // Temporarily unused pending validation

    // TOP: Title at y=35 (close to top edge)
    lv_obj_t *label_title = lv_label_create(scr_gsr);
    lv_label_set_text(label_title, "GSR");
    lv_obj_set_pos(label_title, 0, 35);
    lv_obj_set_width(label_title, 390);
    lv_obj_add_style(label_title, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);

    // PRIMARY METRIC: SCL (Tonic Level) - centered with inline unit
    lv_obj_t *label_scl_title = lv_label_create(scr_gsr);
    lv_label_set_text(label_scl_title, "Tonic Level");
    lv_obj_set_pos(label_scl_title, 0, 80);
    lv_obj_set_width(label_scl_title, 390);
    lv_obj_add_style(label_scl_title, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_scl_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_scl_title, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);

    // SCL value container with inline unit (centered flex row)
    lv_obj_t *cont_scl = lv_obj_create(scr_gsr);
    lv_obj_remove_style_all(cont_scl);
    lv_obj_set_size(cont_scl, 390, 80);
    lv_obj_set_pos(cont_scl, 0, 100);
    lv_obj_set_style_bg_opa(cont_scl, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont_scl, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_scl, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);

    // SCL value - large font (rounded to 1 decimal place)
    label_scl_value = lv_label_create(cont_scl);
    if (has_data) {
        // Round to 1 decimal place: add 5 to x100 value, then use x10 for display
        uint16_t tonic_x10_rounded = (stored_tonic_x100 + 5) / 10;
        lv_label_set_text_fmt(label_scl_value, "%u.%u",
                               tonic_x10_rounded / 10, tonic_x10_rounded % 10);
    } else {
        lv_label_set_text(label_scl_value, "--");
    }
    lv_obj_add_style(label_scl_value, &style_numeric_large, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_scl_value, lv_color_hex(COLOR_GSR_TEAL), LV_PART_MAIN);

    // Inline uS unit
    lv_obj_t *label_scl_unit = lv_label_create(cont_scl);
    lv_label_set_text(label_scl_unit, " uS");
    lv_obj_add_style(label_scl_unit, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_scl_unit, lv_color_hex(COLOR_GSR_TEAL), LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(label_scl_unit, 8, LV_PART_MAIN);  // Baseline align with large number

    // SECONDARY METRIC: SCR Rate - centered with inline unit
    lv_obj_t *label_scr_title = lv_label_create(scr_gsr);
    lv_label_set_text(label_scr_title, "SCR Rate");
    lv_obj_set_pos(label_scr_title, 0, 185);
    lv_obj_set_width(label_scr_title, 390);
    lv_obj_add_style(label_scr_title, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_scr_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_scr_title, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);

    // SCR rate container with inline unit (centered flex row)
    lv_obj_t *cont_scr = lv_obj_create(scr_gsr);
    lv_obj_remove_style_all(cont_scr);
    lv_obj_set_size(cont_scr, 390, 70);
    lv_obj_set_pos(cont_scr, 0, 205);
    lv_obj_set_style_bg_opa(cont_scr, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont_scr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_scr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);

    // SCR rate value - large font
    label_scr_rate_value = lv_label_create(cont_scr);
    if (has_data) {
        lv_label_set_text_fmt(label_scr_rate_value, "%u", stored_peaks_per_min);
    } else {
        lv_label_set_text(label_scr_rate_value, "--");
    }
    lv_obj_add_style(label_scr_rate_value, &style_numeric_large, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_scr_rate_value, lv_color_hex(COLOR_GSR_TEAL), LV_PART_MAIN);

    // Inline /min unit
    lv_obj_t *label_scr_unit = lv_label_create(cont_scr);
    lv_label_set_text(label_scr_unit, " /min");
    lv_obj_add_style(label_scr_unit, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_scr_unit, lv_color_hex(COLOR_GSR_TEAL), LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(label_scr_unit, 8, LV_PART_MAIN);  // Baseline align with large number

    // STATUS: Last measurement time at y=285
    label_gsr_last_update = lv_label_create(scr_gsr);
    if (!has_data) {
        lv_label_set_text(label_gsr_last_update, "No measurement yet");
    } else {
        char last_meas_str[25];
        hpi_helper_get_relative_time_str(gsr_last_update_stored, last_meas_str, sizeof(last_meas_str));
        lv_label_set_text(label_gsr_last_update, last_meas_str);
    }
    lv_obj_set_pos(label_gsr_last_update, 0, 285);
    lv_obj_set_width(label_gsr_last_update, 390);
    lv_obj_set_style_text_color(label_gsr_last_update, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_add_style(label_gsr_last_update, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_gsr_last_update, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // BOTTOM: Start GSR button at y=315
    const int btn_width = 200;
    const int btn_height = 55;
    const int btn_y = 315;

    btn_gsr_measure = hpi_btn_create_primary(scr_gsr);
    lv_obj_set_size(btn_gsr_measure, btn_width, btn_height);
    lv_obj_set_pos(btn_gsr_measure, (390 - btn_width) / 2, btn_y);
    lv_obj_set_style_radius(btn_gsr_measure, 30, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn_gsr_measure, lv_color_hex(COLOR_GSR_TEAL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_gsr_measure, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_gsr_measure, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn_gsr_measure, 0, LV_PART_MAIN);

    lv_obj_t *label_btn_gsr_measure = lv_label_create(btn_gsr_measure);
    lv_label_set_text(label_btn_gsr_measure, LV_SYMBOL_PLAY " Start GSR");
    lv_obj_center(label_btn_gsr_measure);
    lv_obj_set_style_text_color(label_btn_gsr_measure, lv_color_white(), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_gsr_measure, scr_gsr_measure_btn_event_handler, LV_EVENT_CLICKED, NULL);

    hpi_disp_set_curr_screen(SCR_GSR);
    hpi_show_screen(scr_gsr, m_scroll_dir);
}

/**
 * @brief Update GSR display with stress index data
 * @param stress_index Stress index data structure with all metrics
 *
 * Note: Stress level display temporarily hidden pending validation
 */
void hpi_gsr_update_stress_display(const struct hpi_gsr_stress_index_t *stress_index)
{
    if (!stress_index || !stress_index->stress_data_ready) {
        return;
    }

    // Update SCL (tonic level) - convert from x100 format, rounded to 1 decimal place
    if (label_scl_value != NULL) {
        // Round to 1 decimal place: add 5 to x100 value, then use x10 for display
        uint16_t scl_x10_rounded = (stress_index->tonic_level_x100 + 5) / 10;
        lv_label_set_text_fmt(label_scl_value, "%u.%u", scl_x10_rounded / 10, scl_x10_rounded % 10);
    }

    // Update SCR rate (peaks per minute), number only (unit is separate label)
    if (label_scr_rate_value != NULL) {
        lv_label_set_text_fmt(label_scr_rate_value, "%u", stress_index->peaks_per_minute);
    }
}
