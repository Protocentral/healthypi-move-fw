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
static lv_obj_t *btn_spo2_finger;

// Externs - Modern style system
extern lv_style_t style_body_medium;
extern lv_style_t style_numeric_large;
extern lv_style_t style_caption;

/* Extern semaphore to trigger wrist SpO2 one-shot measurement */
extern struct k_sem sem_start_one_shot_spo2;

/* Handler for primary "Measure" button - directly starts wrist SpO2 measurement */
static void scr_spo2_btn_measure_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        /* Smart default: Directly start wrist measurement */
        LOG_DBG("Starting wrist SpO2 measurement");
        k_sem_give(&sem_start_one_shot_spo2);
    }
}

/* Handler for secondary "Finger" button - goes to finger instruction screen */
static void scr_spo2_btn_finger_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        /* Show finger sensor instruction screen before starting */
        LOG_DBG("Navigating to finger SpO2 instruction screen");
        hpi_load_scr_spl(SCR_SPL_SPO2_SCR2, SCROLL_UP, (uint8_t)SCR_SPO2, SPO2_SOURCE_PPG_FI, 0, 0);
    }
}

void draw_scr_spo2(enum scroll_dir m_scroll_dir)
{
    scr_spo2 = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_spo2, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_spo2, LV_OBJ_FLAG_SCROLLABLE);

    /*
     * COMPACT LAYOUT FOR 390x390 ROUND AMOLED
     * ========================================
     * Vertical space budget (center at y=195):
     *   Top safe zone: y=30 to y=60 (title area)
     *   Upper zone: y=60 to y=110 (icon)
     *   Center zone: y=110 to y=200 (value with inline unit)
     *   Lower zone: y=200 to y=250 (last update)
     *   Bottom zone: y=250 to y=310 (side-by-side buttons)
     *   Bottom safe: y=320+ (curved edge)
     *
     * Inline unit (e.g., "97%") saves vertical space vs stacked layout.
     */

    // Get SpO2 data
    uint8_t spo2 = 0;
    int64_t last_update_ts = 0;
    if (hpi_sys_get_last_spo2_update(&spo2, &last_update_ts) != 0)
    {
        spo2 = 0;
        last_update_ts = 0;
    }

    // TOP: Title "SpO2" at y=40
    lv_obj_t *label_title = lv_label_create(scr_spo2);
    lv_label_set_text(label_title, "SpO2");
    lv_obj_set_pos(label_title, 0, 40);
    lv_obj_set_width(label_title, 390);
    lv_obj_add_style(label_title, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);

    // UPPER: Icon centered at y=75
    lv_obj_t *img_spo2 = lv_img_create(scr_spo2);
    lv_img_set_src(img_spo2, &icon_spo2_30x35);
    lv_obj_set_pos(img_spo2, (390 - 30) / 2, 75);
    lv_obj_set_style_img_recolor(img_spo2, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(img_spo2, LV_OPA_COVER, LV_PART_MAIN);

    // CENTER: Large SpO2 value with inline "%" unit at y=125
    // Use a container to center "97%" as a single unit
    lv_obj_t *cont_value = lv_obj_create(scr_spo2);
    lv_obj_remove_style_all(cont_value);
    lv_obj_set_size(cont_value, 390, 70);
    lv_obj_set_pos(cont_value, 0, 125);
    lv_obj_set_style_bg_opa(cont_value, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont_value, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_value, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);

    label_spo2_percent = lv_label_create(cont_value);
    if (spo2 == 0) {
        lv_label_set_text(label_spo2_percent, "--");
    } else {
        lv_label_set_text_fmt(label_spo2_percent, "%d", spo2);
    }
    lv_obj_set_style_text_color(label_spo2_percent, lv_color_white(), LV_PART_MAIN);
    lv_obj_add_style(label_spo2_percent, &style_numeric_large, LV_PART_MAIN);

    // Inline "%" unit (smaller, colored, baseline-aligned)
    lv_obj_t *label_spo2_unit = lv_label_create(cont_value);
    lv_label_set_text(label_spo2_unit, "%");
    lv_obj_set_style_text_color(label_spo2_unit, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN);
    lv_obj_add_style(label_spo2_unit, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(label_spo2_unit, 8, LV_PART_MAIN);  // Align with number baseline

    // LOWER: Last measurement time at y=205
    label_spo2_last_update_time = lv_label_create(scr_spo2);
    char last_meas_str[25];
    hpi_helper_get_relative_time_str(last_update_ts, last_meas_str, sizeof(last_meas_str));
    lv_label_set_text(label_spo2_last_update_time, last_meas_str);
    lv_obj_set_pos(label_spo2_last_update_time, 0, 205);
    lv_obj_set_width(label_spo2_last_update_time, 390);
    lv_obj_set_style_text_color(label_spo2_last_update_time, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_add_style(label_spo2_last_update_time, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_spo2_last_update_time, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    /*
     * BOTTOM: Side-by-side buttons at y=250
     * For 390px wide round display, use two buttons with 8px gap
     * Button width: 120px each, gap: 8px, total: 248px
     */
    const int btn_width = 120;
    const int btn_height = 44;
    const int btn_gap = 8;
    const int total_width = btn_width * 2 + btn_gap;  // 248
    const int btn_y = 250;
    const int left_x = (390 - total_width) / 2;  // 71
    const int right_x = left_x + btn_width + btn_gap;  // 199

    // Left: Primary "Wrist" button (blue filled)
    btn_spo2_measure = hpi_btn_create_primary(scr_spo2);
    lv_obj_set_size(btn_spo2_measure, btn_width, btn_height);
    lv_obj_set_pos(btn_spo2_measure, left_x, btn_y);
    lv_obj_set_style_radius(btn_spo2_measure, 22, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn_spo2_measure, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_spo2_measure, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_spo2_measure, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn_spo2_measure, 0, LV_PART_MAIN);

    lv_obj_t *label_measure = lv_label_create(btn_spo2_measure);
    lv_label_set_text(label_measure, "Wrist");
    lv_obj_center(label_measure);
    lv_obj_set_style_text_color(label_measure, lv_color_white(), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_spo2_measure, scr_spo2_btn_measure_handler, LV_EVENT_CLICKED, NULL);

    // Right: Secondary "Finger" button (outlined)
    btn_spo2_finger = hpi_btn_create_secondary(scr_spo2);
    lv_obj_set_size(btn_spo2_finger, btn_width, btn_height);
    lv_obj_set_pos(btn_spo2_finger, right_x, btn_y);
    lv_obj_set_style_radius(btn_spo2_finger, 22, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn_spo2_finger, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_spo2_finger, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_spo2_finger, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_spo2_finger, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn_spo2_finger, 0, LV_PART_MAIN);

    lv_obj_t *label_finger = lv_label_create(btn_spo2_finger);
    lv_label_set_text(label_finger, "Finger");
    lv_obj_center(label_finger);
    lv_obj_set_style_text_color(label_finger, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_spo2_finger, scr_spo2_btn_finger_handler, LV_EVENT_CLICKED, NULL);

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