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
#include <zephyr/drivers/display.h>
#include <lvgl.h>
#include <stdio.h>
#include <app_version.h>
#include <time.h>
#include <zephyr/posix/time.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/pm/device.h>
#include <zephyr/zbus/zbus.h>

#include "hw_module.h"
#include "hpi_common_types.h"
#include "ui/move_ui.h"

#include <display_sh8601.h>

LOG_MODULE_REGISTER(display_common, LOG_LEVEL_DBG);

// Modern AMOLED-optimized color palette
#define COLOR_SURFACE_DARK    0x1C1C1E
#define COLOR_SURFACE_MEDIUM  0x2C2C2E
#define COLOR_SURFACE_LIGHT   0x3C3C3E
#define COLOR_PRIMARY_BLUE    0x007AFF
#define COLOR_SUCCESS_GREEN   0x34C759
#define COLOR_WARNING_AMBER   0xFF9500
#define COLOR_CRITICAL_RED    0xFF3B30
#define COLOR_TEXT_SECONDARY  0xE5E5E7

#define HPI_BATTERY_LEVEL_FULL 90
#define HPI_BATTERY_LEVEL_HIGH 70
#define HPI_BATTERY_LEVEL_MEDIUM 40
#define HPI_BATTERY_LEVEL_LOW 15


// LVGL Styles
static lv_style_t style_btn;
/* Black button styles (global) */
static lv_style_t style_btn_black;
static lv_style_t style_btn_black_pressed;

/* Modern button styles */
static lv_style_t style_btn_primary;
static lv_style_t style_btn_primary_pressed;
static lv_style_t style_btn_secondary;
static lv_style_t style_btn_icon;

/* Modern arc styles - made global for external access */
lv_style_t style_health_arc;
lv_style_t style_health_arc_bg;

// Global LVGL Styles
lv_style_t style_tiny;
lv_style_t style_scr_black;
lv_style_t style_red_medium;
lv_style_t style_lbl_red_small;

lv_style_t style_white_medium;
lv_style_t style_white_small;
lv_style_t style_white_large;

lv_style_t style_scr_container;

lv_style_t style_lbl_white_14;
lv_style_t style_white_large_numeric;

/* Modern typography styles - made global for external access */
lv_style_t style_headline;
lv_style_t style_body_large;
lv_style_t style_body_medium;
lv_style_t style_caption;

/* Additional specialized styles */
lv_style_t style_numeric_large;  // For large numeric displays (time, main values)
lv_style_t style_numeric_medium; // For medium numeric displays
lv_style_t style_status_small;   // For small status text

lv_style_t style_bg_blue;
lv_style_t style_bg_red;
lv_style_t style_bg_green;
lv_style_t style_bg_purple;

static volatile uint8_t hpi_disp_curr_brightness = DISPLAY_DEFAULT_BRIGHTNESS;

lv_obj_t *cui_battery_percent;
int tmp_scr_parent = 0;

// Externs
extern const struct device *display_dev;

extern struct k_sem sem_stop_one_shot_spo2;

