/*
 * HealthyPi Move GSR Screen
 * 
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 *
 * GSR (Galvanic Skin Response) display with measurement button
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

// GUI Objects
lv_obj_t *scr_gsr;
static lv_obj_t *label_gsr_current;
static lv_obj_t *label_gsr_status;
static lv_obj_t *btn_gsr_measure;
static lv_obj_t *label_gsr_last_update;

// GSR Data Management
static uint16_t gsr_baseline_x100 = 1000;  // 10.00 μS default baseline
static uint8_t baseline_sample_count = 0;
static bool gsr_measurement_active = false;

// Function declarations
static void calculate_gsr_baseline(uint16_t new_value);
static void update_gsr_status(uint16_t current_value);
static void scr_gsr_measure_btn_event_handler(lv_event_t *e);
void hpi_gsr_set_measurement_active(bool active);
void hpi_gsr_disp_plot_add_sample(uint16_t gsr_value_x100);

// Externs
extern lv_style_t style_white_large_numeric;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;

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
 * @brief Update GSR status based on recent measurements
 * @param current_value Current GSR value for status analysis
 */
static void update_gsr_status(uint16_t current_value)
{
    if (label_gsr_status == NULL) return;
    
    if (baseline_sample_count < 5) {
        lv_label_set_text(label_gsr_status, "Calibrating...");
        return;
    }
    
    int16_t trend_diff = current_value - gsr_baseline_x100;
    
    if (trend_diff > 50) {  // +0.50 uS increase
        lv_label_set_text(label_gsr_status, "Up");
    } else if (trend_diff < -50) {  // -0.50 uS decrease
        lv_label_set_text(label_gsr_status, "Down");
    } else {
        lv_label_set_text(label_gsr_status, "Normal");
    }
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
        if (!gsr_measurement_active) {
            gsr_measurement_active = true;
            lv_label_set_text(lv_obj_get_child(btn_gsr_measure, 0), "Stop");
            lv_label_set_text(label_gsr_status, "Measuring...");
            // Open plot screen
            hpi_load_scr_spl(SCR_SPL_PLOT_GSR, SCROLL_UP, (uint32_t)SCR_GSR, 0, 0, 0);
        } else {
            gsr_measurement_active = false;
            lv_label_set_text(lv_obj_get_child(btn_gsr_measure, 0), "Measure");
            lv_label_set_text(label_gsr_status, "Ready");
        }
    }
}

/**
 * @brief Update GSR display with new measurement data (integer-only)
 * @param gsr_value_x100 GSR value multiplied by 100
 * @param gsr_last_update Timestamp of measurement
 */
void hpi_gsr_disp_update_gsr_int(uint16_t gsr_value_x100, int64_t gsr_last_update)
{
    if (label_gsr_current == NULL) {
        return;
    }
    
    // Only update if measurement is active
    if (!gsr_measurement_active) {
        return;
    }
    
    // Update baseline calculation
    calculate_gsr_baseline(gsr_value_x100);
    
    // Update current value display
    uint16_t integer_part = gsr_value_x100 / 100;
    uint16_t decimal_part = gsr_value_x100 % 100;
    char gsr_str[16];
    snprintf(gsr_str, sizeof(gsr_str), "%u.%02u uS", integer_part, decimal_part);
    lv_label_set_text(label_gsr_current, gsr_str);
    
    // Update status
    update_gsr_status(gsr_value_x100);

    // Forward to plot screen if visible
    hpi_gsr_disp_plot_add_sample(gsr_value_x100);
    
    // Update last measurement time
    if (label_gsr_last_update != NULL) {
        char last_meas_str[25];
        hpi_helper_get_relative_time_str(gsr_last_update, last_meas_str, sizeof(last_meas_str));
        lv_label_set_text(label_gsr_last_update, last_meas_str);
    }
}

void hpi_gsr_set_measurement_active(bool active)
{
    gsr_measurement_active = active;
}

