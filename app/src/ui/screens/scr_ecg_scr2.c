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
// #include <zephyr/zbus/zbus.h>  // Commented out - requires CONFIG_ZBUS

#include "hpi_common_types.h"
#include "ui/move_ui.h"
#include "hw_module.h"

static lv_obj_t *scr_ecg_scr2;
// static lv_obj_t *btn_ecg_cancel;  // Commented out - not used
static lv_obj_t *chart_ecg;
static lv_chart_series_t *ser_ecg;
static lv_obj_t *label_ecg_hr;
static lv_obj_t *label_timer;
static lv_obj_t *label_ecg_lead_off;
static lv_obj_t *label_info;

static bool chart_ecg_update = true;
static float y_max_ecg = -1500;
static float y_min_ecg = 1500;

// static bool ecg_plot_hidden = false;

static float gx = 0;

// Performance optimization variables - LVGL 9.2 optimized
static bool prev_lead_off_status = true;
static uint32_t sample_counter = 0;
static const uint32_t RANGE_UPDATE_INTERVAL = 250; // Optimized for LVGL 9.2 refresh cycles
static uint32_t display_update_counter = 0;
static const uint32_t DISPLAY_UPDATE_INTERVAL = 3; // LVGL 9.2 optimal refresh frequency

// High-performance batch processing buffer - aligned for LVGL 9.2
static int32_t batch_data[32] __attribute__((aligned(4))); // Memory-aligned for better performance
static uint32_t batch_count = 0;

// LVGL 9.2 Chart performance configuration flags
static bool chart_auto_refresh_enabled = true;

// Function declarations for LVGL 9.2 optimized chart management
static void ecg_chart_enable_performance_mode(bool enable);
static void ecg_chart_reset_performance_counters(void);

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_large;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_tiny;

extern lv_style_t style_bg_blue;
extern lv_style_t style_bg_red;

extern struct k_sem sem_ecg_cancel;

// Commented out - unused function
/*
static void btn_ecg_cancel_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        hpi_load_screen(SCR_ECG, SCROLL_DOWN);
    }
}
*/

