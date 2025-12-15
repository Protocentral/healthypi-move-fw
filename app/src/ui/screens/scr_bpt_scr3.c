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

LOG_MODULE_REGISTER(scr_bpt_scr3, LOG_LEVEL_DBG);

lv_obj_t *scr_bpt_scr3;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_large_numeric;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_tiny;

extern struct k_sem sem_fi_spo2_est_cancel;
extern struct k_sem sem_fi_bpt_est_cancel;
extern struct k_sem sem_fi_bpt_cal_cancel;
static int source = 0;

void draw_scr_fi_sens_check(enum scroll_dir dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    
    scr_bpt_scr3 = lv_obj_create(NULL);
    source = arg2;
    // AMOLED OPTIMIZATION: Pure black background for power efficiency
    lv_obj_set_style_bg_color(scr_bpt_scr3, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_bpt_scr3, LV_OBJ_FLAG_SCROLLABLE);

    // CIRCULAR AMOLED-OPTIMIZED SENSOR CHECK SCREEN
    // Display center: (195, 195), Usable radius: ~185px
    // Blue theme for blood pressure consistency

    // Screen title - properly positioned at top
    lv_obj_t *label_title = lv_label_create(scr_bpt_scr3);
    lv_label_set_text(label_title, "Sensor Check");
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_add_style(label_title, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);

    // Finger sensor image (positioned above spinner)
    lv_obj_t *img_bpt = lv_img_create(scr_bpt_scr3);
    lv_img_set_src(img_bpt, &img_bpt_finger_90);
    lv_obj_align(img_bpt, LV_ALIGN_CENTER, 0, -50);

    // Progress indicator (centered)
    lv_obj_t *spinner = lv_spinner_create(scr_bpt_scr3);
    lv_obj_set_size(spinner, 100, 100);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x4A90E2), LV_PART_INDICATOR);  // Blue theme

    // Status message (below spinner with proper spacing)
    lv_obj_t *label_info = lv_label_create(scr_bpt_scr3);
    lv_label_set_long_mode(label_info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_info, 300);
    lv_label_set_text(label_info, "Waiting for sensor to connect...");
    lv_obj_align(label_info, LV_ALIGN_CENTER, 0, 80);
    lv_obj_add_style(label_info, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_info, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);

    hpi_disp_set_curr_screen(SCR_SPL_FI_SENS_CHECK);
    hpi_show_screen(scr_bpt_scr3, dir);
}

void gesture_down_scr_fi_sens_check(void)
{
    // Handle gesture down event
    if(source == SCR_SPO2)
    {
        k_sem_give(&sem_fi_spo2_est_cancel);
        hpi_load_screen(SCR_SPO2, SCROLL_DOWN);
    }
    else
    {
      k_sem_give(&sem_fi_bpt_est_cancel);
      k_sem_give(&sem_fi_bpt_cal_cancel);
      hpi_load_screen(SCR_BPT, SCROLL_DOWN);
    }
}