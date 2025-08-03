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
#include "sm/smf_ecg_bioz.h"
#include "trends.h"
#include "hpi_sys.h"

LOG_MODULE_REGISTER(hpi_disp_scr_gsr, LOG_LEVEL_DBG);

#define GSR_SCR_TREND_MAX_POINTS 24

static lv_obj_t *scr_gsr;

// GUI Labels
static lv_obj_t *label_gsr_value;
static lv_obj_t *label_stress_level;
static lv_obj_t *label_gsr_quality;
static lv_obj_t *label_gsr_last_update_time;
static lv_obj_t *btn_gsr_measure;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_large;
extern lv_style_t style_white_medium;
extern lv_style_t style_white_small;
extern lv_style_t style_scr_black;
extern lv_style_t style_tiny;
extern lv_style_t style_bg_blue;
extern lv_style_t style_bg_red;
extern lv_style_t style_bg_green;
extern lv_style_t style_scr_container;

// Stress level event handler
static void scr_gsr_start_btn_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        hpi_load_scr_spl(SCR_SPL_GSR_MEASURE, SCROLL_UP, (uint8_t)SCR_GSR, 0, 0, 0);
    }
}

static void scr_gsr_trends_btn_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        hpi_load_scr_spl(SCR_SPL_GSR_TRENDS, SCROLL_UP, (uint8_t)SCR_GSR, 0, 0, 0);
    }
}

