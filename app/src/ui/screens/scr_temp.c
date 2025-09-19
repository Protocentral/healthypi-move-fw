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
#include "hpi_user_settings_api.h"

LOG_MODULE_REGISTER(scr_temp, LOG_LEVEL_DBG);

// GUI Elements
static lv_obj_t *scr_temp;
static lv_obj_t *label_temp_value;      // Main temperature display value
static lv_obj_t *btn_temp_unit;         // Unit toggle button
static lv_obj_t *arc_temp_zone;         // Temperature progress arc

// Externs - Modern style system
extern lv_style_t style_body_medium;
extern lv_style_t style_numeric_large;
extern lv_style_t style_caption;

// Forward declarations
static void scr_temp_unit_btn_event_handler(lv_event_t *e);

/**
 * @brief Convert temperature from Celsius to Fahrenheit
 * @param temp_c Temperature in Celsius 
 * @return Temperature in Fahrenheit
 */
static float temp_c_to_f(float temp_c)
{
    return (temp_c * 9.0f / 5.0f) + 32.0f;
}

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_white_medium;
extern lv_style_t style_white_large_numeric;

extern lv_style_t style_bg_purple;

/**
 * @brief Convert temperature from Fahrenheit to Celsius
 * @param temp_f Temperature in Fahrenheit (raw value * 100)
 * @return Temperature in Celsius (raw value * 100)
 */
static uint16_t convert_f_to_c(uint16_t temp_f)
{
    // Convert from raw F to raw C: C = (F - 32) * 5/9
    // Since temp_f is already * 100, we handle integer arithmetic
    // Formula: C_raw = (F_raw - 3200) * 5 / 9
    if (temp_f < 3200) {
        // Handle temperatures below freezing point
        return 0;
    }
    
    uint32_t temp_f_minus_32 = temp_f - 3200; // Subtract 32*100
    uint32_t temp_c_raw = (temp_f_minus_32 * 5) / 9;
    uint16_t result = (uint16_t)temp_c_raw;
    
    LOG_DBG("Temperature conversion: %d.%d°F -> %d.%d°C (raw: %d -> %d)", 
            temp_f/100, (temp_f%100)/10, result/100, (result%100)/10, temp_f, result);
    
    return result;
}

/**
 * @brief Get formatted temperature string and unit based on user setting
 * @param temp_f Temperature in Fahrenheit (raw value * 100)
 * @param temp_str Output buffer for temperature string
 * @param temp_str_size Size of temperature string buffer
 * @param unit_str Output buffer for unit string
 * @param unit_str_size Size of unit string buffer
 */
static void get_formatted_temperature(uint16_t temp_f, char *temp_str, size_t temp_str_size, 
                                     char *unit_str, size_t unit_str_size)
{
    uint8_t temp_unit = hpi_user_settings_get_temp_unit();
    
    if (temp_f == 0) {
        snprintf(temp_str, temp_str_size, "--");
        snprintf(unit_str, unit_str_size, "°C"); // Default unit when no data
    } else {
        if (temp_unit == 0) {
            // Celsius - convert from stored Fahrenheit
            uint16_t temp_c = convert_f_to_c(temp_f);
            // Format as integer with decimal: e.g., 2456 becomes "24.5"
            int whole = temp_c / 100;
            int decimal = (temp_c % 100) / 10; // Only show 1 decimal place
            snprintf(temp_str, temp_str_size, "%d.%d", whole, decimal);
            snprintf(unit_str, unit_str_size, "°C");
        } else {
            // Fahrenheit - use stored value directly
            // Format as integer with decimal: e.g., 9860 becomes "98.6"
            int whole = temp_f / 100;
            int decimal = (temp_f % 100) / 10; // Only show 1 decimal place
            snprintf(temp_str, temp_str_size, "%d.%d", whole, decimal);
            snprintf(unit_str, unit_str_size, "°F");
        }
    }
}

