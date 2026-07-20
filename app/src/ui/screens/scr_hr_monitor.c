/*
 * HealthyPi Move — HR monitor (v2 design handoff + P3 trend default)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Layout follows the v2 design system § Heart Rate on a 390×390
 * round AMOLED. Structure is a real vertical stack (not independent absolute
 * Y guesses that can overlap when pill/chips get taller than planned):
 *
 *   header  (pulse + HEART RATE)
 *   hero    (88 Rubik + BPM)          — nudged up for round bezel room
 *   plot    (296×H — spark | LIVE wave | empty)
 *   below   flex column (pad_row 10):
 *             LIVE/24H tonal pill
 *             RESTING
 *             MIN / MAX chips
 *
 * P3: default view is the 24 h sparkline; LIVE swaps the same plot slot.
 * Toggle is view-only — wrist PPG keeps streaming either way.
 */

#include <zephyr/kernel.h>
#include <string.h>
#include <lvgl.h>

#include "hpi_common_types.h"
#include "ui/move_ui.h"
#include "ui/hpi_r0_theme.h"
#include "ui/hpi_r0_metric.h"
#include "ui/hpi_ui_subjects.h"
#include "health/hpi_health_store.h"

#define HR_GREEN V2_ACCENT

/* Plot width is handoff 296. Height is under handoff 74 so LIVE + RESTING +
 * MIN/MAX chips clear the round bezel (chips need ~190 px width → their
 * vertical center must stay ≤ ~130; bottom of chips ≤ ~155). */
#define HR_PLOT_W  296
#define HR_PLOT_H   48

/* Absolute centers for the upper stack. Hero/title are pulled up so the
 * lower flex column (pill → RESTING → chips) fits above the bezel. */
#define HR_Y_TITLE  (-145)
#define HR_Y_VALUE  (-82)
#define HR_Y_PLOT   (-8)   /* 48-tall plot spans −32..+16 */

/* MEASURE-button tonal fill ≈ 16% of 255 (handoff BP MEASURE). */
#define HR_TOGGLE_OPA 41

/* Tight but even rhythm — round display has no room for 10 px row gaps. */
#define HR_BELOW_GAP  6
#define HR_COL_GAP    6

lv_obj_t *g_hr_wave;

static lv_obj_t *s_hr_spark;
static lv_obj_t *s_hr_empty;
static lv_obj_t *s_hr_toggle_lbl;
static bool s_hr_live;
static bool s_hr_has_trend;

static void hr_wave_del(lv_event_t *e)
{
    ARG_UNUSED(e);
    g_hr_wave = NULL;
}

static void hr_spark_del(lv_event_t *e)
{
    ARG_UNUSED(e);
    s_hr_spark = NULL;
    s_hr_empty = NULL;
    s_hr_toggle_lbl = NULL;
    s_hr_live = false;
    s_hr_has_trend = false;
}

