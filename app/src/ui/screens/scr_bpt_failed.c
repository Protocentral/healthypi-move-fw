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
#include <stdio.h>
#include <zephyr/logging/log.h>

#include "hpi_common_types.h"
#include "hw_module.h"
#include "ui/move_ui.h"
#include "ui/hpi_r0_theme.h"

LOG_MODULE_REGISTER(scr_bpt_cal_failed, LOG_LEVEL_DBG);

lv_obj_t *scr_bpt_cal_failed;

static lv_obj_t *btn_bpt_measure;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_large_numeric;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_tiny;


static void scr_bpt_btn_measure_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        //k_sem_give(&sem_bpt_check_sensor); 
        hpi_load_screen(SCR_BPT, SCROLL_UP);
    }
}

void draw_scr_bpt_cal_failed(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    /* P1-4: one screen, three failure paths - pick the copy from arg2 rather
     * than telling a user whose *measurement* failed that their calibration did.
     * arg1 is the parent screen (SCR_BPT at every call site). */
    const char *fail_title;
    const char *fail_info;

    switch (arg2)
    {
    case BPT_FAIL_EST:
        fail_title = "MEASUREMENT FAILED";
        fail_info = "Your blood pressure measurement\nwas not successful.\nPlease try again.";
        break;
    case BPT_FAIL_SENSOR:
        fail_title = "SENSOR ERROR";
        fail_info = "The finger sensor did not respond.\nPlease try again.";
        break;
    case BPT_FAIL_CAL:
    default:
        fail_title = "CALIBRATION FAILED";
        fail_info = "Your calibration was not successful.\nPlease try again.";
        break;
    }

    scr_bpt_cal_failed = lv_obj_create(NULL);
    // AMOLED OPTIMIZATION: Pure black background for power efficiency
    lv_obj_set_style_bg_color(scr_bpt_cal_failed, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_bpt_cal_failed, LV_OBJ_FLAG_SCROLLABLE);

    // CIRCULAR AMOLED-OPTIMIZED CALIBRATION FAILED SCREEN
    // Display center: (195, 195), Usable radius: ~185px
    // Red theme for error/failure indication

    // Error icon at top
    lv_obj_t *label_error = lv_label_create(scr_bpt_cal_failed);
    /* No error/close glyph exists in any matsym bin, so this keeps LVGL's built-in
     * symbol font (which renders from the default font, not a v2 bin). */
    lv_label_set_text(label_error, LV_SYMBOL_CLOSE);
    lv_obj_align(label_error, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_text_color(label_error, lv_color_hex(R0_ERROR), 0);

    // Screen title - properly positioned below icon
    lv_obj_t *label_title = lv_label_create(scr_bpt_cal_failed);
    lv_label_set_text(label_title, fail_title);
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 90);
    lv_obj_set_style_text_font(label_title, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label_title, lv_color_hex(R0_ERROR), 0);
    lv_obj_set_style_text_letter_space(label_title, 1, 0);

    // Error message (centered)
    lv_obj_t *label_info = lv_label_create(scr_bpt_cal_failed);
    lv_label_set_long_mode(label_info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_info, 300);
    lv_label_set_text(label_info, fail_info);
    lv_obj_align(label_info, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_text_font(label_info, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label_info, lv_color_hex(V2_MUTED), 0);

    // BOTTOM ZONE: Action Button (consistent with other screens)
    btn_bpt_measure = hpi_btn_create_secondary(scr_bpt_cal_failed);
    lv_obj_add_event_cb(btn_bpt_measure, scr_bpt_btn_measure_handler, LV_EVENT_CLICKED, NULL);
    lv_obj_set_size(btn_bpt_measure, 160, 56);
    lv_obj_align(btn_bpt_measure, LV_ALIGN_BOTTOM_MID, 0, -30);

    lv_obj_t *label_btn = lv_label_create(btn_bpt_measure);
    lv_label_set_text(label_btn, "CLOSE");
    lv_obj_center(label_btn);
    lv_obj_set_style_text_font(label_btn, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(label_btn, lv_color_hex(V2_VALUE), 0);
    lv_obj_set_style_text_letter_space(label_btn, 1, 0);

    hpi_disp_set_curr_screen(SCR_SPL_BPT_FAILED);
    hpi_show_screen(scr_bpt_cal_failed, m_scroll_dir);
}

void gesture_down_scr_bpt_cal_failed(void)
{
    // Handle gesture down event
    hpi_load_screen(SCR_BPT, SCROLL_DOWN);
}