void draw_scr_ecg_scr2(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_ecg_scr2 = lv_obj_create(NULL);
    lv_obj_add_style(scr_ecg_scr2, &style_scr_black, 0);
    lv_obj_clear_flag(scr_ecg_scr2, LV_OBJ_FLAG_SCROLLABLE);
    // draw_scr_common(scr_ecg_scr2);

    /*Create a container with COLUMN flex direction*/
    lv_obj_t *cont_col = lv_obj_create(scr_ecg_scr2);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 25);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_col, -1, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_top(cont_col, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(cont_col, 1, LV_PART_MAIN);
    lv_obj_add_style(cont_col, &style_scr_black, 0);
    // lv_obj_add_style(cont_col, &style_bg_red, 0);

    // Draw countdown timer container
    lv_obj_t *cont_timer = lv_obj_create(cont_col);
    lv_obj_set_size(cont_timer, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_timer, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(cont_timer, &style_scr_black, 0);
    lv_obj_set_flex_align(cont_timer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    // Draw Countdown Timer
    LV_IMG_DECLARE(timer_32);
    lv_obj_t *img_timer = lv_img_create(cont_timer);
    lv_img_set_src(img_timer, &timer_32);

    label_timer = lv_label_create(cont_timer);
    lv_label_set_text(label_timer, "00");
    lv_obj_add_style(label_timer, &style_white_medium, 0);
    lv_obj_t *label_timer_sub = lv_label_create(cont_timer);
    lv_label_set_text(label_timer_sub, " secs");
    lv_obj_add_style(label_timer_sub, &style_white_medium, 0);

    /*label_info = lv_label_create(cont_col);
    lv_label_set_long_mode(label_info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_info, 300);
    lv_label_set_text(label_info, "Touch the bezel to start");
    lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_style(label_info, &style_white_medium, 0);*/

    /*btn_ecg_cancel = lv_btn_create(cont_col);
    lv_obj_add_event_cb(btn_ecg_cancel, btn_ecg_cancel_handler, LV_EVENT_ALL, NULL);
    // lv_obj_set_height(btn_ecg_cancel, 85);

    lv_obj_t *label_btn = lv_label_create(btn_ecg_cancel);
    lv_label_set_text(label_btn, LV_SYMBOL_CLOSE);
    lv_obj_center(label_btn);*/

    // Create ECG Chart following LVGL 9.2 best practices
    chart_ecg = lv_chart_create(cont_col);
    
    // Set chart dimensions - optimized for round display
    lv_obj_set_size(chart_ecg, 390, 140);
    
    // Configure chart type and fundamental properties
    lv_chart_set_type(chart_ecg, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart_ecg, ECG_DISP_WINDOW_SIZE);
    lv_chart_set_update_mode(chart_ecg, LV_CHART_UPDATE_MODE_CIRCULAR);  // ECG-like behavior
    
    // Set Y-axis range for ECG data (microvolts scaled by /4)
    // ECG signals typically 500-4000 µV, scaled gives us 125-1000 units
    lv_chart_set_range(chart_ecg, LV_CHART_AXIS_PRIMARY_Y, -500, 1500);
    
    // Disable division lines for clean ECG display (LVGL 9 recommendation for performance)
    lv_chart_set_div_line_count(chart_ecg, 0, 0);
    
    // Configure main chart background (LV_PART_MAIN styling)
    lv_obj_set_style_bg_opa(chart_ecg, LV_OPA_TRANSP, LV_PART_MAIN);  // Transparent background
    lv_obj_set_style_border_width(chart_ecg, 0, LV_PART_MAIN);        // No border
    lv_obj_set_style_outline_width(chart_ecg, 0, LV_PART_MAIN);       // No outline
    lv_obj_set_style_pad_all(chart_ecg, 5, LV_PART_MAIN);             // Minimal padding
    
    // Create series for ECG data
    ser_ecg = lv_chart_add_series(chart_ecg, lv_palette_main(LV_PALETTE_ORANGE), LV_CHART_AXIS_PRIMARY_Y);
    
    // Configure line series styling (LV_PART_ITEMS) - LVGL 9.2 way
    lv_obj_set_style_line_width(chart_ecg, 3, LV_PART_ITEMS);         // Line width
    lv_obj_set_style_line_color(chart_ecg, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_ITEMS);
    lv_obj_set_style_line_opa(chart_ecg, LV_OPA_COVER, LV_PART_ITEMS); // Full opacity for medical clarity
    lv_obj_set_style_line_rounded(chart_ecg, false, LV_PART_ITEMS);   // Sharp lines for precision
    
    // Disable points completely (LV_PART_INDICATOR) - LVGL 9.2 standard approach
    lv_obj_set_style_width(chart_ecg, 0, LV_PART_INDICATOR);          // No point width
    lv_obj_set_style_height(chart_ecg, 0, LV_PART_INDICATOR);         // No point height
    lv_obj_set_style_bg_opa(chart_ecg, LV_OPA_TRANSP, LV_PART_INDICATOR); // Transparent points
    lv_obj_set_style_border_opa(chart_ecg, LV_OPA_TRANSP, LV_PART_INDICATOR); // No point borders
    
    // Performance optimizations for real-time ECG display
    lv_obj_add_flag(chart_ecg, LV_OBJ_FLAG_IGNORE_LAYOUT);           // Skip layout calculations
    lv_obj_clear_flag(chart_ecg, LV_OBJ_FLAG_SCROLLABLE);            // Disable scrolling
    lv_obj_clear_flag(chart_ecg, LV_OBJ_FLAG_CLICK_FOCUSABLE);       // No focus events
    
    // Position chart with proper alignment
    lv_obj_align(chart_ecg, LV_ALIGN_CENTER, 0, -35);
    
    // Initialize chart with baseline values using LVGL 9.2 recommended method
    lv_chart_set_all_value(chart_ecg, ser_ecg, 0);
    
    // Initialize performance optimization system
    ecg_chart_reset_performance_counters();
    ecg_chart_enable_performance_mode(true);  // Start in high-performance mode    // Draw Lead off label
    label_ecg_lead_off = lv_label_create(cont_col);
    lv_label_set_long_mode(label_ecg_lead_off, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_ecg_lead_off, 300);
    lv_label_set_text(label_ecg_lead_off, "--");
    lv_obj_set_style_text_align(label_ecg_lead_off, LV_TEXT_ALIGN_CENTER, 0);

    // Draw BPM container
    lv_obj_t *cont_hr = lv_obj_create(cont_col);
    lv_obj_set_size(cont_hr, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_hr, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(cont_hr, &style_scr_black, 0);
    lv_obj_set_flex_align(cont_hr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *img_heart = lv_img_create(cont_hr);
    lv_img_set_src(img_heart, &img_heart_35);

    label_ecg_hr = lv_label_create(cont_hr);
    lv_label_set_text(label_ecg_hr, "00");
    lv_obj_add_style(label_ecg_hr, &style_white_medium, 0);
    lv_obj_t *label_hr_sub = lv_label_create(cont_hr);
    lv_label_set_text(label_hr_sub, " bpm");

    hpi_disp_set_curr_screen(SCR_SPL_ECG_SCR2);
    hpi_show_screen(scr_ecg_scr2, m_scroll_dir);
}

// LVGL 9.2 optimized chart management functions
static void ecg_chart_enable_performance_mode(bool enable)
{
    if (chart_ecg == NULL) return;
    
    if (enable) {
        // Enable performance optimizations
        lv_obj_add_flag(chart_ecg, LV_OBJ_FLAG_IGNORE_LAYOUT);
        chart_auto_refresh_enabled = false;
    } else {
        // Restore normal operation
        lv_obj_clear_flag(chart_ecg, LV_OBJ_FLAG_IGNORE_LAYOUT);
        chart_auto_refresh_enabled = true;
        lv_chart_refresh(chart_ecg);
    }
}

static void ecg_chart_reset_performance_counters(void)
{
    sample_counter = 0;
    display_update_counter = 0;
    batch_count = 0;
    y_max_ecg = -1500;
    y_min_ecg = 1500;
}

// Simplified scaling function - LVGL 9.2 optimized
void hpi_ecg_disp_do_set_scale(int disp_window_size)
{
    // This function is now simplified as range updating is handled 
    // more efficiently in the main plotting function
    if (gx >= disp_window_size)
    {
        gx = 0;
    }
}

// Simplified sample counter - now handled in main plot function  
void hpi_ecg_disp_add_samples(int num_samples)
{
    // Sample counting is now handled directly in the plot function
    // for better performance
}

void hpi_ecg_disp_update_hr(int hr)
{
    if (label_ecg_hr == NULL)
        return;

    // Optimize string formatting for performance
    static char hr_buf[8]; // Static buffer to avoid repeated allocations
    static int last_hr = -1; // Cache last value to avoid unnecessary updates
    
    if (hr != last_hr) { // Only update if value changed
        if (hr == 0)
        {
            strcpy(hr_buf, "--"); // More efficient than sprintf for simple strings
        }
        else
        {
            // Use lightweight integer to string conversion
            if (hr < 100) {
                hr_buf[0] = '0' + (hr / 10);
                hr_buf[1] = '0' + (hr % 10);
                hr_buf[2] = '\0';
            } else {
                hr_buf[0] = '0' + (hr / 100);
                hr_buf[1] = '0' + ((hr / 10) % 10);
                hr_buf[2] = '0' + (hr % 10);
                hr_buf[3] = '\0';
            }
        }
        
        lv_label_set_text(label_ecg_hr, hr_buf);
        last_hr = hr;
    }
}

void hpi_ecg_disp_update_timer(int time_left)
{
    if (label_timer == NULL)
        return;

    // Optimize timer updates with caching
    static int last_time = -1;
    static char time_buf[8];
    
    if (time_left != last_time) { // Only update if changed
        // Use direct integer to string for better performance
        if (time_left < 10) {
            time_buf[0] = '0' + time_left;
            time_buf[1] = '\0';
        } else if (time_left < 100) {
            time_buf[0] = '0' + (time_left / 10);
            time_buf[1] = '0' + (time_left % 10);
            time_buf[2] = '\0';
        } else {
            time_buf[0] = '0' + (time_left / 100);
            time_buf[1] = '0' + ((time_left / 10) % 10);
            time_buf[2] = '0' + (time_left % 10);
            time_buf[3] = '\0';
        }
        
        lv_label_set_text(label_timer, time_buf);
        last_time = time_left;
    }
}

void hpi_ecg_disp_draw_plotECG(int32_t *data_ecg, int num_samples, bool ecg_lead_off)
{
    // Early validation - LVGL 9.2 best practice
    if (chart_ecg_update == false || chart_ecg == NULL || ser_ecg == NULL || data_ecg == NULL || num_samples <= 0) {
        return;
    }

    // Performance optimization: Skip processing if chart is hidden
    if (lv_obj_has_flag(chart_ecg, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    // Intelligent refresh control for smooth performance
    display_update_counter++;
    bool should_refresh_display = (display_update_counter >= DISPLAY_UPDATE_INTERVAL);
    if (should_refresh_display) {
        display_update_counter = 0;
    }

    // Efficient batch processing of ECG samples
    for (int i = 0; i < num_samples; i++)
    {
        // Scale ECG data appropriately for display
        // ECG data in microvolts (µV), divide by 4 for good visualization range
        int32_t scaled_sample = data_ecg[i] / 4;
        
        // Add to batch buffer for efficient processing
        batch_data[batch_count] = scaled_sample;
        batch_count++;
        
        // Update min/max for auto-ranging (every 4th sample for performance)
        if ((sample_counter & 0x3) == 0) { // Bitwise AND is faster than modulo
            if (scaled_sample < y_min_ecg) y_min_ecg = scaled_sample;
            if (scaled_sample > y_max_ecg) y_max_ecg = scaled_sample;
        }
        
        sample_counter++;
        
        // Process batch when full or at end - LVGL 9.2 batch optimization
        if (batch_count >= 32 || i == num_samples - 1) {
            // Use LVGL 9.2 efficient batch update method
            for (int j = 0; j < batch_count; j++) {
                lv_chart_set_next_value(chart_ecg, ser_ecg, batch_data[j]);
            }
            batch_count = 0;
            
            // Optional: Force immediate refresh for this batch if needed
            if (should_refresh_display) {
                lv_chart_refresh(chart_ecg);  // LVGL 9.2 recommended refresh method
            }
        }
    }
    
    // Periodic range adjustment for auto-scaling
    if (sample_counter >= RANGE_UPDATE_INTERVAL)
    {
        // Calculate dynamic range with safety margins
        int32_t range_span = y_max_ecg - y_min_ecg;
        int32_t range_margin = (range_span > 0) ? (range_span / 8) : 100; // 12.5% margin or minimum 100
        
        // Ensure minimum margin for visibility
        if (range_margin < 50) range_margin = 50;
        
        // Update chart range using LVGL 9.2 method
        lv_chart_set_range(chart_ecg, LV_CHART_AXIS_PRIMARY_Y, 
                         y_min_ecg - range_margin, 
                         y_max_ecg + range_margin);
        
        // Reset tracking variables for next cycle
        sample_counter = 0;
        y_max_ecg = -1500;
        y_min_ecg = 1500;
    }
    
    // Update circular buffer position counter
    gx += num_samples;
    if (gx >= ECG_DISP_WINDOW_SIZE) {
        gx = 0;
    }

    // Efficient lead status handling - only update on state change
    if (ecg_lead_off != prev_lead_off_status)
    {
        if (ecg_lead_off)
        {
            lv_label_set_text_static(label_ecg_lead_off, "Lead Off");  // LVGL 9.2 static text optimization
            lv_obj_remove_style_all(label_ecg_lead_off);
            lv_obj_add_style(label_ecg_lead_off, &style_red_medium, 0);
        }
        else
        {
            lv_label_set_text_static(label_ecg_lead_off, "Lead On");   // LVGL 9.2 static text optimization
            lv_obj_remove_style_all(label_ecg_lead_off);
            lv_obj_add_style(label_ecg_lead_off, &style_white_medium, 0);
        }
        prev_lead_off_status = ecg_lead_off;
    }

    // Smart display invalidation - only when necessary
    if (should_refresh_display) {
        lv_obj_invalidate(chart_ecg);  // LVGL 9.2 preferred invalidation method
    }
}

void scr_ecg_lead_on_off_handler(bool lead_on_off)
{
    if (label_info == NULL)
        return;

    if (lead_on_off == false)
    {
        lv_obj_clear_flag(label_info, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(chart_ecg, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(label_info, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(chart_ecg, LV_OBJ_FLAG_HIDDEN);
    }
}

void gesture_down_scr_ecg_2(void)
{
    printk("Cancel ECG\n");
    k_sem_give(&sem_ecg_cancel);
    hpi_load_screen(SCR_ECG, SCROLL_DOWN);
}
