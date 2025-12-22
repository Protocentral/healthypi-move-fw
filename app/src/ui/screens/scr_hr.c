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
    lv_obj_set_style_bg_color(scr_hr, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_hr, LV_OBJ_FLAG_SCROLLABLE);

    /*
     * COMPACT LAYOUT FOR 390x390 ROUND AMOLED
     * ========================================
     * Inline unit layout (e.g., "72 BPM") saves vertical space:
     *   Top safe zone: y=30 to y=60 (title area)
     *   Upper zone: y=60 to y=110 (icon)
     *   Center zone: y=110 to y=200 (value with inline unit)
     *   Lower zone: y=200 to y=250 (last update)
     *   Bottom zone: y=250 to y=310 (button)
     *   Bottom safe: y=320+ (curved edge)
     */

    // Get heart rate data
    uint16_t hr = 0;
    int64_t last_update_ts = 0;
    if (hpi_sys_get_last_hr_update(&hr, &last_update_ts) != 0)
    {
        hr = 0;
        last_update_ts = 0;
    }

    // TOP: Title "Heart Rate" at y=40
    lv_obj_t *label_title = lv_label_create(scr_hr);
    lv_label_set_text(label_title, "Heart Rate");
    lv_obj_set_pos(label_title, 0, 40);
    lv_obj_set_width(label_title, 390);
    lv_obj_add_style(label_title, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);

    // UPPER: Heart icon centered at y=75
    lv_obj_t *img_hr = lv_img_create(scr_hr);
    lv_img_set_src(img_hr, &img_heart_48px);
    lv_obj_set_pos(img_hr, (390 - 48) / 2, 75);
    lv_obj_set_style_img_recolor(img_hr, lv_color_hex(COLOR_CRITICAL_RED), LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(img_hr, LV_OPA_COVER, LV_PART_MAIN);

    // CENTER: Large HR value with inline "BPM" unit at y=130
    // Use a container to center "72 BPM" as a single unit
    lv_obj_t *cont_value = lv_obj_create(scr_hr);
    lv_obj_remove_style_all(cont_value);
    lv_obj_set_size(cont_value, 390, 70);
    lv_obj_set_pos(cont_value, 0, 130);
    lv_obj_set_style_bg_opa(cont_value, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont_value, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_value, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);

    label_hr_bpm = lv_label_create(cont_value);
    if (hr == 0) {
        lv_label_set_text(label_hr_bpm, "--");
    } else {
        lv_label_set_text_fmt(label_hr_bpm, "%d", hr);
    }
    lv_obj_set_style_text_color(label_hr_bpm, lv_color_white(), LV_PART_MAIN);
    lv_obj_add_style(label_hr_bpm, &style_numeric_large, LV_PART_MAIN);

    // Inline "BPM" unit (smaller, colored, baseline-aligned)
    lv_obj_t *label_hr_unit = lv_label_create(cont_value);
    lv_label_set_text(label_hr_unit, " BPM");
    lv_obj_set_style_text_color(label_hr_unit, lv_color_hex(COLOR_CRITICAL_RED), LV_PART_MAIN);
    lv_obj_add_style(label_hr_unit, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(label_hr_unit, 8, LV_PART_MAIN);  // Align with number baseline

    // LOWER: Last measurement time at y=210
    label_hr_last_update_time = lv_label_create(scr_hr);
    char last_meas_str[25];
    hpi_helper_get_relative_time_str(last_update_ts, last_meas_str, sizeof(last_meas_str));
    lv_label_set_text(label_hr_last_update_time, last_meas_str);
    lv_obj_set_pos(label_hr_last_update_time, 0, 210);
    lv_obj_set_width(label_hr_last_update_time, 390);
    lv_obj_set_style_text_color(label_hr_last_update_time, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_add_style(label_hr_last_update_time, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_hr_last_update_time, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // BOTTOM: Single centered "Raw PPG" button at y=250
    const int btn_width = 200;
    const int btn_height = 60;
    const int btn_y = 250;

    lv_obj_t *btn_raw_ppg = hpi_btn_create_primary(scr_hr);
    lv_obj_set_size(btn_raw_ppg, btn_width, btn_height);
    lv_obj_set_pos(btn_raw_ppg, (390 - btn_width) / 2, btn_y);
    lv_obj_set_style_radius(btn_raw_ppg, 30, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn_raw_ppg, lv_color_hex(COLOR_BTN_RED), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_raw_ppg, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_raw_ppg, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn_raw_ppg, 0, LV_PART_MAIN);

    lv_obj_t *label_btn_raw = lv_label_create(btn_raw_ppg);
    lv_label_set_text(label_btn_raw, LV_SYMBOL_PLAY " Raw PPG");
    lv_obj_center(label_btn_raw);
    //lv_obj_set_style_text_font(label_btn_raw, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_btn_raw, lv_color_white(), LV_PART_MAIN);
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
