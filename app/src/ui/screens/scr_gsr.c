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

// Extern semaphore declarations for GSR control
extern struct k_sem sem_gsr_start;
extern struct k_sem sem_gsr_cancel;

// GUI Objects
lv_obj_t *scr_gsr;
static lv_obj_t *label_gsr_current;

//static lv_obj_t *label_gsr_status;
static lv_obj_t *btn_gsr_measure;
//static lv_obj_t *label_gsr_last_update;
static lv_obj_t *arc_gsr_range;  // Add arc reference

static lv_obj_t *label_scr_count;  // Add this at the top with other GUI objects
static lv_obj_t *label_scr_title;

#if defined(CONFIG_HPI_GSR_STRESS_INDEX)
static lv_obj_t *label_stress_level;
static lv_obj_t *label_stress_value;
#endif

// GSR Data Management
static uint16_t gsr_baseline_x100 = 1000;  // 10.00 μS default baseline
static uint8_t baseline_sample_count = 0;
static bool gsr_measurement_active = false;
static int64_t gsr_measurement_start_ts = 0; // uptime (ms) when measurement started

// Function declarations
static void calculate_gsr_baseline(uint16_t new_value);
//static void update_gsr_status(uint16_t current_value);
static void scr_gsr_measure_btn_event_handler(lv_event_t *e);
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
/*static void update_gsr_status(uint16_t current_value)
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
    */

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
            // Start GSR live view - no calculation, just monitoring
            gsr_measurement_active = true;
            gsr_measurement_start_ts = k_uptime_get();
            lv_label_set_text(lv_obj_get_child(btn_gsr_measure, 0), LV_SYMBOL_STOP " Stop");
            //lv_label_set_text(label_gsr_status, "Live Monitoring");
            
            // Open live plot screen
            hpi_load_scr_spl(SCR_SPL_PLOT_GSR, SCROLL_UP, (uint32_t)SCR_GSR, 0, 0, 0);
            
            // Start GSR live view via semaphore
            k_msleep(500);  // Allow screen transition
            k_sem_give(&sem_gsr_start);
        } else {
            // Stop GSR live view
            gsr_measurement_active = false;
            gsr_measurement_start_ts = 0;
            lv_label_set_text(lv_obj_get_child(btn_gsr_measure, 0), LV_SYMBOL_EYE_OPEN " Live View");
         //   lv_label_set_text(label_gsr_status, "Live View Only");
            
            // Stop GSR via semaphore
            k_sem_give(&sem_gsr_cancel);
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
    
    // Only update if live view is active
    if (!gsr_measurement_active) {
        return;
    }
    
    // Display current value for live monitoring (no calculation)
    uint16_t integer_part = gsr_value_x100 / 100;
    uint16_t decimal_part = gsr_value_x100 % 100;
    lv_label_set_text_fmt(label_gsr_current, "%u.%02u", integer_part, decimal_part);
    
    // Update arc to show current value position
    if (arc_gsr_range != NULL) {
        lv_arc_set_value(arc_gsr_range, gsr_value_x100);
    }

    // Note: GSR data is sent to plot for live visualization
}

// // Safe async update for SCR count label
// static void update_scr_count_async(void *param)
// {
//     int scr_count = *((int*)param);
//     if(label_scr_count != NULL) {
//         lv_label_set_text_fmt(label_scr_count, "Last SCR Count: %d", scr_count);
//     }
//     k_free(param);
// }

// void update_scr_count(int scr_count)
// {
//     int *p_count = k_malloc(sizeof(int));
//     if(!p_count) return;
//     *p_count = scr_count;
//     lv_async_call(update_scr_count_async, p_count);
// }


