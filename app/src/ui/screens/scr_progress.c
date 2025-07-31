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
#include <app_version.h>

#include "ui/move_ui.h"

LOG_MODULE_REGISTER(scr_progress, LOG_LEVEL_WRN);

lv_obj_t *scr_progress;

lv_obj_t *label_title;
lv_obj_t *label_subtitle;

lv_obj_t *label_progress;
lv_obj_t *label_progress_status;

lv_obj_t *bar_progress;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_large;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_tiny;

void draw_scr_progress(char *title, char *message)
{
    scr_progress = lv_obj_create(NULL);
    lv_obj_clear_flag(scr_progress, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    lv_obj_t *cont_col = lv_obj_create(scr_progress);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    // lv_obj_set_width(cont_col, lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(cont_col, &style_scr_black, 0);

    lv_obj_set_style_bg_color(cont_col, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(cont_col, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    label_title = lv_label_create(cont_col);
    lv_label_set_text(label_title, title);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 20);

    label_subtitle = lv_label_create(cont_col);
    lv_label_set_text(label_subtitle, message);
    lv_obj_align_to(label_subtitle, label_title, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);

    bar_progress = lv_bar_create(cont_col);
    lv_obj_set_size(bar_progress, 300, 40);

    label_progress = lv_label_create(cont_col);
    lv_label_set_text(label_progress, "0%");
    lv_obj_set_style_text_align(label_progress, LV_TEXT_ALIGN_CENTER, 0);

    hpi_disp_set_curr_screen(SCR_SPL_PROGRESS);
    hpi_show_screen(scr_progress, SCROLL_NONE);
}

void hpi_disp_scr_update_progress(int progress, char *status)
{
    if (label_progress == NULL)
        return;

    lv_label_set_text_fmt(label_progress, "%d %%", progress);
    lv_bar_set_value(bar_progress, progress, LV_ANIM_ON);

    if (status != NULL)
    {
        lv_label_set_text(label_subtitle, status);
    }
}
