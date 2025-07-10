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
#include "hpi_settings_persistence.h"

lv_obj_t *scr_device_user_settings;

// UI elements
static lv_obj_t *btn_user_height;
static lv_obj_t *btn_user_weight;
static lv_obj_t *btn_hand_worn;
static lv_obj_t *btn_time_format;
static lv_obj_t *btn_temp_unit;
static lv_obj_t *sw_auto_sleep;
static lv_obj_t *slider_sleep_timeout;

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

static void slider_sleep_timeout_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    current_ui_settings.sleep_timeout = lv_slider_get_value(slider);
    LOG_DBG("Sleep timeout set to: %d seconds", current_ui_settings.sleep_timeout);
    
    // Auto-save settings
    hpi_auto_save_settings();
}

static void timer_close_msgbox_cb(lv_timer_t *timer)
{
    lv_obj_t *mbox = (lv_obj_t *)timer->user_data;
    lv_msgbox_close(mbox);
    lv_timer_del(timer);
}

static void btn_save_settings_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        LOG_INF("Settings saved");
        // Here you would typically save settings to non-volatile storage
        // For now, just show a confirmation message
        lv_obj_t *mbox = lv_msgbox_create(NULL, "Settings", "Settings saved successfully!", NULL, true);
        lv_obj_center(mbox);
        // Auto-close after 2 seconds
        lv_timer_create(timer_close_msgbox_cb, 2000, mbox);
    }
}

static void btn_reset_settings_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        // Perform factory reset using persistence module
        int rc = hpi_settings_factory_reset();
        if (rc) {
            LOG_ERR("Factory reset failed: %d", rc);
            return;
        }
        
        // Load the reset settings
        rc = hpi_settings_load_all(&current_ui_settings);
        if (rc) {
            LOG_ERR("Failed to load reset settings: %d", rc);
            return;
        }
        
        // Sync external variables
        m_user_height = current_ui_settings.height;
        m_user_weight = current_ui_settings.weight;
        
        // Update UI elements
        hpi_update_setting_labels();
        lv_obj_add_state(sw_auto_sleep, current_ui_settings.auto_sleep_enabled ? LV_STATE_CHECKED : 0);
        if (!current_ui_settings.auto_sleep_enabled) {
            lv_obj_clear_state(sw_auto_sleep, LV_STATE_CHECKED);
        }
        lv_slider_set_value(slider_sleep_timeout, current_ui_settings.sleep_timeout, LV_ANIM_OFF);
        
        LOG_INF("Factory reset completed successfully");
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
            lv_label_set_text(lbl_temp_value, current_ui_settings.temp_unit ? "Fahrenheit" : "Celsius");
        }
    }
}

