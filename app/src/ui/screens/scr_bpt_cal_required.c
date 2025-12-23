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

LOG_MODULE_REGISTER(scr_bpt_cal_required, LOG_LEVEL_DBG);

lv_obj_t *scr_bpt_cal_required;
static lv_obj_t *label_bpt_cal_required;

static lv_obj_t *btn_ok;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_large_numeric;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_tiny;

static void scr_btn_ok_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        LOG_DBG("OK button clicked");
        hpi_load_screen(SCR_BPT, SCROLL_UP);
    }
}

void draw_scr_bpt_cal_required(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_bpt_cal_required = lv_obj_create(NULL);
    // AMOLED OPTIMIZATION: Pure black background for power efficiency
    lv_obj_set_style_bg_color(scr_bpt_cal_required, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_bpt_cal_required, LV_OBJ_FLAG_SCROLLABLE);

    // CIRCULAR AMOLED-OPTIMIZED CALIBRATION REQUIRED SCREEN
    // Display center: (195, 195), Usable radius: ~185px
    // Blue theme for blood pressure consistency

    // Screen title - properly positioned to avoid arc overlap
    lv_obj_t *label_title = lv_label_create(scr_bpt_cal_required);
    lv_label_set_text(label_title, "Calibration Required");
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_add_style(label_title, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);

    // Warning icon (centered above message) - use label for LVGL symbol
    lv_obj_t *label_warning = lv_label_create(scr_bpt_cal_required);
    lv_label_set_text(label_warning, LV_SYMBOL_WARNING);
    lv_obj_align(label_warning, LV_ALIGN_CENTER, 0, -40);
    lv_obj_set_style_text_color(label_warning, lv_color_hex(0x4A90E2), LV_PART_MAIN);  // Blue accent
    lv_obj_set_style_text_font(label_warning, &lv_font_montserrat_24, LV_PART_MAIN);

    // Main message (centered)
    label_bpt_cal_required = lv_label_create(scr_bpt_cal_required);
    lv_label_set_long_mode(label_bpt_cal_required, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_bpt_cal_required, 300);
    lv_label_set_text(label_bpt_cal_required, "Calibration is required before taking BP measurements. Use the HealthyPi Mobile App to complete calibration.");
    lv_obj_align(label_bpt_cal_required, LV_ALIGN_CENTER, 0, 20);
    lv_obj_add_style(label_bpt_cal_required, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_bpt_cal_required, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_bpt_cal_required, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);

    // BOTTOM ZONE: Action Button (consistent with other screens)
    btn_ok = hpi_btn_create_primary(scr_bpt_cal_required);
    //lv_obj_add_event_cb(btn_ok, scr_btn_ok_handler, LV_EVENT_ALL, NULL);
    lv_obj_set_size(btn_ok, 180, 50);  // Standard size matching other screens
    lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_MID, 0, -30);  // Standard bottom positioning
    lv_obj_set_style_radius(btn_ok, 25, LV_PART_MAIN);

    lv_obj_t *label_btn = lv_label_create(btn_ok);
    lv_label_set_text(label_btn, LV_SYMBOL_OK " OK");
    lv_obj_center(label_btn);
    // Note: Do not apply style_body_medium - LVGL symbols require default LVGL font
     lv_obj_add_event_cb(btn_ok, scr_btn_ok_handler, LV_EVENT_CLICKED, NULL);

    hpi_disp_set_curr_screen(SCR_SPL_BPT_CAL_REQUIRED);
    hpi_show_screen(scr_bpt_cal_required, m_scroll_dir);
}

void gesture_down_scr_bpt_cal_required(void)
{
    hpi_load_screen(SCR_BPT, SCROLL_DOWN);
}