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
#include "hpi_sys.h"

LOG_MODULE_REGISTER(hpi_disp_scr_hrv, LOG_LEVEL_DBG);

lv_obj_t *scr_hrv;

// GUI Labels and buttons
static lv_obj_t *label_hrv_title;
static lv_obj_t *label_hrv_description;
static lv_obj_t *btn_hrv_start;

// Externs - Modern style system
extern lv_style_t style_body_medium;
extern lv_style_t style_numeric_large;
extern lv_style_t style_caption;

/**
 * @brief Button event handler for starting HRV evaluation
 */
static void scr_hrv_btn_start_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_CLICKED) {
        LOG_INF("HRV Evaluation started by user");
        
        // Display progress screen immediately
        hpi_load_scr_spl(SCR_SPL_HRV_EVAL_PROGRESS, SCROLL_UP, 0, 0, 0, 0);
        
        // Signal state machine to start HRV evaluation
        extern struct k_sem sem_hrv_eval_start;
        k_sem_give(&sem_hrv_eval_start);
    }
}

/**
 * @brief Draw HRV home screen with measurement controls
 * 
 * Main carousel screen showing HRV evaluation options:
 * - Start button to initiate new HRV evaluation
 */
void draw_scr_hrv(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_hrv = lv_obj_create(NULL);
    // AMOLED OPTIMIZATION: Pure black background for power efficiency
    lv_obj_set_style_bg_color(scr_hrv, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_hrv, LV_OBJ_FLAG_SCROLLABLE);

    // CIRCULAR AMOLED-OPTIMIZED HRV HOME SCREEN
    // Display center: (195, 195), Usable radius: ~185px
    
    // Screen title
    label_hrv_title = lv_label_create(scr_hrv);
    lv_label_set_text(label_hrv_title, "HRV");
    lv_obj_align(label_hrv_title, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_add_style(label_hrv_title, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_hrv_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_hrv_title, lv_color_white(), LV_PART_MAIN);

    // HRV Icon - positioned at top-center below title
    lv_obj_t *img_hrv = lv_img_create(scr_hrv);
    lv_img_set_src(img_hrv, &img_heart_48px);
    lv_obj_align(img_hrv, LV_ALIGN_TOP_MID, 0, 95);
    lv_obj_set_style_img_recolor(img_hrv, lv_color_hex(0xFF6B9D), LV_PART_MAIN);  // Heart color (pink-red)
    lv_obj_set_style_img_recolor_opa(img_hrv, LV_OPA_COVER, LV_PART_MAIN);

    // Description text
    label_hrv_description = lv_label_create(scr_hrv);
    lv_label_set_text(label_hrv_description, "30-second\nHeart Rate\nVariability");
    lv_obj_align(label_hrv_description, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_text_color(label_hrv_description, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_add_style(label_hrv_description, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_hrv_description, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // START BUTTON - Single centered button
    btn_hrv_start = hpi_btn_create_primary(scr_hrv);
    lv_obj_set_size(btn_hrv_start, 180, 50);
    lv_obj_align(btn_hrv_start, LV_ALIGN_CENTER, 0, 70);
    lv_obj_set_style_radius(btn_hrv_start, 25, LV_PART_MAIN);
    
    // AMOLED-optimized button styling with heart pink color
    lv_obj_set_style_bg_color(btn_hrv_start, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_hrv_start, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_hrv_start, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_hrv_start, lv_color_hex(0xFF6B9D), LV_PART_MAIN);
    lv_obj_set_style_border_opa(btn_hrv_start, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn_hrv_start, 0, LV_PART_MAIN);
    
    lv_obj_t *label_start = lv_label_create(btn_hrv_start);
    lv_label_set_text(label_start, LV_SYMBOL_PLAY " Start");
    lv_obj_center(label_start);
    lv_obj_set_style_text_color(label_start, lv_color_hex(0xFF6B9D), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_hrv_start, scr_hrv_btn_start_handler, LV_EVENT_CLICKED, NULL);

    hpi_disp_set_curr_screen(SCR_HRV);
    hpi_show_screen(scr_hrv, m_scroll_dir);
}

/**
 * @brief Gesture handler for HRV carousel screen - gesture down to go to home
 */
void gesture_down_scr_hrv(void)
{
    // Carousel screens don't have gesture down - this would navigate away from carousel
    // Keep placeholder for consistency with other screens
}