void draw_scr_gsr(enum scroll_dir m_scroll_dir)
{
    scr_gsr = lv_obj_create(NULL);
    lv_obj_add_style(scr_gsr, &style_scr_black, 0);
    lv_obj_clear_flag(scr_gsr, LV_OBJ_FLAG_SCROLLABLE);

        // Main container - circular layout for round display
    lv_obj_t *cont_gsr_main = lv_obj_create(scr_gsr);
    lv_obj_set_size(cont_gsr_main, 360, 360);  // Perfect circle within 390x390
    lv_obj_center(cont_gsr_main);
    lv_obj_set_style_radius(cont_gsr_main, 180, 0);  // 180px radius for perfect circle
    lv_obj_set_style_bg_opa(cont_gsr_main, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_gsr_main, 0, LV_PART_MAIN);  // Hide border
    lv_obj_clear_flag(cont_gsr_main, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(cont_gsr_main, 20, 0);

    // Title at top
    lv_obj_t *label_title = lv_label_create(cont_gsr_main);
    lv_label_set_text(label_title, "GSR Monitor");
    lv_obj_add_style(label_title, &style_white_medium, 0);
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 10);

    // Central GSR value display - larger and more prominent
    lv_obj_t *cont_gsr_center = lv_obj_create(cont_gsr_main);
    lv_obj_set_size(cont_gsr_center, 200, 120);
    lv_obj_center(cont_gsr_center);
    lv_obj_set_style_bg_opa(cont_gsr_center, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_gsr_center, 0, LV_PART_MAIN);  // Hide border
    lv_obj_clear_flag(cont_gsr_center, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cont_gsr_center, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_gsr_center, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // GSR Icon
    lv_obj_t *label_gsr_icon = lv_label_create(cont_gsr_center);
    lv_label_set_text(label_gsr_icon, LV_SYMBOL_CHARGE);
    lv_obj_add_style(label_gsr_icon, &style_white_large, 0);
    lv_obj_set_style_text_font(label_gsr_icon, &lv_font_montserrat_24, 0);

        // GSR Value container - horizontal layout for value and unit
    lv_obj_t *cont_value = lv_obj_create(cont_gsr_main);
    lv_obj_set_size(cont_value, 280, 80);
    lv_obj_align(cont_value, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_bg_opa(cont_value, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_value, 0, LV_PART_MAIN);  // Hide border
    lv_obj_clear_flag(cont_value, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cont_value, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_value, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    float m_gsr_value = 0.0;
    int64_t m_gsr_last_update = 0;

    // Get current GSR value from state machine
    if (hpi_gsr_is_active()) {
        m_gsr_value = hpi_gsr_get_current_value();
        m_gsr_last_update = k_ticks_to_ns_floor64(k_uptime_ticks());
    } else {
        m_gsr_value = 0.0;
        m_gsr_last_update = 0;
    }

    label_gsr_value = lv_label_create(cont_value);
    if (m_gsr_value == 0.0f)
    {
        lv_label_set_text(label_gsr_value, "---");
    }
    else
    {
        lv_label_set_text_fmt(label_gsr_value, "%.1f", (double)m_gsr_value);
    }
    lv_obj_add_style(label_gsr_value, &style_white_large, 0);
    lv_obj_set_style_text_font(label_gsr_value, &lv_font_montserrat_24, 0);

    lv_obj_t *label_gsr_unit = lv_label_create(cont_value);
    lv_label_set_text(label_gsr_unit, " µS");
    lv_obj_add_style(label_gsr_unit, &style_white_medium, 0);
    lv_obj_set_style_text_font(label_gsr_unit, &lv_font_montserrat_24, 0);

        // Status indicators - compact horizontal layout at bottom
    lv_obj_t *cont_status = lv_obj_create(cont_gsr_main);
    lv_obj_set_size(cont_status, 320, 60);
    lv_obj_align(cont_status, LV_ALIGN_BOTTOM_MID, 0, -80);
    lv_obj_set_style_bg_opa(cont_status, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_status, 0, LV_PART_MAIN);  // Hide border
    lv_obj_clear_flag(cont_status, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cont_status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_status, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Stress level
    lv_obj_t *cont_stress = lv_obj_create(cont_status);
    lv_obj_set_size(cont_stress, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(cont_stress, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_stress, 0, LV_PART_MAIN);  // Hide border
    lv_obj_clear_flag(cont_stress, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cont_stress, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_stress, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *label_stress_title = lv_label_create(cont_stress);
    lv_label_set_text(label_stress_title, "Stress: ");
    lv_obj_add_style(label_stress_title, &style_white_small, 0);

    label_stress_level = lv_label_create(cont_stress);
    
    // Get stress level from state machine
    uint8_t stress_level = hpi_gsr_get_stress_level();
    const char *stress_strings[] = {"Very Low", "Low", "Moderate", "High", "Very High"};
    if (stress_level < 5) {
        lv_label_set_text(label_stress_level, stress_strings[stress_level]);
    } else {
        lv_label_set_text(label_stress_level, "Unknown");
    }
    lv_obj_add_style(label_stress_level, &style_white_small, 0);

    // Quality indicator
    lv_obj_t *cont_quality = lv_obj_create(cont_status);
    lv_obj_set_size(cont_quality, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(cont_quality, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_quality, 0, LV_PART_MAIN);  // Hide border
    lv_obj_clear_flag(cont_quality, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cont_quality, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_quality, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *label_quality_title = lv_label_create(cont_quality);
    lv_label_set_text(label_quality_title, "Quality: ");
    lv_obj_add_style(label_quality_title, &style_white_small, 0);

    label_gsr_quality = lv_label_create(cont_quality);
    lv_label_set_text(label_gsr_quality, "Good " LV_SYMBOL_OK);
    lv_obj_add_style(label_gsr_quality, &style_white_small, 0);

    // Navigation buttons at bottom - arranged horizontally for round display
    lv_obj_t *cont_buttons = lv_obj_create(cont_gsr_main);
    lv_obj_set_size(cont_buttons, 280, 50);
    lv_obj_align(cont_buttons, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_opa(cont_buttons, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_buttons, 0, LV_PART_MAIN);  // Hide border
    lv_obj_clear_flag(cont_buttons, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cont_buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_buttons, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Measure button - smaller for round display
    btn_gsr_measure = lv_btn_create(cont_buttons);
    lv_obj_add_event_cb(btn_gsr_measure, scr_gsr_start_btn_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_set_size(btn_gsr_measure, 120, 40);

    lv_obj_t *label_btn_gsr_measure = lv_label_create(btn_gsr_measure);
    lv_label_set_text(label_btn_gsr_measure, LV_SYMBOL_PLAY " Measure");
    lv_obj_center(label_btn_gsr_measure);
    lv_obj_set_style_text_font(label_btn_gsr_measure, &lv_font_montserrat_20, 0);

    // Trends button - smaller for round display
    lv_obj_t *btn_gsr_trends = lv_btn_create(cont_buttons);
    lv_obj_add_event_cb(btn_gsr_trends, scr_gsr_trends_btn_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_set_size(btn_gsr_trends, 120, 40);

    lv_obj_t *label_btn_gsr_trends = lv_label_create(btn_gsr_trends);
    lv_label_set_text(label_btn_gsr_trends, LV_SYMBOL_LIST " Trends");
    lv_obj_center(label_btn_gsr_trends);
    lv_obj_set_style_text_font(label_btn_gsr_trends, &lv_font_montserrat_20, 0);

    hpi_disp_set_curr_screen(SCR_GSR);
    hpi_show_screen(scr_gsr, m_scroll_dir);
}

void hpi_disp_gsr_update_value(float gsr_value)
{
    if (label_gsr_value == NULL)
        return;

    if (gsr_value == 0.0f)
    {
        lv_label_set_text(label_gsr_value, "---");
    }
    else
    {
        lv_label_set_text_fmt(label_gsr_value, "%.1f", (double)gsr_value);
    }
}

void hpi_disp_gsr_update_stress_level(uint8_t stress_level)
{
    if (label_stress_level == NULL)
        return;

    const char* stress_labels[] = {"Very Low", "Low", "Moderate", "High", "Very High"};
    const char* stress_symbols[] = {LV_SYMBOL_OK, LV_SYMBOL_OK, LV_SYMBOL_WARNING, LV_SYMBOL_WARNING, LV_SYMBOL_CLOSE};
    
    if (stress_level > 4) stress_level = 4;
    
    char stress_text[32];
    snprintf(stress_text, sizeof(stress_text), "%s %s", stress_labels[stress_level], stress_symbols[stress_level]);
    lv_label_set_text(label_stress_level, stress_text);
    
    // Change color based on stress level
    if (stress_level <= 1) {
        lv_obj_add_style(label_stress_level, &style_bg_green, 0);
    } else if (stress_level <= 2) {
        lv_obj_add_style(label_stress_level, &style_white_medium, 0);
    } else {
        lv_obj_add_style(label_stress_level, &style_red_medium, 0);
    }
}

void hpi_disp_gsr_update_quality(uint8_t quality)
{
    if (label_gsr_quality == NULL)
        return;

    if (quality >= 80) {
        lv_label_set_text(label_gsr_quality, "Good " LV_SYMBOL_OK);
        lv_obj_add_style(label_gsr_quality, &style_bg_green, 0);
    } else if (quality >= 50) {
        lv_label_set_text(label_gsr_quality, "Fair " LV_SYMBOL_WARNING);
        lv_obj_add_style(label_gsr_quality, &style_white_medium, 0);
    } else {
        lv_label_set_text(label_gsr_quality, "Poor " LV_SYMBOL_CLOSE);
        lv_obj_add_style(label_gsr_quality, &style_red_medium, 0);
    }
}
