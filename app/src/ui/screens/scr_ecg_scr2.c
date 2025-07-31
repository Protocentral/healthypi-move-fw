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
static float y_max_ecg = 0;
static float y_min_ecg = 10000;

// static bool ecg_plot_hidden = false;

static float gx = 0;

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

    // Create optimized ECG Chart for LVGL 9
    chart_ecg = lv_chart_create(cont_col);
    lv_obj_set_size(chart_ecg, 390, 140);
    
    // Set chart type and properties for efficiency
    lv_chart_set_type(chart_ecg, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart_ecg, ECG_DISP_WINDOW_SIZE);
    lv_chart_set_update_mode(chart_ecg, LV_CHART_UPDATE_MODE_CIRCULAR);
    
    // Optimize visual settings for performance
    lv_obj_set_style_bg_color(chart_ecg, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(chart_ecg, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(chart_ecg, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(chart_ecg, 5, LV_PART_MAIN);
    
    // Disable grid lines for better performance
    lv_chart_set_div_line_count(chart_ecg, 0, 0);
    
    // Create series with optimized settings
    ser_ecg = lv_chart_add_series(chart_ecg, lv_palette_main(LV_PALETTE_ORANGE), LV_CHART_AXIS_PRIMARY_Y);
    
    // Optimize line rendering for real-time data
    lv_obj_set_style_line_width(chart_ecg, 2, LV_PART_ITEMS);
    lv_obj_set_style_line_rounded(chart_ecg, false, LV_PART_ITEMS);
    lv_obj_set_style_line_opa(chart_ecg, LV_OPA_COVER, LV_PART_ITEMS);
    
    // Set initial range for ECG data (will be auto-adjusted)
    lv_chart_set_range(chart_ecg, LV_CHART_AXIS_PRIMARY_Y, -1000, 1000);
    
    // Position the chart
    lv_obj_align(chart_ecg, LV_ALIGN_CENTER, 0, -35);
    
    // Pre-fill series with zero values for smoother startup
    for (int i = 0; i < ECG_DISP_WINDOW_SIZE; i++) {
        lv_chart_set_next_value(chart_ecg, ser_ecg, 0);
    }

    // Draw Lead off label
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

// Simplified scaling function - now handled more efficiently in main plot function
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

    char buf[32];
    if (hr == 0)
    {
        sprintf(buf, "--");
    }
    else
    {
        sprintf(buf, "%d", hr);
    }

    lv_label_set_text(label_ecg_hr, buf);
}

void hpi_ecg_disp_update_timer(int time_left)
{
    if (label_timer == NULL)
        return;

    lv_label_set_text_fmt(label_timer, "%d", time_left);
}

static bool prev_lead_off_status = true;
static uint32_t sample_counter = 0;
static const uint32_t RANGE_UPDATE_INTERVAL = 100; // Update range every 100 samples

void hpi_ecg_disp_draw_plotECG(int32_t *data_ecg, int num_samples, bool ecg_lead_off)
{
    if (chart_ecg_update == true && chart_ecg != NULL && ser_ecg != NULL)
    {
        // Batch process samples for better performance
        for (int i = 0; i < num_samples; i++)
        {
            // Convert ECG data to display units more efficiently
            int32_t data_ecg_i = (data_ecg[i] * 10000) / 5242880; // Optimized calculation
            
            // Track min/max values for auto-scaling
            if (data_ecg_i < y_min_ecg) y_min_ecg = data_ecg_i;
            if (data_ecg_i > y_max_ecg) y_max_ecg = data_ecg_i;
            
            // Add data point to chart
            lv_chart_set_next_value(chart_ecg, ser_ecg, data_ecg_i);
            
            sample_counter++;
        }
        
        // Update chart range periodically for better performance
        if (sample_counter >= RANGE_UPDATE_INTERVAL)
        {
            // Add some margin to the range for better visualization
            int32_t range_margin = (y_max_ecg - y_min_ecg) / 10;
            lv_chart_set_range(chart_ecg, LV_CHART_AXIS_PRIMARY_Y, 
                             y_min_ecg - range_margin, 
                             y_max_ecg + range_margin);
            
            // Reset for next cycle
            sample_counter = 0;
            y_max_ecg = -900000;
            y_min_ecg = 900000;
        }
        
        // Update samples counter
        gx += num_samples;
        if (gx >= ECG_DISP_WINDOW_SIZE) {
            gx = 0;
        }

        // Handle lead off status changes efficiently
        if (ecg_lead_off != prev_lead_off_status)
        {
            if (ecg_lead_off)
            {
                lv_label_set_text(label_ecg_lead_off, "Lead Off");
                lv_obj_remove_style_all(label_ecg_lead_off);
                lv_obj_add_style(label_ecg_lead_off, &style_red_medium, 0);
            }
            else
            {
                lv_label_set_text(label_ecg_lead_off, "Lead On");
                lv_obj_remove_style_all(label_ecg_lead_off);
                lv_obj_add_style(label_ecg_lead_off, &style_white_medium, 0);
            }
            prev_lead_off_status = ecg_lead_off;
        }
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