void draw_scr_device_user_settings(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    // Initialize settings persistence if needed
    int rc = hpi_settings_persistence_init();
    if (rc) {
        LOG_ERR("Failed to initialize settings persistence: %d", rc);
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

    // Create main container with scroll capability
    lv_obj_t *cont_main = lv_obj_create(scr_device_user_settings);
    lv_obj_set_size(cont_main, 300, 400);
    lv_obj_align_to(cont_main, NULL, LV_ALIGN_TOP_MID, 0, 45);
    lv_obj_set_flex_flow(cont_main, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_main, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(cont_main, 10, 0);
    lv_obj_add_style(cont_main, &style_scr_black, 0);
    lv_obj_set_scrollbar_mode(cont_main, LV_SCROLLBAR_MODE_AUTO);

    // Title
    lv_obj_t *lbl_title = lv_label_create(cont_main);
    lv_label_set_text(lbl_title, "Settings");
    lv_obj_add_style(lbl_title, &style_lbl_white_14, 0);

    // Height input
    lv_obj_t *cont_height = lv_obj_create(cont_main);
    lv_obj_set_size(cont_height, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_height, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_height, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    lv_obj_t *lbl_height = lv_label_create(cont_height);
    lv_label_set_text(lbl_height, "Height:");
    lv_obj_add_style(lbl_height, &style_lbl_white_14, 0);
    
    btn_user_height = lv_btn_create(cont_height);
    lv_obj_set_size(btn_user_height, 120, 40);
    lv_obj_add_event_cb(btn_user_height, btn_height_event_cb, LV_EVENT_ALL, NULL);
    
    lv_obj_t *lbl_height_value = lv_label_create(btn_user_height);
    lv_label_set_text_fmt(lbl_height_value, "%d cm", m_user_height);
    lv_obj_center(lbl_height_value);

    // Weight input
    lv_obj_t *cont_weight = lv_obj_create(cont_main);
    lv_obj_set_size(cont_weight, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_weight, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_weight, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    lv_obj_t *lbl_weight = lv_label_create(cont_weight);
    lv_label_set_text(lbl_weight, "Weight:");
    lv_obj_add_style(lbl_weight, &style_lbl_white_14, 0);
    
    btn_user_weight = lv_btn_create(cont_weight);
    lv_obj_set_size(btn_user_weight, 120, 40);
    lv_obj_add_event_cb(btn_user_weight, btn_weight_event_cb, LV_EVENT_ALL, NULL);
    
    lv_obj_t *lbl_weight_value = lv_label_create(btn_user_weight);
    lv_label_set_text_fmt(lbl_weight_value, "%d kg", m_user_weight);
    lv_obj_center(lbl_weight_value);

    // Hand worn dropdown (for ECG measurement)
    lv_obj_t *cont_hand_worn = lv_obj_create(cont_main);
    lv_obj_set_size(cont_hand_worn, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_hand_worn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_hand_worn, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    lv_obj_t *lbl_hand_worn = lv_label_create(cont_hand_worn);
    lv_label_set_text(lbl_hand_worn, "Watch worn on:");
    lv_obj_add_style(lbl_hand_worn, &style_lbl_white_14, 0);
    
    btn_hand_worn = lv_btn_create(cont_hand_worn);
    lv_obj_set_size(btn_hand_worn, 120, 40);
    lv_obj_add_event_cb(btn_hand_worn, btn_hand_worn_event_cb, LV_EVENT_ALL, NULL);
    
    lv_obj_t *lbl_hand_value = lv_label_create(btn_hand_worn);
    lv_label_set_text(lbl_hand_value, current_ui_settings.hand_worn ? "Right" : "Left");
    lv_obj_center(lbl_hand_value);

    // Time format dropdown
    lv_obj_t *cont_time_format = lv_obj_create(cont_main);
    lv_obj_set_size(cont_time_format, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_time_format, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_time_format, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    lv_obj_t *lbl_time_format = lv_label_create(cont_time_format);
    lv_label_set_text(lbl_time_format, "Time Format:");
    lv_obj_add_style(lbl_time_format, &style_lbl_white_14, 0);
    
    btn_time_format = lv_btn_create(cont_time_format);
    lv_obj_set_size(btn_time_format, 100, 40);
    lv_obj_add_event_cb(btn_time_format, btn_time_format_event_cb, LV_EVENT_ALL, NULL);
    
    lv_obj_t *lbl_time_value = lv_label_create(btn_time_format);
    lv_label_set_text(lbl_time_value, current_ui_settings.time_format ? "12 H" : "24 H");
    lv_obj_center(lbl_time_value);

    // Temperature unit dropdown
    lv_obj_t *cont_temp_unit = lv_obj_create(cont_main);
    lv_obj_set_size(cont_temp_unit, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_temp_unit, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_temp_unit, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    lv_obj_t *lbl_temp_unit = lv_label_create(cont_temp_unit);
    lv_label_set_text(lbl_temp_unit, "Temperature:");
    lv_obj_add_style(lbl_temp_unit, &style_lbl_white_14, 0);
    
    btn_temp_unit = lv_btn_create(cont_temp_unit);
    lv_obj_set_size(btn_temp_unit, 100, 40);
    lv_obj_add_event_cb(btn_temp_unit, btn_temp_unit_event_cb, LV_EVENT_ALL, NULL);
    
    lv_obj_t *lbl_temp_value = lv_label_create(btn_temp_unit);
    lv_label_set_text(lbl_temp_value, current_ui_settings.temp_unit ? "Fahrenheit" : "Celsius");
    lv_obj_center(lbl_temp_value);

    // Auto sleep switch
    lv_obj_t *cont_auto_sleep = lv_obj_create(cont_main);
    lv_obj_set_size(cont_auto_sleep, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_auto_sleep, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_auto_sleep, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    lv_obj_t *lbl_auto_sleep = lv_label_create(cont_auto_sleep);
    lv_label_set_text(lbl_auto_sleep, "Auto Sleep:");
    lv_obj_add_style(lbl_auto_sleep, &style_lbl_white_14, 0);
    
    sw_auto_sleep = lv_switch_create(cont_auto_sleep);
    if (current_ui_settings.auto_sleep_enabled) {
        lv_obj_add_state(sw_auto_sleep, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw_auto_sleep, sw_auto_sleep_event_cb, LV_EVENT_ALL, NULL);

    // Sleep timeout slider
    lv_obj_t *cont_sleep_timeout = lv_obj_create(cont_main);
    lv_obj_set_size(cont_sleep_timeout, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_sleep_timeout, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_sleep_timeout, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl_sleep_timeout = lv_label_create(cont_sleep_timeout);
    lv_label_set_text(lbl_sleep_timeout, "Sleep Timeout (sec):");
    lv_obj_add_style(lbl_sleep_timeout, &style_lbl_white_14, 0);

    slider_sleep_timeout = lv_slider_create(cont_sleep_timeout);
    lv_obj_set_size(slider_sleep_timeout, LV_PCT(100), 20);
    lv_slider_set_range(slider_sleep_timeout, 10, 120);
    lv_slider_set_value(slider_sleep_timeout, current_ui_settings.sleep_timeout, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider_sleep_timeout, slider_sleep_timeout_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Factory Reset line item
    lv_obj_t *cont_factory_reset = lv_obj_create(cont_main);
    lv_obj_set_size(cont_factory_reset, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_factory_reset, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_factory_reset, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    lv_obj_t *lbl_factory_reset = lv_label_create(cont_factory_reset);
    lv_label_set_text(lbl_factory_reset, "Factory Reset:");
    lv_obj_add_style(lbl_factory_reset, &style_lbl_white_14, 0);
    
    lv_obj_t *btn_factory_reset = lv_btn_create(cont_factory_reset);
    lv_obj_set_size(btn_factory_reset, 120, 40);
    lv_obj_add_event_cb(btn_factory_reset, btn_reset_settings_event_cb, LV_EVENT_ALL, NULL);
    
    lv_obj_t *lbl_btn_factory_reset = lv_label_create(btn_factory_reset);
    lv_label_set_text(lbl_btn_factory_reset, LV_SYMBOL_REFRESH " Reset");
    lv_obj_center(lbl_btn_factory_reset);

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
