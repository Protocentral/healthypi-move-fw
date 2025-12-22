/*
 * HealthyPi Move
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Author: Ashwin Whitchurch, Protocentral Electronics
 * Contact: ashwin@protocentral.com
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
static lv_obj_t *label_temp_value;
static lv_obj_t *label_temp_unit;
static lv_obj_t *label_temp_last_update;
static lv_obj_t *btn_temp_unit;

// Externs - Modern style system
extern lv_style_t style_body_medium;
extern lv_style_t style_numeric_large;
extern lv_style_t style_caption;

// Button color for temperature theme
#define COLOR_BTN_ORANGE 0xE65100

// Forward declarations
static void scr_temp_unit_btn_event_handler(lv_event_t *e);

void draw_scr_temp(enum scroll_dir m_scroll_dir)
{
    scr_temp = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_temp, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_temp, LV_OBJ_FLAG_SCROLLABLE);

    /*
     * COMPACT LAYOUT FOR 390x390 ROUND AMOLED
     * ========================================
     * Matching HR/SPO2 screen pattern:
     *   Top: y=40 (title)
     *   Upper: y=75 (icon)
     *   Center: y=130 (value with inline unit)
     *   Lower: y=210 (last update)
     *   Bottom: y=250 (button)
     */

    // Get temperature data
    uint16_t temp_raw = 0;
    int64_t temp_last_update = 0;
    if (hpi_sys_get_last_temp_update(&temp_raw, &temp_last_update) != 0) {
        temp_raw = 0;
        temp_last_update = 0;
    }

    // temp_raw contains temperature in Fahrenheit * 100 (e.g., 9860 = 98.6°F)
    float temp_f = temp_raw / 100.0f;
    float temp_c = (temp_f - 32.0f) * 5.0f / 9.0f;

    // TOP: Title at y=40
    lv_obj_t *label_title = lv_label_create(scr_temp);
    lv_label_set_text(label_title, "Skin Temp.");
    lv_obj_set_pos(label_title, 0, 40);
    lv_obj_set_width(label_title, 390);
    lv_obj_add_style(label_title, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);

    // UPPER: Temperature icon at y=75
    lv_obj_t *img_temp = lv_img_create(scr_temp);
    lv_img_set_src(img_temp, &img_temp_45);
    lv_obj_set_pos(img_temp, (390 - 45) / 2, 75);
    lv_obj_set_style_img_recolor(img_temp, lv_color_hex(0xFF8C00), LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(img_temp, LV_OPA_COVER, LV_PART_MAIN);

    // CENTER: Temperature value with inline unit at y=130
    lv_obj_t *cont_value = lv_obj_create(scr_temp);
    lv_obj_remove_style_all(cont_value);
    lv_obj_set_size(cont_value, 390, 70);
    lv_obj_set_pos(cont_value, 0, 130);
    lv_obj_set_style_bg_opa(cont_value, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont_value, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_value, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);

    label_temp_value = lv_label_create(cont_value);
    uint8_t temp_unit_pref = hpi_user_settings_get_temp_unit();

    if (temp_raw == 0) {
        lv_label_set_text(label_temp_value, "--");
    } else {
        char temp_str[16];
        if (temp_unit_pref == 1) {  // Fahrenheit
            int temp_x10 = (int)(temp_f * 10.0f + 0.5f);
            snprintf(temp_str, sizeof(temp_str), "%d.%d", temp_x10 / 10, temp_x10 % 10);
        } else {  // Celsius
            int temp_x10 = (int)(temp_c * 10.0f + 0.5f);
            snprintf(temp_str, sizeof(temp_str), "%d.%d", temp_x10 / 10, temp_x10 % 10);
        }
        lv_label_set_text(label_temp_value, temp_str);
    }
    lv_obj_set_style_text_color(label_temp_value, lv_color_white(), LV_PART_MAIN);
    lv_obj_add_style(label_temp_value, &style_numeric_large, LV_PART_MAIN);

    // Inline unit (smaller, colored, baseline-aligned)
    label_temp_unit = lv_label_create(cont_value);
    if (temp_raw == 0) {
        lv_label_set_text(label_temp_unit, "");
    } else {
        lv_label_set_text(label_temp_unit, temp_unit_pref == 1 ? " °F" : " °C");
    }
    lv_obj_set_style_text_color(label_temp_unit, lv_color_hex(0xFF8C00), LV_PART_MAIN);
    lv_obj_add_style(label_temp_unit, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(label_temp_unit, 8, LV_PART_MAIN);

    // LOWER: Last measurement time at y=210
    label_temp_last_update = lv_label_create(scr_temp);
    if (temp_raw == 0) {
        lv_label_set_text(label_temp_last_update, "No measurement yet");
    } else {
        char last_meas_str[25];
        hpi_helper_get_relative_time_str(temp_last_update, last_meas_str, sizeof(last_meas_str));
        lv_label_set_text(label_temp_last_update, last_meas_str);
    }
    lv_obj_set_pos(label_temp_last_update, 0, 210);
    lv_obj_set_width(label_temp_last_update, 390);
    lv_obj_set_style_text_color(label_temp_last_update, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_add_style(label_temp_last_update, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_temp_last_update, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // BOTTOM: Unit toggle button at y=250
    const int btn_width = 200;
    const int btn_height = 60;
    const int btn_y = 250;

    btn_temp_unit = hpi_btn_create_primary(scr_temp);
    lv_obj_set_size(btn_temp_unit, btn_width, btn_height);
    lv_obj_set_pos(btn_temp_unit, (390 - btn_width) / 2, btn_y);
    lv_obj_set_style_radius(btn_temp_unit, 30, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn_temp_unit, lv_color_hex(COLOR_BTN_ORANGE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_temp_unit, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_temp_unit, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn_temp_unit, 0, LV_PART_MAIN);

    lv_obj_t *label_btn = lv_label_create(btn_temp_unit);
    // Show the unit to switch TO
    if (temp_unit_pref == 1) {
        lv_label_set_text(label_btn, "Switch to °C");
    } else {
        lv_label_set_text(label_btn, "Switch to °F");
    }
    lv_obj_center(label_btn);
    lv_obj_set_style_text_color(label_btn, lv_color_white(), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_temp_unit, scr_temp_unit_btn_event_handler, LV_EVENT_CLICKED, NULL);

    hpi_disp_set_curr_screen(SCR_TEMP);
    hpi_show_screen(scr_temp, m_scroll_dir);
}

void hpi_temp_disp_update_temp_f(double temp_f, int64_t temp_f_last_update)
{
    if (label_temp_value == NULL || label_temp_unit == NULL || label_temp_last_update == NULL) {
        return;
    }

    float temp_c = (temp_f - 32.0f) * 5.0f / 9.0f;
    uint8_t temp_unit_pref = hpi_user_settings_get_temp_unit();
    char temp_str[16];

    if (temp_unit_pref == 1) {  // Fahrenheit
        int temp_x10 = (int)(temp_f * 10.0f + 0.5f);
        snprintf(temp_str, sizeof(temp_str), "%d.%d", temp_x10 / 10, temp_x10 % 10);
        lv_label_set_text(label_temp_value, temp_str);
        lv_label_set_text(label_temp_unit, " °F");
    } else {  // Celsius
        int temp_x10 = (int)(temp_c * 10.0f + 0.5f);
        snprintf(temp_str, sizeof(temp_str), "%d.%d", temp_x10 / 10, temp_x10 % 10);
        lv_label_set_text(label_temp_value, temp_str);
        lv_label_set_text(label_temp_unit, " °C");
    }

    char last_meas_str[25];
    hpi_helper_get_relative_time_str(temp_f_last_update, last_meas_str, sizeof(last_meas_str));
    lv_label_set_text(label_temp_last_update, last_meas_str);
}

static void scr_temp_unit_btn_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        uint8_t current_unit = hpi_user_settings_get_temp_unit();
        uint8_t new_unit = (current_unit == 0) ? 1 : 0;
        hpi_user_settings_set_temp_unit(new_unit);
        hpi_load_screen(SCR_TEMP, SCROLL_NONE);
    }
}
