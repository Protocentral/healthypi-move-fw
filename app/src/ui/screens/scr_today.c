/*
 * HealthyPi Move
 * 
 * SPDX-License-Identifier: MIT
 *
 *static lv_obj_t *label_date_subtitle = NULL;

// Delete callback - LVGL calls this when auto-deleting the screen during navigation
static void scr_today_delete_event_cb(lv_event_t *e)
{
    // Mark screen as invalid immediately to block any concurrent update attempts
    screen_is_valid = false;
    
    // CRITICAL: NULL the main screen pointer immediately
    // Child pointers will be handled by draw function, but main pointer needs to be NULLed now
    scr_today = NULL;
    
    // NOTE: We don't NULL child object pointers here because LVGL handles them automatically
    // when it deletes the parent screen. The pointers will become dangling, but that's OK
    // because screen_is_valid==false will prevent any access to them.
}025 Protocentral Electronics
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

lv_obj_t *scr_today = NULL;

// Flag to prevent updates during screen recreation (prevents race conditions)
static volatile bool screen_is_valid = false;

// Modern concentric arc-based progress display for full screen utilization
static lv_obj_t *arc_steps = NULL;          // Outer arc for steps
static lv_obj_t *arc_calories = NULL;       // Middle arc for calories  
static lv_obj_t *arc_active_time = NULL;    // Inner arc for active time

// Metric value labels positioned around the arcs
static lv_obj_t *label_steps_value = NULL;
static lv_obj_t *label_calories_value = NULL;
static lv_obj_t *label_time_value = NULL;

// Central date element (title removed for minimalist design)
static lv_obj_t *label_date_subtitle = NULL;

// Icons for metric identification
static lv_obj_t *img_steps_icon = NULL;
static lv_obj_t *img_calories_icon = NULL;
static lv_obj_t *img_time_icon = NULL;

// Event handler called when LVGL auto-deletes the screen (during navigation away)
// This is CRITICAL to NULL out pointers when screen is deleted by LVGL's auto-delete
static void scr_today_delete_event_cb(lv_event_t *e)
{
    LOG_DBG("Today screen being deleted by LVGL - clearing pointers");
    
    // Mark screen as invalid immediately to block any concurrent update attempts
    screen_is_valid = false;
    
    // DO NOT NULL LVGL object pointers here! LVGL is still using them during deletion.
    // The pointers will become invalid after this callback returns, but LVGL needs
    // them to remain valid during the delete process. Instead, we NULL them at the
    // START of the next draw function, which is the safe time to do so.
}

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
    
    // Mark screen as invalid to block any updates during cleanup
    screen_is_valid = false;
    
    // Reset state - quick actions removed for minimalist design
    
    // Clear object references to prevent dangling pointers during sleep/wake cycles
    // Important: Set scr_today to NULL FIRST before clearing other pointers
    // This ensures the update function sees NULL and returns early
    scr_today = NULL;
    
    // Arc objects - clear after scr_today to prevent race conditions
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
    // Set current screen immediately
    hpi_disp_set_curr_screen(SCR_TODAY);
    
    // Mark this screen as invalid during recreation - CRITICAL: Do this FIRST
    // This prevents update function from running during screen creation
    screen_is_valid = false;
    
    // CRITICAL: NULL all pointers immediately to prevent race conditions
    // This ensures update function can't access partially-deleted objects
    scr_today = NULL;
    arc_steps = NULL;
    arc_calories = NULL;
    arc_active_time = NULL;
    label_steps_value = NULL;
    label_calories_value = NULL;
    label_time_value = NULL;
    label_date_subtitle = NULL;
    img_steps_icon = NULL;
    img_calories_icon = NULL;
    img_time_icon = NULL;
    
    // Now create the new screen
    scr_today = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_today, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);  // Solid black background
    lv_obj_clear_flag(scr_today, LV_OBJ_FLAG_SCROLLABLE);

    // CRITICAL: Register delete event callback to NULL pointers when LVGL auto-deletes the screen
    // This prevents dangling pointers when navigating away from this screen
    lv_obj_add_event_cb(scr_today, scr_today_delete_event_cb, LV_EVENT_DELETE, NULL);

    // Solid background without transparency effects

    // OPTIMIZED FOR 390x390 CIRCULAR AMOLED DISPLAY - ARC-BASED DESIGN

    // Date moved to bottom of screen
    label_date_subtitle = lv_label_create(scr_today);
    lv_label_set_text(label_date_subtitle, "Today");
    lv_obj_align(label_date_subtitle, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_text_color(label_date_subtitle, lv_color_hex(0x666666), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_style(label_date_subtitle, &style_lbl_white_14, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_date_subtitle, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // Removed "Today" title for minimalist approach

    // CONCENTRIC ARCS DESIGN - No transparency effects
    
    // Outer Arc - Steps (close to screen edges) - White theme
    arc_steps = lv_arc_create(scr_today);
    lv_obj_set_size(arc_steps, 380, 380);  // Close to screen edges (390px)
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
    lv_obj_set_size(arc_calories, 350, 350);  // 15px gap from outer (360-30=330)
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
    lv_obj_set_size(arc_active_time, 320, 320);  // 15px gap from middle (330-30=300)
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

    // Show the screen
    hpi_show_screen(scr_today, m_scroll_dir);
    
    // Mark screen as valid AFTER all objects are created and screen is shown
    // Objects are created synchronously, animation happens asynchronously
    screen_is_valid = true;
    
    // Update with current data - using default values for initial display
    hpi_scr_today_update_all(0, 0, 0);
}

void hpi_scr_today_update_all(uint16_t steps, uint16_t kcals, uint16_t active_time_s)
{
    // CRITICAL: Check validity flag first to prevent race conditions during screen recreation
    if (!screen_is_valid) {
        return;
    }
    
    // Check scr_today first as a master validity flag
    // If scr_today is NULL, the screen is being destroyed/recreated, so return immediately
    if (scr_today == NULL) {
        return;
    }
    
    // CRITICAL: Check if the screen object is still valid in LVGL
    // This catches the case where we navigated away and LVGL auto-deleted the screen
    if (!lv_obj_is_valid(scr_today)) {
        screen_is_valid = false;  // Update our flag to match reality
        scr_today = NULL;  // Clear the pointer
        return;
    }
    
    // Check for NULL pointers to prevent crashes during sleep/wake cycles
    // This is a secondary check - if scr_today exists but children don't, we have a problem
    if (label_steps_value == NULL || label_calories_value == NULL || label_time_value == NULL ||
        arc_steps == NULL || arc_calories == NULL || arc_active_time == NULL)
    {
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