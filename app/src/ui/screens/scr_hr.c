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
#include "hpi_sys.h"

LOG_MODULE_REGISTER(hpi_disp_scr_hr, LOG_LEVEL_DBG);

#define HR_SCR_TREND_MAX_POINTS 24

static lv_obj_t *scr_hr;

// GUI Labels
static lv_obj_t *label_hr_bpm;
static lv_obj_t *label_hr_last_update_time;

// Externs
extern lv_style_t style_scr_container;
extern lv_style_t style_red_medium;
extern lv_style_t style_white_large;
extern lv_style_t style_white_medium;
extern lv_style_t style_white_small;
extern lv_style_t style_tiny;

extern lv_style_t style_scr_black;
extern lv_style_t style_bg_red;
extern lv_style_t style_bg_blue;
extern lv_style_t style_bg_green;

void draw_scr_hr(enum scroll_dir m_scroll_dir)
{
    scr_hr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr_hr, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    lv_obj_t *cont_col = lv_obj_create(scr_hr);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_col, -1, LV_PART_SCROLLBAR);
    lv_obj_add_style(cont_col, &style_scr_black, 0);

    lv_obj_t *label_signal = lv_label_create(cont_col);
    lv_label_set_text(label_signal, "Heart Rate");
    lv_obj_add_style(label_signal, &style_white_small, 0);

    lv_obj_t *cont_hr = lv_obj_create(cont_col);
    lv_obj_set_size(cont_hr, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_hr, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(cont_hr, &style_scr_black, 0);
    lv_obj_set_flex_align(cont_hr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(cont_hr, 0, 0);
    lv_obj_add_style(cont_hr, &style_scr_black, 0);
    lv_obj_clear_flag(cont_hr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *img_hr = lv_img_create(cont_hr);
    lv_img_set_src(img_hr, &img_heart_70);

    uint16_t hr = 0;
    int64_t last_update_ts = 0;

    if(hpi_sys_get_last_hr_update(&hr, &last_update_ts) != 0)
    {
        hr = 0;
        last_update_ts = 0;
    }

    label_hr_bpm = lv_label_create(cont_hr);
    lv_obj_add_style(label_hr_bpm, &style_white_large, 0);

    if (hr == 0)
    {
        lv_label_set_text(label_hr_bpm, "--");
    }
    else
    {
        lv_label_set_text_fmt(label_hr_bpm, "%d", hr);
    }

    // HR Sub bpm label
    lv_obj_t *label_hr_sub = lv_label_create(cont_hr);
    lv_label_set_text(label_hr_sub, " bpm");
    lv_obj_add_style(label_hr_sub, &style_white_medium, 0);

    label_hr_last_update_time = lv_label_create(cont_col);
   
    char last_meas_str[25];
    hpi_helper_get_relative_time_str(last_update_ts, last_meas_str, sizeof(last_meas_str));
    lv_label_set_text(label_hr_last_update_time, last_meas_str);
    lv_obj_set_style_text_align(label_hr_last_update_time, LV_TEXT_ALIGN_CENTER, 0);  

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

    char last_meas_str[25];
    hpi_helper_get_relative_time_str(last_update_ts, last_meas_str, sizeof(last_meas_str));
    lv_label_set_text(label_hr_last_update_time, last_meas_str);
}
