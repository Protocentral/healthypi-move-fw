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


#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <lvgl.h>
#include <stdio.h>

#include "hpi_common_types.h"
#include "ui/move_ui.h"
#include "hw_module.h"
#include "hpi_sys.h"

LOG_MODULE_REGISTER(hpi_disp_scr_hrv_eval_progress, LOG_LEVEL_DBG);

lv_obj_t *scr_hrv_eval_progress;
// UI elements
static lv_obj_t *label_timer = NULL;
static lv_obj_t *label_ecg_lead_off = NULL;
static lv_obj_t *chart_ecg = NULL;
static lv_chart_series_t *ser_ecg = NULL;
static lv_obj_t *arc_hrv_zone = NULL;
static lv_obj_t *label_intervals_count = NULL;
//static lv_obj_t *label_ecg_info = NULL;

// ECG chart state
static float y_max_ecg = -10000;
static float y_min_ecg = 10000;
static int32_t batch_data[32] __attribute__((aligned(4)));
static uint32_t batch_count = 0;
static uint32_t sample_counter = 0;
static const uint32_t RANGE_UPDATE_INTERVAL = 128;

// Lead and stabilization state
static bool timer_running = false;
static bool timer_paused = true;
static bool lead_on_detected = false;

// Externs
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_body_medium;
extern lv_style_t style_caption;
extern lv_style_t style_numeric_large;

K_MUTEX_DEFINE(Lead_on_off_handler_mutex);
extern struct k_sem sem_hrv_eval_cancel;

// HRV evaluation parameters
#define HRV_MEASUREMENT_DURATION_S 15  

// R-to-R interval data
extern volatile uint16_t hrv_interval_count;
extern struct hpi_hrv_interval_t hrv_intervals[];

// Track measurement state
static int64_t hrv_measurement_start_time = 0;
static bool scr_hrv_progress_active = false;  // Track if screen is currently active
static bool hrv_plot_enabled = true; 

