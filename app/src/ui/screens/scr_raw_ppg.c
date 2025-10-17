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
#include <stdint.h>

#include "hpi_common_types.h"
#include "ui/move_ui.h"

#define PPG_RAW_WINDOW_SIZE 128
lv_obj_t *scr_raw_ppg;

// GUI Charts
static lv_obj_t *chart_ppg;
static lv_chart_series_t *ser_ppg;

// GUI Labels
static lv_obj_t *label_ppg_hr;
static lv_obj_t *label_ppg_spo2;
static lv_obj_t *label_status;
static lv_obj_t *label_ppg_no_signal;

static int parent_screen = 0;

static float y_max_ppg = 0;
static float y_min_ppg = 10000;

float y2_max = 0;
float y2_min = 10000;

float y3_max = 0;
float y3_min = 10000;

static float gx = 0;

// Signal detection and timeout tracking
static uint32_t last_ppg_data_time = 0;
static enum hpi_ppg_status last_scd_state = HPI_PPG_SCD_STATUS_UNKNOWN;
#define PPG_SIGNAL_TIMEOUT_MS 3000  // Show "No Signal" if no data for 3 seconds

extern lv_style_t style_red_medium;
extern lv_style_t style_white_medium;

extern lv_style_t style_scr_black;

#define PPG_SIGNAL_RED 0
#define PPG_SIGNAL_IR 1
#define PPG_SIGNAL_GREEN 2

uint8_t ppg_disp_signal_type = PPG_SIGNAL_RED;

LOG_MODULE_REGISTER(ppg_scr);

