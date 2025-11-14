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

// Modern style system (consistent with main screens)
extern lv_style_t style_body_medium;
extern lv_style_t style_caption;

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
    // AMOLED OPTIMIZATION: Pure black background for power efficiency
    lv_obj_set_style_bg_color(scr_spo2_select, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_spo2_select, LV_OBJ_FLAG_SCROLLABLE);

    // CIRCULAR AMOLED-OPTIMIZED SpO2 SOURCE SELECTION SCREEN
    // Display center: (195, 195), Usable radius: ~185px
    // Full screen utilization with proper spacing
    
    // TOP ZONE: Screen title (positioned higher to use more space)
    lv_obj_t *label_title = lv_label_create(scr_spo2_select);
    lv_label_set_text(label_title, "Select PPG Source");
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 60);  // Higher position for better space utilization
    lv_obj_add_style(label_title, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);

    // UPPER-MID ZONE: Wrist PPG Button (repositioned for better circular layout)
    btn_spo2_select_wr = hpi_btn_create_primary(scr_spo2_select);
    lv_obj_set_size(btn_spo2_select_wr, 280, 85);  // Larger for better touch and space utilization
    lv_obj_align(btn_spo2_select_wr, LV_ALIGN_CENTER, 0, -60);  // Closer to center for circular bounds
    lv_obj_set_style_radius(btn_spo2_select_wr, 20, LV_PART_MAIN);
    
    // AMOLED-optimized button styling for wrist option
    lv_obj_set_style_bg_color(btn_spo2_select_wr, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_spo2_select_wr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_spo2_select_wr, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_spo2_select_wr, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN);
    lv_obj_set_style_border_opa(btn_spo2_select_wr, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn_spo2_select_wr, 0, LV_PART_MAIN);  // No shadow for AMOLED
    
    // Wrist button content with better positioning
    lv_obj_t *img_wrist = lv_img_create(btn_spo2_select_wr);
    lv_img_set_src(img_wrist, &img_wrist_45);
    lv_obj_align(img_wrist, LV_ALIGN_LEFT_MID, 25, 0);
    lv_obj_set_style_img_recolor(img_wrist, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(img_wrist, LV_OPA_80, LV_PART_MAIN);
    
    lv_obj_t *label_ppg_wr = lv_label_create(btn_spo2_select_wr);
    lv_label_set_text(label_ppg_wr, "Wrist");
    lv_obj_align(label_ppg_wr, LV_ALIGN_CENTER, 25, 0);  // Offset right from icon
    lv_obj_set_style_text_color(label_ppg_wr, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN);
    lv_obj_add_style(label_ppg_wr, &style_body_medium, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_spo2_select_wr, scr_spo2_sel_wr_handler, LV_EVENT_CLICKED, NULL);

    // LOWER-MID ZONE: Finger PPG Button (repositioned for better circular layout)
    btn_spo2_select_fi = hpi_btn_create_primary(scr_spo2_select);
    lv_obj_set_size(btn_spo2_select_fi, 280, 85);  // Larger for better touch and space utilization
    lv_obj_align(btn_spo2_select_fi, LV_ALIGN_CENTER, 0, 60);   // Closer to center for circular bounds
    lv_obj_set_style_radius(btn_spo2_select_fi, 20, LV_PART_MAIN);
    
    // AMOLED-optimized button styling for finger option
    lv_obj_set_style_bg_color(btn_spo2_select_fi, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_spo2_select_fi, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_spo2_select_fi, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_spo2_select_fi, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN);
    lv_obj_set_style_border_opa(btn_spo2_select_fi, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn_spo2_select_fi, 0, LV_PART_MAIN);  // No shadow for AMOLED
    
    // Finger button content with better positioning
    lv_obj_t *img_finger = lv_img_create(btn_spo2_select_fi);
    lv_img_set_src(img_finger, &img_bpt_finger_45);
    lv_obj_align(img_finger, LV_ALIGN_LEFT_MID, 25, 0);
    lv_obj_set_style_img_recolor(img_finger, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(img_finger, LV_OPA_80, LV_PART_MAIN);
    
    lv_obj_t *label_ppg_fi = lv_label_create(btn_spo2_select_fi);
    lv_label_set_text(label_ppg_fi, "Finger");
    lv_obj_align(label_ppg_fi, LV_ALIGN_CENTER, 25, 0);  // Offset right from icon
    lv_obj_set_style_text_color(label_ppg_fi, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN);
    lv_obj_add_style(label_ppg_fi, &style_body_medium, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_spo2_select_fi, scr_spo2_sel_fi_handler, LV_EVENT_CLICKED, NULL);

    // BOTTOM ZONE: Gesture hint (repositioned to stay within circular bounds)
    lv_obj_t *label_hint = lv_label_create(scr_spo2_select);
    lv_label_set_text(label_hint, "Swipe down to go back");
    lv_obj_align(label_hint, LV_ALIGN_CENTER, 0, 140);  // Positioned within safe circular area
    lv_obj_set_style_text_color(label_hint, lv_color_hex(0x666666), LV_PART_MAIN);  // Subtle gray
    lv_obj_add_style(label_hint, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    hpi_disp_set_curr_screen(SCR_SPL_SPO2_SELECT);
    hpi_show_screen(scr_spo2_select, m_scroll_dir);
}

void gesture_down_scr_spo2_select(void)
{
    hpi_load_screen(SCR_SPO2, SCROLL_DOWN);
}
