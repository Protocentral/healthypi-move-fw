/*
 * HealthyPi Move GSR Results Screen
 * 
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Displays GSR measurement results after completion
 */

// #include <zephyr/kernel.h>
// #include <zephyr/logging/log.h>
// #include <lvgl.h>
// #include <stdio.h>

// #include "hpi_common_types.h"
// #include "ui/move_ui.h"
// #include "hpi_sys.h"

//LOG_MODULE_REGISTER(hpi_disp_scr_gsr_complete, LOG_LEVEL_DBG);



#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <lvgl.h>
#include <stdio.h>

#include "hpi_common_types.h"
#include "ui/move_ui.h"
#include "hw_module.h"
#include "hpi_sys.h"

lv_obj_t *scr_gsr_complete;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_body_medium;
extern lv_style_t style_caption;

void draw_scr_gsr_complete(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_gsr_complete = lv_obj_create(NULL);
    // AMOLED OPTIMIZATION: Pure black background for power efficiency
    lv_obj_set_style_bg_color(scr_gsr_complete, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_gsr_complete, LV_OBJ_FLAG_SCROLLABLE);

    // CIRCULAR AMOLED-OPTIMIZED ECG COMPLETE SCREEN
    // Display center: (195, 195), Usable radius: ~185px
    // Clean completion display matching design philosophy

    // Screen title - optimized position
    lv_obj_t *label_title = lv_label_create(scr_gsr_complete);
    lv_label_set_text(label_title, "GSR Recording");
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 60);  // Better spacing from top
    lv_obj_add_style(label_title, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);

    // Success icon - centered
    lv_obj_t *img_success = lv_img_create(scr_gsr_complete);
    lv_img_set_src(img_success, &img_complete_85);
    lv_obj_align(img_success, LV_ALIGN_CENTER, 0, -20);  // Centered vertically

    // Status message - below icon
    lv_obj_t *label_status = lv_label_create(scr_gsr_complete);
    lv_label_set_text(label_status, "Complete");
    lv_obj_align(label_status, LV_ALIGN_CENTER, 0, 60);  // Below icon
    lv_obj_add_style(label_status, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_status, lv_color_hex(0x00FF00), LV_PART_MAIN);  // Green for success
    lv_obj_set_style_text_align(label_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // Information text - bottom area
    lv_obj_t *label_info = lv_label_create(scr_gsr_complete);
    lv_label_set_text(label_info, "Download recording from app");
    lv_obj_align(label_info, LV_ALIGN_BOTTOM_MID, 0, -50);  // Bottom with margin
    lv_obj_add_style(label_info, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_info, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(label_info, 280);  // Constrain width for text wrapping
    lv_label_set_long_mode(label_info, LV_LABEL_LONG_WRAP);

    hpi_disp_set_curr_screen(SCR_SPL_GSR_COMPLETE);
    hpi_show_screen(scr_gsr_complete, m_scroll_dir);
    
}

void gesture_down_scr_gsr_complete(void)
{
    // Handle gesture down event
    hpi_load_screen(SCR_GSR, SCROLL_DOWN);
}


// // GUI Objects
// static lv_obj_t *scr_gsr_complete;
// static lv_obj_t *label_baseline_value;
// static lv_obj_t *label_scr_value;
// static lv_obj_t *label_peak_value;
// static lv_obj_t *label_baseline_context;
// static lv_obj_t *label_scr_context;
// static lv_obj_t *label_peak_context;
// static lv_obj_t *btn_done;

// // Externs - Modern style system
// extern lv_style_t style_body_medium;
// extern lv_style_t style_numeric_large;
// extern lv_style_t style_caption;

// // Store results
// static struct hpi_gsr_stress_index_t stored_results = {0};

// /**
//  * @brief Get baseline (tonic level) interpretation
//  * Typical palmar GSR baseline: 2-20 μS
//  * Higher baseline = higher arousal/activation state
//  */
// static const char* get_baseline_context(uint16_t tonic_level_x100)
// {
//     if (tonic_level_x100 < 200) {         // < 2 μS
//         return "Very Low";
//     } else if (tonic_level_x100 < 500) {  // 2-5 μS
//         return "Low";
//     } else if (tonic_level_x100 < 1000) { // 5-10 μS
//         return "Normal";
//     } else if (tonic_level_x100 < 1500) { // 10-15 μS
//         return "Elevated";
//     } else {                              // > 15 μS
//         return "High";
//     }
// }

// /**
//  * @brief Get SCR frequency interpretation
//  * Typical SCR frequency at rest: 1-3 /min
//  * During stress/activity: can increase to 10+ /min
//  */
// static const char* get_scr_frequency_context(uint8_t peaks_per_minute)
// {
//     if (peaks_per_minute == 0) {
//         return "None";
//     } else if (peaks_per_minute <= 2) {
//         return "Low";
//     } else if (peaks_per_minute <= 5) {
//         return "Moderate";
//     } else if (peaks_per_minute <= 8) {
//         return "Active";
//     } else {
//         return "Very Active";
//     }
// }

// /**
//  * @brief Get peak amplitude interpretation
//  * Typical SCR amplitude: 0.1-1.0 μS
//  * Strong responses can exceed 2 μS
//  */
// static const char* get_peak_amplitude_context(uint16_t mean_peak_amplitude_x100)
// {
//     if (mean_peak_amplitude_x100 < 10) {      // < 0.10 μS
//         return "Minimal";
//     } else if (mean_peak_amplitude_x100 < 50) { // 0.10-0.50 μS
//         return "Small";
//     } else if (mean_peak_amplitude_x100 < 100) { // 0.50-1.00 μS
//         return "Typical";
//     } else if (mean_peak_amplitude_x100 < 200) { // 1.00-2.00 μS
//         return "Strong";
//     } else {                                   // > 2.00 μS
//         return "Very Strong";
//     }
// }

// /**
//  * @brief Done button event handler
//  */
// static void scr_gsr_complete_done_btn_event_handler(lv_event_t *e)
// {
//     lv_event_code_t code = lv_event_get_code(e);
//     if (code == LV_EVENT_CLICKED) {
//         hpi_load_screen(SCR_GSR, SCROLL_DOWN);
//     }
// }

// /**
//  * @brief Update display with GSR results
//  */
// void hpi_gsr_complete_update_results(const struct hpi_gsr_stress_index_t *results)
// {
//     if (!results || !results->stress_data_ready) {
//         return;
//     }

//     // Store results
//     memcpy(&stored_results, results, sizeof(struct hpi_gsr_stress_index_t));

//     if (!scr_gsr_complete) {
//         return;  // Screen not created yet
//     }

//     // Update baseline (tonic level)
//     if (label_baseline_value) {
//         lv_label_set_text_fmt(label_baseline_value, "%d.%02d", 
//                                results->tonic_level_x100 / 100, 
//                                results->tonic_level_x100 % 100);
//     }
//     if (label_baseline_context) {
//         lv_label_set_text(label_baseline_context, get_baseline_context(results->tonic_level_x100));
//     }

//     // Update SCR frequency
//     if (label_scr_value) {
//         lv_label_set_text_fmt(label_scr_value, "%d", results->peaks_per_minute);
//     }
//     if (label_scr_context) {
//         lv_label_set_text(label_scr_context, get_scr_frequency_context(results->peaks_per_minute));
//     }

//     // Update peak amplitude
//     if (label_peak_value) {
//         lv_label_set_text_fmt(label_peak_value, "%d.%02d", 
//                                results->mean_peak_amplitude_x100 / 100,
//                                results->mean_peak_amplitude_x100 % 100);
//     }
//     if (label_peak_context) {
//         lv_label_set_text(label_peak_context, get_peak_amplitude_context(results->mean_peak_amplitude_x100));
//     }
// }

// void draw_scr_gsr_complete(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
// {
//     LV_UNUSED(arg1); LV_UNUSED(arg2); LV_UNUSED(arg3); LV_UNUSED(arg4);

//     scr_gsr_complete = lv_obj_create(NULL);
//     // AMOLED OPTIMIZATION: Pure black background for power efficiency
//     lv_obj_set_style_bg_color(scr_gsr_complete, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_clear_flag(scr_gsr_complete, LV_OBJ_FLAG_SCROLLABLE);

//     // CIRCULAR AMOLED-OPTIMIZED GSR RESULTS SCREEN (BLUE THEME)
//     // Display center: (195, 195), Usable radius: ~185px
//     // Three-metric stacked layout following HR/SpO2 patterns

//     // Screen title - clean and simple positioning
//     lv_obj_t *label_title = lv_label_create(scr_gsr_complete);
//     lv_label_set_text(label_title, "GSR Results");
//     lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 20);
//     lv_obj_add_style(label_title, &style_body_medium, LV_PART_MAIN);
//     lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
//     lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);

//     // ===== BASELINE (SCL) SECTION - Top metric =====
//     // Label: Baseline (SCL)
//     lv_obj_t *label_baseline_title = lv_label_create(scr_gsr_complete);
//     lv_label_set_text(label_baseline_title, "Baseline");
//     lv_obj_align(label_baseline_title, LV_ALIGN_TOP_MID, 0, 65);
//     lv_obj_add_style(label_baseline_title, &style_caption, LV_PART_MAIN);
//     lv_obj_set_style_text_color(label_baseline_title, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);

//     // Value: X.XX
//     label_baseline_value = lv_label_create(scr_gsr_complete);
//     lv_label_set_text(label_baseline_value, "0.00");
//     lv_obj_align(label_baseline_value, LV_ALIGN_TOP_MID, 0, 90);
//     lv_obj_add_style(label_baseline_value, &style_numeric_large, LV_PART_MAIN);
//     lv_obj_set_style_text_color(label_baseline_value, lv_color_white(), LV_PART_MAIN);

//     // Unit: μS + Context
//     lv_obj_t *label_baseline_unit = lv_label_create(scr_gsr_complete);
//     lv_label_set_text(label_baseline_unit, "μS");
//     lv_obj_align(label_baseline_unit, LV_ALIGN_TOP_MID, 40, 110);
//     lv_obj_add_style(label_baseline_unit, &style_caption, LV_PART_MAIN);
//     lv_obj_set_style_text_color(label_baseline_unit, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN);

//     label_baseline_context = lv_label_create(scr_gsr_complete);
//     lv_label_set_text(label_baseline_context, "Normal");
//     lv_obj_align(label_baseline_context, LV_ALIGN_TOP_MID, 0, 135);
//     lv_obj_add_style(label_baseline_context, &style_caption, LV_PART_MAIN);
//     lv_obj_set_style_text_color(label_baseline_context, lv_color_hex(0x9E9E9E), LV_PART_MAIN);

//     // ===== SCR FREQUENCY SECTION - Middle metric =====
//     // Label: SCR Frequency
//     lv_obj_t *label_scr_title = lv_label_create(scr_gsr_complete);
//     lv_label_set_text(label_scr_title, "SCR Frequency");
//     lv_obj_align(label_scr_title, LV_ALIGN_CENTER, 0, -35);
//     lv_obj_add_style(label_scr_title, &style_caption, LV_PART_MAIN);
//     lv_obj_set_style_text_color(label_scr_title, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);

//     // Value: X
//     label_scr_value = lv_label_create(scr_gsr_complete);
//     lv_label_set_text(label_scr_value, "0");
//     lv_obj_align(label_scr_value, LV_ALIGN_CENTER, 0, -10);
//     lv_obj_add_style(label_scr_value, &style_numeric_large, LV_PART_MAIN);
//     lv_obj_set_style_text_color(label_scr_value, lv_color_white(), LV_PART_MAIN);

//     // Unit: /min + Context
//     lv_obj_t *label_scr_unit = lv_label_create(scr_gsr_complete);
//     lv_label_set_text(label_scr_unit, "/min");
//     lv_obj_align(label_scr_unit, LV_ALIGN_CENTER, 0, 30);
//     lv_obj_add_style(label_scr_unit, &style_caption, LV_PART_MAIN);
//     lv_obj_set_style_text_color(label_scr_unit, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN);

//     label_scr_context = lv_label_create(scr_gsr_complete);
//     lv_label_set_text(label_scr_context, "Low");
//     lv_obj_align(label_scr_context, LV_ALIGN_CENTER, 0, 50);
//     lv_obj_add_style(label_scr_context, &style_caption, LV_PART_MAIN);
//     lv_obj_set_style_text_color(label_scr_context, lv_color_hex(0x9E9E9E), LV_PART_MAIN);

//     // ===== PEAK AMPLITUDE SECTION - Bottom metric =====
//     // Label: Peak Amplitude
//     lv_obj_t *label_peak_title = lv_label_create(scr_gsr_complete);
//     lv_label_set_text(label_peak_title, "Peak Amplitude");
//     lv_obj_align(label_peak_title, LV_ALIGN_BOTTOM_MID, 0, -125);
//     lv_obj_add_style(label_peak_title, &style_caption, LV_PART_MAIN);
//     lv_obj_set_style_text_color(label_peak_title, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);

//     // Value: X.XX
//     label_peak_value = lv_label_create(scr_gsr_complete);
//     lv_label_set_text(label_peak_value, "0.00");
//     lv_obj_align(label_peak_value, LV_ALIGN_BOTTOM_MID, 0, -100);
//     lv_obj_set_style_text_font(label_peak_value, &lv_font_montserrat_24, LV_PART_MAIN);
//     lv_obj_set_style_text_color(label_peak_value, lv_color_white(), LV_PART_MAIN);

//     // Unit: μS + Context
//     lv_obj_t *label_peak_unit = lv_label_create(scr_gsr_complete);
//     lv_label_set_text(label_peak_unit, "μS");
//     lv_obj_align(label_peak_unit, LV_ALIGN_BOTTOM_MID, 35, -80);
//     lv_obj_add_style(label_peak_unit, &style_caption, LV_PART_MAIN);
//     lv_obj_set_style_text_color(label_peak_unit, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN);

//     label_peak_context = lv_label_create(scr_gsr_complete);
//     lv_label_set_text(label_peak_context, "Typical");
//     lv_obj_align(label_peak_context, LV_ALIGN_BOTTOM_MID, 0, -60);
//     lv_obj_add_style(label_peak_context, &style_caption, LV_PART_MAIN);
//     lv_obj_set_style_text_color(label_peak_context, lv_color_hex(0x9E9E9E), LV_PART_MAIN);

//     // ===== BOTTOM ZONE: Done Button =====
//     btn_done = hpi_btn_create_primary(scr_gsr_complete);
//     lv_obj_set_size(btn_done, 120, 40);
//     lv_obj_align(btn_done, LV_ALIGN_BOTTOM_MID, 0, -10);
//     lv_obj_add_event_cb(btn_done, scr_gsr_complete_done_btn_event_handler, LV_EVENT_CLICKED, NULL);
//     lv_obj_set_style_radius(btn_done, 20, LV_PART_MAIN);
    
//     // AMOLED-optimized button styling - blue theme
//     lv_obj_set_style_bg_color(btn_done, lv_color_hex(0x000000), LV_PART_MAIN);
//     lv_obj_set_style_bg_opa(btn_done, LV_OPA_COVER, LV_PART_MAIN);
//     lv_obj_set_style_border_width(btn_done, 2, LV_PART_MAIN);
//     lv_obj_set_style_border_color(btn_done, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN);
//     lv_obj_set_style_border_opa(btn_done, LV_OPA_80, LV_PART_MAIN);
//     lv_obj_set_style_shadow_width(btn_done, 0, LV_PART_MAIN);

//     lv_obj_t *label_done = lv_label_create(btn_done);
//     lv_label_set_text(label_done, LV_SYMBOL_OK " Done");
//     lv_obj_center(label_done);
//     lv_obj_set_style_text_color(label_done, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN);

//     // Update with stored results if available
//     if (stored_results.stress_data_ready) {
//         hpi_gsr_complete_update_results(&stored_results);
//     }

//     // Load screen with animation
//     hpi_show_screen(scr_gsr_complete, m_scroll_dir);
// }

// void unload_scr_gsr_complete(void)
// {
//     if (scr_gsr_complete != NULL) {
//         lv_obj_del(scr_gsr_complete);
//         scr_gsr_complete = NULL;
//         label_baseline_value = NULL;
//         label_scr_value = NULL;
//         label_peak_value = NULL;
//         label_baseline_context = NULL;
//         label_scr_context = NULL;
//         label_peak_context = NULL;
//         btn_done = NULL;
//     }
// }
//  */