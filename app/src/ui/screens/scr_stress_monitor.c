/*
 * HealthyPi Move — Stress / HRV monitor (v2 design handoff, carousel tile)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * the v2 design system §7 — Stress / HRV, 390×390 round:
 *
 *   indigo ring 182×182 + score 32 inside
 *   BALANCED status
 *   HRV soft pill
 *   10 history bars from HPI_HS_T_HRV_RMSSD trend cache (10×5 min)
 */

#include <zephyr/kernel.h>
#include <string.h>
#include <limits.h>
#include <lvgl.h>

#include "hpi_common_types.h"
#include "ui/move_ui.h"
#include "ui/hpi_r0_theme.h"
#include "ui/hpi_ui_subjects.h"
#include "health/hpi_health_store.h"

#define ST_RING_SIZE   182
#define ST_STROKE       14
#define ST_TRACK_OPA    18
#define ST_BAR_N        10
#define ST_BAR_W         6
#define ST_BAR_GAP       6
#define ST_BAR_ROW_H    38
#define ST_BAR_MIN_H     4

static lv_obj_t *s_bal_lbl;
static lv_obj_t *s_hrv_bars[ST_BAR_N];

static void stress_arc_cb(lv_observer_t *ob, lv_subject_t *s)
{
    lv_obj_t *arc = lv_observer_get_target_obj(ob);
    if (arc) {
        int v = lv_subject_get_int(s);
        if (v < 0) {
            v = 0;
        }
        if (v > 100) {
            v = 100;
        }
        lv_arc_set_value(arc, v);
    }
}

static void stress_apply_band(int v)
{
    if (s_bal_lbl == NULL) {
        return;
    }
    /* "--", not an em dash: HPI_FONT_LABEL (manrope_700_22) is generated for
     * 0x20-0x7E plus a 5-glyph sparse set (° µ · ₂ −). U+2014 is not in it, so
     * the no-data state rendered as a missing-glyph box. "--" is also what every
     * other screen uses for "nothing measured yet". */
    const char *word = "BALANCED";
    if (v <= 0) {
        word = "--";
    } else if (v >= 70) {
        word = "HIGH";
    } else if (v >= 40) {
        word = "MODERATE";
    }
    lv_label_set_text(s_bal_lbl, word);
}

static void stress_score_cb(lv_observer_t *ob, lv_subject_t *s)
{
    lv_obj_t *lbl = lv_observer_get_target_obj(ob);
    int v = lv_subject_get_int(s);
    if (lbl) {
        if (v > 0) {
            lv_label_set_text_fmt(lbl, "%d", v);
        } else {
            lv_label_set_text(lbl, "--");
        }
    }
    stress_apply_band(v);
}

static void stress_del(lv_event_t *e)
{
    ARG_UNUSED(e);
    s_bal_lbl = NULL;
    for (int i = 0; i < ST_BAR_N; i++) {
        s_hrv_bars[i] = NULL;
    }
}

void hpi_stress_hrv_trend_refresh(void)
{
    if (s_hrv_bars[0] == NULL) {
        return;
    }

    struct hpi_hs_trend t;
    if (hpi_hs_trend_get(HPI_HS_T_HRV_RMSSD, &t) != 0) {
        memset(&t, 0, sizeof(t));
    }

    int n = t.n;
    if (n > ST_BAR_N) {
        n = ST_BAR_N;
    }
    if (n <= 0) {
        n = ST_BAR_N;
    }

    int16_t lo = INT16_MAX, hi = INT16_MIN;
    int any = 0;
    for (int i = 0; i < n; i++) {
        if (!(t.valid & (1u << i))) {
            continue;
        }
        any = 1;
        if (t.mean[i] < lo) {
            lo = t.mean[i];
        }
        if (t.mean[i] > hi) {
            hi = t.mean[i];
        }
    }

    int32_t span = (int32_t)hi - (int32_t)lo;
    if (!any || span < 1) {
        span = 1;
        lo = any ? lo : 0;
    }

    for (int i = 0; i < ST_BAR_N; i++) {
        if (s_hrv_bars[i] == NULL) {
            continue;
        }
        int h = ST_BAR_MIN_H;
        if (i < n && (t.valid & (1u << i))) {
            int32_t rel = (int32_t)t.mean[i] - (int32_t)lo;
            h = ST_BAR_MIN_H +
                (int)((rel * (ST_BAR_ROW_H - ST_BAR_MIN_H)) / span);
            if (h > ST_BAR_ROW_H) {
                h = ST_BAR_ROW_H;
            }
            if (h < ST_BAR_MIN_H) {
                h = ST_BAR_MIN_H;
            }
            lv_obj_set_style_bg_opa(s_hrv_bars[i], LV_OPA_COVER, 0);
        } else {
            /* gap: faint stub */
            h = ST_BAR_MIN_H;
            lv_obj_set_style_bg_opa(s_hrv_bars[i], LV_OPA_30, 0);
        }
        lv_obj_set_height(s_hrv_bars[i], h);
    }
}

