/*
 * HealthyPi Move — Height / Weight roller pickers
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Reached from the v2 settings screen (Height / Weight rows). These are the only
 * two survivors of the old device-user-settings menu, which P6 replaced and P2
 * deleted — they were extracted here so that file could go.
 *
 * They now persist through the per-key API (hpi_user_settings_set_height/weight
 * -> hpi_settings_save_single), which writes ONE key. The old menu's
 * hpi_auto_save_settings() called hpi_settings_save_all() with a file-static
 * `current_ui_settings` that was only ever loaded by the menu's own draw
 * function — and that menu became unreachable at P6. So the struct stayed zeroed
 * in BSS, and moving either roller wrote every OTHER setting as 0:
 * auto_sleep_enabled=0 made get_sleep_timeout_ms() return UINT32_MAX ("never
 * sleep" — the watch stopped sleeping until the low-battery path caught it), and
 * units / time format / hand-worn silently reset. hpi_settings_save_all() does
 * no validation, so nothing caught it. Per-key writes cannot express that bug.
 */

#include <zephyr/kernel.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/logging/log.h>

#include "ui/move_ui.h"
#include "hpi_user_settings_api.h"

LOG_MODULE_REGISTER(scr_hw_select, LOG_LEVEL_DBG);

extern lv_style_t style_lbl_white_14;

/* Ranges must match hpi_user_settings_set_height/weight's validation. */
#define HEIGHT_MIN 100
#define HEIGHT_MAX 250
#define WEIGHT_MIN 30
#define WEIGHT_MAX 200

static lv_obj_t *scr_height_select;
static lv_obj_t *roller_height;
static lv_obj_t *scr_weight_select;
static lv_obj_t *roller_weight;

/* Shared skeleton: black screen + centered column + title + roller. */
static lv_obj_t *build_picker(lv_obj_t **scr_out, const char *title,
                              const char *options, uint16_t sel,
                              lv_event_cb_t cb)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

    lv_obj_t *cont = lv_obj_create(scr);
    lv_obj_set_size(cont, 330, 330);
    lv_obj_align_to(cont, NULL, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(cont, 10, 0);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0x000000), 0);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);

    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, title);
    lv_obj_add_style(lbl, &style_lbl_white_14, 0);

    lv_obj_t *roller = lv_roller_create(cont);
    lv_obj_set_size(roller, 270, 240);
    lv_roller_set_options(roller, options, LV_ROLLER_MODE_NORMAL);
    lv_obj_add_event_cb(roller, cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_roller_set_selected(roller, sel, LV_ANIM_OFF);

    *scr_out = scr;
    return roller;
}

/* Build "<lo><unit>\n...<hi><unit>" into caller storage. */
static void build_options(char *buf, size_t cap, int lo, int hi, const char *unit)
{
    size_t used = 0;
    buf[0] = '\0';
    for (int i = lo; i <= hi && used < cap; i++) {
        int n = snprintf(buf + used, cap - used, "%d %s%s", i, unit, (i < hi) ? "\n" : "");
        if (n < 0 || (size_t)n >= cap - used) {
            break;   /* truncate rather than overrun */
        }
        used += (size_t)n;
    }
}

static void roller_height_event_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    uint16_t h = HEIGHT_MIN + lv_roller_get_selected(roller_height);
    if (hpi_user_settings_set_height(h) == 0) {
        LOG_DBG("Height set to %d cm", h);
    } else {
        LOG_ERR("Height %d cm rejected", h);
    }
}

static void roller_weight_event_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    uint16_t w = WEIGHT_MIN + lv_roller_get_selected(roller_weight);
    if (hpi_user_settings_set_weight(w) == 0) {
        LOG_DBG("Weight set to %d kg", w);
    } else {
        LOG_ERR("Weight %d kg rejected", w);
    }
}

void draw_scr_height_select(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2,
                            uint32_t arg3, uint32_t arg4)
{
    ARG_UNUSED(arg1); ARG_UNUSED(arg2); ARG_UNUSED(arg3); ARG_UNUSED(arg4);

    /* ~1.2 KB of option text. Stack-local (as before): app-core RAM is ~99.5%
     * full, so this must not become another static. */
    char options[(HEIGHT_MAX - HEIGHT_MIN + 1) * 8];
    build_options(options, sizeof(options), HEIGHT_MIN, HEIGHT_MAX, "cm");

    uint16_t cur = hpi_user_settings_get_height();
    if (cur < HEIGHT_MIN || cur > HEIGHT_MAX) {
        cur = 170;
    }
    roller_height = build_picker(&scr_height_select, "Select Height", options,
                                 (uint16_t)(cur - HEIGHT_MIN), roller_height_event_cb);

    hpi_disp_set_curr_screen(SCR_SPL_HEIGHT_SELECT);
    hpi_show_screen(scr_height_select, m_scroll_dir);
}

void gesture_down_scr_height_select(void)
{
    hpi_load_scr_spl(SCR_SPL_SETTINGS, SCROLL_UP, 0, 0, 0, 0);
}

void draw_scr_weight_select(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2,
                            uint32_t arg3, uint32_t arg4)
{
    ARG_UNUSED(arg1); ARG_UNUSED(arg2); ARG_UNUSED(arg3); ARG_UNUSED(arg4);

    char options[(WEIGHT_MAX - WEIGHT_MIN + 1) * 8];
    build_options(options, sizeof(options), WEIGHT_MIN, WEIGHT_MAX, "kg");

    uint16_t cur = hpi_user_settings_get_weight();
    if (cur < WEIGHT_MIN || cur > WEIGHT_MAX) {
        cur = 70;
    }
    roller_weight = build_picker(&scr_weight_select, "Select Weight", options,
                                 (uint16_t)(cur - WEIGHT_MIN), roller_weight_event_cb);

    hpi_disp_set_curr_screen(SCR_SPL_WEIGHT_SELECT);
    hpi_show_screen(scr_weight_select, m_scroll_dir);
}

void gesture_down_scr_weight_select(void)
{
    hpi_load_scr_spl(SCR_SPL_SETTINGS, SCROLL_UP, 0, 0, 0, 0);
}
