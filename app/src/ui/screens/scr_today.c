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
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <stdio.h>

#include "ui/move_ui.h"
#include "hw_module.h"

lv_obj_t *scr_today;

static lv_obj_t *today_arc_steps;
static lv_obj_t *today_arc_cals;
static lv_obj_t *today_arc_active_time;

static lv_obj_t *label_today_steps;
static lv_obj_t *label_today_cals;
static lv_obj_t *label_today_active_time;

extern lv_style_t style_white_medium;

static uint16_t m_steps_today_target = 10000;
static uint16_t m_kcals_today_target = 500;
static uint16_t m_active_time_today_target = 30;

void draw_scr_today(enum scroll_dir m_scroll_dir)
{
    scr_today = lv_obj_create(NULL);

    lv_obj_clear_flag(scr_today, LV_OBJ_FLAG_SCROLLABLE);
    draw_scr_common(scr_today);

    lv_obj_t *today_group;
    today_group = lv_obj_create(scr_today);
    lv_obj_set_width(today_group, 360);
    lv_obj_set_height(today_group, 360);
    lv_obj_set_align(today_group, LV_ALIGN_CENTER);
    lv_obj_clear_flag(today_group, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(today_group, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(today_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(today_group, 0, LV_PART_MAIN);

    today_arc_steps = lv_arc_create(today_group);
    lv_obj_set_size(today_arc_steps, 360, 360);
    lv_obj_set_align(today_arc_steps, LV_ALIGN_CENTER);
    lv_arc_set_value(today_arc_steps, 0);
    lv_arc_set_bg_angles(today_arc_steps, 90, 315);
    lv_obj_set_style_arc_color(today_arc_steps, lv_palette_darken(LV_PALETTE_GREY,3), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(today_arc_steps, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(today_arc_steps, true, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(today_arc_steps, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_set_style_arc_color(today_arc_steps, lv_palette_darken(LV_PALETTE_RED, 4), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(today_arc_steps, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(today_arc_steps, true, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    lv_obj_set_style_arc_width(today_arc_steps, 15, LV_PART_MAIN);     
    lv_obj_set_style_arc_width(today_arc_steps, 15, LV_PART_INDICATOR); 
    lv_obj_set_style_bg_color(today_arc_steps, lv_color_hex(0xFFFFFF), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(today_arc_steps, 0, LV_PART_KNOB | LV_STATE_DEFAULT);

    today_arc_cals = lv_arc_create(today_group);
    lv_obj_set_size(today_arc_cals, 310, 310);
    lv_obj_set_align(today_arc_cals, LV_ALIGN_CENTER);
    lv_arc_set_value(today_arc_cals, 0);
    lv_arc_set_bg_angles(today_arc_cals, 90, 315);
    lv_obj_set_style_arc_color(today_arc_cals, lv_palette_darken(LV_PALETTE_GREY,3), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(today_arc_cals, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(today_arc_cals, true, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(today_arc_cals, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_set_style_arc_color(today_arc_cals, lv_palette_darken(LV_PALETTE_YELLOW, 3) , LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(today_arc_cals, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(today_arc_cals, true, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    lv_obj_set_style_arc_width(today_arc_cals, 15, LV_PART_MAIN);      
    lv_obj_set_style_arc_width(today_arc_cals, 15, LV_PART_INDICATOR); 

    lv_obj_set_style_bg_color(today_arc_cals, lv_color_hex(0xFFFFFF), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(today_arc_cals, 0, LV_PART_KNOB | LV_STATE_DEFAULT);

    today_arc_active_time = lv_arc_create(today_group);
    lv_obj_set_size(today_arc_active_time, 260, 260);
    lv_obj_set_align(today_arc_active_time, LV_ALIGN_CENTER);
    lv_arc_set_value(today_arc_active_time, 0);
    lv_arc_set_bg_angles(today_arc_active_time, 90, 315);
    lv_obj_set_style_arc_color(today_arc_active_time,lv_palette_darken(LV_PALETTE_GREY,3), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(today_arc_active_time, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(today_arc_active_time, true, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(today_arc_active_time, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_set_style_arc_color(today_arc_active_time, lv_palette_darken(LV_PALETTE_GREEN, 1), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(today_arc_active_time, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(today_arc_active_time, true, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    lv_obj_set_style_arc_width(today_arc_active_time, 15, LV_PART_MAIN);      
    lv_obj_set_style_arc_width(today_arc_active_time, 15, LV_PART_INDICATOR); 

    lv_obj_set_style_bg_color(today_arc_active_time, lv_color_hex(0xFFFFFF), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(today_arc_active_time, 0, LV_PART_KNOB | LV_STATE_DEFAULT);

    lv_obj_t *img_steps = lv_img_create(today_group);
    lv_img_set_src(img_steps, &img_steps_48);
    lv_obj_align_to(img_steps, NULL, LV_ALIGN_CENTER, -25, -40);

    label_today_steps = lv_label_create(today_group);  
    lv_label_set_text(label_today_steps, "0");
    lv_obj_align_to(label_today_steps, img_steps, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
    lv_obj_set_style_text_color(label_today_steps, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label_today_steps, &lv_font_montserrat_34, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(label_today_steps, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    lv_obj_t *lbl_title_steps = lv_label_create(today_group);
    lv_label_set_text_fmt(lbl_title_steps, "/%d", m_steps_today_target);
    lv_obj_set_style_text_font(lbl_title_steps, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align_to(lbl_title_steps, label_today_steps, LV_ALIGN_OUT_RIGHT_MID, 40, 0);

    lv_obj_t *img_cals = lv_img_create(today_group);
    lv_img_set_src(img_cals, &img_calories_48);
    lv_obj_align_to(img_cals, img_steps, LV_ALIGN_OUT_BOTTOM_MID, 4, 5);

    label_today_cals = lv_label_create(today_group);
    lv_label_set_text(label_today_cals, "0");
    lv_obj_align_to(label_today_cals, img_cals, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
    lv_obj_set_style_text_color(label_today_cals, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label_today_cals, &lv_font_montserrat_34, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(label_today_cals, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    lv_obj_t *lbl_title_cals = lv_label_create(today_group);
    lv_label_set_text_fmt(lbl_title_cals, "/%d", m_kcals_today_target);
    lv_obj_set_style_text_font(lbl_title_cals, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align_to(lbl_title_cals, label_today_cals, LV_ALIGN_OUT_RIGHT_MID, 40, 0);

    lv_obj_t *img_time;
    img_time = lv_img_create(today_group);
    lv_img_set_src(img_time, &img_timer_48);
    lv_obj_align_to(img_time, img_cals, LV_ALIGN_OUT_BOTTOM_MID, -3, 5);

    label_today_active_time = lv_label_create(today_group);
    lv_label_set_text(label_today_active_time, "00:00");
    lv_obj_align_to(label_today_active_time, img_time, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
    lv_obj_set_style_text_color(label_today_active_time, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label_today_active_time, &lv_font_montserrat_34, LV_PART_MAIN | LV_STATE_DEFAULT);

    hpi_disp_set_curr_screen(SCR_TODAY);
    hpi_show_screen(scr_today, m_scroll_dir);
}

void hpi_scr_today_update_all(uint16_t steps, uint16_t kcals, uint16_t active_time_s)
{
    if (label_today_steps == NULL || label_today_cals == NULL || label_today_active_time == NULL)
        return;

    lv_label_set_text_fmt(label_today_steps, "%d", steps);
    lv_label_set_text_fmt(label_today_cals, "%d", kcals);

    uint8_t hours = active_time_s / 3600;
    uint8_t minutes = (active_time_s % 3600) / 60;
    lv_label_set_text_fmt(label_today_active_time, "%02d:%02d",hours, minutes);

    lv_arc_set_value(today_arc_steps, (steps * 100) / m_steps_today_target);
    lv_arc_set_value(today_arc_cals, (kcals * 100) / m_kcals_today_target);
    lv_arc_set_value(today_arc_active_time, (active_time_s * 100) / m_active_time_today_target);
}