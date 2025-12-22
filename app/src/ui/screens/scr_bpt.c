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
#include <lvgl.h>
#include <stdio.h>

#include "ui/move_ui.h"
#include "hpi_common_types.h"
#include "hpi_sys.h"

LOG_MODULE_REGISTER(scr_bpt, LOG_LEVEL_DBG);

lv_obj_t *scr_bpt;

static lv_obj_t *label_bpt_sys;
static lv_obj_t *label_bpt_dia;
static lv_obj_t *label_bpt_last_update_time;

// Externs - Modern style system
extern lv_style_t style_body_medium;
extern lv_style_t style_numeric_large;
extern lv_style_t style_caption;

// Color definitions for BP theme
#define COLOR_BP_SYS    0x4A90E2  // Blue for systolic
#define COLOR_BP_DIA    0x7B68EE  // Purple for diastolic

static void scr_bpt_measure_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        LOG_DBG("Measure click");
        hpi_load_scr_spl(SCR_SPL_FI_SENS_WEAR, SCROLL_UP, SCR_SPL_FI_SENS_CHECK, (uint8_t)SCR_BPT, 0, 0);
    }
}

void draw_scr_bpt(enum scroll_dir m_scroll_dir)
{
    scr_bpt = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_bpt, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_bpt, LV_OBJ_FLAG_SCROLLABLE);

    /*
     * COMPACT LAYOUT FOR 390x390 ROUND AMOLED
     * ========================================
     * Blood pressure needs special handling for dual SYS/DIA values
     * Layout:
     *   Top: y=40 (title)
     *   Upper: y=85 (SYS/DIA labels)
     *   Center: y=115 (large values side by side)
     *   Lower: y=185 (mmHg units)
     *   Status: y=215 (last update)
     *   Bottom: y=260 (button)
     */

    // Get blood pressure data
    uint8_t bpt_sys = 0;
    uint8_t bpt_dia = 0;
    int64_t bpt_time = 0;

    if (hpi_sys_get_last_bp_update(&bpt_sys, &bpt_dia, &bpt_time) != 0) {
        bpt_sys = 0;
        bpt_dia = 0;
        bpt_time = 0;
    }

    // TOP: Title at y=40
    lv_obj_t *label_title = lv_label_create(scr_bpt);
    lv_label_set_text(label_title, "Blood Pressure");
    lv_obj_set_pos(label_title, 0, 40);
    lv_obj_set_width(label_title, 390);
    lv_obj_add_style(label_title, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);

    // UPPER: SYS / DIA labels at y=85
    lv_obj_t *label_sys_title = lv_label_create(scr_bpt);
    lv_label_set_text(label_sys_title, "SYS");
    lv_obj_set_pos(label_sys_title, 70, 85);
    lv_obj_set_width(label_sys_title, 100);
    lv_obj_add_style(label_sys_title, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_sys_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_sys_title, lv_color_hex(COLOR_BP_SYS), LV_PART_MAIN);

    lv_obj_t *label_dia_title = lv_label_create(scr_bpt);
    lv_label_set_text(label_dia_title, "DIA");
    lv_obj_set_pos(label_dia_title, 220, 85);
    lv_obj_set_width(label_dia_title, 100);
    lv_obj_add_style(label_dia_title, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_dia_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_dia_title, lv_color_hex(COLOR_BP_DIA), LV_PART_MAIN);

    // CENTER: Large SYS and DIA values at y=115
    label_bpt_sys = lv_label_create(scr_bpt);
    if (bpt_sys == 0) {
        lv_label_set_text(label_bpt_sys, "--");
    } else {
        lv_label_set_text_fmt(label_bpt_sys, "%d", bpt_sys);
    }
    lv_obj_set_pos(label_bpt_sys, 70, 115);
    lv_obj_set_width(label_bpt_sys, 100);
    lv_obj_add_style(label_bpt_sys, &style_numeric_large, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_bpt_sys, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_bpt_sys, lv_color_white(), LV_PART_MAIN);

    // Slash separator between values
    lv_obj_t *label_slash = lv_label_create(scr_bpt);
    lv_label_set_text(label_slash, "/");
    lv_obj_set_pos(label_slash, 180, 125);
    lv_obj_set_width(label_slash, 30);
    lv_obj_add_style(label_slash, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_slash, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_slash, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);

    label_bpt_dia = lv_label_create(scr_bpt);
    if (bpt_dia == 0) {
        lv_label_set_text(label_bpt_dia, "--");
    } else {
        lv_label_set_text_fmt(label_bpt_dia, "%d", bpt_dia);
    }
    lv_obj_set_pos(label_bpt_dia, 220, 115);
    lv_obj_set_width(label_bpt_dia, 100);
    lv_obj_add_style(label_bpt_dia, &style_numeric_large, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_bpt_dia, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_bpt_dia, lv_color_white(), LV_PART_MAIN);

    // LOWER: mmHg unit labels at y=185
    lv_obj_t *label_sys_unit = lv_label_create(scr_bpt);
    lv_label_set_text(label_sys_unit, "mmHg");
    lv_obj_set_pos(label_sys_unit, 70, 185);
    lv_obj_set_width(label_sys_unit, 100);
    lv_obj_add_style(label_sys_unit, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_sys_unit, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_sys_unit, lv_color_hex(COLOR_BP_SYS), LV_PART_MAIN);

    lv_obj_t *label_dia_unit = lv_label_create(scr_bpt);
    lv_label_set_text(label_dia_unit, "mmHg");
    lv_obj_set_pos(label_dia_unit, 220, 185);
    lv_obj_set_width(label_dia_unit, 100);
    lv_obj_add_style(label_dia_unit, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_dia_unit, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_dia_unit, lv_color_hex(COLOR_BP_DIA), LV_PART_MAIN);

    // STATUS: Last measurement time at y=215
    label_bpt_last_update_time = lv_label_create(scr_bpt);
    if (bpt_sys == 0 && bpt_dia == 0) {
        lv_label_set_text(label_bpt_last_update_time, "No measurement yet");
    } else {
        char last_meas_str[25];
        hpi_helper_get_relative_time_str(bpt_time, last_meas_str, sizeof(last_meas_str));
        lv_label_set_text(label_bpt_last_update_time, last_meas_str);
    }
    lv_obj_set_pos(label_bpt_last_update_time, 0, 215);
    lv_obj_set_width(label_bpt_last_update_time, 390);
    lv_obj_add_style(label_bpt_last_update_time, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_bpt_last_update_time, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_bpt_last_update_time, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);

    // BOTTOM: Measure button at y=260
    const int btn_width = 200;
    const int btn_height = 60;
    const int btn_y = 260;

    lv_obj_t *btn_bpt_measure = hpi_btn_create_primary(scr_bpt);
    lv_obj_set_size(btn_bpt_measure, btn_width, btn_height);
    lv_obj_set_pos(btn_bpt_measure, (390 - btn_width) / 2, btn_y);
    lv_obj_set_style_radius(btn_bpt_measure, 30, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn_bpt_measure, lv_color_hex(COLOR_BTN_BLUE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_bpt_measure, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_bpt_measure, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn_bpt_measure, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_bpt_measure, scr_bpt_measure_handler, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label_btn_measure = lv_label_create(btn_bpt_measure);
    lv_label_set_text(label_btn_measure, LV_SYMBOL_PLAY " Measure");
    lv_obj_center(label_btn_measure);
    lv_obj_set_style_text_color(label_btn_measure, lv_color_white(), LV_PART_MAIN);

    hpi_disp_set_curr_screen(SCR_BPT);
    hpi_show_screen(scr_bpt, m_scroll_dir);
}