void draw_scr_gsr(enum scroll_dir m_scroll_dir)
{
    scr_gsr = lv_obj_create(NULL);
    // AMOLED OPTIMIZATION: Pure black background for power efficiency
    lv_obj_set_style_bg_color(scr_gsr, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_gsr, LV_OBJ_FLAG_SCROLLABLE);

    // CIRCULAR AMOLED-OPTIMIZED GSR SCREEN
    // Display center: (195, 195), Usable radius: ~185px
    
    // Get GSR data first
    uint16_t gsr_value_stored = 0;
    int64_t gsr_last_update_stored = 0;
    bool has_data = (hpi_sys_get_last_gsr_update(&gsr_value_stored, &gsr_last_update_stored) == 0 && gsr_value_stored > 0);

    
    // OUTER RING: GSR Progress Arc (Radius 170-185px)
    arc_gsr_range = lv_arc_create(scr_gsr);
    lv_obj_set_size(arc_gsr_range, 370, 370);  // 185px radius
    lv_obj_center(arc_gsr_range);
    lv_arc_set_range(arc_gsr_range, 50, 5000);  // GSR range 0.5-50.0 μS (x100)
    
    // Background arc: Full 270° track (gray)
    lv_arc_set_bg_angles(arc_gsr_range, 135, 45);  // Full background arc
    
    // Indicator arc: Shows current GSR position from start
    lv_arc_set_angles(arc_gsr_range, 135, 135);  // Start at beginning, will extend based on value
    
    // Set arc value based on current GSR
    if (has_data) {
        lv_arc_set_value(arc_gsr_range, gsr_value_stored);
    } else {
        lv_arc_set_value(arc_gsr_range, 1000);  // Default position (10.0 μS)
    }
    
    // Style the progress arc
    lv_obj_set_style_arc_color(arc_gsr_range, lv_color_hex(0x333333), LV_PART_MAIN);    // Background track
    lv_obj_set_style_arc_width(arc_gsr_range, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_gsr_range, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_INDICATOR);  // Progress indicator
    lv_obj_set_style_arc_width(arc_gsr_range, 6, LV_PART_INDICATOR);
    lv_obj_remove_style(arc_gsr_range, NULL, LV_PART_KNOB);  // Remove knob
    lv_obj_clear_flag(arc_gsr_range, LV_OBJ_FLAG_CLICKABLE);

    // Screen title - clean and simple positioning
    lv_obj_t *label_title = lv_label_create(scr_gsr);
    lv_label_set_text(label_title, "GSR Monitor");
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 50);  // Centered at top, clear of arc
    lv_obj_add_style(label_title, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);

  /* // Status info - centered below unit with proper spacing
    label_gsr_status = lv_label_create(scr_gsr);
    lv_label_set_text(label_gsr_status, "Live View Only");
    lv_obj_align(label_gsr_status, LV_ALIGN_CENTER, 0, -10);  // Centered, better vertical spacing
    lv_obj_set_style_text_color(label_gsr_status, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN);
    lv_obj_add_style(label_gsr_status, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_gsr_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // Informational label - explain live view mode
    label_gsr_last_update = lv_label_create(scr_gsr);
    lv_label_set_long_mode(label_gsr_last_update, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_gsr_last_update, 280);
    lv_label_set_text(label_gsr_last_update, "Real-time monitoring without analysis");
    lv_obj_align(label_gsr_last_update, LV_ALIGN_CENTER, 0, 30);  // Centered, below status with better spacing
    lv_obj_set_style_text_color(label_gsr_last_update, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_add_style(label_gsr_last_update, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_gsr_last_update, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
 */
     
    // BOTTOM ZONE: Action Button (properly centered at bottom)
    btn_gsr_measure = hpi_btn_create_primary(scr_gsr);
    lv_obj_set_size(btn_gsr_measure, 180, 50);  // Width for "Measure" text
    lv_obj_align(btn_gsr_measure, LV_ALIGN_BOTTOM_MID, 0, -30);  // Centered at bottom with margin
    lv_obj_set_style_radius(btn_gsr_measure, 25, LV_PART_MAIN);
    
    // AMOLED-optimized button styling
    lv_obj_set_style_bg_color(btn_gsr_measure, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_gsr_measure, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_gsr_measure, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_gsr_measure, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN);
    lv_obj_set_style_border_opa(btn_gsr_measure, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn_gsr_measure, 0, LV_PART_MAIN);  // No shadow for AMOLED
    
    lv_obj_t *label_btn_gsr_measure = lv_label_create(btn_gsr_measure);
    lv_label_set_text(label_btn_gsr_measure, LV_SYMBOL_EYE_OPEN " Live View");
    lv_obj_center(label_btn_gsr_measure);
    lv_obj_set_style_text_color(label_btn_gsr_measure, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_gsr_measure, scr_gsr_measure_btn_event_handler, LV_EVENT_CLICKED, NULL);

    
   
//     // Inside draw_scr_gsr(), after creating other labels:
//     label_scr_count = lv_label_create(scr_gsr);
//     lv_label_set_text(label_scr_count, "Last SCR Count: --");
//    // lv_obj_align(label_scr_count, LV_ALIGN_BOTTOM_LEFT, 30, -100); // Position appropriately
//     lv_obj_align(label_scr_count, LV_ALIGN_CENTER, 0, 40);  // 80 px below center of screen/chart

