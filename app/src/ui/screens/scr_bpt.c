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
extern lv_style_t style_white_small;
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
    lv_obj_add_style(scr_bpt, &style_scr_black, 0);
    lv_obj_set_scroll_dir(scr_bpt, LV_DIR_VER);

    lv_obj_t *cont_col = lv_obj_create(scr_bpt);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_col, -1, LV_PART_SCROLLBAR);
    lv_obj_add_style(cont_col, &style_scr_black, 0);
    lv_obj_add_style(cont_col, &style_scr_container, 0);
    lv_obj_set_scroll_dir(cont_col, LV_DIR_VER);

    lv_obj_t *label_signal = lv_label_create(cont_col);
    lv_label_set_text(label_signal, "Blood Pressure");

    //lv_obj_t *img1 = lv_img_create(cont_col);
    //lv_img_set_src(img1, &bp_70);

    lv_obj_t *cont_row_bpt = lv_obj_create(cont_col);
    lv_obj_set_size(cont_row_bpt, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_row_bpt, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(cont_row_bpt, &style_scr_black, 0);
    lv_obj_set_flex_align(cont_row_bpt, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_bottom(cont_row_bpt, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(cont_row_bpt, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cont_row_bpt, 0, 0);
    lv_obj_clear_flag(cont_row_bpt, LV_OBJ_FLAG_SCROLLABLE);

    uint8_t bpt_sys;
    uint8_t bpt_dia;
    int64_t bpt_time;

    if (hpi_sys_get_last_bp_update(&bpt_sys, &bpt_dia, &bpt_time) == 0)
    {
        LOG_DBG("Last BPT value: %d/%d", bpt_sys, bpt_dia);
    }
    else
    {
        LOG_ERR("Failed to get last BPT value");
        bpt_sys = 0;
        bpt_dia = 0;
        bpt_time = 0;
    }

    // Create a column container for SYS
    lv_obj_t *cont_col_sys = lv_obj_create(cont_row_bpt);
    lv_obj_set_size(cont_col_sys, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_col_sys, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col_sys, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(cont_col_sys, 0, 0);
    lv_obj_add_style(cont_col_sys, &style_scr_black, 0);

    lv_obj_t *label_sys = lv_label_create(cont_col_sys);
    lv_label_set_text(label_sys, "SYS");
    lv_obj_add_style(label_sys, &style_red_medium, 0);

    lv_obj_t *label_bpt_sys = lv_label_create(cont_col_sys);
    if (bpt_sys == 0)
    {
        lv_label_set_text(label_bpt_sys, "--");
    }
    else
    {
        lv_label_set_text_fmt(label_bpt_sys, "%d", bpt_sys);
    }
    lv_obj_add_style(label_bpt_sys, &style_white_large_numeric, 0);

    lv_obj_t *label_bpt_sys_unit = lv_label_create(cont_col_sys);
    lv_label_set_text(label_bpt_sys_unit, "mmHg");
    lv_obj_add_style(label_bpt_sys_unit, &style_white_small, 0);
    lv_obj_set_style_text_align(label_bpt_sys_unit, LV_TEXT_ALIGN_CENTER, 0);

    // Separator
    // lv_obj_t *label_bpt_sep = lv_label_create(cont_row_bpt);
    // lv_label_set_text(label_bpt_sep, "/");

    // Create a column container for DIA
    lv_obj_t *cont_col_dia = lv_obj_create(cont_row_bpt);
    lv_obj_set_size(cont_col_dia, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_col_dia, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col_dia, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(cont_col_dia, 0, 0);
    lv_obj_add_style(cont_col_dia, &style_scr_black, 0);

    lv_obj_t *label_dia = lv_label_create(cont_col_dia);
    lv_label_set_text(label_dia, "DIA");
    lv_obj_add_style(label_dia, &style_red_medium, 0);

    lv_obj_t *label_bpt_dia = lv_label_create(cont_col_dia);
    if (bpt_dia == 0)
    {
        lv_label_set_text(label_bpt_dia, "--");
    }
    else
    {
        lv_label_set_text_fmt(label_bpt_dia, "%d", bpt_dia);
    }
    lv_obj_add_style(label_bpt_dia, &style_white_large_numeric, 0);

    lv_obj_t *label_bpt_dia_unit = lv_label_create(cont_col_dia);
    lv_label_set_text(label_bpt_dia_unit, "mmHg");
    lv_obj_add_style(label_bpt_dia_unit, &style_white_small, 0);
    lv_obj_set_style_text_align(label_bpt_dia_unit, LV_TEXT_ALIGN_CENTER, 0);

    char last_meas_str[74];
    hpi_helper_get_relative_time_str(bpt_time, last_meas_str, sizeof(last_meas_str));
    //hpi_helper_get_date_time_str(bpt_time, last_meas_str);
    label_bpt_last_update_time = lv_label_create(cont_col);
    lv_label_set_text(label_bpt_last_update_time, last_meas_str);
    lv_obj_set_style_text_align(label_bpt_last_update_time, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *btn_bpt_measure = hpi_btn_create(cont_col);
    lv_obj_add_event_cb(btn_bpt_measure, scr_bpt_measure_handler, LV_EVENT_ALL, NULL);
    lv_obj_set_height(btn_bpt_measure, 80);

    lv_obj_t *label_btn_bpt_measure = lv_label_create(btn_bpt_measure);
    lv_label_set_text(label_btn_bpt_measure, LV_SYMBOL_PLAY " Measure");
    lv_obj_center(label_btn_bpt_measure);

    hpi_disp_set_curr_screen(SCR_BPT);
    hpi_show_screen(scr_bpt, m_scroll_dir);
}