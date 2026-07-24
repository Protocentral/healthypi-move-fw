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
#include <string.h>
#include <app_version.h>

#include "ui/move_ui.h"
#include "ui/hpi_r0_theme.h"   /* R0_TEXT_2 */

LOG_MODULE_REGISTER(boot_module, LOG_LEVEL_WRN);

lv_obj_t *scr_boot;
static lv_obj_t *label_boot_messages;
static lv_obj_t *scroll_container;

void scr_boot_add_final(bool status);

// Externs
extern lv_style_t style_red_medium;

void draw_scr_boot(void)
{
    // Create main screen
    scr_boot = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_boot, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr_boot, LV_OPA_COVER, LV_PART_MAIN);

    /* Round 390×390: keep content inside the circular safe area. Horizontal
     * pad ~52 px keeps left-aligned lines clear of the bezel at mid-height;
     * top pad ~52 px keeps the title below the curved top edge. Cross-axis
     * center so the log column sits on the diameter, not hard-left. */
    lv_obj_t *main_container = lv_obj_create(scr_boot);
    lv_obj_set_size(main_container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(main_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(main_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(main_container, 52, LV_PART_MAIN);
    lv_obj_set_style_pad_right(main_container, 52, LV_PART_MAIN);
    lv_obj_set_style_pad_top(main_container, 52, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(main_container, 48, LV_PART_MAIN);
    lv_obj_clear_flag(main_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(main_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(main_container, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(main_container, 12, LV_PART_MAIN);

    /* FW version only — short enough to sit on the round top without clipping. */
    lv_obj_t *label_fw = lv_label_create(main_container);
    lv_label_set_text(label_fw, "FW: " APP_VERSION_STRING);
    lv_obj_set_style_text_font(label_fw, &HPI_FONT_LABEL, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_fw, lv_color_hex(0xF59E0B), LV_PART_MAIN);
    lv_obj_set_style_text_align(label_fw, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    /* Log column: width fits inside the mid-circle chord (~286 px available
     * with 52 px side pads on 390). Fixed width keeps lines from spreading
     * into the bezel as they get longer. */
    scroll_container = lv_obj_create(main_container);
    lv_obj_set_size(scroll_container, 280, 270);
    lv_obj_set_style_bg_opa(scroll_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(scroll_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scroll_container, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(scroll_container, LV_SCROLLBAR_MODE_OFF);

    label_boot_messages = lv_label_create(scroll_container);
    lv_label_set_text(label_boot_messages, "");
    lv_obj_set_width(label_boot_messages, LV_PCT(100));
    lv_obj_set_height(label_boot_messages, LV_SIZE_CONTENT);
    lv_label_set_long_mode(label_boot_messages, LV_LABEL_LONG_WRAP);
    lv_label_set_recolor(label_boot_messages, true);
    lv_obj_set_style_text_font(label_boot_messages, &HPI_FONT_LABEL, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(label_boot_messages, 6, LV_PART_MAIN);
    /* R0_TEXT_2 for contrast on black; PASS/FAIL tags keep recolored emphasis. */
    lv_obj_set_style_text_color(label_boot_messages, lv_color_hex(R0_TEXT_2), LV_PART_MAIN);
    lv_obj_align(label_boot_messages, LV_ALIGN_TOP_LEFT, 0, 0);

    hpi_disp_set_curr_screen(SCR_SPL_BOOT);
    hpi_show_screen(scr_boot, SCROLL_RIGHT);
}

void scr_boot_add_status(const char *dev_label, bool status, bool show_status)
{
    char buf[80];
    if (show_status)
    {
        /* "Device  ·  OK" / "Device  ·  FAIL" — proportional, no mono pad. */
        sprintf(buf, "%s  \xC2\xB7  %s\n", dev_label,
                status ? "#16A34A OK#" : "#DC2626 FAIL#");
    }
    else
    {
        sprintf(buf, "%s\n", dev_label);
    }

    // Get current text and append new message using static buffer
    const char *current_text = lv_label_get_text(label_boot_messages);
    static char full_text[2048]; // Static buffer to avoid malloc/free

    // Safely copy and concatenate
    strncpy(full_text, current_text, sizeof(full_text) - 1);
    full_text[sizeof(full_text) - 1] = '\0';

    size_t current_len = strlen(full_text);
    size_t remaining = sizeof(full_text) - current_len - 1;

    if (remaining > 0)
    {
        strncat(full_text, buf, remaining);
    }

    lv_label_set_text(label_boot_messages, full_text);

    // Refresh the label to ensure proper sizing
    lv_obj_refresh_style(label_boot_messages, LV_PART_ANY, LV_STYLE_PROP_ANY);
    lv_obj_update_layout(scroll_container);
    
    // Force LVGL to process tasks and then scroll
    lv_task_handler();
    
    // Auto-scroll to bottom to show latest message
    lv_obj_scroll_to_y(scroll_container, LV_COORD_MAX, LV_ANIM_OFF);
}

void scr_boot_add_final(bool status)
{
    char buf[64];
    sprintf(buf, "\n%s\n", status ? "#16A34A ALL CHECKS PASSED#"
                                   : "#DC2626 CHECK FAILED#");

    // Get current text and append final message using static buffer
    const char *current_text = lv_label_get_text(label_boot_messages);
    static char full_text[2048]; // Static buffer to avoid malloc/free

    // Safely copy and concatenate
    strncpy(full_text, current_text, sizeof(full_text) - 1);
    full_text[sizeof(full_text) - 1] = '\0';

    size_t current_len = strlen(full_text);
    size_t remaining = sizeof(full_text) - current_len - 1;

    if (remaining > 0)
    {
        strncat(full_text, buf, remaining);
    }

    lv_label_set_text(label_boot_messages, full_text);

    // Refresh the label to ensure proper sizing
    lv_obj_refresh_style(label_boot_messages, LV_PART_ANY, LV_STYLE_PROP_ANY);
    lv_obj_update_layout(scroll_container);
    
    // Force LVGL to process tasks and then scroll
    lv_task_handler();
    
    // Auto-scroll to bottom to show final message
    lv_obj_scroll_to_y(scroll_container, LV_COORD_MAX, LV_ANIM_OFF);
}