static void dot_opa_cb(void *var, int32_t v)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void hr_spark_refresh(bool force)
{
    static int64_t s_painted_to;
    static bool s_painted;

    if (s_hr_spark == NULL) {
        return;
    }

    struct hpi_hs_trend t;
    if (hpi_hs_trend_get(HPI_HS_T_HR, &t) != 0) {
        memset(&t, 0, sizeof(t));
    }
    if (s_painted && !force && t.to == s_painted_to) {
        return;
    }
    s_painted_to = t.to;
    s_painted = true;

    if (t.valid == 0 || t.n < 2) {
        float z[HPI_R0_SPARK_MAX] = {0};
        hpi_r0_sparkline_set(s_hr_spark, z, 0, 0);
        s_hr_has_trend = false;
        if (s_hr_empty && !s_hr_live) {
            lv_obj_clear_flag(s_hr_empty, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_hr_spark && !s_hr_live) {
            lv_obj_add_flag(s_hr_spark, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    int16_t lo = INT16_MAX, hi = INT16_MIN;
    for (int i = 0; i < t.n; i++) {
        if (!(t.valid & (1u << i))) {
            continue;
        }
        if (t.mean[i] < lo) {
            lo = t.mean[i];
        }
        if (t.mean[i] > hi) {
            hi = t.mean[i];
        }
    }

    int32_t span = (int32_t)hi - (int32_t)lo;
    if (span < 10) {
        int32_t mid = ((int32_t)hi + (int32_t)lo) / 2;
        lo = (int16_t)(mid - 5);
        hi = (int16_t)(mid + 5);
        span = 10;
    }

    float pts[HPI_R0_SPARK_MAX] = {0};
    int n = (t.n > HPI_R0_SPARK_MAX) ? HPI_R0_SPARK_MAX : t.n;
    for (int i = 0; i < n; i++) {
        if (t.valid & (1u << i)) {
            pts[i] = (float)((int32_t)t.mean[i] - (int32_t)lo) / (float)span;
        }
    }
    hpi_r0_sparkline_set(s_hr_spark, pts, t.valid, n);
    s_hr_has_trend = true;

    if (s_hr_empty) {
        lv_obj_add_flag(s_hr_empty, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_hr_spark && !s_hr_live) {
        lv_obj_clear_flag(s_hr_spark, LV_OBJ_FLAG_HIDDEN);
    }
}

static void hr_apply_view(void)
{
    if (g_hr_wave) {
        if (s_hr_live) {
            lv_obj_clear_flag(g_hr_wave, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(g_hr_wave, LV_OBJ_FLAG_HIDDEN);
            hpi_wave_monitor_reset(g_hr_wave);
        }
    }

    if (s_hr_live) {
        if (s_hr_spark) {
            lv_obj_add_flag(s_hr_spark, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_hr_empty) {
            lv_obj_add_flag(s_hr_empty, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        hr_spark_refresh(true);
        if (s_hr_has_trend) {
            if (s_hr_spark) {
                lv_obj_clear_flag(s_hr_spark, LV_OBJ_FLAG_HIDDEN);
            }
            if (s_hr_empty) {
                lv_obj_add_flag(s_hr_empty, LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            if (s_hr_spark) {
                lv_obj_add_flag(s_hr_spark, LV_OBJ_FLAG_HIDDEN);
            }
            if (s_hr_empty) {
                lv_obj_clear_flag(s_hr_empty, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    if (s_hr_toggle_lbl) {
        lv_label_set_text(s_hr_toggle_lbl, s_hr_live ? "24H" : "LIVE");
    }
}

static void hr_toggle_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    s_hr_live = !s_hr_live;
    hr_apply_view();
}

bool hpi_hr_wave_is_live(void)
{
    return s_hr_live && (g_hr_wave != NULL);
}

void hpi_hr_trend_refresh(void)
{
    if (!s_hr_live) {
        hr_spark_refresh(false);
    }
}

/* Compact handoff chip — pad 4×14 keeps the pair above the lower bezel. */
static void hr_stat_chip(lv_obj_t *row, const char *cap, lv_subject_t *subj)
{
    lv_obj_t *c = hpi_v2_chip(row);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_radius(c, 16, 0);
    lv_obj_set_style_pad_hor(c, 14, 0);
    lv_obj_set_style_pad_ver(c, 4, 0);
    lv_obj_set_style_pad_row(c, 0, 0);

    lv_obj_t *v = lv_label_create(c);
    lv_obj_set_style_text_font(v, &HPI_FONT_VALUE, 0);
    lv_obj_set_style_text_color(v, lv_color_hex(V2_VALUE), 0);
    lv_obj_set_style_text_letter_space(v, -1, 0);
    hpi_ui_bind_label(v, subj);

    lv_obj_t *cp = lv_label_create(c);
    lv_label_set_text(cp, cap);
    lv_obj_set_style_text_font(cp, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(cp, lv_color_hex(V2_MUTED), 0);
    lv_obj_set_style_text_letter_space(cp, 2, 0);
}

static const r0_metric_cfg_t hr_cfg = {
    .title  = "HEART RATE",
    .accent = HR_GREEN,
    .value  = &subj_hr,
    .unit   = "BPM",
    .kind   = R0_KIND_WAVEFORM,
    .wave_w = HR_PLOT_W,
    .wave_h = HR_PLOT_H,
    /* Footer is custom so LIVE sits between plot and RESTING without overlap. */
};

void hpi_hr_monitor_into(lv_obj_t *parent)
{
    r0_metric_cfg_t cfg = hr_cfg;
    cfg.accent = hpi_accent_rgb();
    uint32_t accent = cfg.accent;
    r0_metric_ui_t ui = hpi_r0_metric_build_into(parent, &cfg);

    /* Nudge title + hero up — frees the lower half for plot + pill + footer
     * on the round display without changing the metric template globally. */
    if (ui.title) {
        lv_obj_align(ui.title, LV_ALIGN_CENTER, 0, HR_Y_TITLE);
    }
    if (ui.value != NULL) {
        lv_obj_t *val_row = lv_obj_get_parent(ui.value);
        if (val_row != NULL) {
            lv_obj_align(val_row, LV_ALIGN_CENTER, 0, HR_Y_VALUE);
        }
    }

    /* ---- Plot contents: all siblings of parent, same center (no reparent) ----
     * Reparenting the template wave into a 0-then-grown box left coordinates
     * sticky and shifted the lower stack. Keep everything on `parent` with
     * LV_ALIGN_CENTER x=0 so the round tile stays optically centered. */
    g_hr_wave = ui.wave;
    if (ui.wave) {
        lv_obj_align(ui.wave, LV_ALIGN_CENTER, 0, HR_Y_PLOT);
        hpi_wave_monitor_set_window(ui.wave, 100);
        lv_obj_add_event_cb(ui.wave, hr_wave_del, LV_EVENT_DELETE, NULL);
        lv_obj_add_flag(ui.wave, LV_OBJ_FLAG_HIDDEN);
    }

    static const float seed[2] = {0.5f, 0.5f};
    s_hr_spark = hpi_r0_sparkline_create(parent, HR_PLOT_W, HR_PLOT_H, accent, seed, 2);
    lv_obj_align(s_hr_spark, LV_ALIGN_CENTER, 0, HR_Y_PLOT);
    lv_obj_add_event_cb(s_hr_spark, hr_spark_del, LV_EVENT_DELETE, NULL);

    s_hr_empty = lv_label_create(parent);
    lv_label_set_text(s_hr_empty, "NO TREND YET");
    lv_obj_set_style_text_font(s_hr_empty, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(s_hr_empty, lv_color_hex(V2_MUTED), 0);
    lv_obj_set_style_text_letter_space(s_hr_empty, 2, 0);
    lv_obj_align(s_hr_empty, LV_ALIGN_CENTER, 0, HR_Y_PLOT);
    lv_obj_add_flag(s_hr_empty, LV_OBJ_FLAG_HIDDEN);

    /* ---- Lower column: full tile width so children center on the diameter ----
     * A SIZE_CONTENT column aligned while empty grows rightward from the
     * plot's midline (LVGL keeps top-left fixed as size changes) → looks
     * shifted right with a blank left half. Width = 100% avoids that. */
    lv_obj_t *below = lv_obj_create(parent);
    lv_obj_remove_style_all(below);
    lv_obj_set_width(below, LV_PCT(100));
    lv_obj_set_height(below, LV_SIZE_CONTENT);
    lv_obj_clear_flag(below, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(below, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(below, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(below, HR_COL_GAP, 0);

    /* Tonal pill — handoff MEASURE look; visual ~36 px, hit ≥44 via ext area
     * so the footer still clears the round bezel. */
    lv_obj_t *tog = hpi_v2_pill(below, accent, HR_TOGGLE_OPA);
    lv_obj_set_style_pad_hor(tog, 22, 0);
    lv_obj_set_style_pad_ver(tog, 7, 0);
    lv_obj_set_style_min_height(tog, 36, 0);
    lv_obj_add_flag(tog, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tog, hr_toggle_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_ext_click_area(tog, 10);

    s_hr_toggle_lbl = lv_label_create(tog);
    lv_label_set_text(s_hr_toggle_lbl, "LIVE");
    lv_obj_set_style_text_font(s_hr_toggle_lbl, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(s_hr_toggle_lbl, lv_color_hex(accent), 0);
    lv_obj_set_style_text_letter_space(s_hr_toggle_lbl, 2, 0);

    lv_obj_t *rest = lv_label_create(below);
    lv_obj_set_style_text_font(rest, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(rest, lv_color_hex(accent), 0);
    lv_obj_set_style_text_letter_space(rest, 3, 0);
    hpi_ui_bind_label(rest, &subj_hr_resting);

    lv_obj_t *row = lv_obj_create(below);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 14, 0);
    hr_stat_chip(row, "MIN", &subj_hr_min);
    hr_stat_chip(row, "MAX", &subj_hr_max);

    /* Position after children exist so height is final; full width keeps x=0. */
    lv_obj_update_layout(below);
    lv_obj_align_to(below, s_hr_spark, LV_ALIGN_OUT_BOTTOM_MID, 0, HR_BELOW_GAP);

    /* Pulsing live-dot beside the title (handoff 8 px). */
    if (ui.title) {
        lv_obj_t *dot = lv_obj_create(parent);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(accent), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_align_to(dot, ui.title, LV_ALIGN_OUT_LEFT_MID, -8, 0);

        lv_anim_t an;
        lv_anim_init(&an);
        lv_anim_set_var(&an, dot);
        lv_anim_set_exec_cb(&an, dot_opa_cb);
        lv_anim_set_values(&an, 255, 80);
        lv_anim_set_time(&an, 550);
        lv_anim_set_playback_time(&an, 550);
        lv_anim_set_repeat_count(&an, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&an);
    }

    s_hr_live = false;
    hr_apply_view();
}
