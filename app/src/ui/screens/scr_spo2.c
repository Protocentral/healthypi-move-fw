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
#include "trends.h"
#include "hw_module.h"
#include "hpi_sys.h"

LOG_MODULE_REGISTER(hpi_disp_scr_spo2, LOG_LEVEL_DBG);

#define SPO2_SCR_TREND_MAX_POINTS 24

lv_obj_t *scr_spo2;

// GUI Labels
static lv_obj_t *label_spo2_percent;
static lv_obj_t *label_spo2_last_update_time;
static lv_obj_t *btn_spo2_measure;

// Externs - Modern style system
extern lv_style_t style_body_medium;
extern lv_style_t style_numeric_large;
extern lv_style_t style_caption;

static void scr_spo2_btn_measure_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        //hpi_load_scr_spl(SCR_SPL_SPO2_SCR2, SCROLL_UP, (uint8_t)SCR_SPO2, 0, 0, 0);
        hpi_load_scr_spl(SCR_SPL_SPO2_SELECT, SCROLL_UP, (uint8_t)SCR_SPO2, 0, 0, 0);
    }
}

void draw_scr_spo2(enum scroll_dir m_scroll_dir)
{
    scr_spo2 = lv_obj_create(NULL);
    // AMOLED OPTIMIZATION: Pure black background for power efficiency
    lv_obj_set_style_bg_color(scr_spo2, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_spo2, LV_OBJ_FLAG_SCROLLABLE);

    // CIRCULAR AMOLED-OPTIMIZED SPO2 SCREEN (BLUE THEME)
    // Display center: (195, 195), Usable radius: ~185px
    
    // Get SpO2 data first
    uint8_t spo2 = 0;
    int64_t last_update_ts = 0;
    if (hpi_sys_get_last_spo2_update(&spo2, &last_update_ts) != 0)
    {
        spo2 = 0;
        last_update_ts = 0;
    }

    // OUTER RING: SpO2 Zone Progress Arc (Radius 170-185px)
    lv_obj_t *arc_spo2_zone = lv_arc_create(scr_spo2);
    lv_obj_set_size(arc_spo2_zone, 370, 370);  // 185px radius
    lv_obj_center(arc_spo2_zone);
    lv_arc_set_range(arc_spo2_zone, 70, 100);  // SpO2 range 70-100%
    
    // Background arc: Full 270° track (gray)
    lv_arc_set_bg_angles(arc_spo2_zone, 135, 45);  // Full background arc
    
    // Indicator arc: Shows current SpO2 position from start
    lv_arc_set_angles(arc_spo2_zone, 135, 135);  // Start at beginning, will extend based on value
    
    // Set arc value based on current SpO2
    if (spo2 > 0) {
        lv_arc_set_value(arc_spo2_zone, spo2);
    } else {
        lv_arc_set_value(arc_spo2_zone, 95);  // Default/good position
    }
    
    // Style the progress arc - blue theme
    lv_obj_set_style_arc_color(arc_spo2_zone, lv_color_hex(0x333333), LV_PART_MAIN);    // Background track
    lv_obj_set_style_arc_width(arc_spo2_zone, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_spo2_zone, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_INDICATOR);  // Progress indicator
    lv_obj_set_style_arc_width(arc_spo2_zone, 6, LV_PART_INDICATOR);
    lv_obj_remove_style(arc_spo2_zone, NULL, LV_PART_KNOB);  // Remove knob
    lv_obj_clear_flag(arc_spo2_zone, LV_OBJ_FLAG_CLICKABLE);

    // Screen title - properly centered at top (moved down to clear arc overlap)
    lv_obj_t *label_title = lv_label_create(scr_spo2);
    lv_label_set_text(label_title, "SpO2");
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 40);  // Moved down from 10px to 40px to clear arc
    lv_obj_add_style(label_title, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);

    // MID-UPPER RING: SpO2 Icon (clean, no container - using smaller 30x35 icon)
    lv_obj_t *img_spo2 = lv_img_create(scr_spo2);
    lv_img_set_src(img_spo2, &icon_spo2_30x35);
    lv_obj_align(img_spo2, LV_ALIGN_TOP_MID, 0, 95);  // Moved down from 65px to account for lower title
    lv_obj_set_style_img_recolor(img_spo2, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(img_spo2, LV_OPA_COVER, LV_PART_MAIN);

    // CENTRAL ZONE: Main SpO2 Value (properly spaced from icon)
    // Large central metric display for maximum readability
    label_spo2_percent = lv_label_create(scr_spo2);
    if (spo2 == 0) {
        lv_label_set_text(label_spo2_percent, "--");
    } else {
        lv_label_set_text_fmt(label_spo2_percent, "%d", spo2);
    }
    lv_obj_align(label_spo2_percent, LV_ALIGN_CENTER, 0, -10);  // Centered, slightly above middle
    lv_obj_set_style_text_color(label_spo2_percent, lv_color_white(), LV_PART_MAIN);
    lv_obj_add_style(label_spo2_percent, &style_numeric_large, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_spo2_percent, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // Unit label directly below main value with proper spacing
    lv_obj_t *label_spo2_unit = lv_label_create(scr_spo2);
    lv_label_set_text(label_spo2_unit, "%");  // Simplified from "SpO2%"
    lv_obj_align(label_spo2_unit, LV_ALIGN_CENTER, 0, 35);  // Below main value with gap
    lv_obj_set_style_text_color(label_spo2_unit, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN);
    lv_obj_add_style(label_spo2_unit, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_spo2_unit, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // Status info - centered below unit with proper spacing
    label_spo2_last_update_time = lv_label_create(scr_spo2);
    char last_meas_str[25];
    hpi_helper_get_relative_time_str(last_update_ts, last_meas_str, sizeof(last_meas_str));
    lv_label_set_text(label_spo2_last_update_time, last_meas_str);
    lv_obj_align(label_spo2_last_update_time, LV_ALIGN_CENTER, 0, 80);  // Centered, below unit with gap
    lv_obj_set_style_text_color(label_spo2_last_update_time, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_add_style(label_spo2_last_update_time, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_spo2_last_update_time, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // BOTTOM ZONE: Action Button (properly centered at bottom)
    btn_spo2_measure = hpi_btn_create_primary(scr_spo2);
    lv_obj_set_size(btn_spo2_measure, 180, 50);  // Width for "Measure" text
    lv_obj_align(btn_spo2_measure, LV_ALIGN_BOTTOM_MID, 0, -30);  // Centered at bottom with margin
    lv_obj_set_style_radius(btn_spo2_measure, 25, LV_PART_MAIN);
    
    // AMOLED-optimized button styling - blue theme
    lv_obj_set_style_bg_color(btn_spo2_measure, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_spo2_measure, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_spo2_measure, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_spo2_measure, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN);
    lv_obj_set_style_border_opa(btn_spo2_measure, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn_spo2_measure, 0, LV_PART_MAIN);  // No shadow for AMOLED
    
    lv_obj_t *label_measure = lv_label_create(btn_spo2_measure);
    lv_label_set_text(label_measure, LV_SYMBOL_REFRESH " Measure");
    lv_obj_center(label_measure);
    lv_obj_set_style_text_color(label_measure, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_spo2_measure, scr_spo2_btn_measure_handler, LV_EVENT_CLICKED, NULL);

    hpi_disp_set_curr_screen(SCR_SPO2);
    hpi_show_screen(scr_spo2, m_scroll_dir);
}

void hpi_disp_update_spo2(uint8_t spo2, int64_t ts_last_update)
{
    if (label_spo2_percent == NULL)
        return;

    if (spo2 == 0) {
        lv_label_set_text(label_spo2_percent, "--");
    } else {
        lv_label_set_text_fmt(label_spo2_percent, "%d", spo2);
    }

    if (label_spo2_last_update_time != NULL) {
        char last_meas_str[25];
        hpi_helper_get_relative_time_str(ts_last_update, last_meas_str, sizeof(last_meas_str));
        lv_label_set_text(label_spo2_last_update_time, last_meas_str);
    }

    // Update progress arc if it exists (for future dynamic updates)
    // This would require storing the arc object globally if needed
}