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

LOG_MODULE_REGISTER(scr_spo2_timeout, LOG_LEVEL_DBG);

lv_obj_t *scr_spo2_timeout;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_white_large_numeric;


extern struct k_sem sem_fi_spo2_est_cancel;

void draw_scr_spl_spo2_timeout(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_spo2_timeout = lv_obj_create(NULL);
    lv_obj_add_style(scr_spo2_timeout, &style_scr_black, 0);
    lv_obj_add_flag(scr_spo2_timeout, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    // draw_scr_common(scr_spo2_timeout);

    /*Create a container with COLUMN flex direction optimized for 390x390 circular display*/
    lv_obj_t *cont_col = lv_obj_create(scr_spo2_timeout);
    lv_obj_set_size(cont_col, 350, 350); /* Fit within circular bounds */
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_CENTER, 0, 0); /* Center the container */
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    // lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(cont_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(cont_col, &style_scr_black, 0);
    lv_obj_set_style_pad_all(cont_col, 20, LV_PART_MAIN); /* Add padding for circular bounds */

    lv_obj_t *label_signal = lv_label_create(cont_col);
    lv_label_set_text(label_signal, "Measurement Failed");
    lv_obj_add_style(label_signal, &style_red_medium, 0); /* Make title more prominent */

    lv_obj_t *img1 = lv_img_create(cont_col);
    lv_img_set_src(img1, &img_failed_80);

    lv_obj_t *label_info = lv_label_create(cont_col);
    lv_label_set_long_mode(label_info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_info, 280); /* Reduced width for circular display */
    lv_label_set_text(label_info, "Poor skin contact or motion\nTry again with:\n• Better finger placement\n• Stay still during measurement\n");
    lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_style(label_info, &style_white_medium, 0); /* Consistent text styling */

    hpi_disp_set_curr_screen(SCR_SPL_SPO2_TIMEOUT);
    hpi_show_screen(scr_spo2_timeout, m_scroll_dir);
}

void gesture_down_scr_spl_spo2_timeout(void)
{
    
        k_sem_give(&sem_fi_spo2_est_cancel);
        hpi_load_screen(SCR_SPO2, SCROLL_DOWN);
   
}