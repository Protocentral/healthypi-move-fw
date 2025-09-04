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
#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <app_version.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(scr_device_user_settings, LOG_LEVEL_INF);

#include "ui/move_ui.h"
#include "hw_module.h"
#include "hpi_settings_store.h"
#include "sm/smf_ecg_bioz.h"

lv_obj_t *scr_device_user_settings;

// UI elements
static lv_obj_t *btn_user_height;
static lv_obj_t *btn_user_weight;
static lv_obj_t *btn_hand_worn;
static lv_obj_t *btn_time_format;
static lv_obj_t *btn_temp_unit;
static lv_obj_t *sw_auto_sleep;
static lv_obj_t *btn_sleep_timeout;

// External references to user settings (from smf_display.c)
extern uint16_t m_user_height;
extern uint16_t m_user_weight;
extern double m_user_met;

// Settings persistence - we'll sync with the persistence module
static struct hpi_user_settings current_ui_settings;

extern lv_style_t style_scr_black;
extern lv_style_t style_lbl_white_14;

// Function to automatically save settings
static void hpi_auto_save_settings(void)
{
    int rc;
    
    // Sync external height/weight with our settings structure
    current_ui_settings.height = m_user_height;
    current_ui_settings.weight = m_user_weight;
    
    // Save all settings to persistent storage
    rc = hpi_settings_save_all(&current_ui_settings);
    if (rc) {
        LOG_ERR("Failed to save settings: %d", rc);
    } else {
        LOG_INF("Settings automatically saved");
    }
}

// Event callbacks for user settings
static void btn_height_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        LOG_DBG("Height button clicked");
        // Navigate to height selection screen
        hpi_load_scr_spl(SCR_SPL_HEIGHT_SELECT, SCROLL_DOWN, SCR_SPL_DEVICE_USER_SETTINGS, 0, 0, 0);
    }
}

static void btn_weight_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        LOG_DBG("Weight button clicked");
        // Navigate to weight selection screen
        hpi_load_scr_spl(SCR_SPL_WEIGHT_SELECT, SCROLL_DOWN, SCR_SPL_DEVICE_USER_SETTINGS, 0, 0, 0);
    }
}

static void btn_hand_worn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        LOG_DBG("Hand worn button clicked");
        // Navigate to hand worn selection screen
        hpi_load_scr_spl(SCR_SPL_HAND_WORN_SELECT, SCROLL_DOWN, SCR_SPL_DEVICE_USER_SETTINGS, 0, 0, 0);
    }
}

static void btn_time_format_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        LOG_DBG("Time format button clicked");
        // Navigate to time format selection screen
        hpi_load_scr_spl(SCR_SPL_TIME_FORMAT_SELECT, SCROLL_DOWN, SCR_SPL_DEVICE_USER_SETTINGS, 0, 0, 0);
    }
}

static void btn_temp_unit_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        LOG_DBG("Temperature unit button clicked");
        // Navigate to temperature unit selection screen
        hpi_load_scr_spl(SCR_SPL_TEMP_UNIT_SELECT, SCROLL_DOWN, SCR_SPL_DEVICE_USER_SETTINGS, 0, 0, 0);
    }
}

static void sw_auto_sleep_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        bool state = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
        current_ui_settings.auto_sleep_enabled = state;
        LOG_DBG("Auto sleep: %s", state ? "Enabled" : "Disabled");
        
        // Auto-save settings
        hpi_auto_save_settings();
    }
}

static void btn_sleep_timeout_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        LOG_DBG("Sleep timeout button clicked");
        // Navigate to sleep timeout selection screen
        hpi_load_scr_spl(SCR_SPL_SLEEP_TIMEOUT_SELECT, SCROLL_DOWN, SCR_SPL_DEVICE_USER_SETTINGS, 0, 0, 0);
    }
}

