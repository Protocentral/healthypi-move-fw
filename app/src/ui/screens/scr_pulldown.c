/*
 * HealthyPi Move
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Quick-settings shade (swipe-down). Layout follows the v2 handoff: status +
 * brightness rows, a row of 62px circular quick-toggle tiles, then the SETTINGS
 * text pill. The handoff's DND / flashlight / find-phone tiles are dropped (this
 * AMOLED wrist device has no such capabilities); the tile row instead carries
 * the two real settings-backed toggles — Always-on Display and Battery saver —
 * plus a momentary Shutdown tile.
 */

#include <zephyr/kernel.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <app_version.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(scr_pulldown, LOG_LEVEL_INF);

#include "ui/move_ui.h"
#include "ui/hpi_r0_theme.h"
#include "ui/hpi_ui_subjects.h"
#include "hw_module.h"
#include "battery_module.h"

lv_obj_t *scr_pulldown;

static lv_obj_t *label_pulldown_batt_val;
static lv_obj_t *icon_pulldown_batt;
static lv_obj_t *msgbox_shutdown;

extern lv_style_t style_scr_black;
extern lv_style_t style_lbl_white_14;
extern lv_style_t style_body_medium;
extern lv_style_t style_numeric_large;
extern lv_style_t style_caption;

static void brightness_slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    hpi_disp_set_brightness(lv_slider_get_value(slider));
}

static void btn_shutdown_yes_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        LOG_DBG("Shutdown confirmed");
        lv_obj_del(msgbox_shutdown);
        msgbox_shutdown = NULL;
        hpi_hw_pmic_off();
    }
}

static void btn_shutdown_no_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        LOG_DBG("Shutdown cancelled");
        lv_obj_del(msgbox_shutdown);
        msgbox_shutdown = NULL;
    }
}

static void hpi_show_shutdown_mbox(void)
{
    msgbox_shutdown = lv_obj_create(lv_scr_act());
    lv_obj_set_size(msgbox_shutdown, LV_PCT(100), LV_PCT(100));
    lv_obj_center(msgbox_shutdown);
    lv_obj_set_style_bg_opa(msgbox_shutdown, LV_OPA_50, 0);
    lv_obj_set_style_bg_color(msgbox_shutdown, lv_color_black(), 0);

    lv_obj_t *dialog = lv_obj_create(msgbox_shutdown);
    lv_obj_set_size(dialog, 380, 228);
    lv_obj_center(dialog);
    lv_obj_set_style_bg_color(dialog, lv_color_make(64, 64, 64), 0);
    lv_obj_set_style_border_width(dialog, 2, 0);

    lv_obj_t *label = lv_label_create(dialog);
    lv_label_set_text(label, "Shutdown?");
    lv_obj_set_style_text_color(label, lv_color_hex(V2_VALUE), 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 38);

    lv_obj_t *btn_cont = lv_obj_create(dialog);
    lv_obj_set_size(btn_cont, LV_PCT(90), 76);
    lv_obj_align(btn_cont, LV_ALIGN_BOTTOM_MID, 0, -19);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *btn_yes = hpi_btn_create(btn_cont);
    lv_obj_set_size(btn_yes, 114, 57);
    lv_obj_add_event_cb(btn_yes, btn_shutdown_yes_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label_yes = lv_label_create(btn_yes);
    lv_label_set_text(label_yes, "Yes");
    lv_obj_center(label_yes);

    lv_obj_t *btn_no = hpi_btn_create(btn_cont);
    lv_obj_set_size(btn_no, 114, 57);
    lv_obj_add_event_cb(btn_no, btn_shutdown_no_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label_no = lv_label_create(btn_no);
    lv_label_set_text(label_no, "No");
    lv_obj_center(label_no);
}

static void btn_shutdown_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        LOG_INF("Shutdown button clicked");
        hpi_show_shutdown_mbox();
    }
}

static void settings_btn_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    hpi_load_scr_spl(SCR_SPL_SETTINGS, SCROLL_DOWN, SCR_SPL_PULLDOWN, 0, 0, 0);
}

/*
 * 62 px circular quick-toggle tile (v2 handoff language). Off: white @ ~8%
 * surface, #c7ced1 icon. On: accent fill, dark-on-accent icon. Momentary tiles
 * (Shutdown) just stay in the off style. icon_font NULL → LVGL default font
 * (needed for LV_SYMBOL_POWER; matsym has no power glyph).
 */
