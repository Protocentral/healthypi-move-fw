/*
 * HealthyPi Move — Firmware-update (DFU) modal screen
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Full-screen takeover shown while a BLE/SMP OTA is running. Deliberately static
 * (no waveforms/animations) so LVGL redraws don't steal CPU/bus from the SMP +
 * QSPI-flash write path. It is a table-less special screen (no entry in
 * smf_display's screen_func_table), so it has a NULL gesture_down and the
 * nav-hardening in st_display_active_entry makes it unescapable — the user cannot
 * swipe away mid-upload. Driven by the display thread from hpi_dfu state.
 */

#include <lvgl.h>
#include <stdio.h>
#include <zephyr/logging/log.h>

#include "hpi_common_types.h"
#include "hw_module.h"
#include "ui/move_ui.h"
#include "ui/hpi_r0_theme.h"

LOG_MODULE_REGISTER(scr_dfu, LOG_LEVEL_WRN);

lv_obj_t *scr_dfu;

static lv_obj_t *label_dfu_title;
static lv_obj_t *label_dfu_pct;
static lv_obj_t *bar_dfu;
static lv_obj_t *label_dfu_msg;
static lv_obj_t *label_dfu_batt;

static void dfu_del(lv_event_t *e)
{
    ARG_UNUSED(e);
    label_dfu_title = NULL;
    label_dfu_pct = NULL;
    bar_dfu = NULL;
    label_dfu_msg = NULL;
    label_dfu_batt = NULL;
}

void draw_scr_dfu(void)
{
    scr_dfu = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_dfu, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_dfu, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr_dfu, dfu_del, LV_EVENT_DELETE, NULL);

    label_dfu_title = lv_label_create(scr_dfu);
    lv_label_set_text(label_dfu_title, "FIRMWARE UPDATE");
    lv_obj_align(label_dfu_title, LV_ALIGN_CENTER, 0, -120);
    lv_obj_set_style_text_font(label_dfu_title, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(label_dfu_title, lv_color_hex(V2_LABEL), 0);
    lv_obj_set_style_text_letter_space(label_dfu_title, 2, 0);

    /* big percent (HERO subsets %) */
    label_dfu_pct = lv_label_create(scr_dfu);
    lv_label_set_text(label_dfu_pct, "0%");
    lv_obj_align(label_dfu_pct, LV_ALIGN_CENTER, 0, -40);
    lv_obj_set_style_text_font(label_dfu_pct, &HPI_FONT_HERO, 0);
    lv_obj_set_style_text_color(label_dfu_pct, lv_color_hex(V2_BP), 0);
    lv_obj_set_style_text_letter_space(label_dfu_pct, -2, 0);

    bar_dfu = lv_bar_create(scr_dfu);
    lv_obj_set_size(bar_dfu, 240, 10);
    lv_obj_align(bar_dfu, LV_ALIGN_CENTER, 0, 34);
    lv_bar_set_range(bar_dfu, 0, 100);
    lv_bar_set_value(bar_dfu, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar_dfu, 5, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_dfu, 5, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar_dfu, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar_dfu, 26, LV_PART_MAIN);        /* ~10% track */
    lv_obj_set_style_bg_color(bar_dfu, lv_color_hex(V2_BP), LV_PART_INDICATOR);

    label_dfu_msg = lv_label_create(scr_dfu);
    lv_label_set_long_mode(label_dfu_msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_dfu_msg, 300);
    lv_label_set_text(label_dfu_msg, "Keep your phone nearby.\nDon't close the app.");
    lv_obj_align(label_dfu_msg, LV_ALIGN_CENTER, 0, 90);
    lv_obj_set_style_text_font(label_dfu_msg, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_align(label_dfu_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label_dfu_msg, lv_color_hex(V2_MUTED), 0);

    label_dfu_batt = lv_label_create(scr_dfu);
    lv_label_set_text_fmt(label_dfu_batt, "BATTERY %u%%", hw_get_current_battery_level());
    lv_obj_align(label_dfu_batt, LV_ALIGN_CENTER, 0, 150);
    lv_obj_set_style_text_font(label_dfu_batt, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(label_dfu_batt, lv_color_hex(V2_MUTED2), 0);
    lv_obj_set_style_text_letter_space(label_dfu_batt, 2, 0);

    hpi_disp_set_curr_screen(SCR_SPL_DFU);
    hpi_show_screen(scr_dfu, SCROLL_NONE);
}

void hpi_disp_dfu_update(int pct)
{
    if (bar_dfu == NULL || label_dfu_pct == NULL) {
        return;
    }
    if (pct < 0) {
        pct = 0;
    } else if (pct > 100) {
        pct = 100;
    }
    lv_bar_set_value(bar_dfu, pct, LV_ANIM_OFF);
    lv_label_set_text_fmt(label_dfu_pct, "%d%%", pct);
    if (label_dfu_batt != NULL) {
        lv_label_set_text_fmt(label_dfu_batt, "BATTERY %u%%", hw_get_current_battery_level());
    }
}

/* Title + guidance for the terminal phases (finalizing / failed). */
void hpi_disp_dfu_set_phase(const char *title, const char *message, uint32_t accent)
{
    if (label_dfu_title != NULL && title != NULL) {
        lv_label_set_text(label_dfu_title, title);
    }
    if (label_dfu_msg != NULL && message != NULL) {
        lv_label_set_text(label_dfu_msg, message);
    }
    if (label_dfu_pct != NULL) {
        lv_obj_set_style_text_color(label_dfu_pct, lv_color_hex(accent), 0);
    }
    if (bar_dfu != NULL) {
        lv_obj_set_style_bg_color(bar_dfu, lv_color_hex(accent), LV_PART_INDICATOR);
    }
}
