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
#include <lvgl.h>
#include <stdio.h>
#include <zephyr/logging/log.h>

#include "hpi_common_types.h"
#include "hw_module.h"
#include "ui/move_ui.h"

lv_obj_t *scr_bpt_measure;

static lv_obj_t *chart_bpt_ppg;
static lv_chart_series_t *ser_bpt_ppg;

static lv_obj_t *label_hr_bpm;
static lv_obj_t *bar_bpt_progress;
static lv_obj_t *label_progress;

static float y_max_ppg = 0;
static float y_min_ppg = 10000;

static float gx = 0;

// Externs
extern lv_style_t style_lbl_orange;
extern lv_style_t style_red_medium;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;

void draw_scr_bpt_measure(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_bpt_measure = lv_obj_create(NULL);
    // AMOLED OPTIMIZATION: Pure black background for power efficiency
    lv_obj_set_style_bg_color(scr_bpt_measure, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_bpt_measure, LV_OBJ_FLAG_SCROLLABLE);

    // CIRCULAR AMOLED-OPTIMIZED BP MEASUREMENT SCREEN
    // Display center: (195, 195), Usable radius: ~185px
    // Blue/Purple theme for BP measurement consistency

    // OUTER RING: BP Measurement Progress Arc (Radius 170-185px)
    lv_obj_t *arc_bp_progress = lv_arc_create(scr_bpt_measure);
    lv_obj_set_size(arc_bp_progress, 370, 370);  // 185px radius
    lv_obj_center(arc_bp_progress);
    lv_arc_set_range(arc_bp_progress, 0, 100);  // Progress range for measurement duration
    
    // Background arc: Full 270° track (gray)
    lv_arc_set_bg_angles(arc_bp_progress, 135, 45);  // Full background arc
    lv_arc_set_value(arc_bp_progress, 0);  // Start at 0, will be updated by progress
    
    // Style the progress arc - blue theme for BP measurement
    lv_obj_set_style_arc_color(arc_bp_progress, lv_color_hex(0x333333), LV_PART_MAIN);    // Background track
    lv_obj_set_style_arc_width(arc_bp_progress, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_bp_progress, lv_color_hex(0x4A90E2), LV_PART_INDICATOR);  // Blue progress
    lv_obj_set_style_arc_width(arc_bp_progress, 6, LV_PART_INDICATOR);
    lv_obj_remove_style(arc_bp_progress, NULL, LV_PART_KNOB);  // Remove knob
    lv_obj_clear_flag(arc_bp_progress, LV_OBJ_FLAG_CLICKABLE);
    
    // Store reference for progress updates
    bar_bpt_progress = arc_bp_progress;  // Reuse existing variable

    // Screen title - properly positioned to avoid arc overlap
    lv_obj_t *label_title = lv_label_create(scr_bpt_measure);
    lv_label_set_text(label_title, "Blood Pressure");
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 50);  // Moved down to avoid arc overlap
    lv_obj_add_style(label_title, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);

    // Status message below title
    lv_obj_t *label_status = lv_label_create(scr_bpt_measure);
    lv_label_set_text(label_status, "Measuring...");
    lv_obj_align(label_status, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_add_style(label_status, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_status, lv_color_hex(0x4A90E2), LV_PART_MAIN);  // Blue accent

    // Progress percentage (center top area)
    label_progress = lv_label_create(scr_bpt_measure);
    lv_label_set_text(label_progress, "0%");
    lv_obj_align(label_progress, LV_ALIGN_TOP_MID, 0, 105);
    lv_obj_add_style(label_progress, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_progress, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_progress, lv_color_white(), LV_PART_MAIN);

    // CENTRAL ZONE: PPG Chart (positioned in center area)
    chart_bpt_ppg = lv_chart_create(scr_bpt_measure);
    lv_obj_set_size(chart_bpt_ppg, 340, 100);  // Smaller chart for circular design
    lv_obj_align(chart_bpt_ppg, LV_ALIGN_CENTER, 0, -10);  // Centered position
    
    // Configure chart type and fundamental properties
    lv_chart_set_type(chart_bpt_ppg, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart_bpt_ppg, ECG_DISP_WINDOW_SIZE);
    lv_chart_set_update_mode(chart_bpt_ppg, LV_CHART_UPDATE_MODE_CIRCULAR);  // PPG-like behavior
    
    // Set Y-axis range for PPG data
    lv_chart_set_range(chart_bpt_ppg, LV_CHART_AXIS_PRIMARY_Y, -5000, 5000);
    
    // Disable division lines for clean PPG display
    lv_chart_set_div_line_count(chart_bpt_ppg, 0, 0);
    
    // Configure main chart background (transparent for AMOLED)
    lv_obj_set_style_bg_opa(chart_bpt_ppg, LV_OPA_TRANSP, LV_PART_MAIN);  // Transparent background
    lv_obj_set_style_border_width(chart_bpt_ppg, 0, LV_PART_MAIN);        // No border (matches SpO2)
    lv_obj_set_style_outline_width(chart_bpt_ppg, 0, LV_PART_MAIN);       // No outline
    lv_obj_set_style_pad_all(chart_bpt_ppg, 5, LV_PART_MAIN);             // Minimal padding
    
    // Create series for PPG data
    ser_bpt_ppg = lv_chart_add_series(chart_bpt_ppg, lv_palette_main(LV_PALETTE_ORANGE), LV_CHART_AXIS_PRIMARY_Y);
    
    // Configure line series styling - orange theme matching SpO2/raw PPG screens
    lv_obj_set_style_line_width(chart_bpt_ppg, 6, LV_PART_ITEMS);         // Thicker line matching SpO2
    lv_obj_set_style_line_color(chart_bpt_ppg, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_ITEMS);
    lv_obj_set_style_line_opa(chart_bpt_ppg, LV_OPA_COVER, LV_PART_ITEMS); // Full opacity for medical clarity
    lv_obj_set_style_line_rounded(chart_bpt_ppg, false, LV_PART_ITEMS);   // Sharp lines for precision
    
    // Disable points completely
    lv_obj_set_style_width(chart_bpt_ppg, 0, LV_PART_INDICATOR);          // No point width
    lv_obj_set_style_height(chart_bpt_ppg, 0, LV_PART_INDICATOR);         // No point height
    lv_obj_set_style_bg_opa(chart_bpt_ppg, LV_OPA_TRANSP, LV_PART_INDICATOR); // Transparent points
    lv_obj_set_style_border_opa(chart_bpt_ppg, LV_OPA_TRANSP, LV_PART_INDICATOR); // No point borders
    
    // Performance optimizations for real-time PPG display
    lv_obj_add_flag(chart_bpt_ppg, LV_OBJ_FLAG_IGNORE_LAYOUT);           // Skip layout calculations
    lv_obj_clear_flag(chart_bpt_ppg, LV_OBJ_FLAG_SCROLLABLE);            // Disable scrolling
    lv_obj_clear_flag(chart_bpt_ppg, LV_OBJ_FLAG_CLICK_FOCUSABLE);       // No focus events
    
    // Initialize chart with baseline values
    lv_chart_set_all_value(chart_bpt_ppg, ser_bpt_ppg, 0);

    // HR Container below chart (following design pattern)
    lv_obj_t *cont_hr = lv_obj_create(scr_bpt_measure);
    lv_obj_set_size(cont_hr, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(cont_hr, LV_ALIGN_CENTER, 0, 75);  // Below chart
    lv_obj_set_style_bg_opa(cont_hr, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_hr, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont_hr, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont_hr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_hr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Heart Icon
    lv_obj_t *img_heart = lv_img_create(cont_hr);
    lv_img_set_src(img_heart, &img_heart_48px);
    lv_obj_set_style_img_recolor(img_heart, lv_color_hex(COLOR_CRITICAL_RED), LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(img_heart, LV_OPA_COVER, LV_PART_MAIN);

    // HR Value
    label_hr_bpm = lv_label_create(cont_hr);
    lv_label_set_text(label_hr_bpm, "--");
    lv_obj_add_style(label_hr_bpm, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_hr_bpm, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_pad_left(label_hr_bpm, 8, LV_PART_MAIN);

    // HR Unit
    lv_obj_t *label_hr_unit = lv_label_create(cont_hr);
    lv_label_set_text(label_hr_unit, "BPM");
    lv_obj_add_style(label_hr_unit, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_hr_unit, lv_color_hex(COLOR_CRITICAL_RED), LV_PART_MAIN);

    // Instructions at bottom
    lv_obj_t *label_instructions = lv_label_create(scr_bpt_measure);
    lv_label_set_text(label_instructions, "Hold Still");
    lv_obj_align(label_instructions, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_add_style(label_instructions, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_instructions, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_instructions, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);

    hpi_disp_set_curr_screen(SCR_SPL_BPT_MEASURE);
    hpi_show_screen(scr_bpt_measure, m_scroll_dir);
}

void hpi_disp_bpt_update_progress(int progress)
{
    if (label_progress == NULL || bar_bpt_progress == NULL)
    {
        return;
    }

    // Update arc progress (bar_bpt_progress is now the arc)
    lv_arc_set_value(bar_bpt_progress, progress);
    
    // Update progress label
    lv_label_set_text_fmt(label_progress, "%d%%", progress);

    if (progress == 100)
    {
        // Measurement complete
        // Future: Could add completion animations or state changes
    }
}

static void hpi_bpt_disp_do_set_scale(int disp_window_size)
{
    if (gx >= (disp_window_size))
    {

        lv_chart_set_range(chart_bpt_ppg, LV_CHART_AXIS_PRIMARY_Y, y_min_ppg, y_max_ppg);

        gx = 0;

        y_max_ppg = -900000;
        y_min_ppg = 900000;
    }
}

static void hpi_bpt_disp_add_samples(int num_samples)
{
    gx += num_samples;
}

void hpi_disp_bpt_draw_plotPPG(struct hpi_ppg_fi_data_t ppg_sensor_sample)
{
    uint32_t *data_ppg = ppg_sensor_sample.raw_red;

    uint16_t n_sample = ppg_sensor_sample.ppg_num_samples;

    for (int i = 0; i < n_sample; i++)
    {
        float data_ppg_i = (float)(data_ppg[i] * 1.000); // * 0.100);

        if (data_ppg_i == 0)
        {
            return;
        }

        if (data_ppg_i < y_min_ppg)
        {
            y_min_ppg = data_ppg_i;
        }

        if (data_ppg_i > y_max_ppg)
        {
            y_max_ppg = data_ppg_i;
        }

        lv_chart_set_next_value(chart_bpt_ppg, ser_bpt_ppg, data_ppg_i);

        if(ppg_sensor_sample.hr > 0)
        {
            lv_label_set_text_fmt(label_hr_bpm, "%d", ppg_sensor_sample.hr);
        } else
        {
            lv_label_set_text_fmt(label_hr_bpm, "--");
        }

        hpi_bpt_disp_add_samples(1);
        hpi_bpt_disp_do_set_scale(BPT_DISP_WINDOW_SIZE);
    }
}

void gesture_down_scr_bpt_measure(void)
{
    // Handle gesture down event
    hpi_load_screen(SCR_BPT, SCROLL_DOWN);
}