void hpi_stress_monitor_into(lv_obj_t *parent)
{
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_remove_style_all(col);
    lv_obj_set_width(col, LV_PCT(100));
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 0, 0);
    lv_obj_align(col, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(col, stress_del, LV_EVENT_DELETE, NULL);

    lv_obj_t *ring_box = lv_obj_create(col);
    lv_obj_remove_style_all(ring_box);
    lv_obj_set_size(ring_box, ST_RING_SIZE, ST_RING_SIZE);
    lv_obj_clear_flag(ring_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *g = lv_arc_create(ring_box);
    lv_obj_set_size(g, ST_RING_SIZE, ST_RING_SIZE);
    lv_obj_center(g);
    lv_arc_set_rotation(g, 270);
    lv_arc_set_bg_angles(g, 0, 360);
    lv_arc_set_range(g, 0, 100);
    lv_arc_set_value(g, 0);
    lv_obj_set_style_arc_width(g, ST_STROKE, LV_PART_MAIN);
    lv_obj_set_style_arc_width(g, ST_STROKE, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(g, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(g, ST_TRACK_OPA, LV_PART_MAIN);
    lv_obj_set_style_arc_color(g, lv_color_hex(V2_INDIGO), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(g, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(g, true, LV_PART_INDICATOR);
    lv_obj_remove_style(g, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(g, LV_OBJ_FLAG_CLICKABLE);
    lv_subject_add_observer_obj(&subj_stress, stress_arc_cb, g, NULL);

    lv_obj_t *score = lv_label_create(ring_box);
    lv_label_set_text(score, "--");
    lv_obj_center(score);
    lv_obj_set_style_text_font(score, &HPI_FONT_VALUE, 0);
    lv_obj_set_style_text_color(score, lv_color_hex(V2_VALUE), 0);
    lv_obj_set_style_text_letter_space(score, -1, 0);
    lv_subject_add_observer_obj(&subj_stress, stress_score_cb, score, NULL);

    s_bal_lbl = lv_label_create(col);
    lv_label_set_text(s_bal_lbl, "--");   /* see stress_apply_band(): no em dash in this font */
    lv_obj_set_style_margin_top(s_bal_lbl, 10, 0);
    lv_obj_set_style_text_font(s_bal_lbl, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(s_bal_lbl, lv_color_hex(V2_INDIGO), 0);
    lv_obj_set_style_text_letter_space(s_bal_lbl, 3, 0);

    lv_obj_t *pill = hpi_v2_pill(col, 0, 0);
    lv_obj_set_style_margin_top(pill, 8, 0);
    lv_obj_set_style_pad_hor(pill, 18, 0);
    lv_obj_set_style_pad_ver(pill, 7, 0);
    lv_obj_set_style_pad_column(pill, 8, 0);

    lv_obj_t *h0 = lv_label_create(pill);
    lv_label_set_text(h0, "HRV");
    lv_obj_set_style_text_font(h0, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(h0, lv_color_hex(V2_MUTED2), 0);
    lv_obj_set_style_text_letter_space(h0, 1, 0);

    lv_obj_t *hv = lv_label_create(pill);
    lv_label_set_text(hv, "--");
    lv_obj_set_style_text_font(hv, &HPI_FONT_VALUE, 0);
    lv_obj_set_style_text_color(hv, lv_color_hex(V2_INDIGO), 0);
    lv_obj_set_style_text_letter_space(hv, -1, 0);
    hpi_ui_bind_label(hv, &subj_hrv);

    lv_obj_t *hu = lv_label_create(pill);
    lv_label_set_text(hu, "MS");
    lv_obj_set_style_text_font(hu, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(hu, lv_color_hex(V2_MUTED2), 0);
    lv_obj_set_style_text_letter_space(hu, 1, 0);

    lv_obj_t *bars = lv_obj_create(col);
    lv_obj_remove_style_all(bars);
    lv_obj_set_size(bars, LV_SIZE_CONTENT, ST_BAR_ROW_H);
    lv_obj_set_style_margin_top(bars, 12, 0);
    lv_obj_set_flex_flow(bars, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bars, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bars, ST_BAR_GAP, 0);

    for (int i = 0; i < ST_BAR_N; i++) {
        lv_obj_t *b = lv_obj_create(bars);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, ST_BAR_W, ST_BAR_MIN_H);
        lv_obj_set_style_radius(b, 3, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(V2_HRV_BAR), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_30, 0);
        s_hrv_bars[i] = b;
    }

    {
        int v = lv_subject_get_int(&subj_stress);
        if (v > 0) {
            lv_label_set_text_fmt(score, "%d", v);
            lv_arc_set_value(g, v > 100 ? 100 : v);
        }
        stress_apply_band(v);
    }
    hpi_stress_hrv_trend_refresh();
}
