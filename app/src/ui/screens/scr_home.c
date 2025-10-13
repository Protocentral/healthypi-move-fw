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
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
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
#include <zephyr/logging/log.h>
#include <zephyr/drivers/rtc.h>
#include <time.h>

#include "ui/move_ui.h"
#include "hw_module.h"
#include "hpi_user_settings_api.h"

LOG_MODULE_REGISTER(hpi_disp_scr_home, LOG_LEVEL_DBG);

lv_obj_t *scr_home = NULL;

static lv_obj_t *home_step_disp = NULL;
static lv_obj_t *home_hr_disp = NULL;

// Modern arc-based health metrics
static lv_obj_t *arc_steps = NULL;
static lv_obj_t *arc_hr = NULL;
static lv_obj_t *label_steps_value = NULL;
static lv_obj_t *label_hr_value = NULL;

// Contextual quick actions
static lv_obj_t *quick_actions_container = NULL;
static lv_obj_t *btn_quick_hr = NULL;
static lv_obj_t *btn_quick_steps = NULL;
static bool quick_actions_visible = false;
static lv_timer_t *quick_actions_timer = NULL;

static lv_obj_t *ui_home_label_hour = NULL;
static lv_obj_t *ui_home_label_min = NULL;
static lv_obj_t *ui_home_label_date = NULL;
static lv_obj_t *ui_home_label_ampm = NULL;
static lv_obj_t *label_batt_level_val = NULL;

// LVGL delete event callback - called when LVGL auto-deletes this screen
static void scr_home_delete_event_cb(lv_event_t *e)
{
    // Cancel any active timer
    if (quick_actions_timer != NULL) {
        lv_timer_del(quick_actions_timer);
        quick_actions_timer = NULL;
    }
    
    // Reset state
    quick_actions_visible = false;
    
    // CRITICAL: NULL the main screen pointer immediately  
    // This allows the update function check (scr_home != NULL) to work correctly
    scr_home = NULL;
    
    // NOTE: We don't NULL child object pointers here because LVGL handles them automatically
    // when it deletes the parent screen. They'll become dangling but that's OK because
    // the update functions check (scr_home != NULL || child == NULL) before using them.
}

// Cleanup function to prevent memory issues during screen transitions
void hpi_scr_home_cleanup(void)
{
    // Just call the delete callback directly
    scr_home_delete_event_cb(NULL);
}

// Function prototypes
static void format_time_for_display(struct tm in_time, char *time_buf, char *ampm_buf);
static void quick_action_hr_clicked(lv_event_t *e);
static void quick_action_steps_clicked(lv_event_t *e);
static void hide_quick_actions_timer_cb(lv_timer_t *timer);
static void show_quick_actions(void);
static void home_screen_gesture_handler(lv_event_t *e);
void hpi_scr_home_cleanup(void);

static void format_time_for_display(struct tm in_time, char *time_buf, char *ampm_buf)
{
    uint8_t time_format = hpi_user_settings_get_time_format();
    
    if (time_format == 0) {
        // 24-hour format
        sprintf(time_buf, "%02d:%02d", in_time.tm_hour, in_time.tm_min);
        if (ampm_buf) {
            ampm_buf[0] = '\0'; // Empty string for 24-hour format
        }
    } else {
        // 12-hour format
        int hour_12 = in_time.tm_hour;
        bool is_pm = false;
        
        if (hour_12 == 0) {
            hour_12 = 12; // 12 AM
        } else if (hour_12 > 12) {
            hour_12 -= 12; // Convert to 12-hour format
            is_pm = true;
        } else if (hour_12 == 12) {
            is_pm = true; // 12 PM
        }
        
        sprintf(time_buf, "%d:%02d", hour_12, in_time.tm_min);
        if (ampm_buf) {
            sprintf(ampm_buf, "%s", is_pm ? "PM" : "AM");
        }
    }
}

// Event handlers for contextual quick actions
static void quick_action_hr_clicked(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        // Hide quick actions first to prevent conflicts
        if (quick_actions_container != NULL) {
            lv_obj_add_flag(quick_actions_container, LV_OBJ_FLAG_HIDDEN);
            quick_actions_visible = false;
        }
        // Navigate to heart rate screen
        hpi_load_screen(SCR_HR, SCROLL_LEFT);
    }
}

static void quick_action_steps_clicked(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        // Hide quick actions first to prevent conflicts
        if (quick_actions_container != NULL) {
            lv_obj_add_flag(quick_actions_container, LV_OBJ_FLAG_HIDDEN);
            quick_actions_visible = false;
        }
        // Navigate to steps screen
        hpi_load_screen(SCR_TODAY, SCROLL_LEFT);
    }
}

