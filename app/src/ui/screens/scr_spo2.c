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

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_large_numeric;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_tiny;

extern lv_style_t style_bg_blue;
extern lv_style_t style_bg_red;
extern lv_style_t style_bg_green;

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
    lv_obj_add_style(scr_spo2, &style_scr_black, 0);
    lv_obj_clear_flag(scr_spo2, LV_OBJ_FLAG_SCROLLABLE); 

    lv_obj_t *cont_col = lv_obj_create(scr_spo2);
    lv_obj_set_size(cont_col, 390, 390);
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_col, -1, LV_PART_SCROLLBAR);
    lv_obj_add_style(cont_col, &style_scr_black, 0);
    lv_obj_add_style(cont_col, &style_bg_green, 0);

    lv_obj_t *label_signal = lv_label_create(cont_col);
    lv_label_set_text(label_signal, "SpO2");

    lv_obj_t *cont_spo2 = lv_obj_create(cont_col);
    lv_obj_set_size(cont_spo2, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_spo2, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(cont_spo2, &style_scr_black, 0);
    lv_obj_set_flex_align(cont_spo2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_bottom(cont_spo2, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(cont_spo2, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cont_spo2, 0, 0);

    lv_obj_t *img_spo2 = lv_img_create(cont_spo2);
    lv_img_set_src(img_spo2, &icon_spo2_100);

    uint8_t m_spo2_val = 0;
    int64_t m_spo2_time = 0;

    // Get the last SpO2 value and time
    if (hpi_sys_get_last_spo2_update(&m_spo2_val, &m_spo2_time) != 0)
    {
        LOG_ERR("Failed to get last SpO2 value");
        m_spo2_val = 0;
        m_spo2_time = 0;
    }

    label_spo2_percent = lv_label_create(cont_spo2);
    if (m_spo2_val == 0)
    {
        lv_label_set_text(label_spo2_percent, "--");
    }
    else
    {
        lv_label_set_text_fmt(label_spo2_percent, "%d", m_spo2_val);
    }
    lv_obj_add_style(label_spo2_percent, &style_white_large_numeric, 0);

    lv_obj_t *label_spo2_percent_sign = lv_label_create(cont_spo2);
    lv_label_set_text(label_spo2_percent_sign, " %");

    char last_meas_str[25];
    //hpi_helper_get_date_time_str(m_spo2_time, last_meas_str);
    hpi_helper_get_relative_time_str(m_spo2_time, last_meas_str, sizeof(last_meas_str));
    label_spo2_last_update_time = lv_label_create(cont_col);
    lv_label_set_text(label_spo2_last_update_time, last_meas_str);
    lv_obj_set_style_text_align(label_spo2_last_update_time, LV_TEXT_ALIGN_CENTER, 0);

    btn_spo2_measure = hpi_btn_create_primary(cont_col);
    lv_obj_set_height(btn_spo2_measure, 56); // Modern height for better proportions

    lv_obj_t *label_measure = lv_label_create(btn_spo2_measure);
    lv_label_set_text(label_measure, LV_SYMBOL_PLAY " Measure");
    lv_obj_center(label_measure);

    lv_obj_add_event_cb(btn_spo2_measure, scr_spo2_btn_measure_handler, LV_EVENT_ALL, NULL);

    hpi_disp_set_curr_screen(SCR_SPO2);
    hpi_show_screen(scr_spo2, m_scroll_dir);
}