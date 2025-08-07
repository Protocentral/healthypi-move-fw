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
#include "hw_module.h"

lv_obj_t *scr_low_battery;

extern lv_style_t style_red_medium;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_white_large_numeric;

void draw_scr_spl_low_battery(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    uint8_t battery_level = (uint8_t)arg1; // Battery level passed as argument
    bool is_charging = (bool)arg2; // Charging status passed as arg2
    float battery_voltage = (float)arg3 / 100.0f; // Voltage passed as arg3 (multiplied by 100)

    scr_low_battery = lv_obj_create(NULL);
    lv_obj_add_style(scr_low_battery, &style_scr_black, 0);
    lv_obj_add_flag(scr_low_battery, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    /*Create a container with COLUMN flex direction*/
    lv_obj_t *cont_col = lv_obj_create(scr_low_battery);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(cont_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(cont_col, &style_scr_black, 0);

    // Battery Low title in large text
    lv_obj_t *label_battery_low = lv_label_create(cont_col);
    lv_label_set_text(label_battery_low, "Battery Low");
    lv_obj_add_style(label_battery_low, &style_white_medium, 0);

    // Battery icon using low_batt_100 image
    lv_obj_t *img_battery_icon = lv_img_create(cont_col);
    lv_img_set_src(img_battery_icon, &low_batt_100);

    // Display battery percentage - combined number and % symbol
    lv_obj_t *label_battery_percent = lv_label_create(cont_col);
    lv_label_set_text_fmt(label_battery_percent, "%d%%", battery_level);
    lv_obj_add_style(label_battery_percent, &style_white_medium, 0);

    lv_obj_t *label_info = lv_label_create(cont_col);
    lv_label_set_long_mode(label_info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_info, 330);
    lv_label_set_text(label_info, "Please connect charger\n");
    lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_style(label_info, &style_white_medium, 0);

    // Add charging status info if available
    if (is_charging) {
        lv_obj_t *label_charging = lv_label_create(cont_col);
        lv_label_set_text(label_charging, LV_SYMBOL_CHARGE " Charging...");
        lv_obj_add_style(label_charging, &style_white_medium, 0);
        lv_obj_set_style_text_color(label_charging, lv_color_hex(0x00FF00), 0); // Green color for charging
    }

    hpi_disp_set_curr_screen(SCR_SPL_LOW_BATTERY);
    hpi_show_screen(scr_low_battery, m_scroll_dir);
}

void gesture_down_scr_spl_low_battery(void)
{
    // No action on gesture down
}
