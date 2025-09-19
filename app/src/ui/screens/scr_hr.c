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
#include <string.h>

#include "hpi_common_types.h"
#include "ui/move_ui.h"
#include "trends.h"
#include "hpi_sys.h"

LOG_MODULE_REGISTER(hpi_disp_scr_hr, LOG_LEVEL_DBG);

#define HR_SCR_TREND_MAX_POINTS 24

static lv_obj_t *scr_hr;

// GUI Labels
static lv_obj_t *label_hr_bpm;
static lv_obj_t *label_hr_last_update_time;

// Externs - Modern style system
extern lv_style_t style_body_medium;
extern lv_style_t style_numeric_large;
extern lv_style_t style_caption;

/* Prototype for Raw PPG button event handler */
static void scr_hr_btn_raw_event_handler(lv_event_t *e);

void draw_scr_hr(enum scroll_dir m_scroll_dir)
{
    scr_hr = lv_obj_create(NULL);
    // AMOLED OPTIMIZATION: Pure black background for power efficiency
    lv_obj_set_style_bg_color(scr_hr, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_hr, LV_OBJ_FLAG_SCROLLABLE);

    // CIRCULAR AMOLED-OPTIMIZED HEART RATE SCREEN
    // Display center: (195, 195), Usable radius: ~185px
    
    // Get heart rate data first
    uint16_t hr = 0;
    int64_t last_update_ts = 0;
    if(hpi_sys_get_last_hr_update(&hr, &last_update_ts) != 0)
    {
        hr = 0;
        last_update_ts = 0;
    }

    // OUTER RING: HR Zone Progress Arc (Radius 170-185px)
    lv_obj_t *arc_hr_zone = lv_arc_create(scr_hr);
    lv_obj_set_size(arc_hr_zone, 370, 370);  // 185px radius
    lv_obj_center(arc_hr_zone);
    lv_arc_set_range(arc_hr_zone, 60, 200);  // HR range 60-200 BPM
    
    // Background arc: Full 270° track (gray)
    lv_arc_set_bg_angles(arc_hr_zone, 135, 45);  // Full background arc
    
    // Indicator arc: Shows current HR position from start
    lv_arc_set_angles(arc_hr_zone, 135, 135);  // Start at beginning, will extend based on value
    
    // Set arc value based on current HR
    if (hr > 0) {
        lv_arc_set_value(arc_hr_zone, hr);
    } else {
        lv_arc_set_value(arc_hr_zone, 70);  // Default/resting position
    }
    
    // Style the progress arc
    lv_obj_set_style_arc_color(arc_hr_zone, lv_color_hex(0x333333), LV_PART_MAIN);    // Background track
    lv_obj_set_style_arc_width(arc_hr_zone, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_hr_zone, lv_color_hex(COLOR_CRITICAL_RED), LV_PART_INDICATOR);  // Progress indicator
    lv_obj_set_style_arc_width(arc_hr_zone, 6, LV_PART_INDICATOR);
    lv_obj_remove_style(arc_hr_zone, NULL, LV_PART_KNOB);  // Remove knob
    lv_obj_clear_flag(arc_hr_zone, LV_OBJ_FLAG_CLICKABLE);

    // Screen title - clean and simple positioning
    lv_obj_t *label_title = lv_label_create(scr_hr);
    lv_label_set_text(label_title, "Heart Rate");
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 40);  // Centered at top, clear of arc
    lv_obj_add_style(label_title, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);

    // MID-UPPER RING: Heart Icon (clean, no container - using smaller 35px heart icon)
    lv_obj_t *img_hr = lv_img_create(scr_hr);
    lv_img_set_src(img_hr, &img_heart_35);
    lv_obj_align(img_hr, LV_ALIGN_TOP_MID, 0, 95);  // Moved down from 65px to account for lower title
    lv_obj_set_style_img_recolor(img_hr, lv_color_hex(COLOR_CRITICAL_RED), LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(img_hr, LV_OPA_COVER, LV_PART_MAIN);

    // CENTRAL ZONE: Main HR Value (properly spaced from heart icon)
    // Large central metric display for maximum readability
    label_hr_bpm = lv_label_create(scr_hr);
    if (hr == 0) {
        lv_label_set_text(label_hr_bpm, "--");
    } else {
        lv_label_set_text_fmt(label_hr_bpm, "%d", hr);
    }
    lv_obj_align(label_hr_bpm, LV_ALIGN_CENTER, 0, -10);  // Centered, slightly above middle
    lv_obj_set_style_text_color(label_hr_bpm, lv_color_white(), LV_PART_MAIN);
    lv_obj_add_style(label_hr_bpm, &style_numeric_large, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_hr_bpm, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // Unit label directly below main value with proper spacing
    lv_obj_t *label_hr_unit = lv_label_create(scr_hr);
    lv_label_set_text(label_hr_unit, "BPM");
    lv_obj_align(label_hr_unit, LV_ALIGN_CENTER, 0, 35);  // Below main value with gap
    lv_obj_set_style_text_color(label_hr_unit, lv_color_hex(COLOR_CRITICAL_RED), LV_PART_MAIN);
    lv_obj_add_style(label_hr_unit, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_hr_unit, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // Status info - centered below BPM unit with proper spacing
    label_hr_last_update_time = lv_label_create(scr_hr);
    char last_meas_str[25];
    hpi_helper_get_relative_time_str(last_update_ts, last_meas_str, sizeof(last_meas_str));
    lv_label_set_text(label_hr_last_update_time, last_meas_str);
    lv_obj_align(label_hr_last_update_time, LV_ALIGN_CENTER, 0, 80);  // Centered, below BPM with gap
    lv_obj_set_style_text_color(label_hr_last_update_time, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_add_style(label_hr_last_update_time, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_hr_last_update_time, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // BOTTOM ZONE: Action Button (properly centered at bottom)
    lv_obj_t *btn_raw_ppg = hpi_btn_create_primary(scr_hr);
    lv_obj_set_size(btn_raw_ppg, 180, 50);  // Width for "Raw PPG" text
    lv_obj_align(btn_raw_ppg, LV_ALIGN_BOTTOM_MID, 0, -30);  // Centered at bottom with margin
    lv_obj_set_style_radius(btn_raw_ppg, 25, LV_PART_MAIN);
    
    // AMOLED-optimized button styling
    lv_obj_set_style_bg_color(btn_raw_ppg, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_raw_ppg, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_raw_ppg, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_raw_ppg, lv_color_hex(COLOR_CRITICAL_RED), LV_PART_MAIN);
    lv_obj_set_style_border_opa(btn_raw_ppg, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn_raw_ppg, 0, LV_PART_MAIN);  // No shadow for AMOLED
    
    lv_obj_t *label_btn_raw = lv_label_create(btn_raw_ppg);
    lv_label_set_text(label_btn_raw, LV_SYMBOL_PLAY " Raw PPG");
    lv_obj_center(label_btn_raw);
    lv_obj_set_style_text_color(label_btn_raw, lv_color_hex(COLOR_CRITICAL_RED), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_raw_ppg, scr_hr_btn_raw_event_handler, LV_EVENT_CLICKED, NULL);

    hpi_disp_set_curr_screen(SCR_HR);
    hpi_show_screen(scr_hr, m_scroll_dir);
}

void hpi_disp_hr_update_hr(uint16_t hr, int64_t last_update_ts)
{
    if (label_hr_bpm == NULL)
        return;

    if (hr == 0)
    {
        lv_label_set_text(label_hr_bpm, "--");
    }
    else
    {
        lv_label_set_text_fmt(label_hr_bpm, "%d", hr);
    }

    if (label_hr_last_update_time != NULL) {
        char last_meas_str[25];
        hpi_helper_get_relative_time_str(last_update_ts, last_meas_str, sizeof(last_meas_str));
        lv_label_set_text(label_hr_last_update_time, last_meas_str);
    }
}

/* Event handler to open Raw PPG special screen */
static void scr_hr_btn_raw_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        hpi_load_scr_spl(SCR_SPL_RAW_PPG, SCROLL_UP, (uint8_t)SCR_HR, 0, 0, 0);
    }
}
