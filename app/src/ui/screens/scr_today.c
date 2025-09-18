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
#include <lvgl.h>
#include <stdio.h>
#include <time.h>

#include "ui/move_ui.h"
#include "hw_module.h"

LOG_MODULE_REGISTER(hpi_disp_scr_today, LOG_LEVEL_DBG);

lv_obj_t *scr_today;

// Modern concentric arc-based progress display for full screen utilization
static lv_obj_t *arc_steps;          // Outer arc for steps
static lv_obj_t *arc_calories;       // Middle arc for calories  
static lv_obj_t *arc_active_time;    // Inner arc for active time

// Metric value labels positioned around the arcs
static lv_obj_t *label_steps_value;
static lv_obj_t *label_calories_value;
static lv_obj_t *label_time_value;

// Central date element (title removed for minimalist design)
static lv_obj_t *label_date_subtitle;

// Icons for metric identification
static lv_obj_t *img_steps_icon;
static lv_obj_t *img_calories_icon;
static lv_obj_t *img_time_icon;

// Removed quick actions for minimalist design

// Target values
static uint16_t m_steps_today_target = 10000;
static uint16_t m_kcals_today_target = 500;
static uint16_t m_active_time_today_target = 30; // 30 minutes

// Removed quick action function prototypes for minimalist design

// Cleanup function to prevent memory issues during screen transitions
void hpi_scr_today_cleanup(void)
{
    LOG_DBG("Cleaning up today screen");
    
    // Reset state - quick actions removed for minimalist design
    
    // Clear object references to prevent dangling pointers during sleep/wake cycles
    scr_today = NULL;
    
    // Arc objects
    arc_steps = NULL;
    arc_calories = NULL;
    arc_active_time = NULL;
    
    // Minimalist value labels (target labels removed)
    label_steps_value = NULL;
    label_calories_value = NULL;
    label_time_value = NULL;
    
    // Date element (title removed for minimalist design)
    label_date_subtitle = NULL;
    
    // Icons for metric identification
    img_steps_icon = NULL;
    img_calories_icon = NULL;
    img_time_icon = NULL;
    
    LOG_DBG("Today screen cleanup completed");
}

// Quick actions removed for minimalist design

// Removed gesture handler - simplified interaction

// Externs
extern lv_style_t style_lbl_white_14;
extern lv_style_t style_body_medium;
extern lv_style_t style_numeric_large;  // For large hero numbers