// Function to update height and weight button labels
void hpi_update_height_weight_labels(void)
{
    if (btn_user_height) {
        lv_obj_t *lbl_height_value = lv_obj_get_child(btn_user_height, 0);
        if (lbl_height_value) {
            lv_label_set_text_fmt(lbl_height_value, "%d cm", m_user_height);
        }
    }
    
    if (btn_user_weight) {
        lv_obj_t *lbl_weight_value = lv_obj_get_child(btn_user_weight, 0);
        if (lbl_weight_value) {
            lv_label_set_text_fmt(lbl_weight_value, "%d kg", m_user_weight);
        }
    }
}

// Function to update setting button labels
void hpi_update_setting_labels(void)
{
    if (btn_hand_worn) {
        lv_obj_t *lbl_hand_value = lv_obj_get_child(btn_hand_worn, 0);
        if (lbl_hand_value) {
            lv_label_set_text(lbl_hand_value, current_ui_settings.hand_worn ? "Right" : "Left");
        }
    }
    
    if (btn_time_format) {
        lv_obj_t *lbl_time_value = lv_obj_get_child(btn_time_format, 0);
        if (lbl_time_value) {
            lv_label_set_text(lbl_time_value, current_ui_settings.time_format ? "12 H" : "24 H");
        }
    }
    
    if (btn_temp_unit) {
        lv_obj_t *lbl_temp_value = lv_obj_get_child(btn_temp_unit, 0);
        if (lbl_temp_value) {
            lv_label_set_text(lbl_temp_value, current_ui_settings.temp_unit ? "°F" : "°C");
        }
    }
    
    if (btn_sleep_timeout) {
        lv_obj_t *lbl_sleep_timeout_value = lv_obj_get_child(btn_sleep_timeout, 0);
        if (lbl_sleep_timeout_value) {
            lv_label_set_text_fmt(lbl_sleep_timeout_value, "%d s", current_ui_settings.sleep_timeout);
        }
    }
}

