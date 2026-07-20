/*
 * HealthyPi Move
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * P6 metric carousel: a single lv_tileview. Tile 0 is the watch face; tiles
 * 1..N are accent-coded metric tiles from one generic template. Every value is
 * bound to an lv_subject (P6 step A) - there is no poll timer or per-screen
 * update function; the display thread pushes the subjects and each bound label
 * updates itself only when its value changes.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <stdio.h>

#include "ui/move_ui.h"
#include "hpi_sys.h"
#include "ui/hpi_ui_subjects.h"
#include "ui/hpi_ui_typescale.h"
#include "ui/hpi_r0_theme.h"

LOG_MODULE_REGISTER(hpi_disp_scr_carousel, LOG_LEVEL_DBG);

lv_obj_t *scr_carousel = NULL;
static lv_obj_t *carousel_tv = NULL;

/* Lazy tile content: empty tile shells are always created; monitor widgets are
 * populated on first visit. Building all 9 heavy tiles at once OOM'd LVGL
 * (lv_draw_label ASSERT_MALLOC) after the placeholder fill-in. */
static lv_obj_t *carousel_tiles[1 + 9]; /* home + M_COUNT (max 9 metrics) */
static uint16_t carousel_built_mask;   /* bit 0 = home, bit (i+1) = metric i */

/* home (watch face) widgets */
static lv_obj_t *home_hint = NULL;
static lv_obj_t *home_warn = NULL;

/* metric tiles — v2 handoff swipe order:
 * Home · HR · ECG · SpO2 · BP · Temp · Activity · Stress · EDA · Recovery.
 * Recovery (H6 readiness) is appended after the handoff's 9 — a placeholder tile
 * pending a dedicated design. */
enum { M_HR, M_ECG, M_SPO2, M_BPT, M_TEMP, M_ACTIVITY, M_HRV, M_GSR, M_RECOVERY, M_COUNT };

struct metric_desc {
    const char *title;
    uint32_t accent;
    const char *unit;
    int action;   /* SCR_SPL_* to open on tap, or -1 */
};

static const struct metric_desc metrics[M_COUNT] = {
    [M_HR]       = {"HR",   0xFF4D6D, "BPM",  -1},
    [M_ECG]      = {"ECG",  0x34D399, "BPM",  -1},
    [M_SPO2]     = {"SpO2", V2_SPO2, "%",    -1},   /* v2 idle tile owns the source toggle + START */
    [M_TEMP]     = {"Temp", 0xFBBF24, "",     -1},
    [M_ACTIVITY] = {"Activity", 0x16A34A, "", -1},
    [M_HRV]      = {"HRV",  0xA78BFA, "ms",   SCR_SPL_HRV_EVAL_PROGRESS},
    [M_BPT]      = {"BP",   V2_BP,  "mmHg", -1},   /* v2 idle tile owns MEASURE */
    [M_GSR]      = {"EDA",  V2_EDA, "uS",   -1},   /* v2 idle tile owns MEASURE */
    [M_RECOVERY] = {"Recovery", 0x4ADE80, "", -1}, /* H6 readiness 0..100 (placeholder) */
};

/* idx -> subject (see hpi_ui_subjects.c) */
static lv_subject_t *const metric_subj[M_COUNT] = {
    [M_HR] = &subj_hr, [M_SPO2] = &subj_spo2, [M_ECG] = &subj_ecg, [M_TEMP] = &subj_temp,
    [M_ACTIVITY] = &subj_steps, [M_BPT] = &subj_bp, [M_HRV] = &subj_hrv, [M_GSR] = &subj_gsr,
    [M_RECOVERY] = &subj_recovery,
};

extern lv_style_t style_numeric_large;

static void carousel_delete_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    scr_carousel = NULL;
    carousel_tv = NULL;
    carousel_built_mask = 0;
    for (int i = 0; i < (int)(sizeof(carousel_tiles) / sizeof(carousel_tiles[0])); i++) {
        carousel_tiles[i] = NULL;
    }
    home_hint = NULL;
    home_warn = NULL;
}

/* Drop the cached carousel so the next show rebuilds it fresh - used after a
 * settings change (watch face / accent / units) so the new look takes effect. */
void hpi_carousel_rebuild(void)
{
    if (scr_carousel != NULL) {
        lv_obj_del(scr_carousel);   /* delete cb clears scr_carousel + carousel_tv */
    }
}

