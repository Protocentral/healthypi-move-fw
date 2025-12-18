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
#include "hw_module.h"
#include "hpi_sys.h"

LOG_MODULE_REGISTER(hpi_disp_scr_ecg, LOG_LEVEL_DBG);

lv_obj_t *scr_ecg;

static lv_obj_t *btn_ecg_measure;
static lv_obj_t *label_ecg_hr;

// Externs - Modern style system
extern lv_style_t style_body_medium;
extern lv_style_t style_numeric_large;
extern lv_style_t style_caption;

extern struct k_sem sem_ecg_start;

static void scr_ecg_start_btn_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        hpi_load_scr_spl(SCR_SPL_ECG_SCR2, SCROLL_UP, (uint8_t)SCR_ECG, 0, 0, 0);
        k_msleep(500);
        k_sem_give(&sem_ecg_start);
    }
}

void draw_scr_ecg(enum scroll_dir m_scroll_dir)
{
    scr_ecg = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_ecg, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_ecg, LV_OBJ_FLAG_SCROLLABLE);

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

    // Get ECG heart rate data
    uint8_t m_ecg_hr = 0;
    int64_t m_ecg_hr_last_update = 0;
    if (hpi_sys_get_last_ecg_update(&m_ecg_hr, &m_ecg_hr_last_update) != 0)
    {
        LOG_ERR("Error getting last ECG update");
        m_ecg_hr = 0;
        m_ecg_hr_last_update = 0;
    }

    // TOP: Title "ECG" at y=40
    lv_obj_t *label_title = lv_label_create(scr_ecg);
    lv_label_set_text(label_title, "ECG");
    lv_obj_set_pos(label_title, 0, 40);
    lv_obj_set_width(label_title, 390);
    lv_obj_add_style(label_title, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);

    // UPPER: ECG icon centered at y=75 (45x45 icon)
    lv_obj_t *img_ecg = lv_img_create(scr_ecg);
    lv_img_set_src(img_ecg, &ecg_45);
    lv_obj_set_pos(img_ecg, (390 - 45) / 2, 75);
    lv_obj_set_style_img_recolor(img_ecg, lv_color_hex(COLOR_SUCCESS_GREEN), LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(img_ecg, LV_OPA_COVER, LV_PART_MAIN);

    // CENTER: Large ECG HR value with inline "BPM" unit at y=130
    // Use a container to center "72 BPM" as a single unit
    lv_obj_t *cont_value = lv_obj_create(scr_ecg);
    lv_obj_remove_style_all(cont_value);
    lv_obj_set_size(cont_value, 390, 70);
    lv_obj_set_pos(cont_value, 0, 130);
    lv_obj_set_style_bg_opa(cont_value, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont_value, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_value, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);

    label_ecg_hr = lv_label_create(cont_value);
    if (m_ecg_hr == 0) {
        lv_label_set_text(label_ecg_hr, "--");
    } else {
        lv_label_set_text_fmt(label_ecg_hr, "%d", m_ecg_hr);
    }
    lv_obj_set_style_text_color(label_ecg_hr, lv_color_white(), LV_PART_MAIN);
    lv_obj_add_style(label_ecg_hr, &style_numeric_large, LV_PART_MAIN);

    // Inline "BPM" unit (smaller, colored, baseline-aligned)
    lv_obj_t *label_ecg_unit = lv_label_create(cont_value);
    if (m_ecg_hr == 0) {
        lv_label_set_text(label_ecg_unit, "");
    } else {
        lv_label_set_text(label_ecg_unit, " BPM");
    }
    lv_obj_set_style_text_color(label_ecg_unit, lv_color_hex(COLOR_SUCCESS_GREEN), LV_PART_MAIN);
    lv_obj_add_style(label_ecg_unit, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(label_ecg_unit, 8, LV_PART_MAIN);  // Align with number baseline

    // LOWER: Status/last measurement time at y=210
    lv_obj_t *label_ecg_status = lv_label_create(scr_ecg);
    if (m_ecg_hr == 0) {
        lv_label_set_text(label_ecg_status, "Electrocardiogram");
    } else {
        char last_meas_str[25];
        hpi_helper_get_relative_time_str(m_ecg_hr_last_update, last_meas_str, sizeof(last_meas_str));
        lv_label_set_text(label_ecg_status, last_meas_str);
    }
    lv_obj_set_pos(label_ecg_status, 0, 210);
    lv_obj_set_width(label_ecg_status, 390);
    lv_obj_set_style_text_color(label_ecg_status, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_add_style(label_ecg_status, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_ecg_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // BOTTOM: Single centered "Start ECG" button at y=255
    const int btn_width = 160;
    const int btn_height = 44;
    const int btn_y = 255;

    btn_ecg_measure = hpi_btn_create_primary(scr_ecg);
    lv_obj_set_size(btn_ecg_measure, btn_width, btn_height);
    lv_obj_set_pos(btn_ecg_measure, (390 - btn_width) / 2, btn_y);
    lv_obj_set_style_radius(btn_ecg_measure, 22, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn_ecg_measure, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_ecg_measure, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_ecg_measure, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_ecg_measure, lv_color_hex(COLOR_SUCCESS_GREEN), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn_ecg_measure, 0, LV_PART_MAIN);

    lv_obj_t *label_btn_ecg_measure = lv_label_create(btn_ecg_measure);
    lv_label_set_text(label_btn_ecg_measure, LV_SYMBOL_PLAY " Start ECG");
    lv_obj_center(label_btn_ecg_measure);
    lv_obj_set_style_text_color(label_btn_ecg_measure, lv_color_hex(COLOR_SUCCESS_GREEN), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_ecg_measure, scr_ecg_start_btn_event_handler, LV_EVENT_CLICKED, NULL);

    hpi_disp_set_curr_screen(SCR_ECG);
    hpi_show_screen(scr_ecg, m_scroll_dir);
}