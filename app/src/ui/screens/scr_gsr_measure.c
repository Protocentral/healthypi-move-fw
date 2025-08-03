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
#include "sm/smf_ecg_bioz.h"

LOG_MODULE_REGISTER(hpi_disp_scr_gsr_measure, LOG_LEVEL_DBG);

lv_obj_t *scr_gsr_measure;

static lv_obj_t *chart_gsr;
static lv_chart_series_t *ser_gsr;

static lv_obj_t *label_gsr_current;
static lv_obj_t *label_stress_level;
static lv_obj_t *label_quality_status;
static lv_obj_t *label_heart;
static lv_obj_t *label_hr_bpm;

static float y_max_gsr = 0;
static float y_min_gsr = 10000;
static float gx = 0;

// Externs
extern lv_style_t style_lbl_orange;
extern lv_style_t style_red_medium;
extern lv_style_t style_white_medium;
extern lv_style_t style_white_large;
extern lv_style_t style_white_small;
extern lv_style_t style_scr_black;
extern lv_style_t style_bg_green;

void draw_scr_gsr_measure(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_gsr_measure = lv_obj_create(NULL);
    lv_obj_add_style(scr_gsr_measure, &style_scr_black, 0);
    lv_obj_clear_flag(scr_gsr_measure, LV_OBJ_FLAG_SCROLLABLE);

    // Main container optimized for 390x390 round display
    lv_obj_t *cont_main = lv_obj_create(scr_gsr_measure);
    lv_obj_clear_flag(cont_main, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(cont_main, 370, 370);  // Leave margin for round display
    lv_obj_center(cont_main);
    lv_obj_add_style(cont_main, &style_scr_black, 0);
    lv_obj_set_style_radius(cont_main, 185, 0);  // Circular layout
    lv_obj_set_style_border_width(cont_main, 0, LV_PART_MAIN);  // Hide border
    lv_obj_set_style_pad_all(cont_main, 15, 0);

    // Title at top
    lv_obj_t *label_title = lv_label_create(cont_main);
    lv_label_set_text(label_title, "GSR Live Monitor");
    lv_obj_add_style(label_title, &style_white_medium, 0);
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 5);

    // Current GSR value display - compact at top
    lv_obj_t *cont_current = lv_obj_create(cont_main);
    lv_obj_set_size(cont_current, 280, 40);
    lv_obj_align(cont_current, LV_ALIGN_TOP_MID, 0, 35);
    lv_obj_set_style_bg_opa(cont_current, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_current, 0, LV_PART_MAIN);  // Hide border
    lv_obj_clear_flag(cont_current, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cont_current, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_current, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *label_current_title = lv_label_create(cont_current);
    lv_label_set_text(label_current_title, "Current: ");
    lv_obj_add_style(label_current_title, &style_white_small, 0);

    label_gsr_current = lv_label_create(cont_current);
    lv_label_set_text(label_gsr_current, "0.0 µS");
    lv_obj_add_style(label_gsr_current, &style_white_medium, 0);

    // GSR Chart optimized for round display - larger and centered
    chart_gsr = lv_chart_create(cont_main);
    lv_obj_set_size(chart_gsr, 320, 180);  // Optimized size for round display
    lv_obj_align(chart_gsr, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_bg_color(chart_gsr, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(chart_gsr, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(chart_gsr, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(chart_gsr, lv_color_hex(0x404040), LV_PART_MAIN);
    lv_obj_set_style_radius(chart_gsr, 10, LV_PART_MAIN);
    lv_chart_set_point_count(chart_gsr, DISP_WINDOW_SIZE_EDA);
    lv_chart_set_div_line_count(chart_gsr, 3, 2);  // Grid lines for better readability
    lv_chart_set_update_mode(chart_gsr, LV_CHART_UPDATE_MODE_CIRCULAR);
    
    // Use amber/orange color for GSR similar to PPG displays
    ser_gsr = lv_chart_add_series(chart_gsr, lv_palette_main(LV_PALETTE_AMBER), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_line_width(chart_gsr, 3, LV_PART_ITEMS);

    // Status indicators at bottom - compact horizontal layout
    lv_obj_t *cont_status = lv_obj_create(cont_main);
    lv_obj_set_size(cont_status, 340, 80);
    lv_obj_align(cont_status, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_opa(cont_status, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_status, 0, LV_PART_MAIN);  // Hide border
    lv_obj_clear_flag(cont_status, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cont_status, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_status, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Top row: Stress and Quality
    lv_obj_t *cont_indicators = lv_obj_create(cont_status);
    lv_obj_set_size(cont_indicators, 320, 30);
    lv_obj_set_style_bg_opa(cont_indicators, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_indicators, 0, LV_PART_MAIN);  // Hide border
    lv_obj_clear_flag(cont_indicators, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cont_indicators, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_indicators, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Stress level indicator - compact
    lv_obj_t *cont_stress = lv_obj_create(cont_indicators);
    lv_obj_set_size(cont_stress, 140, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(cont_stress, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_stress, 0, LV_PART_MAIN);  // Hide border
    lv_obj_clear_flag(cont_stress, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cont_stress, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_stress, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *label_stress_title = lv_label_create(cont_stress);
    lv_label_set_text(label_stress_title, "Stress:");
    lv_obj_add_style(label_stress_title, &style_white_small, 0);

    label_stress_level = lv_label_create(cont_stress);
    lv_label_set_text(label_stress_level, " Low " LV_SYMBOL_OK);
    lv_obj_add_style(label_stress_level, &style_bg_green, 0);
    lv_obj_set_style_text_font(label_stress_level, &lv_font_montserrat_20, 0);

    // Quality indicator - compact
    lv_obj_t *cont_quality = lv_obj_create(cont_indicators);
    lv_obj_set_size(cont_quality, 140, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(cont_quality, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_quality, 0, LV_PART_MAIN);  // Hide border
    lv_obj_clear_flag(cont_quality, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cont_quality, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_quality, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *label_quality_title = lv_label_create(cont_quality);
    lv_label_set_text(label_quality_title, "Quality:");
    lv_obj_add_style(label_quality_title, &style_white_small, 0);

    label_quality_status = lv_label_create(cont_quality);
    lv_label_set_text(label_quality_status, " Good " LV_SYMBOL_OK);
    lv_obj_add_style(label_quality_status, &style_bg_green, 0);
    lv_obj_set_style_text_font(label_quality_status, &lv_font_montserrat_20, 0);

    // Bottom row: Heart rate correlation - compact
    lv_obj_t *cont_hr = lv_obj_create(cont_status);
    lv_obj_set_size(cont_hr, 200, 30);
    lv_obj_set_style_bg_opa(cont_hr, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_hr, 0, LV_PART_MAIN);  // Hide border
    lv_obj_clear_flag(cont_hr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cont_hr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_hr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // Heart rate display
    label_heart = lv_label_create(cont_hr);
    lv_label_set_text(label_heart, "♥ ");
    lv_obj_set_style_text_color(label_heart, lv_color_make(255, 100, 100), 0);
    lv_obj_set_style_text_font(label_heart, &lv_font_montserrat_20, 0);

    label_hr_bpm = lv_label_create(cont_hr);
    lv_label_set_text(label_hr_bpm, "72");
    lv_obj_add_style(label_hr_bpm, &style_white_medium, 0);
    lv_obj_set_style_text_font(label_hr_bpm, &lv_font_montserrat_20, 0);
    
    lv_obj_t *label_hr_sub = lv_label_create(cont_hr);
    lv_label_set_text(label_hr_sub, " bpm");
    lv_obj_add_style(label_hr_sub, &style_white_small, 0);

    hpi_disp_set_curr_screen(SCR_SPL_GSR_MEASURE);
    hpi_show_screen(scr_gsr_measure, m_scroll_dir);
    
    // Start GSR measurement when entering the screen
    LOG_INF("Starting GSR measurement from UI");
    int ret = hpi_gsr_start_measurement();
    if (ret != 0) {
        LOG_ERR("Failed to start GSR measurement: %d", ret);
    }
}

static void hpi_gsr_disp_do_set_scale(int disp_window_size)
{
    if (gx >= (disp_window_size / 4))
    {
        lv_chart_set_range(chart_gsr, LV_CHART_AXIS_PRIMARY_Y, y_min_gsr, y_max_gsr);

        gx = 0;
        y_max_gsr = -900000;
        y_min_gsr = 900000;
    }
}

static void hpi_gsr_disp_add_samples(int num_samples)
{
    gx += num_samples;
}

void hpi_disp_gsr_draw_plot(struct hpi_gsr_data_t gsr_sensor_sample)
{
    int32_t *data_gsr = gsr_sensor_sample.gsr_samples;
    uint16_t n_sample = gsr_sensor_sample.gsr_num_samples;

    for (int i = 0; i < n_sample; i++)
    {
        float data_gsr_i = (float)(data_gsr[i] * 1.000);

        if (data_gsr_i == 0)
        {
            return;
        }

        if (data_gsr_i < y_min_gsr)
        {
            y_min_gsr = data_gsr_i;
        }

        if (data_gsr_i > y_max_gsr)
        {
            y_max_gsr = data_gsr_i;
        }

        lv_chart_set_next_value(chart_gsr, ser_gsr, data_gsr_i);

        // Update current GSR value
        if (label_gsr_current != NULL)
        {
            lv_label_set_text_fmt(label_gsr_current, "%.1f µS", gsr_sensor_sample.gsr_conductance_us);
        }

        // Update stress level
        if (label_stress_level != NULL)
        {
            const char* stress_labels[] = {"Very Low", "Low", "Moderate", "High", "Very High"};
            const char* stress_symbols[] = {LV_SYMBOL_OK, LV_SYMBOL_OK, LV_SYMBOL_WARNING, LV_SYMBOL_WARNING, LV_SYMBOL_CLOSE};
            
            uint8_t stress_level = gsr_sensor_sample.stress_level;
            if (stress_level > 4) stress_level = 4;
            
            char stress_text[32];
            snprintf(stress_text, sizeof(stress_text), "%s %s", stress_labels[stress_level], stress_symbols[stress_level]);
            lv_label_set_text(label_stress_level, stress_text);
        }

        // Update quality indicator
        if (label_quality_status != NULL)
        {
            uint8_t quality = gsr_sensor_sample.gsr_quality;
            if (quality >= 80) {
                lv_label_set_text(label_quality_status, "Good " LV_SYMBOL_OK);
            } else if (quality >= 50) {
                lv_label_set_text(label_quality_status, "Fair " LV_SYMBOL_WARNING);
            } else {
                lv_label_set_text(label_quality_status, "Poor " LV_SYMBOL_CLOSE);
            }
        }

        // Update correlated heart rate
        if (label_hr_bpm != NULL && gsr_sensor_sample.corr_hr > 0)
        {
            lv_label_set_text_fmt(label_hr_bpm, "%d", gsr_sensor_sample.corr_hr);
        }

        hpi_gsr_disp_add_samples(1);
        hpi_gsr_disp_do_set_scale(DISP_WINDOW_SIZE_EDA);
    }
}

void gesture_down_scr_gsr_measure(void)
{
    // Stop GSR measurement when exiting the screen
    LOG_INF("Stopping GSR measurement from UI");
    int ret = hpi_gsr_stop_measurement();
    if (ret != 0) {
        LOG_ERR("Failed to stop GSR measurement: %d", ret);
    }
    
    // Handle gesture down event
    hpi_load_screen(SCR_GSR, SCROLL_DOWN);
}