static void metric_tap_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < M_COUNT && metrics[idx].action >= 0) {
        hpi_load_scr_spl(metrics[idx].action, SCROLL_UP, SCR_HOME, 0, 0, 0);
    }
}

/* Toggle the home hint / "set time" reminder whenever the time subject changes. */
static void home_timevalid_cb(lv_observer_t *observer, lv_subject_t *subject)
{
    ARG_UNUSED(observer);
    ARG_UNUSED(subject);
    bool ok = hpi_sys_is_time_valid();
    if (home_hint) {
        ok ? lv_obj_clear_flag(home_hint, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(home_hint, LV_OBJ_FLAG_HIDDEN);
    }
    if (home_warn) {
        ok ? lv_obj_add_flag(home_warn, LV_OBJ_FLAG_HIDDEN) : lv_obj_clear_flag(home_warn, LV_OBJ_FLAG_HIDDEN);
    }
}

static void populate_metric_tile(lv_obj_t *tile, int idx)
{
    /* R2: HR is a full swipe-native monitor built from the template (no tap-in). */
    if (idx == M_HR) {
        hpi_hr_monitor_into(tile);
        return;
    }
    /* SpO2 is a spot check with its own Start/Stop control (no tile tap). */
    if (idx == M_SPO2) {
        hpi_spo2_monitor_into(tile);
        return;
    }
    /* Temp + Activity are passive (subject-bound), no measurement flow. */
    if (idx == M_TEMP) {
        hpi_temp_monitor_into(tile);
        return;
    }
    if (idx == M_ACTIVITY) {
        hpi_activity_monitor_into(tile);
        return;
    }
    /* ECG is a spot check with its own Start/Stop control (no tile tap). */
    if (idx == M_ECG) {
        hpi_ecg_monitor_into(tile);
        return;
    }
    /* BP + EDA are v2 idle tiles with their own MEASURE button that launches
     * the finger / GSR capture flow (no tile tap). */
    if (idx == M_BPT) {
        hpi_bpt_monitor_into(tile);
        return;
    }
    if (idx == M_GSR) {
        hpi_eda_monitor_into(tile);
        return;
    }
    /* HRV/Stress stays tappable to reach its measure flow (for now). */
    if (idx == M_HRV) {
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(tile, metric_tap_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
        hpi_stress_monitor_into(tile);
        return;
    }

    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tile, metric_tap_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    lv_obj_t *title = lv_label_create(tile);
    lv_label_set_text(title, metrics[idx].title);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -96);
    lv_obj_set_style_text_color(title, lv_color_hex(metrics[idx].accent), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &FONT_TITLE, LV_PART_MAIN);

    lv_obj_t *val = lv_label_create(tile);
    lv_label_set_text(val, "--");
    lv_obj_align(val, LV_ALIGN_CENTER, 0, -18);
    lv_obj_add_style(val, &style_numeric_large, LV_PART_MAIN);   /* Inter 80 hero */
    lv_obj_set_style_text_color(val, lv_color_hex(metrics[idx].accent), LV_PART_MAIN);
    if (idx == M_BPT) {
        lv_obj_set_style_text_font(val, &FONT_HERO_DUAL, LV_PART_MAIN);  /* "120/80" */
    }
    hpi_ui_bind_label(val, metric_subj[idx]);   /* observer binding */

    if (metrics[idx].unit[0] != '\0') {
        lv_obj_t *unit = lv_label_create(tile);
        lv_label_set_text(unit, metrics[idx].unit);
        lv_obj_align(unit, LV_ALIGN_CENTER, 0, 34);
        lv_obj_set_style_text_color(unit, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);
        lv_obj_set_style_text_font(unit, &FONT_UNIT, LV_PART_MAIN);
    }

    if (metrics[idx].action >= 0) {
        lv_obj_t *hint = lv_label_create(tile);
        lv_label_set_text(hint, "tap to measure");
        lv_obj_align(hint, LV_ALIGN_CENTER, 0, 120);
        lv_obj_set_style_text_color(hint, lv_color_hex(0x5A5A62), LV_PART_MAIN);
        lv_obj_set_style_text_font(hint, &FONT_CAPTION, LV_PART_MAIN);
    }
}

static void build_home_tile(lv_obj_t *tile);
static void build_home_minimal_tile(lv_obj_t *tile);

static void populate_home_tile(lv_obj_t *tile)
{
    if (hpi_v2_face_get() == 1) {
        build_home_minimal_tile(tile);
    } else {
        build_home_tile(tile);
    }
}

/* Build tile content on first show. tile_idx: 0 = home, 1..M_COUNT = metrics. */
static void carousel_ensure_tile(int tile_idx)
{
    if (tile_idx < 0 || tile_idx > M_COUNT) {
        return;
    }
    uint16_t bit = (uint16_t)(1u << tile_idx);
    if (carousel_built_mask & bit) {
        return;
    }
    lv_obj_t *tile = carousel_tiles[tile_idx];
    if (tile == NULL) {
        return;
    }
    if (tile_idx == 0) {
        populate_home_tile(tile);
    } else {
        populate_metric_tile(tile, tile_idx - 1);
    }
    carousel_built_mask |= bit;
    LOG_DBG("carousel: built tile %d", tile_idx);
}

/* Also prepare neighbors so the next swipe is cheap and not empty. */
static void carousel_ensure_neighborhood(int tile_idx)
{
    carousel_ensure_tile(tile_idx);
    if (tile_idx > 0) {
        carousel_ensure_tile(tile_idx - 1);
    }
    if (tile_idx < M_COUNT) {
        carousel_ensure_tile(tile_idx + 1);
    }
}

/* One home complication: Material Symbol icon over its value (R0 design). */
/* One home complication: icon + [Value number over Label caption], left-aligned. */
static void home_comp(lv_obj_t *row, const char *sym, uint32_t color,
                      lv_subject_t *subj, const char *cap)
{
    /* v2 metric chip: soft rounded background. Content stacked into 2 lines
     * (icon + value on top, label below) so the chip stays narrow. */
    lv_obj_t *cell = hpi_v2_chip(row);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cell, 2, 0);
    lv_obj_set_style_pad_hor(cell, 18, 0);

    /* line 1: icon + value */
    lv_obj_t *top = lv_obj_create(cell);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(top, 8, 0);

    lv_obj_t *ic = lv_label_create(top);
    lv_label_set_text(ic, sym);
    lv_obj_set_style_text_font(ic, &HPI_FONT_ICON_LG, 0);   /* Material Symbols 28 */
    lv_obj_set_style_text_color(ic, lv_color_hex(color), 0);

    lv_obj_t *v = lv_label_create(top);
    lv_label_set_text(v, "--");
    lv_obj_set_style_text_font(v, &HPI_FONT_VALUE, 0);      /* Rubik 32 */
    lv_obj_set_style_text_color(v, lv_color_hex(V2_VALUE), 0);
    lv_obj_set_style_text_letter_space(v, -1, 0);
    hpi_ui_bind_label(v, subj);

    /* line 2: label */
    lv_obj_t *cp = lv_label_create(cell);
    lv_label_set_text(cp, cap);
    lv_obj_set_style_text_font(cp, &HPI_FONT_LABEL, 0);     /* Manrope 22 */
    lv_obj_set_style_text_color(cp, lv_color_hex(V2_MUTED), 0);
    lv_obj_set_style_text_letter_space(cp, 1, 0);
}

