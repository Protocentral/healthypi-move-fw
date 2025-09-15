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

// LVGL Styles
static lv_style_t style_btn;
/* Black button styles (global) */
static lv_style_t style_btn_black;
static lv_style_t style_btn_black_pressed;

// Global LVGL Styles
lv_style_t style_tiny;
lv_style_t style_scr_black;
lv_style_t style_red_medium;
lv_style_t style_lbl_red_small;

lv_style_t style_white_small;
lv_style_t style_white_medium;

lv_style_t style_scr_container;

lv_style_t style_lbl_white_14;
lv_style_t style_lbl_white_medium;
lv_style_t style_white_large_numeric;

lv_style_t style_bg_blue;
lv_style_t style_bg_red;
lv_style_t style_bg_green;
lv_style_t style_bg_purple;

static volatile uint8_t hpi_disp_curr_brightness = DISPLAY_DEFAULT_BRIGHTNESS;

static lv_obj_t *label_batt_level;
static lv_obj_t *label_batt_level_val;

static lv_obj_t *lbl_hdr_hour;
static lv_obj_t *lbl_hdr_min;
static lv_obj_t *ui_label_date;

lv_obj_t *cui_battery_percent;
int tmp_scr_parent = 0;

// Externs
extern const struct device *display_dev;

extern struct k_sem sem_stop_one_shot_spo2;

/*Will be called when the styles of the base theme are already added to add new styles*/
static void new_theme_apply_cb(lv_theme_t *th, lv_obj_t *obj)
{
    LV_UNUSED(th);

    
    //lv_style_set_bg_color(&style_scr_black, lv_color_black());
}

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

    // Label White Small
    lv_style_init(&style_white_small);
    lv_style_set_text_color(&style_white_small, lv_color_white());
    lv_style_set_text_font(&style_white_small, &lv_font_montserrat_24);

    lv_style_init(&style_white_medium);
    lv_style_set_text_color(&style_white_medium, lv_color_white());
    lv_style_set_text_font(&style_white_medium, &lv_font_montserrat_24);

    lv_style_init(&style_white_large_numeric);
    lv_style_set_text_color(&style_white_large_numeric, lv_color_white());
    lv_style_set_text_font(&style_white_large_numeric, &oxanium_90); // &ui_font_number_big); //&ui_font_Number_extra);

    // Label Red
    lv_style_init(&style_red_medium);
    lv_style_set_text_color(&style_red_medium, lv_palette_main(LV_PALETTE_RED));
    lv_style_set_text_font(&style_red_medium, &lv_font_montserrat_24);

    // Label White 14
    lv_style_init(&style_lbl_white_14);
    lv_style_set_text_color(&style_lbl_white_14, lv_color_white());
    lv_style_set_text_font(&style_lbl_white_14, &lv_font_montserrat_24);

    // Label Black
    lv_style_init(&style_lbl_white_medium);
    lv_style_set_text_color(&style_lbl_white_medium, lv_color_black());
    lv_style_set_text_font(&style_lbl_white_medium, &lv_font_montserrat_24);

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
    /* internal padding for better touch target and readable label */
    lv_style_set_pad_all(&style_btn_black, 12);
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
    /* keep same internal padding and external margin on pressed state */
    lv_style_set_pad_all(&style_btn_black_pressed, 12);
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

uint8_t hpi_disp_get_brightness(void)
{
    return hpi_disp_curr_brightness;
}

void hpi_disp_update_batt_level(int batt_level, bool charging)
{
    if (label_batt_level == NULL || label_batt_level_val == NULL)
    {
        return;
    }

    if (batt_level <= 0)
    {
        batt_level = 0;
    }

    // printk("Updating battery level: %d\n", batt_level);

    char buf[8];
    sprintf(buf, " %2d %% ", batt_level);
    lv_label_set_text(label_batt_level_val, buf);

    if (batt_level > 75)
    {
        if (charging)
            lv_label_set_text(label_batt_level, LV_SYMBOL_CHARGE " " LV_SYMBOL_BATTERY_FULL "");
        else
            lv_label_set_text(label_batt_level, LV_SYMBOL_BATTERY_FULL);
    }
    else if (batt_level > 50)
    {
        if (charging)
            lv_label_set_text(label_batt_level, LV_SYMBOL_CHARGE " " LV_SYMBOL_BATTERY_3 " ");
        else
            lv_label_set_text(label_batt_level, LV_SYMBOL_BATTERY_3);
    }
    else if (batt_level > 25)
    {
        if (charging)
            lv_label_set_text(label_batt_level, LV_SYMBOL_CHARGE " " LV_SYMBOL_BATTERY_2 " ");
        else
            lv_label_set_text(label_batt_level, LV_SYMBOL_BATTERY_2);
    }
    else if (batt_level > 10)
    {
        if (charging)
            lv_label_set_text(label_batt_level, LV_SYMBOL_CHARGE " " LV_SYMBOL_BATTERY_1 " ");
        else
            lv_label_set_text(label_batt_level, LV_SYMBOL_BATTERY_1);
    }
    else
    {
        if (charging)
            lv_label_set_text(label_batt_level, LV_SYMBOL_CHARGE " " LV_SYMBOL_BATTERY_EMPTY " ");
        else
            lv_label_set_text(label_batt_level, LV_SYMBOL_BATTERY_EMPTY);
    }
}

void hpi_show_screen(lv_obj_t *m_screen, enum scroll_dir m_scroll_dir)
{
    lv_obj_add_event_cb(m_screen, disp_screen_event, LV_EVENT_GESTURE, NULL);

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

void hpi_load_screen(int m_screen, enum scroll_dir m_scroll_dir)
{
    switch (m_screen)
    {
    case SCR_HOME:
        draw_scr_home(m_scroll_dir);
        break;
    case SCR_TODAY:
        draw_scr_today(m_scroll_dir);
        break;
    case SCR_HR:
        draw_scr_hr(m_scroll_dir);
        break;
    case SCR_SPO2:
        draw_scr_spo2(m_scroll_dir);
        break;
    case SCR_BPT:
        draw_scr_bpt(m_scroll_dir);
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