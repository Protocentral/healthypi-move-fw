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
#include "trends.h"

LOG_MODULE_REGISTER(hpi_disp_scr_spo2_scr2, LOG_LEVEL_DBG);

static lv_obj_t *scr_spo2_scr2;
static lv_obj_t *btn_spo2_proceed;

static int spo2_source = 0;

// Modern style system (consistent with main screens)
extern lv_style_t style_body_medium;
extern lv_style_t style_caption;

extern struct k_sem sem_start_one_shot_spo2;
extern struct k_sem sem_fi_spo2_est_start;

static void scr_spo2_btn_proceed_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        if (spo2_source == SPO2_SOURCE_PPG_WR)
        {
            LOG_DBG("Proceeding with wrist PPG sensor");
            k_sem_give(&sem_start_one_shot_spo2);
        }    
        else if (spo2_source == SPO2_SOURCE_PPG_FI)
        {
            LOG_DBG("Proceeding with finger PPG sensor");
            k_sem_give(&sem_fi_spo2_est_start);
        }
        else
        {
            LOG_ERR("Invalid SpO2 source selected: %d", spo2_source);
            return;
        }
    }
}

void draw_scr_spo2_scr2(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    spo2_source = arg2;

    scr_spo2_scr2 = lv_obj_create(NULL);
    // AMOLED OPTIMIZATION: Pure black background for power efficiency
    lv_obj_set_style_bg_color(scr_spo2_scr2, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_spo2_scr2, LV_OBJ_FLAG_SCROLLABLE);

    // CIRCULAR AMOLED-OPTIMIZED SpO2 INSTRUCTION SCREEN
    // Display center: (195, 195), Usable radius: ~185px
    
    // Scroll hint at top
    lv_obj_t *lbl_scroll_cancel = lv_label_create(scr_spo2_scr2);
    lv_label_set_text(lbl_scroll_cancel, LV_SYMBOL_DOWN);
    lv_obj_align(lbl_scroll_cancel, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_text_color(lbl_scroll_cancel, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN);
    lv_obj_add_style(lbl_scroll_cancel, &style_caption, LV_PART_MAIN);

    // UPPER ZONE: Instructional Image (centered in upper area)
    if (spo2_source == SPO2_SOURCE_PPG_WR)
    {
        lv_obj_t *img_spo2_wr = lv_img_create(scr_spo2_scr2);
        lv_img_set_src(img_spo2_wr, &img_spo2_hand);
        lv_obj_align(img_spo2_wr, LV_ALIGN_TOP_MID, 0, 50);  // Positioned in upper area
        lv_obj_set_style_img_recolor(img_spo2_wr, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN);
        lv_obj_set_style_img_recolor_opa(img_spo2_wr, LV_OPA_60, LV_PART_MAIN);
    }
    else if (spo2_source == SPO2_SOURCE_PPG_FI)
    {
        lv_obj_t *img_spo2_fi = lv_img_create(scr_spo2_scr2);
        lv_img_set_src(img_spo2_fi, &img_bpt_finger_90);
        lv_obj_align(img_spo2_fi, LV_ALIGN_TOP_MID, 0, 50);  // Positioned in upper area
        lv_obj_set_style_img_recolor(img_spo2_fi, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN);
        lv_obj_set_style_img_recolor_opa(img_spo2_fi, LV_OPA_60, LV_PART_MAIN);
    }

    // CENTRAL ZONE: Instruction Text (centered with proper wrapping)
    lv_obj_t *label_info = lv_label_create(scr_spo2_scr2);
    lv_label_set_long_mode(label_info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_info, 280);  // Optimized for circular display
    
    if (spo2_source == SPO2_SOURCE_PPG_WR)
    {
        lv_label_set_text(label_info, "Ensure that your Move is worn on the wrist snugly as shown.");
    }
    else if (spo2_source == SPO2_SOURCE_PPG_FI)
    {
        lv_label_set_text(label_info, "Wear finger sensor as per the instructions now");
    }
    
    lv_obj_align(label_info, LV_ALIGN_CENTER, 0, 20);  // Centered with slight downward offset
    lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_info, lv_color_white(), LV_PART_MAIN);
    lv_obj_add_style(label_info, &style_body_medium, LV_PART_MAIN);

    // BOTTOM ZONE: Proceed Button (properly centered at bottom)
    btn_spo2_proceed = hpi_btn_create_primary(scr_spo2_scr2);
    lv_obj_set_size(btn_spo2_proceed, 200, 60);  // Touch-friendly size
    lv_obj_align(btn_spo2_proceed, LV_ALIGN_BOTTOM_MID, 0, -40);  // Centered at bottom with margin
    lv_obj_set_style_radius(btn_spo2_proceed, 25, LV_PART_MAIN);
    
    // AMOLED-optimized button styling
    lv_obj_set_style_bg_color(btn_spo2_proceed, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_spo2_proceed, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_spo2_proceed, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_spo2_proceed, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN);
    lv_obj_set_style_border_opa(btn_spo2_proceed, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn_spo2_proceed, 0, LV_PART_MAIN);  // No shadow for AMOLED
    
    lv_obj_t *label_btn = lv_label_create(btn_spo2_proceed);
    lv_label_set_text(label_btn, LV_SYMBOL_PLAY " Proceed");
    lv_obj_center(label_btn);
    lv_obj_set_style_text_color(label_btn, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN);
    lv_obj_add_style(label_btn, &style_body_medium, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_spo2_proceed, scr_spo2_btn_proceed_handler, LV_EVENT_CLICKED, NULL);

    hpi_disp_set_curr_screen(SCR_SPL_SPO2_SCR2);
    hpi_show_screen(scr_spo2_scr2, m_scroll_dir);
}

void gesture_down_scr_spo2_scr2(void)
{
    hpi_load_screen(SCR_SPO2, SCROLL_DOWN);
}