//     lv_obj_set_style_text_color(label_scr_count, lv_color_white(), LV_PART_MAIN);
//     lv_obj_add_style(label_scr_count, &style_body_medium, LV_PART_MAIN);
//     // Apply the same style as GSR main value
   
//    //NOW update SCR value
//     extern int g_last_scr_count;
//     lv_label_set_text_fmt(label_scr_count, "Last SCR Count: %d", g_last_scr_count); 

    label_scr_title = lv_label_create(scr_gsr);
    lv_label_set_text(label_scr_title, "Last SCR Count");
    lv_obj_align(label_scr_title, LV_ALIGN_CENTER, 0, -60);   // Above the number

    lv_obj_set_style_text_color(label_scr_title, lv_color_white(), LV_PART_MAIN);
    lv_obj_add_style(label_scr_title, &style_body_medium, LV_PART_MAIN);

    label_scr_count = lv_label_create(scr_gsr);
    lv_label_set_text(label_scr_count, "--");
    lv_obj_align(label_scr_count, LV_ALIGN_CENTER, 0, 15);   // Below the title

    lv_obj_set_style_text_color(label_scr_count, lv_color_white(), LV_PART_MAIN);
    lv_obj_add_style(label_scr_count, &style_numeric_large, LV_PART_MAIN);  // BIG font

    extern int g_last_scr_count;
    lv_label_set_text_fmt(label_scr_count, "%d", g_last_scr_count);

     
     // Status info - centered below unit with proper spacing
    lv_obj_t *label_gsr_status = lv_label_create(scr_gsr);
    if (gsr_value_stored == 0) {
        lv_label_set_text(label_gsr_status, "GSR");
    } else {
        char last_meas_str[25];
        hpi_helper_get_relative_time_str(gsr_last_update_stored, last_meas_str, sizeof(last_meas_str));
        lv_label_set_text(label_gsr_status, last_meas_str);
    }
    lv_obj_align(label_gsr_status, LV_ALIGN_CENTER, 0, 80);  // Centered, below unit with gap
    lv_obj_set_style_text_color(label_gsr_status, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_add_style(label_gsr_status, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_gsr_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);


  
#if defined(CONFIG_HPI_GSR_STRESS_INDEX)
    // Stress level indicator - positioned above button
    label_stress_level = lv_label_create(scr_gsr);
    lv_label_set_text(label_stress_level, "Stress");
    lv_obj_align(label_stress_level, LV_ALIGN_BOTTOM_LEFT, 30, -100);
    lv_obj_set_style_text_color(label_stress_level, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_add_style(label_stress_level, &style_caption, LV_PART_MAIN);

    label_stress_value = lv_label_create(scr_gsr);
    lv_label_set_text(label_stress_value, "--");
    lv_obj_align(label_stress_value, LV_ALIGN_BOTTOM_RIGHT, -30, -100);
    lv_obj_set_style_text_color(label_stress_value, lv_color_white(), LV_PART_MAIN);
    lv_obj_add_style(label_stress_value, &style_body_medium, LV_PART_MAIN);
#endif

    hpi_disp_set_curr_screen(SCR_GSR);
    hpi_show_screen(scr_gsr, m_scroll_dir);
}

#if defined(CONFIG_HPI_GSR_STRESS_INDEX)
/**
 * @brief Update stress level display
 * @param stress_index Stress index data structure
 */
void hpi_gsr_update_stress_display(const struct hpi_gsr_stress_index_t *stress_index)
{
    if (!stress_index || !stress_index->stress_data_ready) {
        return;
    }

    if (label_stress_value == NULL) {
        return;
    }

    // Update stress level value
    lv_label_set_text_fmt(label_stress_value, "%u", stress_index->stress_level);

    // Color code based on stress level
    if (stress_index->stress_level < 30) {
        lv_obj_set_style_text_color(label_stress_value, lv_color_hex(0x00FF00), LV_PART_MAIN); // Green - Low
    } else if (stress_index->stress_level < 60) {
        lv_obj_set_style_text_color(label_stress_value, lv_color_hex(0xFFFF00), LV_PART_MAIN); // Yellow - Medium
    } else {
        lv_obj_set_style_text_color(label_stress_value, lv_color_hex(0xFF0000), LV_PART_MAIN); // Red - High
    }
}
#endif