void hpi_hrv_disp_update_timer(uint16_t remaining_s)
{
     // Check if screen is still active before updating
    if (!scr_hrv_progress_active) {
        return;
    }
    
    // Optimize with caching to avoid unnecessary LVGL updates
    static uint16_t last_remaining = 0xFFFF;
    if (remaining_s != last_remaining) {
        last_remaining = remaining_s;
        lv_label_set_text_fmt(label_timer, "%02u", remaining_s);
        if (arc_hrv_zone != NULL) {
            lv_arc_set_value(arc_hrv_zone, (HRV_MEASUREMENT_DURATION_S - remaining_s));
        }
    }

    // Update interval count display
    if (label_intervals_count != NULL) {
        lv_label_set_text_fmt(label_intervals_count, "Intervals: %d", hrv_interval_count);
    }
}
void draw_scr_spl_hrv_eval_progress(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_hrv_eval_progress = lv_obj_create(NULL);
    // AMOLED OPTIMIZATION: Pure black background for power efficiency
    lv_obj_set_style_bg_color(scr_hrv_eval_progress, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_hrv_eval_progress, LV_OBJ_FLAG_SCROLLABLE);

    // Initialize state tracking
    scr_hrv_progress_active = true;  // Mark screen as active
    hrv_measurement_start_time = k_uptime_get();  // Record screen creation time
    timer_running = false;
    timer_paused = true;
    lead_on_detected = false;
    y_max_ecg = -10000;
    y_min_ecg = 10000;

    // OUTER RING: HRV Timer Countdown Arc (Radius 170-185px) - Pink theme for HRV
    arc_hrv_zone = lv_arc_create(scr_hrv_eval_progress);
    lv_obj_set_size(arc_hrv_zone, 370, 370);  // 185px radius
    lv_obj_center(arc_hrv_zone);
    lv_arc_set_range(arc_hrv_zone,0, HRV_MEASUREMENT_DURATION_S);
    lv_arc_set_bg_angles(arc_hrv_zone, 135, 45);  // Full background arc
    lv_arc_set_value(arc_hrv_zone, HRV_MEASUREMENT_DURATION_S);  // Start at full duration
    
    // Style the progress arc - pink/magenta theme for HRV measurement
    lv_obj_set_style_arc_color(arc_hrv_zone, lv_color_hex(0x333333), LV_PART_MAIN);    // Background track
    lv_obj_set_style_arc_width(arc_hrv_zone, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_hrv_zone, lv_color_hex(0x8B0000), LV_PART_INDICATOR);  // Deep pink progress
    lv_obj_set_style_arc_width(arc_hrv_zone, 8, LV_PART_INDICATOR);
    lv_obj_remove_style(arc_hrv_zone, NULL, LV_PART_KNOB);  // Remove knob
    lv_obj_clear_flag(arc_hrv_zone, LV_OBJ_FLAG_CLICKABLE);

    // MID-UPPER RING: Timer container with icon
    lv_obj_t *cont_timer = lv_obj_create(scr_hrv_eval_progress);
    lv_obj_set_size(cont_timer, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(cont_timer, LV_ALIGN_TOP_MID, 0, 85);
    lv_obj_set_style_bg_opa(cont_timer, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_timer, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont_timer, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont_timer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_timer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Timer Icon
    LV_IMG_DECLARE(timer_32);
    lv_obj_t *img_timer = lv_img_create(cont_timer);
    lv_img_set_src(img_timer, &timer_32);
    lv_obj_set_style_img_recolor(img_timer, lv_color_hex(0xFFFFFF), LV_PART_MAIN);   
    lv_obj_set_style_img_recolor_opa(img_timer, LV_OPA_COVER, LV_PART_MAIN);

    // Timer value
    label_timer = lv_label_create(cont_timer);
    //lv_label_set_text(label_timer, "30");
    lv_label_set_text_fmt(label_timer, "%d", HRV_MEASUREMENT_DURATION_S);
    lv_obj_add_style(label_timer, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_timer, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_pad_left(label_timer, 8, LV_PART_MAIN);

    // Timer unit
    lv_obj_t *label_timer_unit = lv_label_create(cont_timer);
    lv_label_set_text(label_timer_unit, "s");
    lv_obj_add_style(label_timer_unit, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_timer_unit, lv_color_white(), LV_PART_MAIN);  

    // CENTRAL ZONE: ECG Chart (positioned in center area - similar to scr_ecg_scr2)
    chart_ecg = lv_chart_create(scr_hrv_eval_progress);
    lv_obj_set_size(chart_ecg, 340, 100);  // Smaller chart for circular design
    lv_obj_align(chart_ecg, LV_ALIGN_CENTER, 0, -10);  // Centered position
    
    // Configure chart type and fundamental properties
    lv_chart_set_type(chart_ecg, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart_ecg, ECG_DISP_WINDOW_SIZE);
    lv_chart_set_update_mode(chart_ecg, LV_CHART_UPDATE_MODE_CIRCULAR);
    
    // Set Y-axis range for ECG data
    lv_chart_set_range(chart_ecg, LV_CHART_AXIS_PRIMARY_Y, -5000, 5000);
    
    // Disable division lines for clean ECG display
    lv_chart_set_div_line_count(chart_ecg, 0, 0);
    
    // Configure main chart background (transparent for AMOLED)
    lv_obj_set_style_bg_opa(chart_ecg, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(chart_ecg, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(chart_ecg, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(chart_ecg, 5, LV_PART_MAIN);
    
    // Create series for ECG data - pink theme for HRV
    ser_ecg = lv_chart_add_series(chart_ecg, lv_color_hex(0x8B0000), LV_CHART_AXIS_PRIMARY_Y);
    
    // Configure line series styling - pink theme for HRV measurement
    lv_obj_set_style_line_width(chart_ecg, 3, LV_PART_ITEMS);
    lv_obj_set_style_line_color(chart_ecg, lv_color_hex(0x8B0000), LV_PART_ITEMS);
    lv_obj_set_style_line_opa(chart_ecg, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_line_rounded(chart_ecg, false, LV_PART_ITEMS);
    
    // Disable points completely
    lv_obj_set_style_width(chart_ecg, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(chart_ecg, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(chart_ecg, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_border_opa(chart_ecg, LV_OPA_TRANSP, LV_PART_INDICATOR);
    
    // Performance optimizations
    lv_obj_add_flag(chart_ecg, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(chart_ecg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(chart_ecg, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    
    // Initialize chart with baseline values
    lv_chart_set_all_value(chart_ecg, ser_ecg, 0); 
  
    batch_count = 0;
    sample_counter = 0;
    hrv_plot_enabled = true;
    LOG_INF("HRV Chart RESET: batch=%d samples=%d", batch_count, sample_counter);
            
    // Lead status display - overlay on chart area
    label_ecg_lead_off = lv_label_create(scr_hrv_eval_progress);
    lv_label_set_long_mode(label_ecg_lead_off, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_ecg_lead_off, 300);
    lv_label_set_text(label_ecg_lead_off, "Place fingers on electrodes\nTimer will start automatically");
    lv_obj_align(label_ecg_lead_off, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_style(label_ecg_lead_off, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_ecg_lead_off, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_ecg_lead_off, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);

    // HRV title and info at bottom
    lv_obj_t *label_hrv_title = lv_label_create(scr_hrv_eval_progress);
    lv_label_set_text(label_hrv_title, "HRV Analysis");
    lv_obj_align(label_hrv_title, LV_ALIGN_BOTTOM_MID, 0, -50);
    lv_obj_add_style(label_hrv_title, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_hrv_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_hrv_title, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);

    // R-to-R interval count display
    label_intervals_count = lv_label_create(scr_hrv_eval_progress);
    lv_label_set_text_fmt(label_intervals_count, "Intervals: %d", hrv_interval_count);
    lv_obj_align(label_intervals_count, LV_ALIGN_BOTTOM_MID, 0, -25);
    lv_obj_add_style(label_intervals_count, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_intervals_count, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_intervals_count, lv_color_hex(COLOR_SUCCESS_GREEN), LV_PART_MAIN);

    hpi_disp_set_curr_screen(SCR_SPL_HRV_EVAL_PROGRESS);
    hpi_show_screen(scr_hrv_eval_progress, m_scroll_dir);
}

void gesture_down_scr_spl_hrv_eval_progress(void)
{
    // Mark screen as inactive to prevent timer callback from updating
    printk("Exiting HRV Evaluation Progress Screen via gesture\n");
    unload_scr_hrv_eval_progress(); 
    hpi_hrv_timer_reset();
    k_sem_give(&sem_hrv_eval_cancel);
    // Return to HRV home screen
    hpi_load_screen(SCR_HRV, SCROLL_DOWN);
}

void hpi_ecg_disp_draw_plotECG_hrv(int32_t *data_ecg, int num_samples, bool ecg_lead_off)
{
    // Early validation - LVGL 9.2 best practice
    if (!hrv_plot_enabled || chart_ecg == NULL || ser_ecg == NULL || data_ecg == NULL || num_samples <= 0) {
        return;
    }

    if(ecg_lead_off)
    {
       
        return;
    }
    // Performance optimization: Skip processing if chart is hidden
    if (lv_obj_has_flag(chart_ecg, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    // Batch processing for efficiency
    for (int i = 0; i < num_samples; i++)
    {
        batch_data[batch_count++] = data_ecg[i];
        
        // Process batch when full or at end of samples
        if (batch_count >= 32 || i == num_samples - 1) {
            // Process batch
            for (uint32_t j = 0; j < batch_count; j++) {
                lv_chart_set_next_value(chart_ecg, ser_ecg, batch_data[j]);
                
                // Track min/max for auto-scaling
                if (batch_data[j] < y_min_ecg) y_min_ecg = batch_data[j];
                if (batch_data[j] > y_max_ecg) y_max_ecg = batch_data[j];
            }
            
            sample_counter += batch_count; // Fix: Update sample counter correctly
            batch_count = 0;
        }
    }
    
    // Auto-scaling logic
    if (sample_counter % RANGE_UPDATE_INTERVAL == 0) {
        if (y_max_ecg > y_min_ecg) {
            float range = y_max_ecg - y_min_ecg;
            float margin = range * 0.1f; // 10% margin
            
            int32_t new_min = (int32_t)(y_min_ecg - margin);
            int32_t new_max = (int32_t)(y_max_ecg + margin);
            
            // Ensure reasonable minimum range
            if ((new_max - new_min) < 1000) {
                int32_t center = (new_min + new_max) / 2;
                new_min = center - 500;
                new_max = center + 500;
            }
            
            lv_chart_set_range(chart_ecg, LV_CHART_AXIS_PRIMARY_Y, new_min, new_max);
        }
        
        // Reset for next interval
        y_min_ecg = 10000;
        y_max_ecg = -10000;
    }
}
void scr_hrv_lead_on_off_handler(bool lead_off)
{
    LOG_INF("HRV Screen handler: lead_off=%s", lead_off ? "true" : "false");
    if (label_ecg_lead_off == NULL || chart_ecg == NULL) {
        LOG_WRN("label_info is NULL, screen handler returning early");
        return;
    }

    k_mutex_lock(&Lead_on_off_handler_mutex, K_FOREVER);
    lead_on_detected = !lead_off;
    k_mutex_unlock(&Lead_on_off_handler_mutex);

    if (lead_on_detected )  // Lead ON condition
    {
        lv_obj_add_flag(label_ecg_lead_off, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(chart_ecg, LV_OBJ_FLAG_HIDDEN);   
    }
    else  // Lead OFF condition 
    {
        lv_obj_clear_flag(label_ecg_lead_off, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(chart_ecg, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(label_ecg_lead_off, "Leads are not in contact\nPlace fingers on electrodes to continue");
        
    }

}

void hpi_hrv_timer_start(void)
{
    k_mutex_lock(&Lead_on_off_handler_mutex, K_FOREVER);
    timer_running = true;
    timer_paused = false;
    lead_on_detected = true;
    k_mutex_unlock(&Lead_on_off_handler_mutex); 
    LOG_INF("HRV timer STARTED - leads detected (running=%s, paused=%s)", 
            timer_running ? "true" : "false", timer_paused ? "true" : "false");
}
void hpi_hrv_timer_reset(void)
{
    k_mutex_lock(&Lead_on_off_handler_mutex, K_FOREVER);
    timer_running = false;
    timer_paused = true;
    lead_on_detected = false;
    k_mutex_unlock(&Lead_on_off_handler_mutex);
    
    LOG_INF("HRV timer RESET - ready for fresh start (running=%s, paused=%s)",
            timer_running ? "true" : "false", timer_paused ? "true" : "false");
}

void unload_scr_hrv_eval_progress(void)
{
    scr_hrv_progress_active = false;
    batch_count = 0; sample_counter = 0;
    y_max_ecg = -10000; y_min_ecg = 10000;
    lead_on_detected = false;
    
    if (scr_hrv_eval_progress) {
        lv_obj_del(scr_hrv_eval_progress);
        scr_hrv_eval_progress = NULL;
        chart_ecg = NULL; ser_ecg = NULL;
        label_timer = NULL; label_ecg_lead_off = NULL;
    }
}
