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
#include <zephyr/drivers/rtc.h>
#include <zephyr/pm/device.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/settings/settings.h>

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
lv_style_t style_health_arc_bg;

// Global LVGL Styles
lv_style_t style_scr_black;
lv_style_t style_red_medium;

lv_style_t style_white_medium;
lv_style_t style_white_small;


lv_style_t style_lbl_white_14;
lv_style_t style_white_large_numeric;

/* Modern typography styles - made global for external access */
lv_style_t style_body_medium;
lv_style_t style_caption;

/* Additional specialized styles */
lv_style_t style_numeric_large;  // For large numeric displays (time, main values)


static uint8_t hpi_disp_curr_brightness = DISPLAY_DEFAULT_BRIGHTNESS;
#define KEY_BRIGHTNESS "display/brightness"
#define MIN_BRIGHTNESS_PERCENT 5 

lv_obj_t *cui_battery_percent;
int tmp_scr_parent = 0;

// Externs
extern const struct device *display_dev;


static int brightness_settings_set(const char *name, size_t len,
                                   settings_read_cb read_cb, void *cb_arg)
{
    if (strcmp(name, "brightness") == 0) {
        if (len == sizeof(hpi_disp_curr_brightness)) {
            read_cb(cb_arg, &hpi_disp_curr_brightness,
                    sizeof(hpi_disp_curr_brightness));
        }
        return 0;
    }
    return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(display,
                               "display",
                               NULL,
                               brightness_settings_set,
                               NULL,
                               NULL);

void display_init_styles(void)
{
    /*Initialize the styles*/
    lv_style_init(&style_btn);
    lv_style_set_bg_color(&style_btn, lv_palette_darken(LV_PALETTE_GREY, 4));
    lv_style_set_border_color(&style_btn, lv_palette_darken(LV_PALETTE_RED, 3));
    lv_style_set_border_width(&style_btn, 3);

    // Subscript (Unit) label style

    lv_style_init(&style_white_medium);
    lv_style_set_text_color(&style_white_medium, lv_color_white());
    lv_style_set_text_font(&style_white_medium, &manrope_700_22);

    lv_style_init(&style_white_large_numeric);
    lv_style_set_text_color(&style_white_large_numeric, lv_color_white());
    lv_style_set_text_font(&style_white_large_numeric, &rubik_500_32); /* was oxanium_90 (retired for flash) */

    // Label Red
    lv_style_init(&style_red_medium);
    lv_style_set_text_color(&style_red_medium, lv_palette_main(LV_PALETTE_RED));
    lv_style_set_text_font(&style_red_medium, &manrope_700_22);

    // Label White 14
    lv_style_init(&style_lbl_white_14);
    lv_style_set_text_color(&style_lbl_white_14, lv_color_white());
    lv_style_set_text_font(&style_lbl_white_14, &manrope_700_22);

    // Container for scrollable screen layout

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

    /* (dead style_bg_blue/red/green/purple gradient styles removed - unused) */

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

    lv_style_init(&style_health_arc_bg);
    lv_style_set_arc_width(&style_health_arc_bg, 6);
    lv_style_set_arc_rounded(&style_health_arc_bg, true);
    lv_style_set_arc_color(&style_health_arc_bg, lv_color_hex(COLOR_SURFACE_LIGHT));
    lv_style_set_arc_opa(&style_health_arc_bg, LV_OPA_50);

    /* Initialize modern typography styles */


    lv_style_init(&style_body_medium);
    lv_style_set_text_color(&style_body_medium, lv_color_white());
    lv_style_set_text_font(&style_body_medium, &manrope_700_22); /* Standard body text */

    lv_style_init(&style_caption);
    lv_style_set_text_color(&style_caption, lv_color_hex(COLOR_TEXT_SECONDARY));
    lv_style_set_text_font(&style_caption, &manrope_700_22); /* Small labels and captions - increased to 24px minimum for small display readability */

    /* Initialize numeric display styles */
    lv_style_init(&style_numeric_large);
    lv_style_set_text_color(&style_numeric_large, lv_color_white());
    lv_style_set_text_font(&style_numeric_large, &rubik_500_32); /* Large numeric displays (time, hero values) */


    // Style for small status text

    //lv_disp_set_bg_color(NULL, lv_color_black());
}

void hpi_disp_restore_brightness(void)
{
    /* Load ONLY the "display" subtree (see the SETTINGS_STATIC_HANDLER_DEFINE above).
     *
     * This used to call the GLOBAL settings_load(), which re-runs EVERY registered
     * settings handler -- including Bluetooth's. ble_module_init() has already called
     * settings_load() once after bt_enable(), so this second pass made the BT host
     * re-load its bonded keys and re-add their IRKs, corrupting its resolving-list
     * bookkeeping. The symptom surfaces LATER, on disconnect: the host re-adds a
     * bonded peer's IRK (the entry cannot be touched while the link is up), the
     * controller sees an address already in the list, and returns
     * HCI 0x12 "Invalid HCI Command Parameters":
     *
     *     <wrn> bt_hci_core: opcode 0x2027 status 0x12
     *     <err> bt_id: Failed to add IRK to controller
     *
     * A peer whose IRK is not in the resolving list cannot have its RPA resolved, so
     * the watch stops recognising the bonded phone across reconnects.
     *
     * Nothing outside the display subtree has any business being re-loaded to read one
     * brightness byte. */
    (void)settings_load_subtree("display");
    hpi_disp_set_brightness(hpi_disp_curr_brightness);
}


/* A6: draw_scr_common() removed — every v2 screen styles its own root object
 * (bg 0x0E1114 + scroll flags), so nothing had called this since the v1 screens
 * went away. */

void hpi_disp_set_brightness(uint8_t brightness_percent)
{
    // Prevent screen from becoming completely black
    if (brightness_percent < MIN_BRIGHTNESS_PERCENT) {
        brightness_percent = MIN_BRIGHTNESS_PERCENT;
    }

    uint8_t brightness = (uint8_t)((brightness_percent * 255) / 100);
    display_set_brightness(display_dev, brightness);

    if (hpi_disp_curr_brightness != brightness_percent) {
        hpi_disp_curr_brightness = brightness_percent;

        settings_save_one(KEY_BRIGHTNESS,
                          &hpi_disp_curr_brightness,
                          sizeof(hpi_disp_curr_brightness));
    }
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

/* P5 screen cache: the carousel overview screens (SCR_LIST_START..SCR_LIST_END)
 * are built once and kept resident, so swiping between them reuses the existing
 * lv_obj tree instead of rebuilding it every time. Special (SCR_SPL_*) screens
 * are still rebuilt and auto-deleted as before. `auto_del` in lv_scr_load_anim
 * deletes the *outgoing* screen, so we suppress it only when leaving a cached
 * carousel screen; leaving a special screen still deletes it (no leak, no manual
 * lifetime juggling). */
#define HPI_SCR_IS_CAROUSEL(id) ((id) > SCR_LIST_START && (id) < SCR_LIST_END)
static int g_shown_scr_id = -1;

void hpi_show_screen(lv_obj_t *m_screen, enum scroll_dir m_scroll_dir)
{
    /* Re-showing a cached screen would stack a second gesture handler, so remove
     * any existing one first -> exactly one handler regardless of cache hits. */
    lv_obj_remove_event_cb(m_screen, disp_screen_event);
    lv_obj_add_event_cb(m_screen, disp_screen_event, LV_EVENT_GESTURE, NULL);

    /* Keep the outgoing screen alive if it is a cached carousel screen. */
    bool auto_del = !HPI_SCR_IS_CAROUSEL(g_shown_scr_id);

    /* The SH8601 is SPI-bound: a full 390x390 frame takes ~74 ms to push, so any
     * multi-frame slide/fade transition looks slow and jagged. Load instantly -
     * a clean single-frame swap - regardless of gesture direction. (The OVER_*
     * transitions even at SCREEN_TRANS_TIME=0 could still show a one-frame
     * composite artifact; ANIM_NONE avoids that entirely.) The direction arg is
     * retained for callers/back-compat but no longer drives a transition. */
    (void)m_scroll_dir;
    lv_scr_load_anim(m_screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, auto_del);

    /* Force a full repaint of the shown screen. A freshly-built screen is
     * already fully dirty, but a *cached* screen re-shown (e.g. reloaded on the
     * same page after wake-from-sleep, where lv_scr_load_anim to the already-
     * active screen is a no-op) would otherwise only redraw its dirty widgets
     * and leave the rest of the panel showing stale/garbage GRAM. Invalidating
     * marks the whole screen dirty so LVGL repaints it. */
    lv_obj_invalidate(m_screen);

    /* Record the now-current screen so the *next* transition knows whether the
     * screen it is leaving is cached. Set by the draw fn just before this call. */
    g_shown_scr_id = hpi_disp_get_curr_screen();
}

void hpi_load_screen(int m_screen, enum scroll_dir m_scroll_dir)
{
    // CRITICAL: Set global transition flag to suspend ALL screen updates
    // This protects the entire screen loading process across all screens
    screen_transition_in_progress = true;
    
    switch (m_screen)
    {
    case SCR_HOME:
        hpi_carousel_show(SCR_HOME, m_scroll_dir);   /* P6: tileview carousel (tile 0) */
        break;
    /* P6: all metric overview screens are now tiles in the carousel. Route each
     * to the carousel at the matching tile (e.g. measurement-return lands on it). */
    case SCR_HR:
    case SCR_SPO2:
    case SCR_BPT:
    case SCR_TEMP:
    case SCR_ECG:
    case SCR_ACTIVITY:
    case SCR_HRV:
    case SCR_GSR:
    case SCR_RECOVERY:
        hpi_carousel_show(m_screen, m_scroll_dir);
        break;
    default:
        printk("Invalid screen: %d", m_screen);
    }
    
    // CRITICAL: Clear transition flag after screen is loaded
    // This re-enables screen updates
    screen_transition_in_progress = false;
}

/* A6: hpi_move_load_scr_pulldown() removed — a one-line wrapper around
 * draw_scr_pulldown() with no callers; the shade is reached through
 * hpi_load_scr_spl(SCR_SPL_PULLDOWN, ...) like every other special screen. */

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