void draw_scr_spl_raw_ppg(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    parent_screen = arg1;

    scr_raw_ppg = lv_obj_create(NULL);
    lv_obj_clear_flag(scr_raw_ppg, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    lv_obj_t *cont_col = lv_obj_create(scr_raw_ppg);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    // lv_obj_set_width(cont_col, lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_col, -1, LV_PART_SCROLLBAR);
    lv_obj_add_style(cont_col, &style_scr_black, 0);

    lv_obj_t *label_signal = lv_label_create(cont_col);
    lv_label_set_text(label_signal, "PPG");
    lv_obj_align(label_signal, LV_ALIGN_TOP_MID, 0, 5);

    chart_ppg = lv_chart_create(cont_col);
    /* Match SpO2 measure chart styling */
    lv_obj_set_size(chart_ppg, 390, 140);
    lv_obj_set_style_bg_color(chart_ppg, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(chart_ppg, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(chart_ppg, 0, LV_PART_MAIN);
    lv_chart_set_point_count(chart_ppg, PPG_RAW_WINDOW_SIZE);
    lv_chart_set_div_line_count(chart_ppg, 0, 0);
    lv_chart_set_update_mode(chart_ppg, LV_CHART_UPDATE_MODE_CIRCULAR);
    lv_obj_align(chart_ppg, LV_ALIGN_CENTER, 0, -35);
    
    // Set initial Y-axis range suitable for PPG data (typically 0-65535 for raw values)
    // Start with a reasonable range around typical PPG baseline
    lv_chart_set_range(chart_ppg, LV_CHART_AXIS_PRIMARY_Y, 0, 65535);

    ser_ppg = lv_chart_add_series(chart_ppg, lv_palette_main(LV_PALETTE_ORANGE), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_line_width(chart_ppg, 6, LV_PART_ITEMS);

    // Draw BPM container
    lv_obj_t *cont_hr = lv_obj_create(cont_col);
    lv_obj_set_size(cont_hr, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_hr, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(cont_hr, &style_scr_black, 0);
    lv_obj_set_flex_align(cont_hr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *img_heart = lv_img_create(cont_hr);
    lv_img_set_src(img_heart, &img_heart_48px);

    label_ppg_hr = lv_label_create(cont_hr);
    lv_label_set_text(label_ppg_hr, "00");
    lv_obj_add_style(label_ppg_hr, &style_white_medium, 0);
    lv_obj_t *label_hr_sub = lv_label_create(cont_hr);
    lv_label_set_text(label_hr_sub, " bpm");

    /*lv_obj_t *btn_settings = lv_btn_create(sfcr_raw_ppg);
    lv_obj_set_width(btn_settings, 80);
    lv_obj_set_height(btn_settings, 80);
    lv_obj_set_x(btn_settings, 0);
    lv_obj_align_to(btn_settings, NULL, LV_ALIGN_CENTER, 0, 150);
    lv_obj_add_flag(btn_settings, LV_OBJ_FLAG_SCROLL_ON_FOCUS); /// Flags
    lv_obj_clear_flag(btn_settings, LV_OBJ_FLAG_SCROLLABLE);    /// Flags
    lv_obj_set_style_radius(btn_settings, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn_settings, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn_settings, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(btn_settings, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_settings, ppg_settings_button_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ui_hr_number = lv_label_create(btn_settings);
    lv_obj_set_width(ui_hr_number, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_hr_number, LV_SIZE_CONTENT); /// 1
    lv_obj_set_align(ui_hr_number, LV_ALIGN_BOTTOM_MID);
    lv_label_set_text(ui_hr_number, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(ui_hr_number, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_hr_number, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    //lv_obj_set_style_text_font(ui_hr_number, &lv_font_montserrat_42, LV_PART_MAIN | LV_STATE_DEFAULT);
    */
    // PPG Sensor Status label
    label_status = lv_label_create(scr_raw_ppg);
    lv_label_set_text(label_status, "--");
    lv_obj_align_to(label_status, chart_ppg, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_obj_set_style_text_align(label_status, LV_TEXT_ALIGN_CENTER, 0);

    // Create "No Signal" overlay label (initially hidden)
    label_ppg_no_signal = lv_label_create(scr_raw_ppg);
    lv_label_set_text(label_ppg_no_signal, "No Signal");
    lv_obj_align(label_ppg_no_signal, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_style_text_color(label_ppg_no_signal, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_add_style(label_ppg_no_signal, &style_white_medium, 0);
    lv_obj_add_flag(label_ppg_no_signal, LV_OBJ_FLAG_HIDDEN); // Start hidden

    // Initialize timestamp and SCD state
    last_ppg_data_time = k_uptime_get_32();
    last_scd_state = HPI_PPG_SCD_STATUS_UNKNOWN;

    // Reset min/max tracking for fresh autoscaling
    y_min_ppg = 10000;
    y_max_ppg = 0;
    gx = 0;
    
    // Reset autoscale state to ensure first update happens
    hpi_ppg_autoscale_reset();

    hpi_disp_set_curr_screen(SCR_SPL_RAW_PPG);
    hpi_show_screen(scr_raw_ppg, m_scroll_dir);
}

void gesture_down_scr_spl_raw_ppg(void)
{
    /* Return to parent screen on gesture down */
    hpi_load_screen(parent_screen, SCROLL_DOWN);
}

void hpi_ppg_disp_update_hr(int hr)
{
    if (label_ppg_hr == NULL)
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

    lv_label_set_text(label_ppg_hr, buf);
}

static void hpi_ppg_disp_add_samples(int num_samples)
{
    gx += num_samples;
}

/* Delegate autoscale to shared helper to keep behavior consistent across screens */
static void hpi_ppg_disp_do_set_scale(int disp_window_size)
{
    hpi_ppg_disp_do_set_scale_shared(chart_ppg, &y_min_ppg, &y_max_ppg, &gx, disp_window_size);
}

/* Update "No Signal" label visibility based on data presence and SCD status */
static void hpi_ppg_update_signal_status(enum hpi_ppg_status scd_state)
{
    if (label_ppg_no_signal == NULL)
        return;

    uint32_t current_time = k_uptime_get_32();
    bool timeout = (current_time - last_ppg_data_time) > PPG_SIGNAL_TIMEOUT_MS;
    bool no_skin_contact = (scd_state == HPI_PPG_SCD_OFF_SKIN);

    // Show "No Signal" if timeout OR no skin contact (but ONLY check skin contact if we have valid state)
    if (timeout || no_skin_contact)
    {
        lv_obj_clear_flag(label_ppg_no_signal, LV_OBJ_FLAG_HIDDEN);
        
        // Update message based on reason - prioritize skin contact message when both conditions exist
        if (no_skin_contact)
        {
            lv_label_set_text(label_ppg_no_signal, "No Skin Contact");
        }
        else if (timeout)
        {
            lv_label_set_text(label_ppg_no_signal, "No Signal");
        }
    }
    else
    {
        lv_obj_add_flag(label_ppg_no_signal, LV_OBJ_FLAG_HIDDEN);
    }
}

void hpi_disp_ppg_draw_plotPPG(struct hpi_ppg_wr_data_t ppg_sensor_sample)
{
    // Update last data received timestamp
    last_ppg_data_time = k_uptime_get_32();

    // Store the SCD state for use in periodic timeout checks
    last_scd_state = ppg_sensor_sample.scd_state;

    // Update signal status based on SCD state
    hpi_ppg_update_signal_status(ppg_sensor_sample.scd_state);

    uint32_t *data_ppg = ppg_sensor_sample.raw_green;

    // Find min/max in current batch for accurate tracking
    uint32_t batch_min = UINT32_MAX;
    uint32_t batch_max = 0;
    
    for (int i = 0; i < ppg_sensor_sample.ppg_num_samples; i++)
    {
        if (data_ppg[i] < batch_min) batch_min = data_ppg[i];
        if (data_ppg[i] > batch_max) batch_max = data_ppg[i];
    }
    
    // Update global min/max with batch values
    if (y_min_ppg == 10000)
    {
        y_min_ppg = batch_min;
    }
    else
    {
        if (batch_min < y_min_ppg) y_min_ppg = batch_min;
    }
    
    if (y_max_ppg == 0)
    {
        y_max_ppg = batch_max;
    }
    else
    {
        if (batch_max > y_max_ppg) y_max_ppg = batch_max;
    }

    // Plot all samples
    for (int i = 0; i < ppg_sensor_sample.ppg_num_samples; i++)
    {
        lv_chart_set_next_value(chart_ppg, ser_ppg, data_ppg[i]);
        hpi_ppg_disp_add_samples(1);
    }
    
    // Call autoscale once per batch, not per sample, for better performance
    hpi_ppg_disp_do_set_scale(PPG_RAW_WINDOW_SIZE);
}

/* Public function to check for signal timeout - called periodically by display SM */
void hpi_ppg_check_signal_timeout(void)
{
    // Use the last known SCD state for timeout checks
    // This way we only show timeout, not incorrectly assuming "no skin contact"
    hpi_ppg_update_signal_status(last_scd_state);
}
