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
lv_obj_t *scr_raw_ppg_hrv;
lv_obj_t *scr_ppg_outgauge_hrv;

// GUI Charts
static lv_obj_t *chart_ppg_hrv;
static lv_chart_series_t *ser_ppg_hrv;

// GUI Labels
static lv_obj_t *label_ppg_hr_hrv;
static lv_obj_t *label_ppg_spo2_hrv;
static lv_obj_t *label_status_hrv;
static lv_obj_t *label_ppg_no_signal_hrv;
static lv_obj_t *arc_hrv_zone;
static lv_obj_t *label_timer_hrv;

static int parent_screen = 0;

static float y_max_ppg_hrv = 0;
static float y_min_ppg_hrv = 10000;

float y2_max_hrv = 0;
float y2_min_hrv = 10000;

float y3_max_hrv = 0;
float y3_min_hrv = 10000;

static float gx_hrv = 0;

// Signal detection and timeout tracking
static uint32_t last_ppg_data_time = 0;
static enum hpi_ppg_status last_scd_state = HPI_PPG_SCD_STATUS_UNKNOWN;
#define PPG_SIGNAL_TIMEOUT_MS 3000  // Show "No Signal" if no data for 3 seconds

// Timer control variables for lead-based automatic start/stop
static bool timer_running = false;
static bool timer_paused = true;  // Start paused, wait for lead ON
static bool lead_on_detected = false;

// Performance optimization variables - LVGL 9.2 optimized
static uint32_t sample_counter = 0;
static const uint32_t RANGE_UPDATE_INTERVAL = 128; // Update range every 64 samples - Less frequent for better performance


extern lv_style_t style_red_medium;
extern lv_style_t style_white_medium;

extern lv_style_t style_scr_black;

#define PPG_SIGNAL_RED 0
#define PPG_SIGNAL_IR 1
#define PPG_SIGNAL_GREEN 2

uint8_t ppg_disp_signal_type = PPG_SIGNAL_RED;

LOG_MODULE_REGISTER(ppg_scr_hrv);

