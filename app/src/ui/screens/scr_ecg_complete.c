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
#include "hpi_sys.h"

lv_obj_t *scr_ecg_complete;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_body_medium;
extern lv_style_t style_caption;

void draw_scr_ecg_complete(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_ecg_complete = lv_obj_create(NULL);
    // AMOLED OPTIMIZATION: Pure black background for power efficiency
    lv_obj_set_style_bg_color(scr_ecg_complete, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_ecg_complete, LV_OBJ_FLAG_SCROLLABLE);

    // CIRCULAR AMOLED-OPTIMIZED ECG COMPLETE SCREEN
    // Display center: (195, 195), Usable radius: ~185px
    // Clean completion display matching design philosophy

    // Screen title - optimized position
    lv_obj_t *label_title = lv_label_create(scr_ecg_complete);
    lv_label_set_text(label_title, "ECG Recording");
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 60);  // Better spacing from top
    lv_obj_add_style(label_title, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);

    // Success icon - centered
    lv_obj_t *img_success = lv_img_create(scr_ecg_complete);
    lv_img_set_src(img_success, &img_complete_85);
    lv_obj_align(img_success, LV_ALIGN_CENTER, 0, -20);  // Centered vertically

    // Status message - below icon
    lv_obj_t *label_status = lv_label_create(scr_ecg_complete);
    lv_label_set_text(label_status, "Complete");
    lv_obj_align(label_status, LV_ALIGN_CENTER, 0, 60);  // Below icon
    lv_obj_add_style(label_status, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_status, lv_color_hex(0x00FF00), LV_PART_MAIN);  // Green for success
    lv_obj_set_style_text_align(label_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // Information text - bottom area
    lv_obj_t *label_info = lv_label_create(scr_ecg_complete);
    lv_label_set_text(label_info, "Download recording from app");
    lv_obj_align(label_info, LV_ALIGN_BOTTOM_MID, 0, -50);  // Bottom with margin
    lv_obj_add_style(label_info, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_info, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(label_info, 280);  // Constrain width for text wrapping
    lv_label_set_long_mode(label_info, LV_LABEL_LONG_WRAP);

    hpi_disp_set_curr_screen(SCR_SPL_ECG_COMPLETE);
    hpi_show_screen(scr_ecg_complete, m_scroll_dir);
}

void gesture_down_scr_ecg_complete(void)
{
    // Handle gesture down event
    hpi_load_screen(SCR_ECG, SCROLL_DOWN);
}