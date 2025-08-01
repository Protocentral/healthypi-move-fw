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

K_SEM_DEFINE(sem_spo2_complete, 0, 1);

lv_obj_t *scr_spo2_complete;

static lv_obj_t *label_spo2_percent;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_white_large;

void draw_scr_spl_spo2_complete(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    int parent_screen = arg1; // Parent screen passed from the previous screen
    int meas_spo2 = arg2;     // Spo2 value passed from the previous screen

    scr_spo2_complete = lv_obj_create(NULL);
    lv_obj_add_style(scr_spo2_complete, &style_scr_black, 0);
    lv_obj_clear_flag(scr_spo2_complete, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    lv_obj_t *cont_col = lv_obj_create(scr_spo2_complete);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(cont_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(cont_col, &style_scr_black, 0);

    lv_obj_t *lbl_info_scroll = lv_label_create(cont_col);
    lv_label_set_text(lbl_info_scroll, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_color(lbl_info_scroll, lv_palette_lighten(LV_PALETTE_RED, 1), LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *img1 = lv_img_create(cont_col);
    lv_img_set_src(img1, &img_complete_85);

    lv_obj_t *label_signal = lv_label_create(cont_col);
    lv_label_set_text(label_signal, "SpO2");

    lv_obj_t *cont_spo2_val = lv_obj_create(cont_col);
    lv_obj_set_size(cont_spo2_val, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_spo2_val, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(cont_spo2_val, &style_scr_black, 0);
    lv_obj_set_flex_align(cont_spo2_val, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *img_spo2 = lv_img_create(cont_spo2_val);
    lv_img_set_src(img_spo2, &icon_spo2_100);

    label_spo2_percent = lv_label_create(cont_spo2_val);
    lv_label_set_text_fmt(label_spo2_percent, "%d", meas_spo2);
    lv_obj_set_style_text_align(label_spo2_percent, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_style(label_spo2_percent, &style_white_large, 0);

    lv_obj_t *label_spo2_percent_sign = lv_label_create(cont_spo2_val);
    lv_label_set_text(label_spo2_percent_sign, " %");

    hpi_disp_set_curr_screen(SCR_SPL_SPO2_COMPLETE);
    hpi_show_screen(scr_spo2_complete, m_scroll_dir);
    // k_sem_give(&sem_spo2_complete);
}

void gesture_down_scr_spl_spo2_complete(void)
{
    hpi_load_screen(SCR_SPO2, SCROLL_DOWN);
}