void draw_scr_today(enum scroll_dir m_scroll_dir)
{
    // Clean up any previous state to prevent crashes
    hpi_scr_today_cleanup();
    
    scr_today = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_today, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);  // Solid black background
    lv_obj_clear_flag(scr_today, LV_OBJ_FLAG_SCROLLABLE);

    // Solid background without transparency effects

    // OPTIMIZED FOR 390x390 CIRCULAR AMOLED DISPLAY - ARC-BASED DESIGN

    // Date moved to bottom of screen
    label_date_subtitle = lv_label_create(scr_today);
    lv_label_set_text(label_date_subtitle, "Sep 18, 2025");
    lv_obj_align(label_date_subtitle, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_text_color(label_date_subtitle, lv_color_hex(0x666666), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_style(label_date_subtitle, &style_lbl_white_14, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_date_subtitle, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // Removed "Today" title for minimalist approach

    // CONCENTRIC ARCS DESIGN - No transparency effects
    
    // Outer Arc - Steps (close to screen edges) - White theme
    arc_steps = lv_arc_create(scr_today);
    lv_obj_set_size(arc_steps, 360, 360);  // Close to screen edges (390px)
    lv_obj_center(arc_steps);
    lv_arc_set_range(arc_steps, 0, 100);
    lv_arc_set_value(arc_steps, 0);
    lv_arc_set_bg_angles(arc_steps, 120, 60);
    // White theme with lighter background
    lv_obj_set_style_arc_color(arc_steps, lv_color_hex(0x404040), LV_PART_MAIN);  // Lighter gray background
    lv_obj_set_style_arc_width(arc_steps, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_steps, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR);  // White indicator
    lv_obj_set_style_arc_width(arc_steps, 12, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(arc_steps, lv_color_black(), LV_PART_KNOB);
    lv_obj_clear_flag(arc_steps, LV_OBJ_FLAG_CLICKABLE);

    // Middle Arc - Calories - Green theme
    arc_calories = lv_arc_create(scr_today);
    lv_obj_set_size(arc_calories, 330, 330);  // 15px gap from outer (360-30=330)
    lv_obj_center(arc_calories);
    lv_arc_set_range(arc_calories, 0, 100);
    lv_arc_set_value(arc_calories, 0);
    lv_arc_set_bg_angles(arc_calories, 120, 60);
    lv_obj_set_style_arc_color(arc_calories, lv_color_hex(0x1A3D1A), LV_PART_MAIN);  // Dark green background
    lv_obj_set_style_arc_width(arc_calories, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_calories, lv_color_hex(COLOR_SUCCESS_GREEN), LV_PART_INDICATOR);  // Green indicator
    lv_obj_set_style_arc_width(arc_calories, 10, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(arc_calories, lv_color_black(), LV_PART_KNOB);
    lv_obj_clear_flag(arc_calories, LV_OBJ_FLAG_CLICKABLE);

    // Inner Arc - Active Time - Blue theme
    arc_active_time = lv_arc_create(scr_today);
    lv_obj_set_size(arc_active_time, 300, 300);  // 15px gap from middle (330-30=300)
    lv_obj_center(arc_active_time);
    lv_arc_set_range(arc_active_time, 0, 100);
    lv_arc_set_value(arc_active_time, 0);
    lv_arc_set_bg_angles(arc_active_time, 120, 60);
    lv_obj_set_style_arc_color(arc_active_time, lv_color_hex(0x1A1A3D), LV_PART_MAIN);  // Dark blue background
    lv_obj_set_style_arc_width(arc_active_time, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_active_time, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_INDICATOR);  // Blue indicator
    lv_obj_set_style_arc_width(arc_active_time, 8, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(arc_active_time, lv_color_black(), LV_PART_KNOB);
    lv_obj_clear_flag(arc_active_time, LV_OBJ_FLAG_CLICKABLE);

    // ORGANIZED CENTER DATA DISPLAY - With identifying icons
    
    // Steps icon and data - Top position - White theme to match arc
    img_steps_icon = lv_img_create(scr_today);
    lv_img_set_src(img_steps_icon, &img_steps_48);
    lv_obj_align(img_steps_icon, LV_ALIGN_CENTER, 0, -70);
    lv_obj_set_style_img_recolor(img_steps_icon, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_img_recolor_opa(img_steps_icon, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    // HERO METRIC: Steps - Large numeric font for prominence
    label_steps_value = lv_label_create(scr_today);
    lv_label_set_text(label_steps_value, "0");
    lv_obj_align(label_steps_value, LV_ALIGN_CENTER, 0, 5);  // Below icon with increased spacing
    lv_obj_set_style_text_color(label_steps_value, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_style(label_steps_value, &style_numeric_large, LV_PART_MAIN);  // Large numeric style for hero
    lv_obj_set_style_text_align(label_steps_value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // Calories icon and data - Bottom left - Green theme to match arc
    img_calories_icon = lv_img_create(scr_today);
    lv_img_set_src(img_calories_icon, &img_calories_48);
    lv_obj_align(img_calories_icon, LV_ALIGN_CENTER, -80, 45);
    lv_obj_set_style_img_recolor(img_calories_icon, lv_color_hex(COLOR_SUCCESS_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_img_recolor_opa(img_calories_icon, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    // SECONDARY METRIC: Calories - Green to match middle arc
    label_calories_value = lv_label_create(scr_today);
    lv_label_set_text(label_calories_value, "0 cal");
    lv_obj_align(label_calories_value, LV_ALIGN_CENTER, -80, 85);  // Increased from 75 to 85 (10px more spacing)
    lv_obj_set_style_text_color(label_calories_value, lv_color_hex(COLOR_SUCCESS_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_style(label_calories_value, &style_lbl_white_14, LV_PART_MAIN);  // Smaller style
    lv_obj_set_style_text_align(label_calories_value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // Time icon and data - Bottom right - Blue theme to match arc
    img_time_icon = lv_img_create(scr_today);
    lv_img_set_src(img_time_icon, &img_timer_48);
    lv_obj_align(img_time_icon, LV_ALIGN_CENTER, 80, 45);
    lv_obj_set_style_img_recolor(img_time_icon, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_img_recolor_opa(img_time_icon, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    // SECONDARY METRIC: Active Time - Blue to match inner arc
    label_time_value = lv_label_create(scr_today);
    lv_label_set_text(label_time_value, "00:00");
    lv_obj_align(label_time_value, LV_ALIGN_CENTER, 80, 85);  // Increased from 75 to 85 (10px more spacing)
    lv_obj_set_style_text_color(label_time_value, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_style(label_time_value, &style_lbl_white_14, LV_PART_MAIN);  // Smaller style
    lv_obj_set_style_text_align(label_time_value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // Quick actions removed for minimalist design

    hpi_disp_set_curr_screen(SCR_TODAY);
    hpi_show_screen(scr_today, m_scroll_dir);
    
    // Update with current data - using default values for initial display
    hpi_scr_today_update_all(0, 0, 0);
}

void hpi_scr_today_update_all(uint16_t steps, uint16_t kcals, uint16_t active_time_s)
{
    // Check for NULL pointers to prevent crashes during sleep/wake cycles
    if (label_steps_value == NULL || label_calories_value == NULL || label_time_value == NULL ||
        arc_steps == NULL || arc_calories == NULL || arc_active_time == NULL)
    {
        LOG_WRN("Today screen objects not initialized, skipping update");
        return;
    }

    // Update Hero Element - Steps (formatted for readability)
    char steps_buf[16];
    if (steps >= 10000) {
        sprintf(steps_buf, "%.1fk", steps / 1000.0);
    } else if (steps >= 1000) {
        sprintf(steps_buf, "%.1fk", steps / 1000.0);
    } else {
        sprintf(steps_buf, "%d", steps);
    }
    lv_label_set_text(label_steps_value, steps_buf);
    
    int steps_percent = (steps * 100) / m_steps_today_target;
    if (steps_percent > 100) steps_percent = 100;
    lv_arc_set_value(arc_steps, steps_percent);  // Arc instead of bar
    
    // Target labels removed for minimalist design

    // Update Secondary Elements - Calories (with "cal" suffix)
    lv_label_set_text_fmt(label_calories_value, "%d cal", kcals);
    
    int calories_percent = (kcals * 100) / m_kcals_today_target;
    if (calories_percent > 100) calories_percent = 100;
    lv_arc_set_value(arc_calories, calories_percent);  // Arc instead of bar
    
    // Target labels removed for minimalist design

    // Update Active Time (formatted as HH:MM)
    uint8_t hours = active_time_s / 3600;
    uint8_t minutes = (active_time_s % 3600) / 60;
    lv_label_set_text_fmt(label_time_value, "%02d:%02d", hours, minutes);
    
    // Convert active_time_s to minutes for comparison with target
    uint16_t active_time_minutes = active_time_s / 60;
    int time_percent = (active_time_minutes * 100) / m_active_time_today_target;
    if (time_percent > 100) time_percent = 100;
    lv_arc_set_value(arc_active_time, time_percent);  // Arc instead of bar
    
    // Target labels removed for minimalist design
}

void hpi_scr_today_update_date(struct tm date)
{
    if (label_date_subtitle == NULL)
        return;

    char date_buf[32];
    char mon_strs[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    sprintf(date_buf, "%s %02d", mon_strs[date.tm_mon], date.tm_mday);
    lv_label_set_text(label_date_subtitle, date_buf);
}