void draw_scr_spl_raw_ppg_hrv(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    parent_screen = arg1;

    scr_ppg_outgauge_hrv; = lv_obj_create(NULL);
    // AMOLED OPTIMIZATION: Pure black background for power efficiency
    lv_obj_set_style_bg_color(scr_ppg_outgauge_hrv, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_ppg_outgauge_hrv, LV_OBJ_FLAG_SCROLLABLE);

    // CIRCULAR AMOLED-OPTIMIZED ECG MEASUREMENT SCREEN
    // Display center: (195, 195), Usable radius: ~185px
    // Orange/amber theme for ECG measurement consistency

    // Get ECG/HR data
    uint16_t hr = 0;
    int64_t hr_last_update = 0;
    if (hpi_sys_get_last_hr_update(&hr, &hr_last_update) != 0) {
        hr = 0;
        hr_last_update = 0;
    }

    // OUTER RING: ECG Timer Countdown Arc (Radius 170-185px) - Orange theme for measurement

    arc_hrv_zone = lv_arc_create(scr_ppg_outgauge_hrv);
    lv_obj_set_size(arc_hrv_zone, 370, 370);  // 185px radius
    lv_obj_center(arc_hrv_zone);
    lv_arc_set_range(arc_hrv_zone, 0, 30);  // Timer range: 0-30 seconds
    
    // Background arc: Full 270° track (gray)
    lv_arc_set_bg_angles(arc_hrv_zone, 135, 45);  // Full background arc
    lv_arc_set_value(arc_hrv_zone, 30);  // Start at full (30 seconds), will countdown to 0
    
    // Style the progress arc - orange theme for ECG measurement
    lv_obj_set_style_arc_color(arc_hrv_zone, lv_color_hex(0x333333), LV_PART_MAIN);    // Background track
    lv_obj_set_style_arc_width(arc_hrv_zone, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_hrv_zone, lv_color_hex(0xFF8C00), LV_PART_INDICATOR);  // Orange progress
    lv_obj_set_style_arc_width(arc_hrv_zone, 6, LV_PART_INDICATOR);
    lv_obj_remove_style(arc_hrv_zone, NULL, LV_PART_KNOB);  // Remove knob
    lv_obj_clear_flag(arc_hrv_zone, LV_OBJ_FLAG_CLICKABLE);

    // Screen title - properly positioned to avoid arc overlap
    // MID-UPPER RING: Timer container with icon (following design pattern)
    lv_obj_t *cont_timer = lv_obj_create(scr_ppg_outgauge_hrv);
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
    lv_obj_set_style_img_recolor(img_timer, lv_color_hex(0xFF8C00), LV_PART_MAIN);  // Orange theme
    lv_obj_set_style_img_recolor_opa(img_timer, LV_OPA_COVER, LV_PART_MAIN);

    // Timer value
    label_timer_hrv = lv_label_create(cont_timer);
    lv_label_set_text(label_timer_hrv, "30");
    lv_obj_add_style(label_timer_hrv, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_timer_hrv, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_pad_left(label_timer_hrv, 8, LV_PART_MAIN);

    // Timer unit
    lv_obj_t *label_timer_unit = lv_label_create(cont_timer);
    lv_label_set_text(label_timer_unit, "s");
    lv_obj_add_style(label_timer_unit, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_timer_unit, lv_color_hex(0xFF8C00), LV_PART_MAIN);  // Orange accent

    // Initialize timer state - start paused, waiting for lead ON detection
    timer_running = false;
    timer_paused = true;
    lead_on_detected = false;



    scr_raw_ppg_hrv = lv_obj_create(NULL);
    lv_obj_clear_flag(scr_raw_ppg_hrv, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    lv_obj_t *cont_col_hrv = lv_obj_create(scr_raw_ppg);
    lv_obj_set_size(cont_col_hrv, lv_pct(100), lv_pct(100));
    // lv_obj_set_width(cont_col, lv_pct(100));
    lv_obj_align_to(cont_col_hrv, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col_hrv, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col_hrv, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_col_hrv, -1, LV_PART_SCROLLBAR);
    lv_obj_add_style(cont_col_hrv, &style_scr_black, 0);

    lv_obj_t *label_signal_hrv = lv_label_create(cont_col);
    lv_label_set_text(label_signal_hrv, "PPG");
    lv_obj_align(label_signal_hrv, LV_ALIGN_TOP_MID, 0, 5);

    chart_ppg_hrv = lv_chart_create(cont_col);
    /* Match SpO2 measure chart styling */
    lv_obj_set_size(chart_ppg_hrv, 390, 140);
    lv_obj_set_style_bg_color(chart_ppg_hrv, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(chart_ppg_hrv, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(chart_ppg_hrv, 0, LV_PART_MAIN);
    lv_chart_set_point_count(chart_ppg_hrv, PPG_RAW_WINDOW_SIZE);
    lv_chart_set_div_line_count(chart_ppg_hrv, 0, 0);
    lv_chart_set_update_mode(chart_ppg_hrv, LV_CHART_UPDATE_MODE_CIRCULAR);
    lv_obj_align(chart_ppg_hrv, LV_ALIGN_CENTER, 0, -35);
    
    // Set initial Y-axis range suitable for PPG data (typically 0-65535 for raw values)
    // Start with a reasonable range around typical PPG baseline
    lv_chart_set_range(chart_ppg_hrv, LV_CHART_AXIS_PRIMARY_Y, 0, 65535);

    ser_ppg_hrv= lv_chart_add_series(chart_ppg_hrv, lv_palette_main(LV_PALETTE_ORANGE), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_line_width(chart_ppg_hrv, 6, LV_PART_ITEMS);

    // Draw BPM container
    lv_obj_t *cont_hr = lv_obj_create(cont_col_hrv);
    lv_obj_set_size(cont_hr, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_hr, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(cont_hr, &style_scr_black, 0);
    lv_obj_set_flex_align(cont_hr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *img_heart = lv_img_create(cont_hr);
    lv_img_set_src(img_heart, &img_heart_48px);

    label_ppg_hr_hrv = lv_label_create(cont_hr);
    lv_label_set_text(label_ppg_hr_hrv, "00");
    lv_obj_add_style(label_ppg_hr_hrv, &style_white_medium, 0);
    lv_obj_t *label_hr_sub = lv_label_create(cont_hr);
    lv_label_set_text(label_hr_sub, " bpm");

    // PPG Sensor Status label
    label_status_hrv = lv_label_create(scr_raw_ppg);
    lv_label_set_text(label_status_hrv, "--");
    lv_obj_align_to(label_status_hrv, chart_ppg_hrv, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_obj_set_style_text_align(label_status_hrv, LV_TEXT_ALIGN_CENTER, 0);

    // Create "No Signal" overlay label (initially hidden)
    label_ppg_no_signal_hrv = lv_label_create(scr_raw_ppg);
    lv_label_set_text(label_ppg_no_signal_hrv, "No Signal");
    lv_obj_align(label_ppg_no_signal_hrv, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_style_text_color(label_ppg_no_signal_hrv, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_add_style(label_ppg_no_signal_hrv, &style_white_medium, 0);
    lv_obj_add_flag(label_ppg_no_signal_hrv, LV_OBJ_FLAG_HIDDEN); // Start hidden

    // Initialize timestamp and SCD state
    last_ppg_data_time = k_uptime_get_32();
    last_scd_state = HPI_PPG_SCD_STATUS_UNKNOWN;

    // Reset min/max tracking for fresh autoscaling
    y_min_ppg_hrv = 10000;
    y_max_ppg_hrv = 0;
    gx_hrv = 0;
    
    // Reset autoscale state to ensure first update happens
    hpi_ppg_autoscale_reset();

    hpi_disp_set_curr_screen(SCR_SPL_HRV_PLOT);
    hpi_show_screen(scr_raw_ppg_hrv, m_scroll_dir);
}

void gesture_down_scr_spl_raw_ppg_hrv(void)
{
    /* Return to parent screen on gesture down */
    hpi_load_screen(parent_screen, SCROLL_DOWN);
}

void hpi_ppg_disp_update_hr_hrv(int hr)
{
    if (label_ppg_hr_hrv == NULL)
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

    lv_label_set_text(label_ppg_hr_hrv, buf);
}

static void hpi_ppg_disp_add_samples_hrv(int num_samples)
{
    gx_hrv += num_samples;
}

/* Delegate autoscale to shared helper to keep behavior consistent across screens */
static void hpi_ppg_disp_do_set_scale_hrv(int disp_window_size)
{
    hpi_ppg_disp_do_set_scale_shared(chart_ppg_hrv, &y_min_ppg_hrv, &y_max_ppg_hrv, &gx_hrv disp_window_size);
}

/* Update "No Signal" label visibility based on data presence and SCD status */
static void hpi_ppg_update_signal_status_hrv(enum hpi_ppg_status scd_state)
{
    if (label_ppg_no_signal_hrv == NULL)
        return;

    uint32_t current_time = k_uptime_get_32();
    bool timeout = (current_time - last_ppg_data_time) > PPG_SIGNAL_TIMEOUT_MS;
    bool no_skin_contact = (scd_state == HPI_PPG_SCD_OFF_SKIN);

    // Show "No Signal" if timeout OR no skin contact (but ONLY check skin contact if we have valid state)
    if (timeout || no_skin_contact)
    {
        lv_obj_clear_flag(label_ppg_no_signal_hrv, LV_OBJ_FLAG_HIDDEN);
        
        // Update message based on reason - prioritize skin contact message when both conditions exist
        if (no_skin_contact)
        {
            lv_label_set_text(label_ppg_no_signal_hrv, "No Skin Contact");
        }
        else if (timeout)
        {
            lv_label_set_text(label_ppg_no_signal_hrv, "No Signal");
        }
    }
    else
    {
        lv_obj_add_flag(label_ppg_no_signal_hrv, LV_OBJ_FLAG_HIDDEN);
    }
}

void hpi_disp_ppg_draw_plotPPG_hrv(struct hpi_ppg_wr_data_t ppg_sensor_sample)
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
    if (y_min_ppg_hrv == 10000)
    {
        y_min_ppg_hrv = batch_min;
    }
    else
    {
        if (batch_min < y_min_ppg_hrv) y_min_ppg_hrv = batch_min;
    }
    
    if (y_max_ppg_hrv == 0)
    {
        y_max_ppg_hrv = batch_max;
    }
    else
    {
        if (batch_max > y_max_ppg_hrv) y_max_ppg_hrv = batch_max;
    }

    // Plot all samples
    for (int i = 0; i < ppg_sensor_sample.ppg_num_samples; i++)
    {
        lv_chart_set_next_value(chart_ppg_hrv, ser_ppg_hrv, data_ppg[i]);
        hpi_ppg_disp_add_samples_hrv(1);
    }
    
    // Call autoscale once per batch, not per sample, for better performance
    hpi_ppg_disp_do_set_scale_hrv(PPG_RAW_WINDOW_SIZE);
}

/* Public function to check for signal timeout - called periodically by display SM */
void hpi_ppg_check_signal_timeout_hrv(void)
{
    // Use the last known SCD state for timeout checks
    // This way we only show timeout, not incorrectly assuming "no skin contact"
    hpi_ppg_update_signal_status_hrv(last_scd_state);
}