static void hide_quick_actions_timer_cb(lv_timer_t *timer)
{
    if (timer != NULL && quick_actions_container != NULL && lv_obj_is_valid(quick_actions_container)) {
        lv_obj_add_flag(quick_actions_container, LV_OBJ_FLAG_HIDDEN);
        quick_actions_visible = false;
    }
    
    // Clear timer reference
    quick_actions_timer = NULL;
    
    if (timer != NULL) {
        lv_timer_del(timer);
    }
}

static void show_quick_actions(void)
{
    if (quick_actions_container != NULL && lv_obj_is_valid(quick_actions_container) && !quick_actions_visible) {
        lv_obj_clear_flag(quick_actions_container, LV_OBJ_FLAG_HIDDEN);
        quick_actions_visible = true;
        
        // Cancel existing timer if any
        if (quick_actions_timer != NULL) {
            lv_timer_del(quick_actions_timer);
        }
        
        // Auto-hide after 3 seconds
        quick_actions_timer = lv_timer_create(hide_quick_actions_timer_cb, 3000, NULL);
        if (quick_actions_timer != NULL) {
            lv_timer_set_repeat_count(quick_actions_timer, 1);
        }
    }
}

static void home_screen_gesture_handler(lv_event_t *e)
{
    if (e == NULL) return;
    
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_GESTURE) {
        lv_indev_t *indev = lv_indev_get_act();
        if (indev != NULL) {
            lv_dir_t dir = lv_indev_get_gesture_dir(indev);
            if (dir == LV_DIR_TOP) {
                // Upward swipe shows quick actions
                show_quick_actions();
            }
        }
    } else if (code == LV_EVENT_CLICKED) {
        // Tap also shows quick actions
        show_quick_actions();
    }
}

// Externs
extern lv_style_t style_lbl_white_14;
extern lv_style_t style_body_medium;
extern lv_style_t style_numeric_large;