/* R0 design-language home (finalized): status row (date . battery) -> Hero time
 * -> 2 complications, over the amber radar motif. */
static void build_home_tile(lv_obj_t *tile)
{
    lv_obj_set_style_bg_color(tile, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    hpi_v2_dial_bg(tile);   /* dial motif behind the watch face */

    /* status row: DATE . [batt icon] NN% */
    lv_obj_t *strow = lv_obj_create(tile);
    lv_obj_remove_style_all(strow);
    lv_obj_set_size(strow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(strow, LV_ALIGN_CENTER, 0, -116);
    lv_obj_set_flex_flow(strow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(strow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(strow, 7, 0);

    lv_obj_t *date = lv_label_create(strow);
    lv_label_set_text(date, "---");
    lv_obj_set_style_text_font(date, &R0_FONT_STATUS, 0);
    lv_obj_set_style_text_color(date, lv_color_hex(R0_STATUS), 0);
    lv_obj_set_style_text_letter_space(date, 1, 0);
    hpi_ui_bind_label(date, &subj_date);

    lv_obj_t *sep = lv_label_create(strow);
    lv_label_set_text(sep, "\xC2\xB7");   /* middle dot */
    lv_obj_set_style_text_font(sep, &R0_FONT_STATUS, 0);
    lv_obj_set_style_text_color(sep, lv_color_hex(R0_MUTED), 0);

    /* Battery: matsym battery_full / battery_charging_full + "NN%". Charging
     * swaps to the bolt-in-battery glyph and green tint (subj_batt_charging). */
    lv_obj_t *bicon = lv_label_create(strow);
    lv_label_set_text(bicon, SYM_BATT);
    lv_obj_set_style_text_font(bicon, &R0_FONT_ICON_SM, 0);
    lv_obj_set_style_text_color(bicon, lv_color_hex(R0_SPO2), 0);
    hpi_ui_bind_batt_icon(bicon);

    lv_obj_t *batt = lv_label_create(strow);
    lv_label_set_text(batt, "--%");
    lv_obj_set_style_text_font(batt, &R0_FONT_STATUS, 0);
    lv_obj_set_style_text_color(batt, lv_color_hex(R0_TEXT_0), 0);
    hpi_ui_bind_batt_pct(batt);

    /* big time (Hero) + AM/PM (Label, top-aligned to the numerals) */
    lv_obj_t *trow = lv_obj_create(tile);
    lv_obj_remove_style_all(trow);
    lv_obj_set_size(trow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(trow, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_flex_flow(trow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(trow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(trow, 6, 0);

    lv_obj_t *time = lv_label_create(trow);
    lv_label_set_text(time, "00:00");
    lv_obj_set_style_text_font(time, &R0_FONT_HERO, 0);
    lv_obj_set_style_text_color(time, lv_color_white(), 0);
    hpi_ui_bind_label(time, &subj_time);

    lv_obj_t *ampm = lv_label_create(trow);
    lv_label_set_text(ampm, "");
    lv_obj_set_style_text_font(ampm, &R0_FONT_LABEL, 0);
    lv_obj_set_style_text_color(ampm, lv_color_hex(R0_TEXT_4), 0);
    lv_obj_set_style_pad_top(ampm, 18, 0);   /* sit near the top of the 84px numerals */
    hpi_ui_bind_label(ampm, &subj_ampm);

    /* 2 complications side by side: Heart / Steps (battery lives in status row).
     * Chip content is stacked (icon + value over label) so each chip is narrow
     * enough for both to fit the round safe area. */
    lv_obj_t *row = lv_obj_create(tile);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(row, LV_ALIGN_CENTER, 0, 92);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 12, 0);
    home_comp(row, SYM_HR,    hpi_accent_rgb(), &subj_hr, "BPM");
    home_comp(row, SYM_STEPS, R0_GREEN,  &subj_steps, "STEPS");

    /* set-time reminder (shown only when the clock isn't valid) */
    home_hint = NULL;
    home_warn = lv_label_create(tile);
    lv_label_set_text(home_warn, "Set time from app");
    lv_obj_align(home_warn, LV_ALIGN_CENTER, 0, 150);
    lv_obj_set_style_text_color(home_warn, lv_color_hex(R0_WARNING), LV_PART_MAIN);
    lv_obj_set_style_text_font(home_warn, &R0_FONT_LABEL, LV_PART_MAIN);

    lv_subject_add_observer_obj(&subj_time, home_timevalid_cb, tile, NULL);
}

/* Minimal-face seconds bar: indicator width = seconds/59, stepped once/second. */
static void min_sec_bar_cb(lv_observer_t *obs, lv_subject_t *subj)
{
    lv_obj_t *bar = lv_observer_get_target_obj(obs);
    lv_bar_set_value(bar, lv_subject_get_int(subj), LV_ANIM_OFF);
}

/* v2 Minimal watch face: hero time only + a thin accent seconds bar + the
 * "DATE · AM/PM" line. Built and selectable via hpi_home_set_face(), but not
 * the default yet (activation is a later phase). */
static void build_home_minimal_tile(lv_obj_t *tile)
{
    lv_obj_set_style_bg_color(tile, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    hpi_v2_dial_bg(tile);   /* dial motif behind the minimal face */

    /* hero time (Rubik 88) */
    lv_obj_t *time = lv_label_create(tile);
    lv_label_set_text(time, "00:00");
    lv_obj_align(time, LV_ALIGN_CENTER, 0, -22);
    lv_obj_set_style_text_font(time, &HPI_FONT_HERO, 0);
    lv_obj_set_style_text_color(time, lv_color_hex(V2_VALUE), 0);
    lv_obj_set_style_text_letter_space(time, -2, 0);
    hpi_ui_bind_label(time, &subj_time);

    /* thin accent seconds progress bar (128px wide, fills over the minute) */
    lv_obj_t *bar = lv_bar_create(tile);
    lv_obj_set_size(bar, 128, 2);
    lv_obj_align_to(bar, time, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);
    lv_bar_set_range(bar, 0, 59);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, LV_PART_MAIN);   /* v2: no track, bar only */
    lv_obj_set_style_bg_color(bar, lv_color_hex(hpi_accent_rgb()), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 1, LV_PART_INDICATOR);
    lv_subject_add_observer_obj(&subj_sec, min_sec_bar_cb, bar, NULL);

    /* DATE · AM/PM (Manrope 22, secondary muted) */
    lv_obj_t *drow = lv_obj_create(tile);
    lv_obj_remove_style_all(drow);
    lv_obj_set_size(drow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align_to(drow, bar, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);
    lv_obj_set_flex_flow(drow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(drow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(drow, 6, 0);

    lv_obj_t *date = lv_label_create(drow);
    lv_label_set_text(date, "---");
    lv_obj_set_style_text_font(date, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(date, lv_color_hex(V2_MUTED2), 0);
    lv_obj_set_style_text_letter_space(date, 2, 0);
    hpi_ui_bind_label(date, &subj_date);

    lv_obj_t *sep = lv_label_create(drow);
    lv_label_set_text(sep, "\xC2\xB7");   /* middle dot */
    lv_obj_set_style_text_font(sep, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(sep, lv_color_hex(V2_MUTED2), 0);

    lv_obj_t *ampm = lv_label_create(drow);
    lv_label_set_text(ampm, "");
    lv_obj_set_style_text_font(ampm, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(ampm, lv_color_hex(V2_MUTED2), 0);
    hpi_ui_bind_label(ampm, &subj_ampm);

    /* set-time reminder (shown only when the clock isn't valid) */
    home_hint = NULL;
    home_warn = lv_label_create(tile);
    lv_label_set_text(home_warn, "Set time from app");
    lv_obj_align(home_warn, LV_ALIGN_CENTER, 0, 150);
    lv_obj_set_style_text_color(home_warn, lv_color_hex(R0_WARNING), LV_PART_MAIN);
    lv_obj_set_style_text_font(home_warn, &HPI_FONT_LABEL, LV_PART_MAIN);
    lv_subject_add_observer_obj(&subj_time, home_timevalid_cb, tile, NULL);
}

/* Watch-face selection is a persisted v2 pref (hpi_v2_face_*). Takes effect on
 * the next carousel (re)build. hpi_home_set_face kept as a thin alias. */
void hpi_home_set_face(int face)
{
    hpi_v2_face_set(face);
}

static int carousel_pending_tile = 0;
static int carousel_cur_tile = 0;   /* currently shown tile index (0..M_COUNT) */

/* Instant one-step tile change (dir = +1 next / -1 prev), clamped at the ends.
 * Driven by the L/R swipe gesture instead of lv_tileview's native scroll+snap,
 * which on the SPI-bound panel animates slowly and jaggedly - here the tile
 * swaps in a single frame (LV_ANIM_OFF). */
void hpi_carousel_step(int dir)
{
    if (carousel_tv == NULL) {
        return;
    }
    int next = carousel_cur_tile + dir;
    if (next < 0 || next > M_COUNT) {   /* home (0) .. last metric (M_COUNT) */
        return;                          /* clamp at ends, no wrap */
    }
    /* Leaving the ECG / EDA tile mid-measurement cancels it (a spot check is
     * attended; don't let it keep recording invisibly in the background). */
    if (carousel_cur_tile == (M_ECG + 1) && next != (M_ECG + 1)) {
        hpi_ecg_monitor_leave();
    }
    if (carousel_cur_tile == (M_GSR + 1) && next != (M_GSR + 1)) {
        hpi_eda_monitor_leave();
    }
    carousel_ensure_neighborhood(next);
    carousel_cur_tile = next;
    lv_tileview_set_tile_by_index(carousel_tv, next, 0, LV_ANIM_OFF);
}

/* Map a carousel screen id to its tile index (home = 0, metrics 1..N). */
static int carousel_tile_for_screen(int scr)
{
    switch (scr) {
    case SCR_HR:   return M_HR + 1;
    case SCR_SPO2: return M_SPO2 + 1;
    case SCR_ECG:  return M_ECG + 1;
    case SCR_TEMP: return M_TEMP + 1;
    case SCR_ACTIVITY: return M_ACTIVITY + 1;
    case SCR_BPT:  return M_BPT + 1;
    case SCR_HRV:  return M_HRV + 1;
    case SCR_GSR:  return M_GSR + 1;
    case SCR_RECOVERY: return M_RECOVERY + 1;
    default:       return 0; /* SCR_HOME */
    }
}

/* Reverse of carousel_tile_for_screen: the screen id the active tile stands for.
 * The carousel keeps curr_screen pinned at SCR_HOME (it is one LVGL screen), so
 * anything that needs to name the tile the user is actually looking at - the
 * sleep/wake screen save, say - has to ask here. */
int hpi_carousel_curr_screen(void)
{
    switch (carousel_cur_tile - 1) {
    case M_HR:   return SCR_HR;
    case M_SPO2: return SCR_SPO2;
    case M_ECG:  return SCR_ECG;
    case M_TEMP: return SCR_TEMP;
    case M_ACTIVITY: return SCR_ACTIVITY;
    case M_BPT:  return SCR_BPT;
    case M_HRV:  return SCR_HRV;
    case M_GSR:  return SCR_GSR;
    case M_RECOVERY: return SCR_RECOVERY;
    default:     return SCR_HOME;
    }
}

/* Show the carousel with the tile for `scr` active (e.g. returning from a
 * measurement lands on that metric). All former SCR_HR..SCR_GSR loads route
 * here. */
void hpi_carousel_show(int scr, enum scroll_dir dir)
{
    carousel_pending_tile = carousel_tile_for_screen(scr);
    draw_scr_carousel(dir);
}

void draw_scr_carousel(enum scroll_dir m_scroll_dir)
{
    /* P5-style cache: reuse the resident tileview instead of rebuilding. */
    if (scr_carousel != NULL) {
        hpi_disp_set_curr_screen(SCR_HOME);
        carousel_ensure_neighborhood(carousel_pending_tile);
        carousel_cur_tile = carousel_pending_tile;
        lv_tileview_set_tile_by_index(carousel_tv, carousel_pending_tile, 0, LV_ANIM_OFF);
        hpi_show_screen(scr_carousel, m_scroll_dir);
        return;
    }

    hpi_disp_set_curr_screen(SCR_HOME);

    scr_carousel = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_carousel, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_carousel, LV_OBJ_FLAG_SCROLLABLE);

    carousel_tv = lv_tileview_create(scr_carousel);
    lv_obj_set_size(carousel_tv, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(carousel_tv, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(carousel_tv, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(carousel_tv, LV_OPA_TRANSP, LV_PART_SCROLLBAR);
    /* SPI-bound panel: the native tileview scroll+snap animation is slow and
     * jagged, so disable touch-scrolling entirely and drive tile changes
     * instantly from the L/R swipe gesture (see hpi_carousel_step). Programmatic
     * set_tile still works with scrolling off. */
    lv_obj_clear_flag(carousel_tv, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(carousel_tv, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_clear_flag(carousel_tv, LV_OBJ_FLAG_SCROLL_ELASTIC);

    carousel_built_mask = 0;

    /* Empty shells for every tile; populate home + target + neighbors only.
     * Full eager build of all monitors OOM'd the LVGL pool on first paint. */
    for (int i = 0; i <= M_COUNT; i++) {
        lv_obj_t *tile = lv_tileview_add_tile(carousel_tv, i, 0, LV_DIR_HOR);
        lv_obj_set_style_bg_color(tile, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        carousel_tiles[i] = tile;
    }

    lv_obj_add_event_cb(scr_carousel, carousel_delete_cb, LV_EVENT_DELETE, NULL);

    carousel_ensure_neighborhood(carousel_pending_tile);
    carousel_cur_tile = carousel_pending_tile;
    lv_tileview_set_tile_by_index(carousel_tv, carousel_pending_tile, 0, LV_ANIM_OFF);
    hpi_show_screen(scr_carousel, m_scroll_dir);
}
