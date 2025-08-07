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

#include "ui/move_ui.h"

LOG_MODULE_REGISTER(scr_spo2_select, LOG_LEVEL_DBG);

lv_obj_t *scr_spo2_select;
lv_obj_t *btn_spo2_select_fi;
lv_obj_t *btn_spo2_select_wr;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_large_numeric;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_tiny;

extern lv_style_t style_bg_blue;
extern lv_style_t style_bg_red;

static void scr_spo2_sel_fi_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        hpi_load_scr_spl(SCR_SPL_SPO2_SCR2, SCROLL_UP, (uint8_t)SCR_SPO2, SPO2_SOURCE_PPG_FI, 0, 0);
    }
}

static void scr_spo2_sel_wr_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        hpi_load_scr_spl(SCR_SPL_SPO2_SCR2, SCROLL_UP, (uint8_t)SCR_SPO2, SPO2_SOURCE_PPG_WR, 0, 0);
    }
}

void draw_scr_spo2_select(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_spo2_select = lv_obj_create(NULL);
    lv_obj_clear_flag(scr_spo2_select, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    lv_obj_t *cont_col = lv_obj_create(scr_spo2_select);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_col, -1, LV_PART_SCROLLBAR);
    lv_obj_add_style(cont_col, &style_scr_black, 0);
    // lv_obj_add_style(cont_col, &style_bg_red, 0);

    lv_obj_t *lbl_info_scroll = lv_label_create(cont_col);
    lv_label_set_text(lbl_info_scroll, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_color(lbl_info_scroll, lv_palette_darken(LV_PALETTE_RED, 2), LV_PART_MAIN);

    lv_obj_t *label_signal = lv_label_create(cont_col);
    lv_label_set_text(label_signal, "Select PPG Source");
    // lv_obj_add_style(label_signal, &style_white_medium, 0);
    lv_obj_set_style_text_align(label_signal, LV_TEXT_ALIGN_CENTER, 0);

    // Button for PPG WR
    btn_spo2_select_wr = lv_btn_create(cont_col);
    lv_obj_t *img_wrist = lv_img_create(btn_spo2_select_wr);
    lv_img_set_src(img_wrist, &img_wrist_45);
    lv_obj_align(img_wrist, LV_ALIGN_CENTER, -30, 0);
    lv_obj_t *label_ppg_wr = lv_label_create(btn_spo2_select_wr);
    lv_label_set_text(label_ppg_wr, "Wrist");
    lv_obj_align_to(label_ppg_wr, img_wrist, LV_ALIGN_OUT_RIGHT_MID, 45, 0);
    lv_obj_set_size(btn_spo2_select_wr, 200, 90);
    lv_obj_add_event_cb(btn_spo2_select_wr, scr_spo2_sel_wr_handler, LV_EVENT_CLICKED, NULL);

    // Button for PPG FI
    btn_spo2_select_fi = lv_btn_create(cont_col);
    lv_obj_t *img_finger = lv_img_create(btn_spo2_select_fi);
    lv_img_set_src(img_finger, &img_bpt_finger_45);
    lv_obj_align(img_finger, LV_ALIGN_CENTER, -30, 0);
    lv_obj_t *label_ppg_fi = lv_label_create(btn_spo2_select_fi);
    lv_label_set_text(label_ppg_fi, "Finger");
    lv_obj_align_to(label_ppg_fi, img_finger, LV_ALIGN_OUT_RIGHT_MID, 45, 0);
    lv_obj_set_size(btn_spo2_select_fi, 200, 90);
    lv_obj_add_event_cb(btn_spo2_select_fi, scr_spo2_sel_fi_handler, LV_EVENT_CLICKED, NULL);

    hpi_disp_set_curr_screen(SCR_SPL_SPO2_SELECT);
    hpi_show_screen(scr_spo2_select, m_scroll_dir);
}

void gesture_down_scr_spo2_select(void)
{
    hpi_load_screen(SCR_SPO2, SCROLL_DOWN);
}