void display_init_styles(void)
{
    /*Initialize the styles*/
    lv_style_init(&style_btn);
    lv_style_set_bg_color(&style_btn, lv_palette_darken(LV_PALETTE_GREY, 4));
    lv_style_set_border_color(&style_btn, lv_palette_darken(LV_PALETTE_RED, 3));
    lv_style_set_border_width(&style_btn, 3);

    // Subscript (Unit) label style
    lv_style_init(&style_tiny);
    lv_style_set_text_color(&style_tiny, lv_color_white());
    lv_style_set_text_font(&style_tiny, &lv_font_montserrat_20);

    lv_style_init(&style_white_medium);
    lv_style_set_text_color(&style_white_medium, lv_color_white());
    lv_style_set_text_font(&style_white_medium, &inter_semibold_24);

    lv_style_init(&style_white_large_numeric);
    lv_style_set_text_color(&style_white_large_numeric, lv_color_white());
    lv_style_set_text_font(&style_white_large_numeric, &oxanium_90); // &ui_font_number_big); //&ui_font_Number_extra);

    // Label Red
    lv_style_init(&style_red_medium);
    lv_style_set_text_color(&style_red_medium, lv_palette_main(LV_PALETTE_RED));
    lv_style_set_text_font(&style_red_medium, &inter_semibold_24);

    // Label White 14
    lv_style_init(&style_lbl_white_14);
    lv_style_set_text_color(&style_lbl_white_14, lv_color_white());
    lv_style_set_text_font(&style_lbl_white_14, &inter_semibold_24);

    // Container for scrollable screen layout
    lv_style_init(&style_scr_container);
    lv_style_set_flex_flow(&style_scr_container, LV_FLEX_FLOW_ROW_WRAP);
    lv_style_set_flex_main_place(&style_scr_container, LV_FLEX_ALIGN_SPACE_EVENLY);
    lv_style_set_flex_cross_place(&style_scr_container, LV_FLEX_ALIGN_CENTER);

    // Black screen background
    lv_style_init(&style_scr_black);
    lv_style_set_bg_opa(&style_scr_black, LV_OPA_COVER);
    lv_style_set_border_width(&style_scr_black, 0);
    lv_style_set_bg_color(&style_scr_black, lv_color_black());

    /* Initialize global black button styles */
    lv_style_init(&style_btn_black);
    lv_style_set_bg_color(&style_btn_black, lv_color_black());
    lv_style_set_bg_opa(&style_btn_black, LV_OPA_COVER);
    lv_style_set_border_width(&style_btn_black, 0);
    lv_style_set_text_color(&style_btn_black, lv_color_white());
    lv_style_set_radius(&style_btn_black, 6);
    /* Increased padding for better touch target and readable label */
    lv_style_set_pad_top(&style_btn_black, 20);
    lv_style_set_pad_bottom(&style_btn_black, 20);
    lv_style_set_pad_left(&style_btn_black, 16);
    lv_style_set_pad_right(&style_btn_black, 16);
    /* Set minimum height for touch-friendly buttons on small display */
    lv_style_set_min_height(&style_btn_black, 56);
    /* external margin so buttons have breathing room from other objects */
    lv_style_set_margin_all(&style_btn_black, 6);
    /* Orange outline to make the button stand out */
    lv_style_set_outline_width(&style_btn_black, 3);
    lv_style_set_outline_color(&style_btn_black, lv_color_hex(0xFF9900));
    lv_style_set_outline_opa(&style_btn_black, LV_OPA_COVER);
    lv_style_set_outline_pad(&style_btn_black, 2);

    lv_style_init(&style_btn_black_pressed);
    lv_style_set_bg_color(&style_btn_black_pressed, lv_color_hex(0x222222));
    lv_style_set_bg_opa(&style_btn_black_pressed, LV_OPA_COVER);
    lv_style_set_text_color(&style_btn_black_pressed, lv_color_white());
    /* Keep pressed state outlined as well (slightly thinner) */
    lv_style_set_outline_width(&style_btn_black_pressed, 2);
    lv_style_set_outline_color(&style_btn_black_pressed, lv_color_hex(0xFF9900));
    lv_style_set_outline_opa(&style_btn_black_pressed, LV_OPA_COVER);
    /* Keep same increased padding and external margin on pressed state */
    lv_style_set_pad_top(&style_btn_black_pressed, 20);
    lv_style_set_pad_bottom(&style_btn_black_pressed, 20);
    lv_style_set_pad_left(&style_btn_black_pressed, 16);
    lv_style_set_pad_right(&style_btn_black_pressed, 16);
    lv_style_set_min_height(&style_btn_black_pressed, 56);
    lv_style_set_margin_all(&style_btn_black_pressed, 6);

    lv_style_init(&style_bg_blue);
    lv_style_set_radius(&style_bg_blue, 15);
    lv_style_set_bg_opa(&style_bg_blue, LV_OPA_COVER);
    static lv_grad_dsc_t grad;
    grad.dir = LV_GRAD_DIR_VER;
    grad.stops_count = 2;
    grad.stops[0].color = lv_color_black();
    grad.stops[1].color = lv_palette_darken(LV_PALETTE_BLUE, 4);
    grad.stops[0].frac = 168;
    grad.stops[1].frac = 255;
    lv_style_set_bg_grad(&style_bg_blue, &grad);

    lv_style_init(&style_bg_red);
    lv_style_set_radius(&style_bg_red, 15);
    lv_style_set_bg_opa(&style_bg_red, LV_OPA_COVER);
    static lv_grad_dsc_t grad_red;
    grad_red.dir = LV_GRAD_DIR_VER;
    grad_red.stops_count = 2;
    grad_red.stops[0].color = lv_color_black();
    grad_red.stops[1].color = lv_palette_darken(LV_PALETTE_DEEP_ORANGE, 4);
    grad_red.stops[0].frac = 168;
    grad_red.stops[1].frac = 255;
    lv_style_set_bg_grad(&style_bg_red, &grad_red);

    lv_style_init(&style_bg_green);
    lv_style_set_radius(&style_bg_green, 15);
    lv_style_set_bg_opa(&style_bg_green, LV_OPA_COVER);
    static lv_grad_dsc_t grad_green;
    grad_green.dir = LV_GRAD_DIR_VER;
    grad_green.stops_count = 2;
    grad_green.stops[0].color = lv_color_black();
    grad_green.stops[1].color = lv_palette_darken(LV_PALETTE_CYAN, 2);
    grad_green.stops[0].frac = 168;
    grad_green.stops[1].frac = 255;
    lv_style_set_bg_grad(&style_bg_green, &grad_green);

    lv_style_init(&style_bg_purple);
    lv_style_set_radius(&style_bg_purple, 15);
    lv_style_set_bg_opa(&style_bg_purple, LV_OPA_COVER);
    static lv_grad_dsc_t grad_purple;
    grad_purple.dir = LV_GRAD_DIR_VER;
    grad_purple.stops_count = 2;
    grad_purple.stops[0].color = lv_color_black();
    grad_purple.stops[1].color = lv_palette_darken(LV_PALETTE_PURPLE, 4);
    grad_purple.stops[0].frac = 168;
    grad_purple.stops[1].frac = 255;
    lv_style_set_bg_grad(&style_bg_purple, &grad_purple);

    /* Initialize modern primary button styles */
    lv_style_init(&style_btn_primary);
    lv_style_set_bg_color(&style_btn_primary, lv_color_hex(COLOR_SURFACE_MEDIUM));
    lv_style_set_bg_opa(&style_btn_primary, LV_OPA_COVER);
    lv_style_set_border_width(&style_btn_primary, 1);
    lv_style_set_border_color(&style_btn_primary, lv_color_hex(COLOR_PRIMARY_BLUE));
    lv_style_set_border_opa(&style_btn_primary, LV_OPA_COVER);
    lv_style_set_radius(&style_btn_primary, 24);
    lv_style_set_text_color(&style_btn_primary, lv_color_white());
    /* Increased padding for better touch interaction on small display */
    lv_style_set_pad_top(&style_btn_primary, 24);
    lv_style_set_pad_bottom(&style_btn_primary, 24);
    lv_style_set_pad_left(&style_btn_primary, 20);
    lv_style_set_pad_right(&style_btn_primary, 20);
    /* Set minimum height for touch-friendly buttons */
    lv_style_set_min_height(&style_btn_primary, 60);
    lv_style_set_margin_all(&style_btn_primary, 8);
    /* Subtle shadow for depth */
    lv_style_set_shadow_width(&style_btn_primary, 8);
    lv_style_set_shadow_color(&style_btn_primary, lv_color_hex(COLOR_PRIMARY_BLUE));
    lv_style_set_shadow_opa(&style_btn_primary, LV_OPA_20);
    lv_style_set_shadow_spread(&style_btn_primary, 0);
    lv_style_set_shadow_ofs_x(&style_btn_primary, 0);
    lv_style_set_shadow_ofs_y(&style_btn_primary, 2);

    lv_style_init(&style_btn_primary_pressed);
    lv_style_set_bg_color(&style_btn_primary_pressed, lv_color_hex(COLOR_SURFACE_DARK));
    lv_style_set_bg_opa(&style_btn_primary_pressed, LV_OPA_COVER);
    lv_style_set_border_width(&style_btn_primary_pressed, 2);
    lv_style_set_border_color(&style_btn_primary_pressed, lv_color_hex(COLOR_PRIMARY_BLUE));
    lv_style_set_text_color(&style_btn_primary_pressed, lv_color_white());
    /* Keep same increased padding for pressed state */
    lv_style_set_pad_top(&style_btn_primary_pressed, 24);
    lv_style_set_pad_bottom(&style_btn_primary_pressed, 24);
    lv_style_set_pad_left(&style_btn_primary_pressed, 20);
    lv_style_set_pad_right(&style_btn_primary_pressed, 20);
    lv_style_set_min_height(&style_btn_primary_pressed, 60);
    lv_style_set_shadow_width(&style_btn_primary_pressed, 4);
    lv_style_set_shadow_opa(&style_btn_primary_pressed, LV_OPA_40);

    /* Initialize modern secondary button styles */
    lv_style_init(&style_btn_secondary);
    lv_style_set_bg_opa(&style_btn_secondary, LV_OPA_10);
    lv_style_set_bg_color(&style_btn_secondary, lv_color_white());
    lv_style_set_border_width(&style_btn_secondary, 1);
    lv_style_set_border_color(&style_btn_secondary, lv_color_hex(COLOR_SURFACE_LIGHT));
    lv_style_set_border_opa(&style_btn_secondary, LV_OPA_COVER);
    lv_style_set_radius(&style_btn_secondary, 20);
    lv_style_set_text_color(&style_btn_secondary, lv_color_white());
    /* Increased padding for better touch interaction */
    lv_style_set_pad_top(&style_btn_secondary, 20);
    lv_style_set_pad_bottom(&style_btn_secondary, 20);
    lv_style_set_pad_left(&style_btn_secondary, 16);
    lv_style_set_pad_right(&style_btn_secondary, 16);
    lv_style_set_min_height(&style_btn_secondary, 56);
    lv_style_set_margin_all(&style_btn_secondary, 6);

    /* Initialize icon button styles */
    lv_style_init(&style_btn_icon);
    lv_style_set_bg_color(&style_btn_icon, lv_color_hex(COLOR_SURFACE_MEDIUM));
    lv_style_set_bg_opa(&style_btn_icon, LV_OPA_COVER);
    lv_style_set_radius(&style_btn_icon, 32); /* 64px diameter / 2 - increased from 56px */
    lv_style_set_border_width(&style_btn_icon, 0);
    lv_style_set_pad_all(&style_btn_icon, 20); /* Increased padding for better touch */
    lv_style_set_shadow_width(&style_btn_icon, 6);
    lv_style_set_shadow_color(&style_btn_icon, lv_color_black());
    lv_style_set_shadow_opa(&style_btn_icon, LV_OPA_30);

    /* Initialize modern arc styles */
    lv_style_init(&style_health_arc);
    lv_style_set_arc_width(&style_health_arc, 6);
    lv_style_set_arc_rounded(&style_health_arc, true);
    lv_style_set_arc_color(&style_health_arc, lv_color_hex(COLOR_PRIMARY_BLUE));

    lv_style_init(&style_health_arc_bg);
    lv_style_set_arc_width(&style_health_arc_bg, 6);
    lv_style_set_arc_rounded(&style_health_arc_bg, true);
    lv_style_set_arc_color(&style_health_arc_bg, lv_color_hex(COLOR_SURFACE_LIGHT));
    lv_style_set_arc_opa(&style_health_arc_bg, LV_OPA_50);

    /* Initialize modern typography styles */
    lv_style_init(&style_headline);
    lv_style_set_text_color(&style_headline, lv_color_white());
    lv_style_set_text_font(&style_headline, &inter_semibold_24); /* Headlines use Inter SemiBold 24px - increased for readability */

    lv_style_init(&style_body_large);
    lv_style_set_text_color(&style_body_large, lv_color_white());
    lv_style_set_text_font(&style_body_large, &inter_semibold_24); /* Metric values use Inter SemiBold 24px - increased for readability */

    lv_style_init(&style_body_medium);
    lv_style_set_text_color(&style_body_medium, lv_color_white());
    lv_style_set_text_font(&style_body_medium, &inter_semibold_24); /* Standard body text */

    lv_style_init(&style_caption);
    lv_style_set_text_color(&style_caption, lv_color_hex(COLOR_TEXT_SECONDARY));
    lv_style_set_text_font(&style_caption, &inter_semibold_24); /* Small labels and captions - increased to 24px minimum for small display readability */

    /* Initialize numeric display styles */
    lv_style_init(&style_numeric_large);
    lv_style_set_text_color(&style_numeric_large, lv_color_white());
    lv_style_set_text_font(&style_numeric_large, &inter_semibold_80_time); /* Large numeric displays (time, hero values) */

    lv_style_init(&style_numeric_medium);
    lv_style_set_text_color(&style_numeric_medium, lv_color_white());
    lv_style_set_text_font(&style_numeric_medium, &inter_semibold_24); /* Medium numeric displays */

    // Style for small status text
    lv_style_init(&style_status_small);
    lv_style_set_text_color(&style_status_small, lv_color_hex(COLOR_TEXT_SECONDARY));
    lv_style_set_text_font(&style_status_small, &inter_semibold_24); /* Status text - increased to 24px minimum for small display readability */

    //lv_disp_set_bg_color(NULL, lv_color_black());
}

