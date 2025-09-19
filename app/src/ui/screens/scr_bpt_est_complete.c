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


#include <lvgl.h>
#include <stdio.h>
#include <zephyr/logging/log.h>

#include "hpi_common_types.h"
#include "hw_module.h"
#include "ui/move_ui.h"

LOG_MODULE_REGISTER(scr_bpt_est_complete, LOG_LEVEL_DBG);

lv_obj_t *scr_bpt_est_complete;

static lv_obj_t *btn_close;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_large_numeric;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_tiny;

static void scr_btn_close_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        // k_sem_give(&sem_bpt_check_sensor);
        hpi_load_screen(SCR_BPT, SCROLL_UP);
    }
}

void draw_scr_bpt_est_complete(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_bpt_est_complete = lv_obj_create(NULL);
    // AMOLED OPTIMIZATION: Pure black background for power efficiency
    lv_obj_set_style_bg_color(scr_bpt_est_complete, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_bpt_est_complete, LV_OBJ_FLAG_SCROLLABLE);

    // CIRCULAR AMOLED-OPTIMIZED BP COMPLETION SCREEN
    // Display center: (195, 195), Usable radius: ~185px
    // Green theme for successful completion

    // SUCCESS RING: Complete BP Measurement Arc (Radius 170-185px)
    lv_obj_t *arc_success = lv_arc_create(scr_bpt_est_complete);
    lv_obj_set_size(arc_success, 370, 370);  // 185px radius
    lv_obj_center(arc_success);
    lv_arc_set_range(arc_success, 0, 100);  // Full completion
    
    // Background arc: Full 270° track (gray)
    lv_arc_set_bg_angles(arc_success, 135, 45);  // Full background arc
    lv_arc_set_value(arc_success, 100);  // Complete
    
    // Style the success arc - green theme for completion
    lv_obj_set_style_arc_color(arc_success, lv_color_hex(0x333333), LV_PART_MAIN);    // Background track
    lv_obj_set_style_arc_width(arc_success, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_success, lv_color_hex(0x4CAF50), LV_PART_INDICATOR);  // Green success
    lv_obj_set_style_arc_width(arc_success, 6, LV_PART_INDICATOR);
    lv_obj_remove_style(arc_success, NULL, LV_PART_KNOB);  // Remove knob
    lv_obj_clear_flag(arc_success, LV_OBJ_FLAG_CLICKABLE);

    // Success icon at top
    lv_obj_t *label_success = lv_label_create(scr_bpt_est_complete);
    lv_label_set_text(label_success, LV_SYMBOL_OK);
    lv_obj_align(label_success, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_text_color(label_success, lv_color_hex(0x4CAF50), LV_PART_MAIN);  // Green
    lv_obj_set_style_text_font(label_success, &lv_font_montserrat_24, LV_PART_MAIN);

    // CENTRAL ZONE: BP Results (positioned in center area)
    lv_obj_t *cont_bp_results = lv_obj_create(scr_bpt_est_complete);
    lv_obj_set_size(cont_bp_results, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(cont_bp_results, LV_ALIGN_CENTER, 0, -20);  // Slightly above center
    lv_obj_set_style_bg_opa(cont_bp_results, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_bp_results, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont_bp_results, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont_bp_results, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_bp_results, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // SYS Column (left side)
    lv_obj_t *cont_sys = lv_obj_create(cont_bp_results);
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
    lv_obj_add_style(label_sys, &style_red_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_sys, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // SYS Value
    lv_obj_t *label_sys_value = lv_label_create(cont_sys);
    lv_label_set_text_fmt(label_sys_value, "%d", arg1);
    lv_obj_add_style(label_sys_value, &style_white_large_numeric, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_sys_value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // SYS Unit
    lv_obj_t *label_sys_unit = lv_label_create(cont_sys);
    lv_label_set_text(label_sys_unit, "mmHg");
    lv_obj_add_style(label_sys_unit, &style_white_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_sys_unit, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // Separator visual element
    lv_obj_t *separator = lv_obj_create(cont_bp_results);
    lv_obj_set_size(separator, 2, 60);  // Thin vertical line
    lv_obj_set_style_bg_color(separator, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(separator, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(separator, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(separator, 1, LV_PART_MAIN);

    // DIA Column (right side)
    lv_obj_t *cont_dia = lv_obj_create(cont_bp_results);
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
    lv_obj_add_style(label_dia, &style_red_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_dia, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // DIA Value
    lv_obj_t *label_dia_value = lv_label_create(cont_dia);
    lv_label_set_text_fmt(label_dia_value, "%d", arg2);
    lv_obj_add_style(label_dia_value, &style_white_large_numeric, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_dia_value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // DIA Unit
    lv_obj_t *label_dia_unit = lv_label_create(cont_dia);
    lv_label_set_text(label_dia_unit, "mmHg");
    lv_obj_add_style(label_dia_unit, &style_white_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_dia_unit, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // HR display below BP values (if available)
    if (arg3 > 0) {
        lv_obj_t *cont_hr = lv_obj_create(scr_bpt_est_complete);
        lv_obj_set_size(cont_hr, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_align(cont_hr, LV_ALIGN_CENTER, 0, 55);  // Below BP values
        lv_obj_set_style_bg_opa(cont_hr, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(cont_hr, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(cont_hr, 0, LV_PART_MAIN);
        lv_obj_set_flex_flow(cont_hr, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(cont_hr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // Heart Icon
        lv_obj_t *img_heart = lv_img_create(cont_hr);
        lv_img_set_src(img_heart, &img_heart_35);

        // HR Value
        lv_obj_t *label_hr_value = lv_label_create(cont_hr);
        lv_label_set_text_fmt(label_hr_value, "%d", arg3);
        lv_obj_add_style(label_hr_value, &style_white_medium, LV_PART_MAIN);
        lv_obj_set_style_pad_left(label_hr_value, 8, LV_PART_MAIN);

        // HR Unit
        lv_obj_t *label_hr_unit = lv_label_create(cont_hr);
        lv_label_set_text(label_hr_unit, "BPM");
        lv_obj_add_style(label_hr_unit, &style_white_medium, LV_PART_MAIN);
    }

    // Swipe down hint at bottom
    lv_obj_t *label_hint = lv_label_create(scr_bpt_est_complete);
    lv_label_set_text(label_hint, LV_SYMBOL_DOWN " Swipe down to continue");
    lv_obj_align(label_hint, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_add_style(label_hint, &style_white_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    hpi_disp_set_curr_screen(SCR_SPL_BPT_EST_COMPLETE);
    hpi_show_screen(scr_bpt_est_complete, m_scroll_dir);
}

void gesture_down_scr_bpt_est_complete(void)
{
    hpi_load_screen(SCR_BPT, SCROLL_DOWN);
}