void draw_scr_home(enum scroll_dir m_scroll_dir)
{
    // Set current screen immediately
    hpi_disp_set_curr_screen(SCR_HOME);
    
    // Cancel any active quick actions timer from previous instance
    if (quick_actions_timer != NULL) {
        lv_timer_del(quick_actions_timer);
        quick_actions_timer = NULL;
    }
    quick_actions_visible = false;
    
    // NULL all object pointers at the start of draw to prevent use of stale/dangling pointers
    // LVGL auto-delete will handle deletion, we just need to ensure our pointers are clean
    scr_home = NULL;
    arc_steps = NULL;
    arc_hr = NULL;
    label_steps_value = NULL;
    label_hr_value = NULL;
    quick_actions_container = NULL;
    btn_quick_hr = NULL;
    btn_quick_steps = NULL;
    ui_home_label_hour = NULL;
    ui_home_label_min = NULL;
    ui_home_label_date = NULL;
    ui_home_label_ampm = NULL;
    label_batt_level_val = NULL;
    home_step_disp = NULL;
    home_hr_disp = NULL;
    
    scr_home = lv_obj_create(NULL);

    lv_obj_set_style_bg_color(scr_home, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_home, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    draw_bg(scr_home);

    // CIRCULAR DISPLAY OPTIMIZED LAYOUT - 390x390 round AMOLED

    // Health metrics arcs - positioned for circular display visibility
    
    // Steps arc (left arc, 8-10:30 o'clock position with gap)
    arc_steps = lv_arc_create(scr_home);
    lv_obj_set_size(arc_steps, 370, 370);  // Smaller radius to fit circle
    lv_obj_center(arc_steps);
    lv_arc_set_range(arc_steps, 0, 10000); // 10k step goal
    lv_arc_set_value(arc_steps, 0); // Start at 0, will be updated by ZBus data
    // LVGL 9 API: Use lv_arc_set_bg_angles to set the background arc range
    lv_arc_set_bg_angles(arc_steps, 180, 270); // Left side arc (6-9 o'clock) - clear separation
    // Use modern arc styling
    lv_obj_set_style_arc_color(arc_steps, lv_color_hex(COLOR_WARNING_AMBER), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc_steps, 6, LV_PART_INDICATOR);  // Thinner, more elegant
    lv_obj_add_style(arc_steps, &style_health_arc_bg, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc_steps, true, LV_PART_INDICATOR | LV_PART_MAIN);
    lv_obj_remove_style(arc_steps, NULL, LV_PART_KNOB); // Remove knob
    lv_obj_clear_flag(arc_steps, LV_OBJ_FLAG_CLICKABLE); // Make non-interactive

    // Steps icon - positioned at the geometric center of the left arc (225°)
    lv_obj_t *img_steps = lv_img_create(scr_home);
    lv_img_set_src(img_steps, &img_steps_48);
    lv_obj_align(img_steps, LV_ALIGN_CENTER, -95, -95);  // Closer to center at 225° angle
    lv_obj_set_style_img_recolor(img_steps, lv_color_hex(COLOR_WARNING_AMBER), LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(img_steps, LV_OPA_COVER, LV_PART_MAIN);

    // Steps value label - positioned below icon at arc center
    label_steps_value = lv_label_create(scr_home);
    lv_obj_align(label_steps_value, LV_ALIGN_CENTER, -95, -50);  // Below icon, closer to center
    lv_label_set_text(label_steps_value, "0"); // Start at 0, will be updated by ZBus data
    lv_obj_set_style_text_color(label_steps_value, lv_color_hex(COLOR_WARNING_AMBER), LV_PART_MAIN);
    lv_obj_add_style(label_steps_value, &style_body_medium, LV_PART_MAIN);  // Use 24px font style for better readability
    lv_obj_set_style_text_align(label_steps_value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // Heart rate arc (right arc, 1:30-4 o'clock position with gap)
    arc_hr = lv_arc_create(scr_home);
    lv_obj_set_size(arc_hr, 370, 370);  // Same size as steps arc
    lv_obj_center(arc_hr);
    lv_arc_set_range(arc_hr, 40, 180); // HR range
    lv_arc_set_value(arc_hr, 70); // Default to resting HR position, will be updated by ZBus data
    // LVGL 9 API: Use lv_arc_set_bg_angles to set the background arc range
    lv_arc_set_bg_angles(arc_hr, 0, 90); // Right side arc (12-3 o'clock) - clear separation  
    // Use modern arc styling
    lv_obj_set_style_arc_color(arc_hr, lv_color_hex(COLOR_CRITICAL_RED), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc_hr, 6, LV_PART_INDICATOR); // Thinner, more elegant
    lv_obj_add_style(arc_hr, &style_health_arc_bg, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc_hr, true, LV_PART_INDICATOR | LV_PART_MAIN);
    lv_obj_remove_style(arc_hr, NULL, LV_PART_KNOB); // Remove knob
    lv_obj_clear_flag(arc_hr, LV_OBJ_FLAG_CLICKABLE); // Make non-interactive

    // Heart rate icon - positioned at the geometric center of the right arc (45°)
    lv_obj_t *img_hr = lv_img_create(scr_home);
    lv_img_set_src(img_hr, &img_heart_48px);  // Use 48px heart icon
    lv_obj_align(img_hr, LV_ALIGN_CENTER, 105, 75);  // Closer to center at 45° angle
    lv_obj_set_style_img_recolor(img_hr, lv_color_hex(COLOR_CRITICAL_RED), LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(img_hr, LV_OPA_COVER, LV_PART_MAIN);

    // Heart rate value label - positioned below icon at arc center
    label_hr_value = lv_label_create(scr_home);
    lv_obj_align(label_hr_value, LV_ALIGN_CENTER, 105, 115);  // Below icon, closer to center
    lv_label_set_text(label_hr_value, "--"); // Start with placeholder, will be updated by ZBus data
    lv_obj_set_style_text_color(label_hr_value, lv_color_hex(COLOR_CRITICAL_RED), LV_PART_MAIN);
    lv_obj_add_style(label_hr_value, &style_body_medium, LV_PART_MAIN);  // Use 24px font style for better readability
    lv_obj_set_style_text_align(label_hr_value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // Keep legacy button variables for compatibility
    home_step_disp = label_steps_value;
    home_hr_disp = label_hr_value;

    // Contextual quick actions - circular bottom area
    quick_actions_container = lv_obj_create(scr_home);
    lv_obj_set_size(quick_actions_container, 260, 50);  // Narrower for circular display
    lv_obj_align(quick_actions_container, LV_ALIGN_CENTER, 0, 110);  // Bottom area within circle
    lv_obj_set_style_bg_color(quick_actions_container, lv_color_hex(COLOR_SURFACE_DARK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(quick_actions_container, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_radius(quick_actions_container, 25, LV_PART_MAIN);
    lv_obj_set_style_border_opa(quick_actions_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(quick_actions_container, 6, LV_PART_MAIN);
    lv_obj_add_flag(quick_actions_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(quick_actions_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(quick_actions_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(quick_actions_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Quick action buttons - modern secondary button styling
    btn_quick_hr = hpi_btn_create_secondary(quick_actions_container);
    lv_obj_set_size(btn_quick_hr, 100, 38);
    lv_obj_set_style_bg_color(btn_quick_hr, lv_color_hex(COLOR_CRITICAL_RED), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_quick_hr, LV_OPA_20, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_quick_hr, quick_action_hr_clicked, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *lbl_hr = lv_label_create(btn_quick_hr);
    lv_label_set_text(lbl_hr, "HR");
    lv_obj_add_style(lbl_hr, &style_body_medium, LV_PART_MAIN);
    lv_obj_center(lbl_hr);

    btn_quick_steps = hpi_btn_create_secondary(quick_actions_container);
    lv_obj_set_size(btn_quick_steps, 100, 38);
    lv_obj_set_style_bg_color(btn_quick_steps, lv_color_hex(COLOR_WARNING_AMBER), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_quick_steps, LV_OPA_20, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_quick_steps, quick_action_steps_clicked, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *lbl_steps = lv_label_create(btn_quick_steps);
    lv_label_set_text(lbl_steps, "Steps");
    lv_obj_add_style(lbl_steps, &style_body_medium, LV_PART_MAIN);
    lv_obj_center(lbl_steps);

    // Gesture handler - DISABLED to prevent quick actions from appearing
    // lv_obj_add_event_cb(scr_home, home_screen_gesture_handler, LV_EVENT_GESTURE, NULL);
    // lv_obj_add_event_cb(scr_home, home_screen_gesture_handler, LV_EVENT_CLICKED, NULL);

    // CENTERED TIME DISPLAY - optimized for circular watchface
    ui_home_label_hour = lv_label_create(scr_home);
    lv_obj_set_width(ui_home_label_hour, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_home_label_hour, LV_SIZE_CONTENT);
    lv_obj_align(ui_home_label_hour, LV_ALIGN_CENTER, 0, 10);  // Moved down to avoid overlap with steps text
    lv_label_set_text(ui_home_label_hour, "00:00");
    lv_obj_set_style_text_color(ui_home_label_hour, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_home_label_hour, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_style(ui_home_label_hour, &style_numeric_large, LV_PART_MAIN | LV_STATE_DEFAULT);  // Large numeric style for time display
    lv_obj_set_style_text_align(ui_home_label_hour, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    // AM/PM indicator - positioned carefully within circular bounds
    ui_home_label_ampm = lv_label_create(scr_home);
    lv_obj_align(ui_home_label_ampm, LV_ALIGN_CENTER, 0, 45);  // Closer to time, reduced spacing
    lv_label_set_text(ui_home_label_ampm, "");
    lv_obj_set_style_text_color(ui_home_label_ampm, lv_color_hex(0x999999), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_home_label_ampm, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_style(ui_home_label_ampm, &style_body_medium, LV_PART_MAIN);  // Use consistent style
    lv_obj_set_style_text_align(ui_home_label_ampm, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Hidden minutes label for compatibility
    ui_home_label_min = lv_label_create(scr_home);
    lv_obj_add_flag(ui_home_label_min, LV_OBJ_FLAG_HIDDEN);

    // Date display - positioned in lower center within circular bounds
    ui_home_label_date = lv_label_create(scr_home);
    lv_obj_set_width(ui_home_label_date, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_home_label_date, LV_SIZE_CONTENT);
    lv_label_set_text(ui_home_label_date, "-- --- ----");
    lv_obj_align(ui_home_label_date, LV_ALIGN_CENTER, 0, 70);  // Closer to AM/PM, reduced spacing
    lv_obj_set_style_text_color(ui_home_label_date, lv_color_hex(0xBBBBBB), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_home_label_date, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_style(ui_home_label_date, &style_body_medium, LV_PART_MAIN);  // Use consistent style
    lv_obj_set_style_text_align(ui_home_label_date, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Battery indicator - positioned at 12 o'clock within circular bounds
    label_batt_level_val = lv_label_create(scr_home);
    lv_label_set_text(label_batt_level_val, LV_SYMBOL_BATTERY_FULL " 85%");  // Battery symbol with percentage
    lv_obj_align(label_batt_level_val, LV_ALIGN_CENTER, 0, -140);  // Top center, within circle
    lv_obj_set_style_text_color(label_batt_level_val, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label_batt_level_val, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);  // Use readable 20pt font
    lv_obj_set_style_text_opa(label_batt_level_val, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(label_batt_level_val, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Register delete callback to cleanup pointers when LVGL auto-deletes this screen
    lv_obj_add_event_cb(scr_home, scr_home_delete_event_cb, LV_EVENT_DELETE, NULL);

    // Show the screen
    hpi_show_screen(scr_home, m_scroll_dir);
}

void hpi_scr_home_update_time_date(struct tm in_time)
{
    if (ui_home_label_hour == NULL || ui_home_label_date == NULL)
        return;

    char time_buf[16];  // Increased buffer size
    char ampm_buf[4];   // Increased buffer size
    char date_buf[32];

    char mon_strs[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    // Format unified time display according to user's 12/24 hour preference
    format_time_for_display(in_time, time_buf, ampm_buf);
    
    lv_label_set_text(ui_home_label_hour, time_buf);
    
    // Update AM/PM label if it exists
    if (ui_home_label_ampm != NULL) {
        lv_label_set_text(ui_home_label_ampm, ampm_buf);
    }

    sprintf(date_buf, "%02d %s %04d", in_time.tm_mday, mon_strs[in_time.tm_mon], in_time.tm_year + 1900);
    lv_label_set_text(ui_home_label_date, date_buf);
}

void hpi_home_hr_update(int hr)
{
    // printk("HR Update : %d\n", hr);
    if (label_hr_value == NULL || arc_hr == NULL)
        return;

    char buf[12]; // Increased buffer size to prevent overflow
    if (hr > 0) {
        sprintf(buf, "%d", hr);
        // Update arc value based on heart rate (40-180 range)
        int arc_value = hr;
        if (arc_value < 40) arc_value = 40;
        if (arc_value > 180) arc_value = 180;
        lv_arc_set_value(arc_hr, arc_value);
    } else {
        sprintf(buf, "--");
        lv_arc_set_value(arc_hr, 70); // Default to resting HR position
    }
    lv_label_set_text(label_hr_value, buf);
}

void hpi_home_steps_update(int steps)
{
    // printk("Steps Update : %d\n", steps);
    if (label_steps_value == NULL || arc_steps == NULL)
        return;

    char buf[16]; // Increased buffer size to prevent overflow
    if (steps >= 1000) {
        sprintf(buf, "%.1fk", steps / 1000.0);
    } else {
        sprintf(buf, "%d", steps);
    }
    
    // Update arc value based on steps (0-10000 range)
    int arc_value = steps;
    if (arc_value > 10000) arc_value = 10000;
    lv_arc_set_value(arc_steps, arc_value);
    
    lv_label_set_text(label_steps_value, buf);
}

void hpi_disp_home_update_batt_level(int batt_level, bool charging)
{
    if (label_batt_level_val == NULL)
    {
        return;
    }

    if (batt_level < 0)
    {
        batt_level = 0;
    }

    // Battery display with LVGL built-in symbols based on level and charging status
    const char* battery_symbol;
    
    if (charging) {
        battery_symbol = LV_SYMBOL_CHARGE; // Lightning bolt for charging
    } else {
        // Different battery symbols based on charge level using LVGL built-in symbols
        if (batt_level >= 90) {
            battery_symbol = LV_SYMBOL_BATTERY_FULL; // Full battery (90-100%)
        } else if (batt_level >= 65) {
            battery_symbol = LV_SYMBOL_BATTERY_3;    // 3/4 battery (65-89%)
        } else if (batt_level >= 35) {
            battery_symbol = LV_SYMBOL_BATTERY_2;    // 2/4 battery (35-64%)
        } else if (batt_level >= 15) {
            battery_symbol = LV_SYMBOL_BATTERY_1;    // 1/4 battery (15-34%)
        } else {
            battery_symbol = LV_SYMBOL_BATTERY_EMPTY; // Empty battery (0-14%)
        }
    }
    
    lv_label_set_text_fmt(label_batt_level_val, "%s %d%%", battery_symbol, batt_level);
    
    // Color coding for battery levels - brighter colors for AMOLED visibility
    if (charging) {
        lv_obj_set_style_text_color(label_batt_level_val, lv_color_hex(0x66FF66), LV_PART_MAIN);  // Bright green when charging
    } else if (batt_level <= 15) {
        lv_obj_set_style_text_color(label_batt_level_val, lv_color_hex(0xFF6666), LV_PART_MAIN);  // Brighter red
    } else if (batt_level <= 30) {
        lv_obj_set_style_text_color(label_batt_level_val, lv_color_hex(0xFFBB66), LV_PART_MAIN);  // Brighter orange
    } else {
        lv_obj_set_style_text_color(label_batt_level_val, lv_color_hex(0xFFFFFF), LV_PART_MAIN);  // Bright white for normal levels
    }
}