/*
 * HealthyPi Move — Activity monitor (v2 design handoff, carousel tile)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * the v2 design system §6 — Activity, on a 390×390 round AMOLED:
 *
 *   triple concentric rings (move / exercise / stand) + run icon
 *   steps  (32 Rubik green + STEPS caption)
 *   two pills: distance MI (accent) · calories KCAL (blue)
 *
 * Live today: steps (BMI323), distance + kcal derived from steps (profile
 * weight). Outer ring = steps vs 10k goal. Exercise / stand rings stay at 0%
 * until those metrics exist (no fake demo fills).
 */

#include <zephyr/kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <lvgl.h>

#include "hpi_common_types.h"
#include "hpi_user_profile.h"
#include "ui/move_ui.h"
#include "ui/hpi_r0_theme.h"
#include "ui/hpi_ui_subjects.h"

/* Handoff: radii 74 / 57 / 40, stroke 13, track white@7%. */
#define ACT_STROKE       13
#define ACT_R_MOVE       74
#define ACT_R_EXERCISE   57
#define ACT_R_STAND      40
#define ACT_TRACK_OPA    18   /* ~7% of 255 */
#define ACT_STEPS_GOAL   10000

static lv_obj_t *s_arc_move;
static lv_obj_t *s_mi_val;
static lv_obj_t *s_kcal_val;

static void act_arcs_del(lv_event_t *e)
{
    ARG_UNUSED(e);
    s_arc_move = NULL;
    s_mi_val = NULL;
    s_kcal_val = NULL;
}