static void tile_apply_state(lv_obj_t *tile, bool on)
{
    lv_obj_t *ic = lv_obj_get_child(tile, 0);
    if (on) {
        lv_obj_set_style_bg_color(tile, lv_color_hex(hpi_accent_rgb()), 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(ic, lv_color_hex(R0_ON_ACCENT), 0);
    } else {
        lv_obj_set_style_bg_color(tile, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(tile, 20, 0);   /* ~8% soft surface */
        lv_obj_set_style_text_color(ic, lv_color_hex(0xC7CED1), 0);
    }
}

static lv_obj_t *make_tile(lv_obj_t *parent, const char *icon,
                           const lv_font_t *icon_font, bool on, lv_event_cb_t cb)
{
    lv_obj_t *t = lv_obj_create(parent);
    lv_obj_remove_style_all(t);
    lv_obj_set_size(t, 62, 62);
    lv_obj_set_style_radius(t, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(t, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ic = lv_label_create(t);
    lv_label_set_text(ic, icon);
    if (icon_font != NULL) {
        lv_obj_set_style_text_font(ic, icon_font, 0);
    }
    lv_obj_center(ic);

    tile_apply_state(t, on);   /* icon is child 0 → sets bg + icon color */
    return t;
}

static void tile_aod_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    hpi_v2_aod_set(!hpi_v2_aod_get());
    tile_apply_state(lv_event_get_target(e), hpi_v2_aod_get());
}

/* A7: the Battery-saver quick tile is gone — the flag it toggled was never read
 * by any power policy. Setting storage stays in hpi_v2_widgets.c so the tile can
 * come back unchanged when there is something for it to do. */

/* P1-1: shade return target when opened from Settings → Brightness, etc. */
static uint32_t m_pulldown_parent = SCR_HOME;

void draw_scr_pulldown(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    ARG_UNUSED(arg4);

    m_pulldown_parent = (arg1 != 0) ? arg1 : SCR_HOME;
    scr_pulldown = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_pulldown, lv_color_hex(0x0E1114), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(scr_pulldown, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(scr_pulldown, LV_OBJ_FLAG_SCROLLABLE);

    /* Row 1: clock + battery pill */
    lv_obj_t *clock = lv_label_create(scr_pulldown);
    lv_label_set_text(clock, "00:00");
    lv_obj_set_style_text_font(clock, &HPI_FONT_VALUE, 0);   /* Rubik 32 */
    lv_obj_set_style_text_color(clock, lv_color_hex(V2_VALUE), 0);
    lv_obj_align(clock, LV_ALIGN_TOP_MID, -34, 48);
    hpi_ui_bind_label(clock, &subj_time);

    lv_obj_t *bpill = hpi_v2_pill(scr_pulldown, 0, 0);
    lv_obj_align_to(bpill, clock, LV_ALIGN_OUT_RIGHT_MID, 14, 0);
    lv_obj_t *bicon = lv_label_create(bpill);
    lv_label_set_text(bicon, SYM_BATT);
    lv_obj_set_style_text_font(bicon, &HPI_FONT_ICON, 0);
    lv_obj_set_style_text_color(bicon, lv_color_hex(V2_SPO2), 0);
    icon_pulldown_batt = bicon;
    hpi_ui_bind_batt_icon(bicon);

    label_pulldown_batt_val = lv_label_create(bpill);
    lv_label_set_text(label_pulldown_batt_val, "--%");
    lv_obj_set_style_text_font(label_pulldown_batt_val, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(label_pulldown_batt_val, lv_color_hex(0xB6BEC1), 0);
    hpi_ui_bind_batt_pct(label_pulldown_batt_val);

    /* Row 2: brightness slider with lo/hi icons */
    lv_obj_t *b_lo = lv_label_create(scr_pulldown);
    lv_label_set_text(b_lo, SYM_BRIGHT_LO);
    lv_obj_set_style_text_font(b_lo, &HPI_FONT_ICON, 0);
    lv_obj_set_style_text_color(b_lo, lv_color_hex(0x7F888C), 0);
    lv_obj_align(b_lo, LV_ALIGN_CENTER, -140, -58);

    lv_obj_t *slider = lv_slider_create(scr_pulldown);
    lv_obj_set_size(slider, 200, 10);
    lv_obj_align(slider, LV_ALIGN_CENTER, 0, -58);
    lv_slider_set_range(slider, 10, 100);
    lv_slider_set_value(slider, hpi_disp_get_brightness(), LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, brightness_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(hpi_accent_rgb()), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(hpi_accent_rgb()), LV_PART_KNOB);

    lv_obj_t *b_hi = lv_label_create(scr_pulldown);
    lv_label_set_text(b_hi, SYM_BRIGHT_HI);
    lv_obj_set_style_text_font(b_hi, &HPI_FONT_ICON, 0);
    lv_obj_set_style_text_color(b_hi, lv_color_hex(0xC7CED1), 0);
    lv_obj_align(b_hi, LV_ALIGN_CENTER, 140, -58);

    /* Row 3: quick-toggle tiles — Always-on (real toggle) + momentary Shutdown.
     * (Handoff DND/flashlight/find-phone dropped; Battery saver removed in A7.) */
    lv_obj_t *trow = lv_obj_create(scr_pulldown);
    lv_obj_remove_style_all(trow);
    lv_obj_set_size(trow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(trow, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_flex_flow(trow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(trow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(trow, 14, 0);

    /* The AOD glyph lives only in matsym_26 (HPI_FONT_ICON_MD). */
    make_tile(trow, SYM_AOD, &HPI_FONT_ICON_MD, hpi_v2_aod_get(), tile_aod_cb);
    /* Power glyph is LVGL built-in only — leave font NULL for the default bin. */
    make_tile(trow, LV_SYMBOL_POWER, NULL, false, btn_shutdown_event_cb);

    /* Row 4: SETTINGS text pill (handoff) — soft white @ ~8%, full radius. */
    lv_obj_t *set_btn = lv_obj_create(scr_pulldown);
    lv_obj_remove_style_all(set_btn);
    lv_obj_set_size(set_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(set_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(set_btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(set_btn, 20, 0);
    lv_obj_set_style_pad_hor(set_btn, 26, 0);
    lv_obj_set_style_pad_ver(set_btn, 12, 0);
    lv_obj_set_style_pad_column(set_btn, 10, 0);
    lv_obj_align(set_btn, LV_ALIGN_CENTER, 0, 108);
    lv_obj_clear_flag(set_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(set_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(set_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(set_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(set_btn, settings_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *set_ic = lv_label_create(set_btn);
    lv_label_set_text(set_ic, SYM_SETTINGS);
    lv_obj_set_style_text_font(set_ic, &HPI_FONT_ICON, 0);   /* matsym 24 */
    lv_obj_set_style_text_color(set_ic, lv_color_hex(0xEEF1F2), 0);

    lv_obj_t *set_lbl = lv_label_create(set_btn);
    lv_label_set_text(set_lbl, "SETTINGS");
    lv_obj_set_style_text_font(set_lbl, &HPI_FONT_LABEL, 0);  /* Manrope 700 22 */
    lv_obj_set_style_text_color(set_lbl, lv_color_hex(0xEEF1F2), 0);
    lv_obj_set_style_text_letter_space(set_lbl, 1, 0);        /* ~.06em tracking */

    hpi_disp_set_curr_screen(SCR_SPL_PULLDOWN);
    hpi_show_screen(scr_pulldown, m_scroll_dir);
}

void hpi_disp_settings_update_batt_level(int batt_level, bool charging)
{
    /* Battery UI is subject-bound; subjects update from the display push path.
     * Keep this hook for call sites that refresh while the shade is open. */
    ARG_UNUSED(batt_level);
    ARG_UNUSED(charging);
    if (label_pulldown_batt_val == NULL) {
        return;
    }
}

void gesture_down_scr_pulldown(void)
{
    /* P1-1: honour the opening screen. */
    if (m_pulldown_parent >= SCR_SPL_LIST_START) {
        hpi_load_scr_spl(m_pulldown_parent, SCROLL_DOWN, SCR_HOME, 0, 0, 0);
    } else {
        hpi_load_screen(m_pulldown_parent, SCROLL_DOWN);
    }
    m_pulldown_parent = SCR_HOME;
}