void draw_scr_device_user_settings(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    // Initialize settings persistence if needed
    int rc = hpi_settings_store_init();
    if (rc) {
        LOG_ERR("Failed to initialize settings store: %d", rc);
    }
    
    // Load current settings
    rc = hpi_settings_load_all(&current_ui_settings);
    if (rc) {
        LOG_ERR("Failed to load settings: %d", rc);
        // Use defaults if load fails
        current_ui_settings.height = DEFAULT_USER_HEIGHT;
        current_ui_settings.weight = DEFAULT_USER_WEIGHT;
        current_ui_settings.hand_worn = DEFAULT_HAND_WORN;
        current_ui_settings.time_format = DEFAULT_TIME_FORMAT;
        current_ui_settings.temp_unit = DEFAULT_TEMP_UNIT;
        current_ui_settings.auto_sleep_enabled = DEFAULT_AUTO_SLEEP;
        current_ui_settings.sleep_timeout = DEFAULT_SLEEP_TIMEOUT;
        current_ui_settings.backlight_timeout = DEFAULT_BACKLIGHT_TIMEOUT;
        current_ui_settings.raise_to_wake = DEFAULT_RAISE_TO_WAKE;
        current_ui_settings.button_sounds = DEFAULT_BUTTON_SOUNDS;
    }
    
    // Sync external variables with loaded settings
    m_user_height = current_ui_settings.height;
    m_user_weight = current_ui_settings.weight;

    scr_device_user_settings = lv_obj_create(NULL);
    draw_scr_common(scr_device_user_settings);
    
    // Disable scrolling on the main screen
    lv_obj_set_scrollbar_mode(scr_device_user_settings, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(scr_device_user_settings, LV_OBJ_FLAG_SCROLLABLE);

    // Create simplified main container with scroll capability - optimized for 390x390 round display
    lv_obj_t *cont_main = lv_obj_create(scr_device_user_settings);
    lv_obj_set_size(cont_main, 340, 340);
    lv_obj_align_to(cont_main, NULL, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_all(cont_main, 12, 0);
    lv_obj_add_style(cont_main, &style_scr_black, 0);
    lv_obj_set_scrollbar_mode(cont_main, LV_SCROLLBAR_MODE_ON);
    lv_obj_set_style_radius(cont_main, 170, 0); // Make container circular
    lv_obj_set_scroll_dir(cont_main, LV_DIR_VER); // Disable horizontal scrolling

    // Title - centered for round display
    lv_obj_t *lbl_title = lv_label_create(cont_main);
    lv_label_set_text(lbl_title, "Settings");
    lv_obj_add_style(lbl_title, &style_lbl_white_14, 0);
    lv_obj_set_pos(lbl_title, 130, 10);

    int16_t y_pos = 45;
    const int16_t item_height = 80;
    const int16_t label_x = 20;
    const int16_t button_x = 180;

    // Height input - optimized layout for round display
    lv_obj_t *lbl_height = lv_label_create(cont_main);
    lv_label_set_text(lbl_height, "Height:");
    lv_obj_add_style(lbl_height, &style_lbl_white_14, 0);
    lv_obj_set_pos(lbl_height, label_x, y_pos + 18);
    
    btn_user_height = lv_btn_create(cont_main);
    lv_obj_set_size(btn_user_height, 140, 55);
    lv_obj_set_pos(btn_user_height, button_x - 10, y_pos);
    lv_obj_add_event_cb(btn_user_height, btn_height_event_cb, LV_EVENT_ALL, NULL);
    
    lv_obj_t *lbl_height_value = lv_label_create(btn_user_height);
    lv_label_set_text_fmt(lbl_height_value, "%d cm", m_user_height);
    lv_obj_center(lbl_height_value);

    y_pos += item_height;

    // Weight input - optimized layout for round display
    lv_obj_t *lbl_weight = lv_label_create(cont_main);
    lv_label_set_text(lbl_weight, "Weight:");
    lv_obj_add_style(lbl_weight, &style_lbl_white_14, 0);
    lv_obj_set_pos(lbl_weight, label_x, y_pos + 18);
    
    btn_user_weight = lv_btn_create(cont_main);
    lv_obj_set_size(btn_user_weight, 140, 55);
    lv_obj_set_pos(btn_user_weight, button_x - 10, y_pos);
    lv_obj_add_event_cb(btn_user_weight, btn_weight_event_cb, LV_EVENT_ALL, NULL);
    
    lv_obj_t *lbl_weight_value = lv_label_create(btn_user_weight);
    lv_label_set_text_fmt(lbl_weight_value, "%d kg", m_user_weight);
    lv_obj_center(lbl_weight_value);

    y_pos += item_height;

    // Hand worn - optimized layout for round display
    lv_obj_t *lbl_hand_worn = lv_label_create(cont_main);
    lv_label_set_text(lbl_hand_worn, "Worn on:");
    lv_obj_add_style(lbl_hand_worn, &style_lbl_white_14, 0);
    lv_obj_set_pos(lbl_hand_worn, label_x, y_pos + 18);
    
    btn_hand_worn = lv_btn_create(cont_main);
    lv_obj_set_size(btn_hand_worn, 140, 55);
    lv_obj_set_pos(btn_hand_worn, button_x - 10, y_pos);
    lv_obj_add_event_cb(btn_hand_worn, btn_hand_worn_event_cb, LV_EVENT_ALL, NULL);
    
    lv_obj_t *lbl_hand_value = lv_label_create(btn_hand_worn);
    lv_label_set_text(lbl_hand_value, current_ui_settings.hand_worn ? "Right" : "Left");
    lv_obj_center(lbl_hand_value);

    y_pos += item_height;

    // Time format - optimized layout for round display
    lv_obj_t *lbl_time_format = lv_label_create(cont_main);
    lv_label_set_text(lbl_time_format, "Time:");
    lv_obj_add_style(lbl_time_format, &style_lbl_white_14, 0);
    lv_obj_set_pos(lbl_time_format, label_x, y_pos + 18);
    
    btn_time_format = lv_btn_create(cont_main);
    lv_obj_set_size(btn_time_format, 120, 55);
    lv_obj_set_pos(btn_time_format, button_x, y_pos);
    lv_obj_add_event_cb(btn_time_format, btn_time_format_event_cb, LV_EVENT_ALL, NULL);
    
    lv_obj_t *lbl_time_value = lv_label_create(btn_time_format);
    lv_label_set_text(lbl_time_value, current_ui_settings.time_format ? "12 H" : "24 H");
    lv_obj_center(lbl_time_value);

    y_pos += item_height;

    // Temperature unit - optimized layout for round display
    lv_obj_t *lbl_temp_unit = lv_label_create(cont_main);
    lv_label_set_text(lbl_temp_unit, "Temp:");
    lv_obj_add_style(lbl_temp_unit, &style_lbl_white_14, 0);
    lv_obj_set_pos(lbl_temp_unit, label_x, y_pos + 18);
    
    btn_temp_unit = lv_btn_create(cont_main);
    lv_obj_set_size(btn_temp_unit, 120, 55);
    lv_obj_set_pos(btn_temp_unit, button_x, y_pos);
    lv_obj_add_event_cb(btn_temp_unit, btn_temp_unit_event_cb, LV_EVENT_ALL, NULL);
    
    lv_obj_t *lbl_temp_value = lv_label_create(btn_temp_unit);
    lv_label_set_text(lbl_temp_value, current_ui_settings.temp_unit ? "°F" : "°C");
    lv_obj_center(lbl_temp_value);

    y_pos += item_height;

    // Auto sleep switch - optimized layout for round display
    lv_obj_t *lbl_auto_sleep = lv_label_create(cont_main);
    lv_label_set_text(lbl_auto_sleep, "Auto Sleep:");
    lv_obj_add_style(lbl_auto_sleep, &style_lbl_white_14, 0);
    lv_obj_set_pos(lbl_auto_sleep, label_x, y_pos + 18);
    
    sw_auto_sleep = lv_switch_create(cont_main);
    lv_obj_set_size(sw_auto_sleep, 80, 45);
    lv_obj_set_pos(sw_auto_sleep, button_x + 20, y_pos + 8);
    if (current_ui_settings.auto_sleep_enabled) {
        lv_obj_add_state(sw_auto_sleep, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw_auto_sleep, sw_auto_sleep_event_cb, LV_EVENT_ALL, NULL);

    y_pos += item_height;

    // Sleep timeout selection - optimized layout for round display
    lv_obj_t *lbl_sleep_timeout = lv_label_create(cont_main);
    lv_label_set_text(lbl_sleep_timeout, "Sleep Time");
    lv_obj_add_style(lbl_sleep_timeout, &style_lbl_white_14, 0);
    lv_obj_set_pos(lbl_sleep_timeout, label_x, y_pos + 18);
    
    btn_sleep_timeout = lv_btn_create(cont_main);
    lv_obj_set_size(btn_sleep_timeout, 140, 55);
    lv_obj_set_pos(btn_sleep_timeout, button_x - 10, y_pos);
    lv_obj_add_event_cb(btn_sleep_timeout, btn_sleep_timeout_event_cb, LV_EVENT_ALL, NULL);
    
    lv_obj_t *lbl_sleep_timeout_value = lv_label_create(btn_sleep_timeout);
    lv_label_set_text_fmt(lbl_sleep_timeout_value, "%d s", current_ui_settings.sleep_timeout);
    lv_obj_center(lbl_sleep_timeout_value);

    hpi_disp_set_curr_screen(SCR_SPL_DEVICE_USER_SETTINGS);
    hpi_show_screen(scr_device_user_settings, m_scroll_dir);
    
    // Update height and weight labels with current values
    hpi_update_height_weight_labels();
    // Update setting button labels
    hpi_update_setting_labels();
}

void gesture_down_scr_device_user_settings(void)
{
    // Auto-save settings before leaving the screen
    hpi_auto_save_settings();
    
    // Return to main settings screen
    hpi_load_scr_spl(SCR_SPL_PULLDOWN, SCROLL_UP, 0, 0, 0, 0);
}

// Height selection screen with roller widget
static lv_obj_t *scr_height_select;
static lv_obj_t *roller_height;

static void roller_height_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        // Get selected height and update immediately
        uint16_t selected = lv_roller_get_selected(roller_height);
        m_user_height = 100 + selected;
        LOG_DBG("Height changed to: %d cm", m_user_height);
        
        // Auto-save settings
        hpi_auto_save_settings();
    }
}

void draw_scr_height_select(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_height_select = lv_obj_create(NULL);
    draw_scr_common(scr_height_select);

    // Create main container
    lv_obj_t *cont_main = lv_obj_create(scr_height_select);
    lv_obj_set_size(cont_main, 330, 330);
    lv_obj_align_to(cont_main, NULL, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_flex_flow(cont_main, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_main, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(cont_main, 10, 0);
    lv_obj_add_style(cont_main, &style_scr_black, 0);
    lv_obj_set_scroll_dir(cont_main, LV_DIR_VER); // Disable horizontal scrolling

    // Title
    lv_obj_t *lbl_title = lv_label_create(cont_main);
    lv_label_set_text(lbl_title, "Select Height");
    lv_obj_add_style(lbl_title, &style_lbl_white_14, 0);

    // Create height options string (100-250 cm)
    char height_options[3000] = "";  // Large buffer for all options
    for (int i = 100; i <= 250; i++) {
        char temp[10];
        snprintf(temp, sizeof(temp), "%d cm", i);
        strcat(height_options, temp);
        if (i < 250) {
            strcat(height_options, "\n");
        }
    }

    // Height roller
    roller_height = lv_roller_create(cont_main);
    lv_obj_set_size(roller_height, 270, 240);
    lv_roller_set_options(roller_height, height_options, LV_ROLLER_MODE_NORMAL);
    lv_obj_add_event_cb(roller_height, roller_height_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    
    // Set current value (subtract 100 to get index)
    uint16_t current_index = m_user_height - 100;
    if (current_index > 150) current_index = 70; // Default to 170cm if out of range
    lv_roller_set_selected(roller_height, current_index, LV_ANIM_OFF);

    hpi_disp_set_curr_screen(SCR_SPL_HEIGHT_SELECT);
    hpi_show_screen(scr_height_select, m_scroll_dir);
}

void gesture_down_scr_height_select(void)
{
    // Return to device settings screen
    hpi_load_scr_spl(SCR_SPL_DEVICE_USER_SETTINGS, SCROLL_UP, 0, 0, 0, 0);
}

void gesture_right_scr_height_select(void)
{
    // Return to device settings screen
    hpi_load_scr_spl(SCR_SPL_DEVICE_USER_SETTINGS, SCROLL_UP, 0, 0, 0, 0);
}

// Weight selection screen with roller widget
static lv_obj_t *scr_weight_select;
static lv_obj_t *roller_weight;

static void roller_weight_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        // Get selected weight and update immediately
        uint16_t selected = lv_roller_get_selected(roller_weight);
        m_user_weight = 30 + selected;
        LOG_DBG("Weight changed to: %d kg", m_user_weight);
        
        // Auto-save settings
        hpi_auto_save_settings();
    }
}

void draw_scr_weight_select(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_weight_select = lv_obj_create(NULL);
    draw_scr_common(scr_weight_select);

    // Create main container
    lv_obj_t *cont_main = lv_obj_create(scr_weight_select);
    lv_obj_set_size(cont_main, 330, 330);
    lv_obj_align_to(cont_main, NULL, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_flex_flow(cont_main, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_main, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(cont_main, 10, 0);
    lv_obj_add_style(cont_main, &style_scr_black, 0);
    lv_obj_set_scroll_dir(cont_main, LV_DIR_VER); // Disable horizontal scrolling

    // Title
    lv_obj_t *lbl_title = lv_label_create(cont_main);
    lv_label_set_text(lbl_title, "Select Weight");
    lv_obj_add_style(lbl_title, &style_lbl_white_14, 0);

    // Create weight options string (30-200 kg)
    char weight_options[2000] = "";  // Large buffer for all options
    for (int i = 30; i <= 200; i++) {
        char temp[10];
        snprintf(temp, sizeof(temp), "%d kg", i);
        strcat(weight_options, temp);
        if (i < 200) {
            strcat(weight_options, "\n");
        }
    }

    // Weight roller
    roller_weight = lv_roller_create(cont_main);
    lv_obj_set_size(roller_weight, 270, 240);
    lv_roller_set_options(roller_weight, weight_options, LV_ROLLER_MODE_NORMAL);
    lv_obj_add_event_cb(roller_weight, roller_weight_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    
    // Set current value (subtract 30 to get index)
    uint16_t current_index = m_user_weight - 30;
    if (current_index > 170) current_index = 40; // Default to 70kg if out of range
    lv_roller_set_selected(roller_weight, current_index, LV_ANIM_OFF);

    hpi_disp_set_curr_screen(SCR_SPL_WEIGHT_SELECT);
    hpi_show_screen(scr_weight_select, m_scroll_dir);
}

void gesture_down_scr_weight_select(void)
{
    // Return to device settings screen
    hpi_load_scr_spl(SCR_SPL_DEVICE_USER_SETTINGS, SCROLL_UP, 0, 0, 0, 0);
}

void gesture_right_scr_weight_select(void)
{
    // Return to device settings screen
    hpi_load_scr_spl(SCR_SPL_DEVICE_USER_SETTINGS, SCROLL_UP, 0, 0, 0, 0);
    
}

// Hand worn selection screen with roller widget
static lv_obj_t *scr_hand_worn_select;
static lv_obj_t *roller_hand_worn;

static void roller_hand_worn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        // Get selected hand and update immediately
        uint16_t selected = lv_roller_get_selected(roller_hand_worn);
        current_ui_settings.hand_worn = selected;
        LOG_DBG("Hand worn changed to: %s", selected ? "Right hand" : "Left hand");
        
        // Auto-save settings
        hpi_auto_save_settings();
        
        // Reconfigure ECG leads immediately if ECG is active
        reconfigure_ecg_leads_for_hand_worn();
    }
}

void draw_scr_hand_worn_select(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_hand_worn_select = lv_obj_create(NULL);
    draw_scr_common(scr_hand_worn_select);

    // Create main container
    lv_obj_t *cont_main = lv_obj_create(scr_hand_worn_select);
    lv_obj_set_size(cont_main, 330, 330);
    lv_obj_align_to(cont_main, NULL, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_flex_flow(cont_main, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_main, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(cont_main, 10, 0);
    lv_obj_add_style(cont_main, &style_scr_black, 0);
    lv_obj_set_scroll_dir(cont_main, LV_DIR_VER); // Disable horizontal scrolling

    // Title
    lv_obj_t *lbl_title = lv_label_create(cont_main);
    lv_label_set_text(lbl_title, "Watch Worn On");
    lv_obj_add_style(lbl_title, &style_lbl_white_14, 0);

    // Hand worn roller
    roller_hand_worn = lv_roller_create(cont_main);
    lv_obj_set_size(roller_hand_worn, 270, 210);
    lv_roller_set_options(roller_hand_worn, "Left hand\nRight hand", LV_ROLLER_MODE_NORMAL);
    lv_obj_add_event_cb(roller_hand_worn, roller_hand_worn_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    
    // Set current value
    lv_roller_set_selected(roller_hand_worn, current_ui_settings.hand_worn, LV_ANIM_OFF);

    hpi_disp_set_curr_screen(SCR_SPL_HAND_WORN_SELECT);
    hpi_show_screen(scr_hand_worn_select, m_scroll_dir);
}

void gesture_down_scr_hand_worn_select(void)
{
    // Return to device settings screen
    hpi_load_scr_spl(SCR_SPL_DEVICE_USER_SETTINGS, SCROLL_UP, 0, 0, 0, 0);
}

void gesture_right_scr_hand_worn_select(void)
{
    // Return to device settings screen
    hpi_load_scr_spl(SCR_SPL_DEVICE_USER_SETTINGS, SCROLL_UP, 0, 0, 0, 0);
}

// Time format selection screen with roller widget
static lv_obj_t *scr_time_format_select;
static lv_obj_t *roller_time_format;

static void roller_time_format_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        // Get selected time format and update immediately
        uint16_t selected = lv_roller_get_selected(roller_time_format);
        current_ui_settings.time_format = selected;
        LOG_DBG("Time format changed to: %s", selected ? "12 Hours" : "24 Hours");
        
        // Auto-save settings
        hpi_auto_save_settings();
    }
}

void draw_scr_time_format_select(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_time_format_select = lv_obj_create(NULL);
    draw_scr_common(scr_time_format_select);

    // Create main container
    lv_obj_t *cont_main = lv_obj_create(scr_time_format_select);
    lv_obj_set_size(cont_main, 330, 330);
    lv_obj_align_to(cont_main, NULL, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_flex_flow(cont_main, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_main, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(cont_main, 10, 0);
    lv_obj_add_style(cont_main, &style_scr_black, 0);
    lv_obj_set_scroll_dir(cont_main, LV_DIR_VER); // Disable horizontal scrolling

    // Title
    lv_obj_t *lbl_title = lv_label_create(cont_main);
    lv_label_set_text(lbl_title, "Select Time Format");
    lv_obj_add_style(lbl_title, &style_lbl_white_14, 0);

    // Time format roller
    roller_time_format = lv_roller_create(cont_main);
    lv_obj_set_size(roller_time_format, 270, 210);
    lv_roller_set_options(roller_time_format, "24 Hours\n12 Hours", LV_ROLLER_MODE_NORMAL);
    lv_obj_add_event_cb(roller_time_format, roller_time_format_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    
    // Set current value
    lv_roller_set_selected(roller_time_format, current_ui_settings.time_format, LV_ANIM_OFF);

    hpi_disp_set_curr_screen(SCR_SPL_TIME_FORMAT_SELECT);
    hpi_show_screen(scr_time_format_select, m_scroll_dir);
}

void gesture_down_scr_time_format_select(void)
{
    // Return to device settings screen
    hpi_load_scr_spl(SCR_SPL_DEVICE_USER_SETTINGS, SCROLL_UP, 0, 0, 0, 0);
}

void gesture_right_scr_time_format_select(void)
{
    // Return to device settings screen
    hpi_load_scr_spl(SCR_SPL_DEVICE_USER_SETTINGS, SCROLL_UP, 0, 0, 0, 0);
}

// Temperature unit selection screen with roller widget
static lv_obj_t *scr_temp_unit_select;
static lv_obj_t *roller_temp_unit;

static void roller_temp_unit_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        // Get selected temperature unit and update immediately
        uint16_t selected = lv_roller_get_selected(roller_temp_unit);
        current_ui_settings.temp_unit = selected;
        LOG_DBG("Temperature unit changed to: %s", selected ? "Fahrenheit" : "Celsius");
        
        // Auto-save settings
        hpi_auto_save_settings();
    }
}

void draw_scr_temp_unit_select(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_temp_unit_select = lv_obj_create(NULL);
    draw_scr_common(scr_temp_unit_select);

    // Create main container
    lv_obj_t *cont_main = lv_obj_create(scr_temp_unit_select);
    lv_obj_set_size(cont_main, 330, 330);
    lv_obj_align_to(cont_main, NULL, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_flex_flow(cont_main, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_main, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(cont_main, 10, 0);
    lv_obj_add_style(cont_main, &style_scr_black, 0);

    // Title
    lv_obj_t *lbl_title = lv_label_create(cont_main);
    lv_label_set_text(lbl_title, "Select Temperature Unit");
    lv_obj_add_style(lbl_title, &style_lbl_white_14, 0);

    // Temperature unit roller
    roller_temp_unit = lv_roller_create(cont_main);
    lv_obj_set_size(roller_temp_unit, 270, 210);
    lv_roller_set_options(roller_temp_unit, "Celsius\nFahrenheit", LV_ROLLER_MODE_NORMAL);
    lv_obj_add_event_cb(roller_temp_unit, roller_temp_unit_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    
    // Set current value
    lv_roller_set_selected(roller_temp_unit, current_ui_settings.temp_unit, LV_ANIM_OFF);

    hpi_disp_set_curr_screen(SCR_SPL_TEMP_UNIT_SELECT);
    hpi_show_screen(scr_temp_unit_select, m_scroll_dir);
}

void gesture_down_scr_temp_unit_select(void)
{
    // Return to device settings screen
    hpi_load_scr_spl(SCR_SPL_DEVICE_USER_SETTINGS, SCROLL_UP, 0, 0, 0, 0);
}

void gesture_right_scr_temp_unit_select(void)
{
    // Return to device settings screen
    hpi_load_scr_spl(SCR_SPL_DEVICE_USER_SETTINGS, SCROLL_UP, 0, 0, 0, 0);
}

// Sleep timeout selection screen with roller widget
static lv_obj_t *scr_sleep_timeout_select;
static lv_obj_t *roller_sleep_timeout;

// Sleep timeout options array
static const int sleep_timeout_options[] = {10, 15, 30, 45, 60, 90, 120};
static const int sleep_timeout_count = sizeof(sleep_timeout_options) / sizeof(sleep_timeout_options[0]);

static void roller_sleep_timeout_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        // Get selected sleep timeout option and update immediately
        uint16_t selected = lv_roller_get_selected(roller_sleep_timeout);
        if (selected < sleep_timeout_count) {
            current_ui_settings.sleep_timeout = sleep_timeout_options[selected];
            LOG_DBG("Sleep timeout changed to: %d seconds", current_ui_settings.sleep_timeout);
            
            // Auto-save settings
            hpi_auto_save_settings();
        }
    }
}

void draw_scr_sleep_timeout_select(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_sleep_timeout_select = lv_obj_create(NULL);
    draw_scr_common(scr_sleep_timeout_select);

    // Create main container
    lv_obj_t *cont_main = lv_obj_create(scr_sleep_timeout_select);
    lv_obj_set_size(cont_main, 330, 330);
    lv_obj_align_to(cont_main, NULL, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_flex_flow(cont_main, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_main, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(cont_main, 10, 0);
    lv_obj_add_style(cont_main, &style_scr_black, 0);
    lv_obj_set_scroll_dir(cont_main, LV_DIR_VER); // Disable horizontal scrolling

    // Title
    lv_obj_t *lbl_title = lv_label_create(cont_main);
    lv_label_set_text(lbl_title, "Select Sleep Timeout");
    lv_obj_add_style(lbl_title, &style_lbl_white_14, 0);

    // Sleep timeout roller
    roller_sleep_timeout = lv_roller_create(cont_main);
    lv_obj_set_size(roller_sleep_timeout, 270, 210);
    lv_roller_set_options(roller_sleep_timeout, "10 seconds\n15 seconds\n30 seconds\n45 seconds\n60 seconds\n90 seconds\n120 seconds", LV_ROLLER_MODE_NORMAL);
    lv_obj_add_event_cb(roller_sleep_timeout, roller_sleep_timeout_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    
    // Find and set current value
    int current_index = 0;
    for (int i = 0; i < sleep_timeout_count; i++) {
        if (sleep_timeout_options[i] == current_ui_settings.sleep_timeout) {
            current_index = i;
            break;
        }
    }
    lv_roller_set_selected(roller_sleep_timeout, current_index, LV_ANIM_OFF);

    hpi_disp_set_curr_screen(SCR_SPL_SLEEP_TIMEOUT_SELECT);
    hpi_show_screen(scr_sleep_timeout_select, m_scroll_dir);
}

void gesture_down_scr_sleep_timeout_select(void)
{
    // Return to device settings screen
    hpi_load_scr_spl(SCR_SPL_DEVICE_USER_SETTINGS, SCROLL_UP, 0, 0, 0, 0);
}

void gesture_right_scr_sleep_timeout_select(void)
{
    // Return to device settings screen
    hpi_load_scr_spl(SCR_SPL_DEVICE_USER_SETTINGS, SCROLL_UP, 0, 0, 0, 0);
}