static lv_obj_t *act_ring(lv_obj_t *parent, int r_px, uint32_t color, int pct)
{
    lv_obj_t *a = lv_arc_create(parent);
    int size = r_px * 2 + ACT_STROKE;
    lv_obj_set_size(a, size, size);
    lv_obj_center(a);
    lv_arc_set_rotation(a, 270);
    lv_arc_set_bg_angles(a, 0, 360);
    lv_arc_set_range(a, 0, 100);
    if (pct < 0) {
        pct = 0;
    }
    if (pct > 100) {
        pct = 100;
    }
    lv_arc_set_value(a, pct);
    lv_obj_set_style_arc_width(a, ACT_STROKE, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, ACT_STROKE, LV_PART_INDICATOR);
    /* Handoff track: rgba(255,255,255,.07) */
    lv_obj_set_style_arc_color(a, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, ACT_TRACK_OPA, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(a, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    return a;
}

/* Handoff footer pill: value (32 Rubik) + unit (22 Manrope) on soft chip. */
static void act_metric_pill(lv_obj_t *row, uint32_t val_color, const char *unit,
                            lv_obj_t **out_val)
{
    lv_obj_t *pill = hpi_v2_pill(row, 0, 0); /* soft white @5% */
    lv_obj_set_style_pad_hor(pill, 16, 0);
    lv_obj_set_style_pad_ver(pill, 8, 0);
    lv_obj_set_style_pad_column(pill, 6, 0);

    lv_obj_t *v = lv_label_create(pill);
    lv_label_set_text(v, "--");
    lv_obj_set_style_text_font(v, &HPI_FONT_VALUE, 0);
    lv_obj_set_style_text_color(v, lv_color_hex(val_color), 0);
    lv_obj_set_style_text_letter_space(v, -1, 0);
    *out_val = v;

    lv_obj_t *u = lv_label_create(pill);
    lv_label_set_text(u, unit);
    lv_obj_set_style_text_font(u, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(u, lv_color_hex(V2_MUTED), 0);
    lv_obj_set_style_text_letter_space(u, 1, 0);
}

/* Keep MI / KCAL pills and move ring in sync with steps. */
static void act_steps_obs(lv_observer_t *observer, lv_subject_t *subject)
{
    ARG_UNUSED(observer);
    const char *s = lv_subject_get_string(subject);
    int steps = 0;
    if (s != NULL && s[0] != '-' && s[0] != '\0') {
        /* Steps subject is thousands-grouped ("6,524") — strip commas. */
        char plain[16];
        size_t j = 0;
        for (size_t i = 0; s[i] != '\0' && j + 1 < sizeof(plain); i++) {
            if (s[i] >= '0' && s[i] <= '9') {
                plain[j++] = s[i];
            }
        }
        plain[j] = '\0';
        steps = (j > 0) ? atoi(plain) : 0;
        if (steps < 0) {
            steps = 0;
        }
    }

    if (s_mi_val) {
        /* ~0.762 m/step → miles ×10 */
        int dist_x10 = (int)(steps * 0.004735f);
        char b[12];
        snprintf(b, sizeof(b), "%d.%d", dist_x10 / 10, dist_x10 % 10);
        lv_label_set_text(s_mi_val, b);
    }

    if (s_kcal_val) {
        /* Step-derived only here — avoid nested hpi_hs_summary() from an
         * observer that already runs under the display push path. */
        uint16_t kcal = hpi_get_kcals_from_steps((uint16_t)(steps > 65535 ? 65535 : steps));
        char b[12];
        snprintf(b, sizeof(b), "%u", (unsigned)kcal);
        lv_label_set_text(s_kcal_val, b);
    }

    if (s_arc_move) {
        int pct = (steps * 100) / ACT_STEPS_GOAL;
        if (pct > 100) {
            pct = 100;
        }
        lv_arc_set_value(s_arc_move, pct);
    }
}

void hpi_activity_monitor_into(lv_obj_t *parent)
{
    uint32_t accent = hpi_accent_rgb();

    /*
     * Full-width centered column (same pattern as the handoff HTML flex
     * column on the round 390). Avoids the absolute-Y sparse layout that
     * left a large empty band under the rings.
     */
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
    lv_obj_add_event_cb(col, act_arcs_del, LV_EVENT_DELETE, NULL);

    /* Ring host: outer diameter ≈ 2*74+13 = 161 (handoff ~174 box). */
    lv_obj_t *rings = lv_obj_create(col);
    lv_obj_remove_style_all(rings);
    lv_obj_set_size(rings, ACT_R_MOVE * 2 + ACT_STROKE, ACT_R_MOVE * 2 + ACT_STROKE);
    lv_obj_clear_flag(rings, LV_OBJ_FLAG_SCROLLABLE);

    /* Outer = move (green), middle = exercise (accent), inner = stand (blue).
     * Exercise/stand stay at 0% until product metrics exist — no demo fills. */
    s_arc_move = act_ring(rings, ACT_R_MOVE, V2_GREEN, 0);
    act_ring(rings, ACT_R_EXERCISE, accent, 0);
    act_ring(rings, ACT_R_STAND, V2_SPO2, 0);

    lv_obj_t *ic = lv_label_create(rings);
    lv_label_set_text(ic, SYM_RUN);
    lv_obj_center(ic);
    lv_obj_set_style_text_font(ic, &HPI_FONT_ICON_LG, 0); /* 28 px bin */
    lv_obj_set_style_text_color(ic, lv_color_hex(0xEEF1F2), 0);

    /* Handoff: steps row margin-top 18 under rings. */
    lv_obj_t *srow = lv_obj_create(col);
    lv_obj_remove_style_all(srow);
    lv_obj_set_size(srow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_margin_top(srow, 18, 0);
    lv_obj_set_flex_flow(srow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(srow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(srow, 9, 0);

    lv_obj_t *sv = lv_label_create(srow);
    lv_label_set_text(sv, "--");
    lv_obj_set_style_text_font(sv, &HPI_FONT_VALUE, 0);
    lv_obj_set_style_text_color(sv, lv_color_hex(V2_GREEN), 0);
    lv_obj_set_style_text_letter_space(sv, -1, 0);
    hpi_ui_bind_label(sv, &subj_steps);

    lv_obj_t *sc = lv_label_create(srow);
    lv_label_set_text(sc, "STEPS");
    lv_obj_set_style_text_font(sc, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(sc, lv_color_hex(V2_MUTED), 0);
    lv_obj_set_style_text_letter_space(sc, 1, 0);

    /* Handoff: two pills, gap 12, margin-top 12. */
    lv_obj_t *prow = lv_obj_create(col);
    lv_obj_remove_style_all(prow);
    lv_obj_set_size(prow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_margin_top(prow, 12, 0);
    lv_obj_set_flex_flow(prow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(prow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(prow, 12, 0);

    act_metric_pill(prow, accent, "MI", &s_mi_val);
    act_metric_pill(prow, V2_SPO2, "KCAL", &s_kcal_val);

    /* Seed derived pills + move ring from current steps. */
    lv_subject_add_observer_obj(&subj_steps, act_steps_obs, col, NULL);
    act_steps_obs(NULL, &subj_steps);
}