void draw_scr_gsr(enum scroll_dir m_scroll_dir)
{
    scr_gsr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr_gsr, LV_OBJ_FLAG_SCROLLABLE);
    draw_scr_common(scr_gsr);

    // Main container with column layout
    lv_obj_t *cont_col = lv_obj_create(scr_gsr);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(cont_col, &style_scr_black, 0);

    // Title section
    lv_obj_t *label_title = lv_label_create(cont_col);
    lv_label_set_text(label_title, "GSR");
    lv_obj_add_style(label_title, &style_white_medium, 0);

    // Current value display container
    lv_obj_t *cont_gsr = lv_obj_create(cont_col);
    lv_obj_set_size(cont_gsr, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_gsr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_gsr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(cont_gsr, 0, LV_PART_MAIN);
    lv_obj_add_style(cont_gsr, &style_scr_black, 0);
    lv_obj_clear_flag(cont_gsr, LV_OBJ_FLAG_SCROLLABLE);

    // GSR icon (using a heart icon as placeholder)
    lv_obj_t *img_gsr = lv_img_create(cont_gsr);
    lv_img_set_src(img_gsr, &ecg_70);  // Reuse ECG icon for now

    // Current value display
    label_gsr_current = lv_label_create(cont_gsr);
    lv_label_set_text(label_gsr_current, "-- uS");
    lv_obj_add_style(label_gsr_current, &style_white_large_numeric, 0);

    // Status indicator
    label_gsr_status = lv_label_create(cont_col);
    lv_label_set_text(label_gsr_status, "Ready");
    lv_obj_add_style(label_gsr_status, &style_white_medium, 0);

    // Last update time
    label_gsr_last_update = lv_label_create(cont_col);
    lv_label_set_text(label_gsr_last_update, "");
    lv_obj_add_style(label_gsr_last_update, &style_white_medium, 0);
    lv_obj_set_style_text_align(label_gsr_last_update, LV_TEXT_ALIGN_CENTER, 0);

    // Measure button
    btn_gsr_measure = hpi_btn_create(cont_col);
    lv_obj_add_event_cb(btn_gsr_measure, scr_gsr_measure_btn_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_set_height(btn_gsr_measure, 80);

    lv_obj_t *label_btn_gsr_measure = lv_label_create(btn_gsr_measure);
    lv_label_set_text(label_btn_gsr_measure, "Measure");
    lv_obj_center(label_btn_gsr_measure);

    // Try to load existing GSR data if available
    uint16_t gsr_value_stored = 0;
    int64_t gsr_last_update_stored = 0;
    
    if (hpi_sys_get_last_gsr_update(&gsr_value_stored, &gsr_last_update_stored) == 0 && gsr_value_stored > 0) {
        // Show stored value but don't start measurement
        uint16_t integer_part = gsr_value_stored / 100;
        uint16_t decimal_part = gsr_value_stored % 100;
        char gsr_str[16];
    snprintf(gsr_str, sizeof(gsr_str), "%u.%02u uS", integer_part, decimal_part);
        lv_label_set_text(label_gsr_current, gsr_str);
        
        char last_meas_str[25];
        hpi_helper_get_relative_time_str(gsr_last_update_stored, last_meas_str, sizeof(last_meas_str));
        lv_label_set_text(label_gsr_last_update, last_meas_str);
    }

    hpi_disp_set_curr_screen(SCR_GSR);
    hpi_show_screen(scr_gsr, m_scroll_dir);
}

/**
 * @brief Convert BioZ measurement to GSR value (simplified integer math)
 * @param bioz_raw Raw BioZ value from MAX30001
 * @return GSR value in microsiemens * 100 (0 if invalid)
 */
static uint16_t convert_bioz_to_gsr(int32_t bioz_raw)
{
    // Input validation
    if (bioz_raw == 0) {
        return 0;
    }
    
    // Simple conversion avoiding floating point
    // BioZ impedance is inversely related to conductance
    // GSR (conductance) = 1 / resistance
    
    uint32_t abs_bioz = (bioz_raw < 0) ? -bioz_raw : bioz_raw;
    
    // Avoid division by zero and very small values
    if (abs_bioz < 100) {
        return 0;
    }
    
    // Simple inverse relationship with scaling
    // Target range: 0.5 to 50 μS (50 to 5000 when x100)
    uint32_t gsr_x100;
    
    if (abs_bioz > 10000) {
        // Low impedance = High conductance
        gsr_x100 = 300000 / abs_bioz;  // Scale factor for reasonable range
    } else {
        // Medium impedance = Medium conductance  
        gsr_x100 = 150000 / abs_bioz;
    }
    
    // Clamp to reasonable GSR range (0.5 to 50 μS)
    if (gsr_x100 < 50) gsr_x100 = 50;    // Min 0.50 μS
    if (gsr_x100 > 5000) gsr_x100 = 5000; // Max 50.00 μS
    
    return (uint16_t)gsr_x100;
}

/**
 * @brief Process new BioZ sample and update GSR trends
 * @param bioz_sample Raw BioZ sample from MAX30001
 */
void hpi_gsr_process_bioz_sample(int32_t bioz_sample)
{
    uint16_t gsr_value_x100 = convert_bioz_to_gsr(bioz_sample);
    
    if (gsr_value_x100 > 0) {
        int64_t current_time = k_uptime_get();
        
        // Store GSR value in system
        hpi_sys_set_last_gsr_update(gsr_value_x100, hw_get_sys_time_ts());
        
        // Update display if currently on GSR screen
        if (hpi_disp_get_curr_screen() == SCR_GSR) {
            hpi_gsr_disp_update_gsr_int(gsr_value_x100, current_time);
        }
    }
}
