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


#include <lvgl.h>
#include "hpi_evt.h"
#include <stdio.h>
#include <zephyr/logging/log.h>

#include "hpi_common_types.h"
#include "hw_module.h"
#include "ui/move_ui.h"
#include "ui/hpi_r0_theme.h"

LOG_MODULE_REGISTER(scr_bpt_scr3, LOG_LEVEL_DBG);

lv_obj_t *scr_bpt_scr3;

static int parent_screen = 0;
static int op_mode = 0;

/* Sensor-check ("seating the finger sensor") — v2. Shares the measure chrome:
 * pulsing-dot header + finger image + a blue status line + muted hint. Reached
 * by both the BP and SpO2 finger flows (V2_SPO2 == V2_BP, 0x6FB3CC). */
void draw_scr_fi_sens_check(enum scroll_dir dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_bpt_scr3 = lv_obj_create(NULL);
    // arg1 = parent screen (SCR_BPT or SCR_SPO2)
    // arg2 = operation mode (PPG_FI_OP_MODE_*)
    parent_screen = arg1;
    op_mode = arg2;
    lv_obj_set_style_bg_color(scr_bpt_scr3, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_bpt_scr3, LV_OBJ_FLAG_SCROLLABLE);

    /* header: pulsing dot + SENSOR CHECK */
    hpi_bpt_make_title_row(scr_bpt_scr3, "SENSOR CHECK");

    /* finger sensor image, centred */
    lv_obj_t *img_bpt = lv_img_create(scr_bpt_scr3);
    lv_img_set_src(img_bpt, &img_bpt_finger_90);
    lv_obj_align(img_bpt, LV_ALIGN_CENTER, 0, -30);

    /* status line — blue */
    lv_obj_t *label_status = lv_label_create(scr_bpt_scr3);
    lv_label_set_text(label_status, "SEATING SENSOR");
    lv_obj_align(label_status, LV_ALIGN_CENTER, 0, 66);
    lv_obj_set_style_text_font(label_status, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(label_status, lv_color_hex(V2_BP), 0);
    lv_obj_set_style_text_letter_space(label_status, 2, 0);

    /* muted instruction */
    lv_obj_t *label_info = lv_label_create(scr_bpt_scr3);
    lv_label_set_long_mode(label_info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_info, 280);
    lv_label_set_text(label_info, "Insert the finger sensor and keep still");
    lv_obj_align(label_info, LV_ALIGN_CENTER, 0, 104);
    lv_obj_set_style_text_font(label_info, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label_info, lv_color_hex(V2_MUTED), 0);

    hpi_disp_set_curr_screen(SCR_SPL_FI_SENS_CHECK);
    hpi_show_screen(scr_bpt_scr3, dir);
}

void gesture_down_scr_fi_sens_check(void)
{
    // Handle gesture down event - cancel the current operation
    // op_mode values: 0=IDLE, 1=BPT_EST, 2=BPT_CAL, 3=SPO2_EST
    LOG_INF("Gesture Down on Sensor Check Screen - parent=%d, op_mode=%d", parent_screen, op_mode);

    if (parent_screen == SCR_SPO2)
    {
        // SpO2 estimation cancel
        k_event_post(&fi_evt, EVT_FI_SPO2_CANCEL);
    }
    else if (op_mode == 2)  // PPG_FI_OP_MODE_BPT_CAL
    {
        // BPT calibration cancel
        k_event_post(&fi_evt, EVT_FI_BPT_CAL_CANCEL);
    }
    else
    {
        // BPT estimation cancel (op_mode == 1 or default)
        k_event_post(&fi_evt, EVT_FI_BPT_EST_CANCEL);
    }

    // Navigate back to the parent screen
    hpi_load_screen(parent_screen, SCROLL_DOWN);
}