void draw_scr_common(lv_obj_t *parent)
{
    lv_obj_add_style(parent, &style_scr_black, 0);
    lv_obj_set_scroll_dir(parent, LV_DIR_VER);
    // lv_obj_clear_flag(scr_bpt, LV_OBJ_FLAG_SCROLLABLE);
}

void hpi_disp_set_brightness(uint8_t brightness_percent)
{
    uint8_t brightness = (uint8_t)((brightness_percent * 255) / 100);
    display_set_brightness(display_dev, brightness);
    hpi_disp_curr_brightness = brightness_percent;
}

/* Helper to create a pre-styled black button */
lv_obj_t *hpi_btn_create(lv_obj_t *parent)
{
    lv_obj_t *btn = lv_btn_create(parent);
    if (!btn) {
        return NULL;
    }
    lv_obj_add_style(btn, &style_btn_black, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_style(btn, &style_btn_black_pressed, LV_PART_MAIN | LV_STATE_PRESSED);
    return btn;
}

/* Helper to create a modern primary button */
lv_obj_t *hpi_btn_create_primary(lv_obj_t *parent)
{
    lv_obj_t *btn = lv_btn_create(parent);
    if (!btn) {
        return NULL;
    }
    lv_obj_add_style(btn, &style_btn_primary, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_style(btn, &style_btn_primary_pressed, LV_PART_MAIN | LV_STATE_PRESSED);
    return btn;
}

/* Helper to create a modern secondary button */
lv_obj_t *hpi_btn_create_secondary(lv_obj_t *parent)
{
    lv_obj_t *btn = lv_btn_create(parent);
    if (!btn) {
        return NULL;
    }
    lv_obj_add_style(btn, &style_btn_secondary, LV_PART_MAIN | LV_STATE_DEFAULT);
    return btn;
}

/* Helper to create a modern icon button */
lv_obj_t *hpi_btn_create_icon(lv_obj_t *parent)
{
    lv_obj_t *btn = lv_btn_create(parent);
    if (!btn) {
        return NULL;
    }
    lv_obj_add_style(btn, &style_btn_icon, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_size(btn, 64, 64); /* Increased size for better touch on small display */
    return btn;
}

uint8_t hpi_disp_get_brightness(void)
{
    return hpi_disp_curr_brightness;
}

/**
 * @brief Get the appropriate battery symbol for a given battery level and charging state
 * @param level Battery level percentage (0-100)
 * @param charging Whether the battery is currently charging
 * @return LVGL symbol string for the battery state
 */
const char* hpi_get_battery_symbol(uint8_t level, bool charging)
{
    if (charging) {
        return LV_SYMBOL_CHARGE; // Lightning bolt for charging
    }
    
    // Battery symbols based on level thresholds
    if (level >= HPI_BATTERY_LEVEL_FULL) {
        return LV_SYMBOL_BATTERY_FULL;  // Full battery (90-100%)
    } else if (level >= HPI_BATTERY_LEVEL_HIGH) {
        return LV_SYMBOL_BATTERY_3;     // 3/4 battery (65-89%)
    } else if (level >= HPI_BATTERY_LEVEL_MEDIUM) {
        return LV_SYMBOL_BATTERY_2;     // 2/4 battery (35-64%)
    } else if (level >= HPI_BATTERY_LEVEL_LOW) {
        return LV_SYMBOL_BATTERY_1;     // 1/4 battery (15-34%)
    } else {
        return LV_SYMBOL_BATTERY_EMPTY; // Empty battery (0-14%)
    }
}

/**
 * @brief Get the appropriate color for battery display
 * @param level Battery level percentage (0-100)
 * @param charging Whether the battery is currently charging
 * @return LVGL color for the battery display
 */
lv_color_t hpi_get_battery_color(uint8_t level, bool charging)
{
    if (charging) {
        return lv_color_hex(0x66FF66);  // Bright green when charging
    } else if (level <= HPI_BATTERY_LEVEL_LOW) {
        return lv_color_hex(0xFF6666);  // Bright red for low battery
    } else if (level <= 30) {
        return lv_color_hex(0xFFBB66);  // Bright orange for warning
    } else {
        return lv_color_hex(0xFFFFFF);  // Bright white for normal levels
    }
}

void hpi_show_screen(lv_obj_t *m_screen, enum scroll_dir m_scroll_dir)
{
    lv_obj_add_event_cb(m_screen, disp_screen_event, LV_EVENT_GESTURE, NULL);

    // Let LVGL automatically delete the old screen after animation completes
    // This is the safest approach as LVGL handles the timing correctly
    if (m_scroll_dir == SCROLL_LEFT)
    {
        lv_scr_load_anim(m_screen, LV_SCR_LOAD_ANIM_OVER_LEFT, SCREEN_TRANS_TIME, 0, true);
    }
    else if (m_scroll_dir == SCROLL_RIGHT)
    {
        lv_scr_load_anim(m_screen, LV_SCR_LOAD_ANIM_OVER_RIGHT, SCREEN_TRANS_TIME, 0, true);
    }
    else if (m_scroll_dir == SCROLL_UP)
    {
        lv_scr_load_anim(m_screen, LV_SCR_LOAD_ANIM_OVER_TOP, SCREEN_TRANS_TIME, 0, true);
    }
    else if (m_scroll_dir == SCROLL_DOWN)
    {
        lv_scr_load_anim(m_screen, LV_SCR_LOAD_ANIM_OVER_BOTTOM, SCREEN_TRANS_TIME, 0, true);
    }
    else
    {
        lv_scr_load_anim(m_screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    }
}
extern volatile bool screen_transition_in_progress;
void hpi_load_screen(int m_screen, enum scroll_dir m_scroll_dir)
{
    // CRITICAL: Set global transition flag to suspend ALL screen updates
    // This protects the entire screen loading process across all screens
    screen_transition_in_progress = true;
    
    switch (m_screen)
    {
    case SCR_HOME:
        draw_scr_home(m_scroll_dir);
        break;
#if defined(CONFIG_HPI_TODAY_SCREEN)
    case SCR_TODAY:
        draw_scr_today(m_scroll_dir);
        break;
#endif
    case SCR_HR:
        draw_scr_hr(m_scroll_dir);
        break;
    case SCR_SPO2:
        draw_scr_spo2(m_scroll_dir);
        break;
    case SCR_BPT:
        draw_scr_bpt(m_scroll_dir);
        break;
    case SCR_HRV_SUMMARY:
        //draw_scr_hrv_frequency_compact(m_scroll_dir,0,0,0,0);
        draw_scr_hrv_layout(m_scroll_dir);
        //draw_scr_spl_raw_ppg_hrv(m_scroll_dir,0,0,0,0);
        break;
    

    case SCR_TEMP:
        draw_scr_temp(m_scroll_dir);
        break;
    case SCR_ECG:
        draw_scr_ecg(m_scroll_dir);
        break;
    case SCR_GSR:
#if defined(CONFIG_HPI_GSR_SCREEN)
        draw_scr_gsr(m_scroll_dir);
#else
    printk("GSR screen disabled by config\n");
#endif
        break;

    /*case SCR_PLOT_EDA:
        draw_scr_pre(m_scroll_dir);
        break;*/
    default:
        printk("Invalid screen: %d", m_screen);
    }
    
    // CRITICAL: Clear transition flag after screen is loaded
    // This re-enables screen updates
    screen_transition_in_progress = false;
}

void hpi_move_load_scr_pulldown(enum scroll_dir m_scroll_dir)
{
    draw_scr_pulldown(m_scroll_dir, 0, 0, 0, 0);
}

/*
void disp_spl_screen_event(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    // lv_obj_t *target = lv_event_get_target(e);

    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_BOTTOM)
    {
        lv_indev_wait_release(lv_indev_get_act());
        printk("Down at %d\n", curr_screen);

        if (curr_screen == SCR_SPL_PULLDOWN)
        {
            hpi_load_screen(SCR_HOME, SCROLL_DOWN);
        }
    }
}*/

void draw_bg(lv_obj_t *parent)
{
    lv_obj_t *logo_bg = lv_img_create(parent);
    lv_img_set_src(logo_bg, &bck_heart_2_180);
    lv_obj_set_width(logo_bg, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(logo_bg, LV_SIZE_CONTENT); /// 1
    lv_obj_set_align(logo_bg, LV_ALIGN_CENTER);
    lv_obj_add_flag(logo_bg, LV_OBJ_FLAG_ADV_HITTEST);  /// Flags
    lv_obj_clear_flag(logo_bg, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    lv_obj_add_style(parent, &style_scr_black, 0);
}