void draw_scr_temp(enum scroll_dir m_scroll_dir)
{
    scr_temp = lv_obj_create(NULL);
    // AMOLED OPTIMIZATION: Pure black background for power efficiency
    lv_obj_set_style_bg_color(scr_temp, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_temp, LV_OBJ_FLAG_SCROLLABLE);

    // CIRCULAR AMOLED-OPTIMIZED TEMPERATURE SCREEN
    // Display center: (195, 195), Usable radius: ~185px
    // Orange/amber theme for warmth association

    // Get temperature data
    uint16_t temp_raw = 0;
    int64_t temp_last_update = 0;
    if (hpi_sys_get_last_temp_update(&temp_raw, &temp_last_update) != 0)
    {
        LOG_ERR("Error getting last temperature update");
        temp_raw = 0;
        temp_last_update = 0;
    }
    
    // Convert raw temperature to float for display
    float temp_c = temp_raw / 100.0f;

    // OUTER RING: Temperature Progress Arc (Radius 170-185px) - Orange theme for warmth
    arc_temp_zone = lv_arc_create(scr_temp);
    lv_obj_set_size(arc_temp_zone, 370, 370);  // 185px radius
    lv_obj_center(arc_temp_zone);
    lv_arc_set_range(arc_temp_zone, 30, 42);  // Normal body temp range 30-42°C
    
    // Background arc: Full 270° track (gray)
    lv_arc_set_bg_angles(arc_temp_zone, 135, 45);  // Full background arc
    
    // Set arc value based on temperature
    if (temp_raw > 0) {
        // Clamp temperature to display range (30-42°C)
        float display_temp = temp_c;
        if (display_temp < 30.0f) display_temp = 30.0f;
        if (display_temp > 42.0f) display_temp = 42.0f;
        lv_arc_set_value(arc_temp_zone, (int)(display_temp * 10));  // Scale for arc range
    } else {
        lv_arc_set_value(arc_temp_zone, 350);  // Mid-range when no data
    }
    
    // Style the progress arc - orange/amber theme for temperature
    lv_obj_set_style_arc_color(arc_temp_zone, lv_color_hex(0x333333), LV_PART_MAIN);    // Background track
    lv_obj_set_style_arc_width(arc_temp_zone, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_temp_zone, lv_color_hex(0xFF8C00), LV_PART_INDICATOR);  // Orange progress
    lv_obj_set_style_arc_width(arc_temp_zone, 6, LV_PART_INDICATOR);
    lv_obj_remove_style(arc_temp_zone, NULL, LV_PART_KNOB);  // Remove knob
    lv_obj_clear_flag(arc_temp_zone, LV_OBJ_FLAG_CLICKABLE);

    // Screen title - properly positioned to avoid arc overlap
    lv_obj_t *label_title = lv_label_create(scr_temp);
    lv_label_set_text(label_title, "TEMPERATURE");
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 40);  // Centered at top, clear of arc
    lv_obj_add_style(label_title, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);

    // MID-UPPER RING: Temperature Icon (clean, no container - using 45x45 icon for circular display)
    lv_obj_t *img_temp = lv_img_create(scr_temp);
    lv_img_set_src(img_temp, &img_temp_45);  // Using properly sized 45x45 icon for circular display
    lv_obj_align(img_temp, LV_ALIGN_TOP_MID, 0, 95);
    lv_obj_set_style_img_recolor(img_temp, lv_color_hex(0xFF8C00), LV_PART_MAIN);  // Orange recolor
    lv_obj_set_style_img_recolor_opa(img_temp, LV_OPA_COVER, LV_PART_MAIN);

    // CENTRAL ZONE: Main Temperature Value (properly spaced from icon)
    label_temp_value = lv_label_create(scr_temp);
    if (temp_raw == 0) {
        lv_label_set_text(label_temp_value, "READY");
    } else {
        // Get temperature preference from user settings
        uint8_t temp_unit = hpi_user_settings_get_temp_unit();
        if (temp_unit == 1) {  // 1 = Fahrenheit
            float temp_f = temp_c_to_f(temp_c);
            lv_label_set_text_fmt(label_temp_value, "%.1f", (double)temp_f);
        } else {  // 0 = Celsius
            lv_label_set_text_fmt(label_temp_value, "%.1f", (double)temp_c);
        }
    }
    lv_obj_align(label_temp_value, LV_ALIGN_CENTER, 0, -10);  // Centered, slightly above middle
    lv_obj_set_style_text_color(label_temp_value, lv_color_white(), LV_PART_MAIN);
    lv_obj_add_style(label_temp_value, &style_numeric_large, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_temp_value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // Unit label directly below main value
    lv_obj_t *label_temp_unit = lv_label_create(scr_temp);
    if (temp_raw == 0) {
        lv_label_set_text(label_temp_unit, "Place sensor on skin");
    } else {
        // Display unit based on user preference
        uint8_t temp_unit = hpi_user_settings_get_temp_unit();
        if (temp_unit == 1) {  // 1 = Fahrenheit
            lv_label_set_text(label_temp_unit, "°F");
        } else {  // 0 = Celsius
            lv_label_set_text(label_temp_unit, "°C");
        }
    }
    lv_obj_align(label_temp_unit, LV_ALIGN_CENTER, 0, 35);  // Below main value with gap
    lv_obj_set_style_text_color(label_temp_unit, lv_color_hex(0xFF8C00), LV_PART_MAIN);  // Orange accent
    lv_obj_add_style(label_temp_unit, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_temp_unit, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // Status info - centered below unit with proper spacing
    lv_obj_t *label_temp_status = lv_label_create(scr_temp);
    if (temp_raw == 0) {
        lv_label_set_text(label_temp_status, "Body temperature");
    } else {
        char last_meas_str[25];
        hpi_helper_get_relative_time_str(temp_last_update, last_meas_str, sizeof(last_meas_str));
        lv_label_set_text(label_temp_status, last_meas_str);
    }
    lv_obj_align(label_temp_status, LV_ALIGN_CENTER, 0, 80);  // Centered, below unit with gap
    lv_obj_set_style_text_color(label_temp_status, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_add_style(label_temp_status, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_temp_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // BOTTOM ZONE: Unit Toggle Button (properly centered at bottom)
    btn_temp_unit = hpi_btn_create_primary(scr_temp);
    lv_obj_set_size(btn_temp_unit, 120, 50);  // Smaller width for unit toggle
    lv_obj_align(btn_temp_unit, LV_ALIGN_BOTTOM_MID, 0, -30);  // Centered at bottom with margin
    lv_obj_set_style_radius(btn_temp_unit, 25, LV_PART_MAIN);
    
    // AMOLED-optimized button styling - orange theme
    lv_obj_set_style_bg_color(btn_temp_unit, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_temp_unit, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_temp_unit, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_temp_unit, lv_color_hex(0xFF8C00), LV_PART_MAIN);
    lv_obj_set_style_border_opa(btn_temp_unit, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn_temp_unit, 0, LV_PART_MAIN);  // No shadow for AMOLED
    
    lv_obj_t *label_btn_temp_unit = lv_label_create(btn_temp_unit);
    // Show current unit preference
    uint8_t temp_unit = hpi_user_settings_get_temp_unit();
    if (temp_unit == 1) {  // 1 = Fahrenheit, show opposite for toggle
        lv_label_set_text(label_btn_temp_unit, "°C");
    } else {  // 0 = Celsius, show opposite for toggle
        lv_label_set_text(label_btn_temp_unit, "°F");
    }
    lv_obj_center(label_btn_temp_unit);
    lv_obj_set_style_text_color(label_btn_temp_unit, lv_color_hex(0xFF8C00), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_temp_unit, scr_temp_unit_btn_event_handler, LV_EVENT_CLICKED, NULL);

    hpi_disp_set_curr_screen(SCR_TEMP);
    hpi_show_screen(scr_temp, m_scroll_dir);
}

/**
 * @brief Update temperature display - compatibility function for display state machine
 * @param temp_f Temperature in Fahrenheit
 * @param temp_f_last_update Last update timestamp
 * @note In the new circular design, temperature updates are handled via screen refresh
 */
void hpi_temp_disp_update_temp_f(double temp_f, int64_t temp_f_last_update)
{
    // In the new circular AMOLED design, temperature updates are handled by full screen refresh
    // This function is kept for compatibility with the display state machine
    // The actual temperature display is updated when the screen is drawn/refreshed
    LOG_DBG("Temperature update notification: %.1f°F at %" PRId64, temp_f, temp_f_last_update);
}

// Temperature screen event handlers
static void scr_temp_unit_btn_event_handler(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        // Toggle temperature unit preference
        uint8_t current_unit = hpi_user_settings_get_temp_unit();
        uint8_t new_unit = (current_unit == 0) ? 1 : 0;  // 0=Celsius, 1=Fahrenheit
        hpi_user_settings_set_temp_unit(new_unit);
        
        // Refresh the screen to show new unit
        hpi_load_screen(SCR_TEMP, SCROLL_NONE);
    }
}