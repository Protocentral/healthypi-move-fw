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

#include "hpi_sys.h"
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
    if (v < 0) {
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
        if (v >= 0) {
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
// void hpi_stress_monitor_into(lv_obj_t *parent)
// {
//     lv_obj_t *col = lv_obj_create(parent);
//     lv_obj_remove_style_all(col);
//     lv_obj_set_width(col, LV_PCT(100));
//     lv_obj_set_height(col, LV_SIZE_CONTENT);
//     lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
//     lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
//     lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
//                           LV_FLEX_ALIGN_CENTER);
//     lv_obj_set_style_pad_row(col, 0, 0);
//     lv_obj_align(col, LV_ALIGN_CENTER, 0, 0);
//     lv_obj_add_event_cb(col, stress_del, LV_EVENT_DELETE, NULL);

//     lv_obj_t *ring_box = lv_obj_create(col);
//     lv_obj_remove_style_all(ring_box);
//     lv_obj_set_size(ring_box, ST_RING_SIZE, ST_RING_SIZE);
//     lv_obj_clear_flag(ring_box, LV_OBJ_FLAG_SCROLLABLE);

//     lv_obj_t *g = lv_arc_create(ring_box);
//     lv_obj_set_size(g, ST_RING_SIZE, ST_RING_SIZE);
//     lv_obj_center(g);
//     lv_arc_set_rotation(g, 270);
//     lv_arc_set_bg_angles(g, 0, 360);
//     lv_arc_set_range(g, 0, 100);
//     lv_arc_set_value(g, 0);
//     lv_obj_set_style_arc_width(g, ST_STROKE, LV_PART_MAIN);
//     lv_obj_set_style_arc_width(g, ST_STROKE, LV_PART_INDICATOR);
//     lv_obj_set_style_arc_color(g, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
//     lv_obj_set_style_arc_opa(g, ST_TRACK_OPA, LV_PART_MAIN);
//     lv_obj_set_style_arc_color(g, lv_color_hex(V2_INDIGO), LV_PART_INDICATOR);
//     lv_obj_set_style_arc_opa(g, LV_OPA_COVER, LV_PART_INDICATOR);
//     lv_obj_set_style_arc_rounded(g, true, LV_PART_INDICATOR);
//     lv_obj_remove_style(g, NULL, LV_PART_KNOB);
//     lv_obj_remove_flag(g, LV_OBJ_FLAG_CLICKABLE);
//     lv_subject_add_observer_obj(&subj_rmssd, stress_arc_cb, g, NULL);

//     // lv_obj_t *score = lv_label_create(ring_box);
//     // lv_label_set_text(score, "--");
//     // lv_obj_center(score);
//     // lv_obj_set_style_text_font(score, &HPI_FONT_VALUE, 0);
//     // lv_obj_set_style_text_color(score, lv_color_hex(V2_VALUE), 0);
//     // lv_obj_set_style_text_letter_space(score, -1, 0);
//     // lv_subject_add_observer_obj(&subj_rmssd, stress_score_cb, score, NULL);

//     // lv_obj_t *hu = lv_label_create(score);
//     // lv_label_set_text(hu, "MS");
//     // lv_obj_set_style_text_font(hu, &HPI_FONT_LABEL, 0);
//     // lv_obj_set_style_text_color(hu, lv_color_hex(V2_MUTED2), 0);
//     // lv_obj_set_style_text_letter_space(hu, 1, 0);
//     lv_obj_t *value_box = lv_obj_create(ring_box);
//     lv_obj_remove_style_all(value_box);
//     lv_obj_set_size(value_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
//     lv_obj_set_flex_flow(value_box, LV_FLEX_FLOW_ROW);
//     lv_obj_set_flex_align(value_box,
//                         LV_FLEX_ALIGN_CENTER,
//                         LV_FLEX_ALIGN_CENTER,
//                         LV_FLEX_ALIGN_CENTER);
//     lv_obj_center(value_box);

//     /* RMSSD value */
//     lv_obj_t *score = lv_label_create(value_box);
//     lv_label_set_text(score, "--");
//     lv_obj_set_style_text_font(score, &HPI_FONT_VALUE, 0);
//     lv_obj_set_style_text_color(score, lv_color_hex(V2_VALUE), 0);
//     lv_obj_set_style_text_letter_space(score, -1, 0);

//     /* "MS" */
//     lv_obj_t *hu = lv_label_create(value_box);
//     lv_label_set_text(hu, "MS");
//     lv_obj_set_style_text_font(hu, &HPI_FONT_LABEL, 0);
//     lv_obj_set_style_text_color(hu, lv_color_hex(V2_MUTED2), 0);
//     lv_obj_set_style_text_letter_space(hu, 1, 0);

//     lv_subject_add_observer_obj(&subj_rmssd, stress_score_cb, score, NULL);

//     s_bal_lbl = lv_label_create(col);
//     lv_label_set_text(s_bal_lbl, "--");   /* see stress_apply_band(): no em dash in this font */
//     lv_obj_set_style_margin_top(s_bal_lbl, 10, 0);
//     lv_obj_set_style_text_font(s_bal_lbl, &HPI_FONT_LABEL, 0);
//     lv_obj_set_style_text_color(s_bal_lbl, lv_color_hex(V2_INDIGO), 0);
//     lv_obj_set_style_text_letter_space(s_bal_lbl, 3, 0);

//     lv_obj_t *pill = hpi_v2_pill(col, 0, 0);
//     lv_obj_set_style_margin_top(pill, 8, 0);
//     lv_obj_set_style_pad_hor(pill, 18, 0);
//     lv_obj_set_style_pad_ver(pill, 7, 0);
//     lv_obj_set_style_pad_column(pill, 8, 0);

//     lv_obj_t *h0 = lv_label_create(pill);
//     lv_label_set_text(h0, "HRV");
//     lv_obj_set_style_text_font(h0, &HPI_FONT_LABEL, 0);
//     lv_obj_set_style_text_color(h0, lv_color_hex(V2_MUTED2), 0);
//     lv_obj_set_style_text_letter_space(h0, 1, 0);

//     // lv_obj_t *hv = lv_label_create(pill);
//     // lv_label_set_text(hv, "--");
//     // lv_obj_set_style_text_font(hv, &HPI_FONT_VALUE, 0);
//     // lv_obj_set_style_text_color(hv, lv_color_hex(V2_INDIGO), 0);
//     // lv_obj_set_style_text_letter_space(hv, -1, 0);
//     //hpi_ui_bind_label(hv, &subj_rmssd);
  

//     // lv_obj_t *hu = lv_label_create(pill);
//     // lv_label_set_text(hu, "MS");
//     // lv_obj_set_style_text_font(hu, &HPI_FONT_LABEL, 0);
//     // lv_obj_set_style_text_color(hu, lv_color_hex(V2_MUTED2), 0);
//     // lv_obj_set_style_text_letter_space(hu, 1, 0);

//     lv_obj_t *bars = lv_obj_create(col);
//     lv_obj_remove_style_all(bars);
//     lv_obj_set_size(bars, LV_SIZE_CONTENT, ST_BAR_ROW_H);
//     lv_obj_set_style_margin_top(bars, 12, 0);
//     lv_obj_set_flex_flow(bars, LV_FLEX_FLOW_ROW);
//     lv_obj_set_flex_align(bars, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END,
//                           LV_FLEX_ALIGN_CENTER);
//     lv_obj_set_style_pad_column(bars, ST_BAR_GAP, 0);

//     for (int i = 0; i < ST_BAR_N; i++) {
//         lv_obj_t *b = lv_obj_create(bars);
//         lv_obj_remove_style_all(b);
//         lv_obj_set_size(b, ST_BAR_W, ST_BAR_MIN_H);
//         lv_obj_set_style_radius(b, 3, 0);
//         lv_obj_set_style_bg_color(b, lv_color_hex(V2_HRV_BAR), 0);
//         lv_obj_set_style_bg_opa(b, LV_OPA_30, 0);
//         s_hrv_bars[i] = b;
//     }

//     {
//         int v = lv_subject_get_int(&subj_rmssd);
//         printk("Stress monitor init in screen file: rmssd %d\n", v);
//         if (v >= 0) {
//             lv_label_set_text_fmt(score, "%d", v);
//             lv_arc_set_value(g, v > 100 ? 100 : v);
//             printk("Stress monitor init: rmssd %d, score %d\n", v, v);
//         }
//         else
//         {
//             lv_label_set_text(score, "--");
//             lv_arc_set_value(g, 0);
//         }
//         stress_apply_band(v);
//     }
//     hpi_stress_hrv_trend_refresh();
// }
/* Colors for baseline deviation badge */

// #define V2_BADGE_GOOD_TXT    0x4ADE80   /* green text */
// #define V2_BADGE_BAD_TXT     0xDC2626   /* red text */
// #define V2_BADGE_NEUTRAL_TXT V2_MUTED2

static lv_obj_t *s_last_rmssd;
static lv_obj_t *s_baseline_badge;
static lv_obj_t *s_baseline_dev_label;
static lv_obj_t *s_last_measured;
static lv_obj_t *icon;

void rmssd_baseline_monitor_cb(lv_observer_t *ob, lv_subject_t *s)
{
    char buf[24];
    uint32_t txt = V2_INDIGO;   /* default text color for baseline badge */

    int32_t dev_ms = lv_subject_get_int(s);

    if(dev_ms == -999) {
        snprintf(buf, sizeof(buf), "baseline forming");
    } else if (dev_ms > 0) {
        snprintf(buf, sizeof(buf), "+%dms baseline", dev_ms);
        lv_label_set_text(icon, SYM_TREND);
    } else if(dev_ms >= -1 && dev_ms <= 1){
        snprintf(buf, sizeof(buf), "at baseline");
        lv_label_set_text(icon, SYM_TREND_FLAT);
    } else {
        snprintf(buf, sizeof(buf),"%dms baseline", dev_ms);
        lv_label_set_text(icon, SYM_TREND_DN);

    }
    lv_label_set_text(s_baseline_dev_label, buf);
    /* Text color */
    lv_obj_set_style_text_color(s_baseline_dev_label, lv_color_hex(txt), 0);
    /* Trend icon color */
    lv_obj_set_style_text_color(icon, lv_color_hex(txt), 0);
    /* Badge background color */
    lv_obj_set_style_bg_color(s_baseline_badge,lv_color_hex(txt), LV_PART_MAIN);
    /* make the background a little transparent */
    lv_obj_set_style_bg_opa(s_baseline_badge,LV_OPA_20, LV_PART_MAIN);
}

void rmssd_last_measured_cb(lv_observer_t *ob, lv_subject_t *s)
{
    char buf[32];
    uint32_t seconds_ago = lv_subject_get_int(s) / 1000;  /* ms -> s */

    if (seconds_ago < 60) {
        snprintf(buf, sizeof(buf), "just now");
    } else if (seconds_ago < 3600) {
        snprintf(buf, sizeof(buf), "%lu min ago", (unsigned long)(seconds_ago / 60));
    } else if (seconds_ago < 86400) {
        snprintf(buf, sizeof(buf), "%lu hr ago", (unsigned long)(seconds_ago / 3600));
    } else {
        snprintf(buf, sizeof(buf), "%lu d ago", (unsigned long)(seconds_ago / 86400));
    }

    lv_label_set_text(s_last_measured, buf);
}

void hpi_stress_monitor_into(lv_obj_t *parent)
{
    uint32_t accent = hpi_accent_rgb();

    /* Title: "HRV" — same slot temp uses for its title (-128) */
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "HRV");
    lv_obj_set_style_text_font(title, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(V2_LABEL), 0);
    lv_obj_set_style_text_letter_space(title, 2, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -128);

    /* Value row: "-- ms" — same slot temp uses for its hero value (-52) */
    lv_obj_t *value_box = lv_obj_create(parent);
    lv_obj_remove_style_all(value_box);
    lv_obj_set_size(value_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(value_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(value_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                           LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(value_box, 4, 0);
    lv_obj_align(value_box, LV_ALIGN_CENTER, 0, -52);

    s_last_rmssd = lv_label_create(value_box);
    lv_label_set_text(s_last_rmssd, "--");
    lv_obj_set_style_text_font(s_last_rmssd, &HPI_FONT_HERO, 0);
    lv_obj_set_style_text_color(s_last_rmssd, lv_color_hex(V2_INDIGO), 0);
    lv_obj_set_style_text_letter_space(s_last_rmssd, -2, 0);
    hpi_ui_bind_label(s_last_rmssd, &subj_rmssd);

    lv_obj_t *unit = lv_label_create(value_box);
    lv_label_set_text(unit, "ms");
    lv_obj_set_style_text_font(unit, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(unit, lv_color_hex(V2_MUTED2), 0);

    /* Baseline deviation — pill, same slot temp uses for its delta chip (+34) */
    s_baseline_badge = hpi_v2_pill(parent, accent, V2_TINT_OPA);
    lv_obj_align(s_baseline_badge, LV_ALIGN_CENTER, 0, 34);
    lv_obj_set_style_pad_hor(s_baseline_badge, 18, 0);
    lv_obj_set_style_pad_ver(s_baseline_badge, 9, 0);

    icon = lv_label_create(s_baseline_badge);
    lv_label_set_text(icon, SYM_TREND_FLAT);
    lv_obj_set_style_text_font(icon, &HPI_FONT_ICON, 0); 
    lv_obj_set_style_text_color(icon, lv_color_hex(accent), 0);

    s_baseline_dev_label = lv_label_create(s_baseline_badge);
    lv_label_set_text(s_baseline_dev_label, "ms baseline");
    lv_obj_set_style_text_font(s_baseline_dev_label, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(s_baseline_dev_label, lv_color_hex(V2_INDIGO), 0);
    lv_obj_center(s_baseline_dev_label);
    lv_subject_add_observer_obj(&subj_rmssd_deviation, rmssd_baseline_monitor_cb, s_baseline_dev_label, NULL);


    /* Footer: last measured recency — same slot temp uses for its caption (+138) */
    s_last_measured = lv_label_create(parent);
    lv_label_set_text(s_last_measured, "--");
    lv_obj_set_style_text_font(s_last_measured, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(s_last_measured, lv_color_hex(V2_MUTED), 0);
    lv_obj_set_style_text_letter_space(s_last_measured, 2, 0);
    lv_obj_align(s_last_measured, LV_ALIGN_CENTER, 0, 138);
    lv_subject_add_observer_obj(&subj_rmssd_age, rmssd_last_measured_cb, s_last_measured, NULL);

}

