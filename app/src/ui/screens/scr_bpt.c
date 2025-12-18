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
#include <lvgl.h>
#include <stdio.h>
#include <app_version.h>
#include <zephyr/logging/log.h>

#include "ui/move_ui.h"
#include "hpi_common_types.h"
#include "hpi_sys.h"

LOG_MODULE_REGISTER(scr_bpt, LOG_LEVEL_DBG);

lv_obj_t *scr_bpt;

static lv_obj_t *label_bpt_last_update_time;

extern lv_style_t style_scr_black;
extern lv_style_t style_scr_container;
extern lv_style_t style_white_large_numeric;
extern lv_style_t style_white_medium;
extern lv_style_t style_white_medium;
extern lv_style_t style_red_medium;

static void scr_bpt_measure_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        LOG_DBG("Measure click");
        hpi_load_scr_spl(SCR_SPL_FI_SENS_WEAR, SCROLL_UP, SCR_SPL_FI_SENS_CHECK , (uint8_t)SCR_BPT, 0, 0);
    }
}

void draw_scr_bpt(enum scroll_dir m_scroll_dir)
{
    scr_bpt = lv_obj_create(NULL);
    // AMOLED OPTIMIZATION: Pure black background for power efficiency
    lv_obj_set_style_bg_color(scr_bpt, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_bpt, LV_OBJ_FLAG_SCROLLABLE);

    // CIRCULAR AMOLED-OPTIMIZED BLOOD PRESSURE SCREEN
    // Display center: (195, 195), Usable radius: ~185px
    // Blue/Purple theme for blood pressure measurements

    // Get blood pressure data
    uint8_t bpt_sys = 0;
    uint8_t bpt_dia = 0;
    int64_t bpt_time = 0;

    if (hpi_sys_get_last_bp_update(&bpt_sys, &bpt_dia, &bpt_time) == 0) {
        LOG_DBG("Last BPT value: %d/%d", bpt_sys, bpt_dia);
    } else {
        LOG_ERR("Failed to get last BPT value");
        bpt_sys = 0;
        bpt_dia = 0;
        bpt_time = 0;
    }

    // OUTER RING: BP Progress Arc (Radius 170-185px) - Blue/Purple theme for BP
    lv_obj_t *arc_bp_zone = lv_arc_create(scr_bpt);
    lv_obj_set_size(arc_bp_zone, 370, 370);  // 185px radius
    lv_obj_center(arc_bp_zone);
    lv_arc_set_range(arc_bp_zone, 0, 200);  // Range representing systolic pressure
    
    // Background arc: Full 270° track (gray)
    lv_arc_set_bg_angles(arc_bp_zone, 135, 45);  // Full background arc
    lv_arc_set_value(arc_bp_zone, bpt_sys > 0 ? bpt_sys : 0);  // Show systolic as progress
    
    // Style the progress arc - blue theme for blood pressure
    lv_obj_set_style_arc_color(arc_bp_zone, lv_color_hex(0x333333), LV_PART_MAIN);    // Background track
    lv_obj_set_style_arc_width(arc_bp_zone, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_bp_zone, lv_color_hex(0x4A90E2), LV_PART_INDICATOR);  // Blue progress
    lv_obj_set_style_arc_width(arc_bp_zone, 6, LV_PART_INDICATOR);
    lv_obj_remove_style(arc_bp_zone, NULL, LV_PART_KNOB);  // Remove knob
    lv_obj_clear_flag(arc_bp_zone, LV_OBJ_FLAG_CLICKABLE);

    // Screen title - properly positioned to avoid arc overlap
    lv_obj_t *label_title = lv_label_create(scr_bpt);
    lv_label_set_text(label_title, "Blood Pressure");
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 40);  // Centered at top, clear of arc
    lv_obj_add_style(label_title, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);

    // CENTRAL ZONE: BP Values Container (positioned in center area)
    lv_obj_t *cont_bp_values = lv_obj_create(scr_bpt);
    lv_obj_set_size(cont_bp_values, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(cont_bp_values, LV_ALIGN_CENTER, 0, -20);  // Slightly above center
    lv_obj_set_style_bg_opa(cont_bp_values, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_bp_values, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont_bp_values, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont_bp_values, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_bp_values, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // SYS Column (left side)
    lv_obj_t *cont_sys = lv_obj_create(cont_bp_values);
    lv_obj_set_size(cont_sys, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(cont_sys, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_sys, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont_sys, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_right(cont_sys, 15, LV_PART_MAIN);  // Spacing between SYS and DIA
    lv_obj_set_flex_flow(cont_sys, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_sys, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // SYS Label
    lv_obj_t *label_sys = lv_label_create(cont_sys);
    lv_label_set_text(label_sys, "SYS");
    lv_obj_add_style(label_sys, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_sys, lv_color_hex(0x4A90E2), LV_PART_MAIN);  // Blue accent
    lv_obj_set_style_text_align(label_sys, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // SYS Value
    lv_obj_t *label_bpt_sys = lv_label_create(cont_sys);
    if (bpt_sys == 0) {
        lv_label_set_text(label_bpt_sys, "--");
    } else {
        lv_label_set_text_fmt(label_bpt_sys, "%d", bpt_sys);
    }
    lv_obj_add_style(label_bpt_sys, &style_numeric_large, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_bpt_sys, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_align(label_bpt_sys, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // SYS Unit
    lv_obj_t *label_sys_unit = lv_label_create(cont_sys);
    lv_label_set_text(label_sys_unit, "mmHg");
    lv_obj_add_style(label_sys_unit, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_sys_unit, lv_color_hex(0x4A90E2), LV_PART_MAIN);
    lv_obj_set_style_text_align(label_sys_unit, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // Separator visual element
    lv_obj_t *separator = lv_obj_create(cont_bp_values);
    lv_obj_set_size(separator, 2, 60);  // Thin vertical line
    lv_obj_set_style_bg_color(separator, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(separator, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(separator, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(separator, 1, LV_PART_MAIN);

    // DIA Column (right side)
    lv_obj_t *cont_dia = lv_obj_create(cont_bp_values);
    lv_obj_set_size(cont_dia, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(cont_dia, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_dia, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont_dia, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(cont_dia, 15, LV_PART_MAIN);  // Spacing between SYS and DIA
    lv_obj_set_flex_flow(cont_dia, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_dia, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // DIA Label
    lv_obj_t *label_dia = lv_label_create(cont_dia);
    lv_label_set_text(label_dia, "DIA");
    lv_obj_add_style(label_dia, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_dia, lv_color_hex(0x7B68EE), LV_PART_MAIN);  // Purple accent
    lv_obj_set_style_text_align(label_dia, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // DIA Value
    lv_obj_t *label_bpt_dia = lv_label_create(cont_dia);
    if (bpt_dia == 0) {
        lv_label_set_text(label_bpt_dia, "--");
    } else {
        lv_label_set_text_fmt(label_bpt_dia, "%d", bpt_dia);
    }
    lv_obj_add_style(label_bpt_dia, &style_numeric_large, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_bpt_dia, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_align(label_bpt_dia, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // DIA Unit
    lv_obj_t *label_dia_unit = lv_label_create(cont_dia);
    lv_label_set_text(label_dia_unit, "mmHg");
    lv_obj_add_style(label_dia_unit, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_dia_unit, lv_color_hex(0x7B68EE), LV_PART_MAIN);
    lv_obj_set_style_text_align(label_dia_unit, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // Last measurement time (positioned below values with proper spacing)
    // Fixed: Reduced from 74 to 32 bytes to match other screens and prevent stack/heap corruption
    // Maximum string length from hpi_helper_get_relative_time_str is ~20 chars
    char last_meas_str[32];
    hpi_helper_get_relative_time_str(bpt_time, last_meas_str, sizeof(last_meas_str));
    label_bpt_last_update_time = lv_label_create(scr_bpt);
    lv_label_set_text(label_bpt_last_update_time, last_meas_str);
    lv_obj_align(label_bpt_last_update_time, LV_ALIGN_CENTER, 0, 80);  // Increased spacing to match other screens
    lv_obj_add_style(label_bpt_last_update_time, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_bpt_last_update_time, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_bpt_last_update_time, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);

    // BOTTOM ZONE: Action Button (consistent with other screens - 200x60, dark background, white text)
    lv_obj_t *btn_bpt_measure = hpi_btn_create_primary(scr_bpt);
    lv_obj_set_size(btn_bpt_measure, 200, 60);
    lv_obj_align(btn_bpt_measure, LV_ALIGN_BOTTOM_MID, 0, -35);
    lv_obj_set_style_radius(btn_bpt_measure, 30, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn_bpt_measure, lv_color_hex(COLOR_BTN_BLUE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_bpt_measure, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_bpt_measure, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn_bpt_measure, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_bpt_measure, scr_bpt_measure_handler, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label_btn_measure = lv_label_create(btn_bpt_measure);
    lv_label_set_text(label_btn_measure, LV_SYMBOL_PLAY " Measure");
    lv_obj_center(label_btn_measure);
    lv_obj_set_style_text_color(label_btn_measure, lv_color_white(), LV_PART_MAIN);

    hpi_disp_set_curr_screen(SCR_BPT);
    hpi_show_screen(scr_bpt, m_scroll_dir);
}
