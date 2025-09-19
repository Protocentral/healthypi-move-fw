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
    // AMOLED OPTIMIZATION: Pure black background for power efficiency
    lv_obj_set_style_bg_color(scr_ecg, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_ecg, LV_OBJ_FLAG_SCROLLABLE);

    // CIRCULAR AMOLED-OPTIMIZED ECG SCREEN
    // Display center: (195, 195), Usable radius: ~185px

    // Get ECG heart rate data first
    uint8_t m_ecg_hr = 0;
    int64_t m_ecg_hr_last_update = 0;
    if (hpi_sys_get_last_ecg_update(&m_ecg_hr, &m_ecg_hr_last_update) != 0)
    {
        LOG_ERR("Error getting last ECG update");
        m_ecg_hr = 0;
        m_ecg_hr_last_update = 0;
    }

    // OUTER RING: ECG Progress Arc (Radius 170-185px) - Green theme for electrical activity
    lv_obj_t *arc_ecg_zone = lv_arc_create(scr_ecg);
    lv_obj_set_size(arc_ecg_zone, 370, 370);  // 185px radius
    lv_obj_center(arc_ecg_zone);
    lv_arc_set_range(arc_ecg_zone, 0, 100);  // ECG readiness indicator 0-100%
    
    // Background arc: Full 270° track (gray)
    lv_arc_set_bg_angles(arc_ecg_zone, 135, 45);  // Full background arc
    
    // Indicator arc: Shows ECG system status/readiness
    lv_arc_set_angles(arc_ecg_zone, 135, 135);  // Start at beginning
    
    // Set arc value based on ECG status
    if (m_ecg_hr > 0) {
        lv_arc_set_value(arc_ecg_zone, 100);  // Full arc when ECG data available
    } else {
        lv_arc_set_value(arc_ecg_zone, 25);   // Partial arc when ready for measurement
    }
    
    // Style the progress arc - green theme for electrical activity
    lv_obj_set_style_arc_color(arc_ecg_zone, lv_color_hex(0x333333), LV_PART_MAIN);    // Background track
    lv_obj_set_style_arc_width(arc_ecg_zone, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_ecg_zone, lv_color_hex(COLOR_SUCCESS_GREEN), LV_PART_INDICATOR);  // Progress indicator
    lv_obj_set_style_arc_width(arc_ecg_zone, 6, LV_PART_INDICATOR);
    lv_obj_remove_style(arc_ecg_zone, NULL, LV_PART_KNOB);  // Remove knob
    lv_obj_clear_flag(arc_ecg_zone, LV_OBJ_FLAG_CLICKABLE);

    // Screen title - properly positioned to avoid arc overlap
    lv_obj_t *label_title = lv_label_create(scr_ecg);
    lv_label_set_text(label_title, "ECG");
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 40);  // Centered at top, clear of arc
    lv_obj_add_style(label_title, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);

    // ECG icon (centered, 45x45 icon for better circular display proportions)
    lv_obj_t *img_ecg = lv_img_create(scr_ecg);
    lv_img_set_src(img_ecg, &ecg_45);
    lv_obj_set_style_img_recolor(img_ecg, lv_color_hex(COLOR_SUCCESS_GREEN), 0);
    lv_obj_set_style_img_recolor_opa(img_ecg, LV_OPA_COVER, 0);
    lv_obj_align(img_ecg, LV_ALIGN_CENTER, 0, -45);  // Positioned above center for arc design

    // CENTRAL ZONE: Main ECG Value/Status (properly spaced from icon)
    label_ecg_hr = lv_label_create(scr_ecg);
    if (m_ecg_hr == 0) {
        lv_label_set_text(label_ecg_hr, "--");
    } else {
        lv_label_set_text_fmt(label_ecg_hr, "%d", m_ecg_hr);
    }
    lv_obj_align(label_ecg_hr, LV_ALIGN_CENTER, 0, -10);  // Centered, slightly above middle
    lv_obj_set_style_text_color(label_ecg_hr, lv_color_white(), LV_PART_MAIN);
    lv_obj_add_style(label_ecg_hr, &style_numeric_large, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_ecg_hr, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // Unit/Status label directly below main value
    lv_obj_t *label_ecg_unit = lv_label_create(scr_ecg);
    if (m_ecg_hr == 0) {
        lv_label_set_text(label_ecg_unit, "Place fingers on electrodes");
    } else {
        lv_label_set_text(label_ecg_unit, "BPM");
    }
    lv_obj_align(label_ecg_unit, LV_ALIGN_CENTER, 0, 35);  // Below main value with gap
    lv_obj_set_style_text_color(label_ecg_unit, lv_color_hex(COLOR_SUCCESS_GREEN), LV_PART_MAIN);
    lv_obj_add_style(label_ecg_unit, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_ecg_unit, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // Status info - centered below unit with proper spacing
    lv_obj_t *label_ecg_status = lv_label_create(scr_ecg);
    if (m_ecg_hr == 0) {
        lv_label_set_text(label_ecg_status, "Electrocardiogram");
    } else {
        char last_meas_str[25];
        hpi_helper_get_relative_time_str(m_ecg_hr_last_update, last_meas_str, sizeof(last_meas_str));
        lv_label_set_text(label_ecg_status, last_meas_str);
    }
    lv_obj_align(label_ecg_status, LV_ALIGN_CENTER, 0, 80);  // Centered, below unit with gap
    lv_obj_set_style_text_color(label_ecg_status, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_add_style(label_ecg_status, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_ecg_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // BOTTOM ZONE: Action Button (properly centered at bottom)
    btn_ecg_measure = hpi_btn_create_primary(scr_ecg);
    lv_obj_set_size(btn_ecg_measure, 180, 50);  // Width for "Start ECG" text
    lv_obj_align(btn_ecg_measure, LV_ALIGN_BOTTOM_MID, 0, -30);  // Centered at bottom with margin
    lv_obj_set_style_radius(btn_ecg_measure, 25, LV_PART_MAIN);
    
    // AMOLED-optimized button styling - green theme
    lv_obj_set_style_bg_color(btn_ecg_measure, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_ecg_measure, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_ecg_measure, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_ecg_measure, lv_color_hex(COLOR_SUCCESS_GREEN), LV_PART_MAIN);
    lv_obj_set_style_border_opa(btn_ecg_measure, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn_ecg_measure, 0, LV_PART_MAIN);  // No shadow for AMOLED
    
    lv_obj_t *label_btn_ecg_measure = lv_label_create(btn_ecg_measure);
    lv_label_set_text(label_btn_ecg_measure, LV_SYMBOL_PLAY " Start ECG");
    lv_obj_center(label_btn_ecg_measure);
    lv_obj_set_style_text_color(label_btn_ecg_measure, lv_color_hex(COLOR_SUCCESS_GREEN), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_ecg_measure, scr_ecg_start_btn_event_handler, LV_EVENT_CLICKED, NULL);

    hpi_disp_set_curr_screen(SCR_ECG);
    hpi_show_screen(scr_ecg, m_scroll_dir);
}