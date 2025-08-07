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

lv_obj_t *scr_temp;

// GUI Labels
static lv_obj_t *label_temp_f;
static lv_obj_t *label_temp_unit;
static lv_obj_t *label_temp_last_update_time;

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
    lv_obj_clear_flag(scr_temp, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    draw_scr_common(scr_temp);

    lv_obj_t *cont_col = lv_obj_create(scr_temp);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_col, -1, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_left(cont_col, 0, LV_PART_MAIN);
    lv_obj_add_style(cont_col, &style_scr_black, 0);
    // lv_obj_add_style(cont_col, &style_bg_purple, 0);

    lv_obj_t *label_signal = lv_label_create(cont_col);
    lv_label_set_text(label_signal, "Skin Temp.");
    lv_obj_set_style_text_align(label_signal, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *img_temp = lv_img_create(cont_col);
    lv_img_set_src(img_temp, &img_temp_100);

    uint16_t temp_f = 0;
    int64_t temp_f_last_update = 0;

    if (hpi_sys_get_last_temp_update(&temp_f, &temp_f_last_update) == 0)
    {
        uint16_t temp_c = convert_f_to_c(temp_f);
        LOG_DBG("Last Temp value: %d (%d.%d°F, %d.%d°C)", temp_f, 
                temp_f/100, (temp_f%100)/10, temp_c/100, (temp_c%100)/10);
    }
    else
    {
        LOG_ERR("Failed to get last Temp value");
        temp_f = 0;
        temp_f_last_update = 0;
    }

    lv_obj_t *cont_temp = lv_obj_create(cont_col);
    lv_obj_set_size(cont_temp, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_temp, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(cont_temp, &style_scr_black, 0);
    lv_obj_set_flex_align(cont_temp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_bottom(cont_temp, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(cont_temp, 0, LV_PART_MAIN);

    lv_obj_set_style_bg_opa(cont_temp, 0, 0);

    label_temp_f = lv_label_create(cont_temp);
    
    // Format temperature according to user setting
    char temp_str[10];
    char unit_str[5];
    get_formatted_temperature(temp_f, temp_str, sizeof(temp_str), unit_str, sizeof(unit_str));
    
    lv_label_set_text(label_temp_f, temp_str);
    lv_obj_add_style(label_temp_f, &style_white_large_numeric, 0);

    label_temp_unit = lv_label_create(cont_temp);
    lv_label_set_text(label_temp_unit, unit_str);
    lv_obj_add_style(label_temp_unit, &style_white_medium, 0);  // Add proper styling to unit label

    char last_meas_str[25];
    hpi_helper_get_relative_time_str(temp_f_last_update, last_meas_str, sizeof(last_meas_str));
    label_temp_last_update_time = lv_label_create(cont_col);
    lv_label_set_text(label_temp_last_update_time, last_meas_str);
    lv_obj_set_style_text_align(label_temp_last_update_time, LV_TEXT_ALIGN_CENTER, 0);

    hpi_disp_set_curr_screen(SCR_TEMP);
    hpi_show_screen(scr_temp, m_scroll_dir);
}

void hpi_temp_disp_update_temp_f(double temp_f, int64_t temp_f_last_update)
{
    if (label_temp_f == NULL || label_temp_unit == NULL) {
        LOG_WRN("Temperature display labels not initialized");
        return;
    }

    // Convert double to uint16_t for consistency with the helper function
    uint16_t temp_f_raw = (uint16_t)(temp_f * 100.0);
    
    LOG_DBG("Updating temperature display: %d.%d°F (raw: %d)", 
            temp_f_raw/100, (temp_f_raw%100)/10, temp_f_raw);
    
    // Format temperature according to user setting
    char temp_str[10];
    char unit_str[5];
    get_formatted_temperature(temp_f_raw, temp_str, sizeof(temp_str), unit_str, sizeof(unit_str));
    
    LOG_DBG("Formatted temperature: %s %s", temp_str, unit_str);
    
    lv_label_set_text(label_temp_f, temp_str);
    lv_label_set_text(label_temp_unit, unit_str);

    char last_meas_str[25];
    hpi_helper_get_relative_time_str(temp_f_last_update, last_meas_str, sizeof(last_meas_str));
    lv_label_set_text(label_temp_last_update_time, last_meas_str);
    lv_obj_set_style_text_align(label_temp_last_update_time, LV_TEXT_ALIGN_CENTER, 0);
}