/*
 * HealthyPi Move — Temperature monitor (R0 design, carousel screen)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Template SPOT screen: thermo icon + WRIST TEMP -> hero degF -> delta chip ->
 * trend sparkline. No waveform, no measurement flow (passive, subject-bound).
 *
 * P3 (2026-07-16): the sparkline is real. It reads the store's cached 7-day
 * skin-temp series (hpi_hs_trend_get) instead of the hardcoded 7-point polyline
 * it shipped with — same machinery as the HR tile's 24 h trend.
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
#include "hpi_user_settings_api.h"

static lv_obj_t *s_temp_spark;
static lv_obj_t *s_temp_cap;

static void temp_spark_del(lv_event_t *e)
{
    ARG_UNUSED(e);
    s_temp_spark = NULL;
    s_temp_cap = NULL;
}

/* Repaint from the cached series. Display-thread only; the series itself is
 * computed on the store thread (hpi_hs_series does file I/O). */
static void temp_spark_refresh(bool force)
{
    static int64_t s_painted_to;
    static bool s_painted;

    if (s_temp_spark == NULL) {
        return;
    }

    struct hpi_hs_trend t;
    if (hpi_hs_trend_get(HPI_HS_T_SKIN_TEMP, &t) != 0) {
        memset(&t, 0, sizeof(t));
    }
    if (s_painted && !force && t.to == s_painted_to) {
        return;
    }
    s_painted_to = t.to;
    s_painted = true;

    if (t.valid == 0 || t.n < 2) {
        float z[HPI_R0_SPARK_MAX] = {0};
        hpi_r0_sparkline_set(s_temp_spark, z, 0, 0);
        if (s_temp_cap) {
            lv_label_set_text(s_temp_cap, "NO TREND YET");
        }
        return;
    }

    /* Autoscale: skin temp moves by tenths of a degree, so a fixed axis would
     * flatten the whole week into one line. Values are degC x100. */
    int16_t lo = INT16_MAX, hi = INT16_MIN;
    for (int i = 0; i < t.n; i++) {
        if (!(t.valid & (1u << i))) {
            continue;
        }
        if (t.mean[i] < lo) lo = t.mean[i];
        if (t.mean[i] > hi) hi = t.mean[i];
    }

    int32_t span = (int32_t)hi - (int32_t)lo;
    if (span < 50) {          /* < 0.5 degC of movement: pad to a readable band */
        int32_t mid = ((int32_t)hi + (int32_t)lo) / 2;
        lo = (int16_t)(mid - 25);
        span = 50;
    }

    float pts[HPI_R0_SPARK_MAX] = {0};
    int n = (t.n > HPI_R0_SPARK_MAX) ? HPI_R0_SPARK_MAX : t.n;
    for (int i = 0; i < n; i++) {
        if (t.valid & (1u << i)) {
            pts[i] = (float)((int32_t)t.mean[i] - (int32_t)lo) / (float)span;
        }
    }
    hpi_r0_sparkline_set(s_temp_spark, pts, t.valid, n);
    if (s_temp_cap) {
        lv_label_set_text(s_temp_cap, "LAST 7 NIGHTS");
    }
}

void hpi_temp_trend_refresh(void)
{
    temp_spark_refresh(false);
}

static const r0_metric_cfg_t temp_cfg = {
    .title  = "WRIST TEMP",
    .accent = 0,   /* set at build from hpi_accent_rgb() */
    .value  = &subj_temp,
    .unit   = "\xC2\xB0" "F",   /* seed; overridden from user settings */
    .kind   = R0_KIND_SPOT,
};

void hpi_temp_monitor_into(lv_obj_t *parent)
{
    uint32_t accent = hpi_accent_rgb();   /* v2: Temp follows the user accent */
    r0_metric_cfg_t cfg = temp_cfg;
    cfg.accent = accent;
    /* 0 = °C, 1 = °F (hpi_user_settings). Unit label honors the setting here.
     * The hero value itself (subj_temp) is ALREADY in the user's unit: the
     * display loop converts the sensor °F to °C when the setting is Celsius
     * before writing the subject (smf_display.c: absolute temp = C*9/5+32 /
     * inverse). Do NOT convert again here or the hero double-converts. */
    cfg.unit = (hpi_user_settings_get_temp_unit() == 0) ? "\xC2\xB0" "C" : "\xC2\xB0" "F";
    r0_metric_ui_t ui = hpi_r0_metric_build_into(parent, &cfg);

    /* thermostat icon to the left of the title */
    if (ui.title) {
        lv_obj_t *ic = lv_label_create(parent);
        lv_label_set_text(ic, SYM_THERMO);
        lv_obj_set_style_text_font(ic, &R0_FONT_ICON, 0);
        lv_obj_set_style_text_color(ic, lv_color_hex(accent), 0);
        lv_obj_align_to(ic, ui.title, LV_ALIGN_OUT_LEFT_MID, -8, 0);
    }

    /* Delta pill — handoff amber tint (accent @ ~13%) + trend icon + text. */
    lv_obj_t *chip = hpi_v2_pill(parent, accent, V2_TINT_OPA);
    lv_obj_align(chip, LV_ALIGN_CENTER, 0, 34);
    lv_obj_set_style_pad_hor(chip, 18, 0);
    lv_obj_set_style_pad_ver(chip, 9, 0);
    lv_obj_set_style_pad_column(chip, 6, 0);

    lv_obj_t *ti = lv_label_create(chip);
    lv_label_set_text(ti, SYM_TREND);
    lv_obj_set_style_text_font(ti, &HPI_FONT_ICON, 0);
    lv_obj_set_style_text_color(ti, lv_color_hex(accent), 0);

    lv_obj_t *tt = lv_label_create(chip);
    lv_obj_set_style_text_font(tt, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(tt, lv_color_hex(accent), 0);
    /* "+0.3° baseline" / "baseline forming". subj_temp_dev is ALREADY in the
     * user's unit and — being a DELTA — is converted with *9/5 only, NO +32
     * offset (hpi_ui_subjects.c: hpi_ui_subj_set_temp_dev_x100). Verified. */
    hpi_ui_bind_label(tt, &subj_temp_dev);

    /* Trend sparkline + caption — 7-night series (handoff 176×48, caption muted). */
    static const float seed[2] = {0.5f, 0.5f};
    s_temp_spark = hpi_r0_sparkline_create(parent, 176, 48, accent, seed, 2);
    lv_obj_align(s_temp_spark, LV_ALIGN_CENTER, 0, 100);
    lv_obj_add_event_cb(s_temp_spark, temp_spark_del, LV_EVENT_DELETE, NULL);

    s_temp_cap = lv_label_create(parent);
    lv_label_set_text(s_temp_cap, "LAST 7 NIGHTS");
    lv_obj_set_style_text_font(s_temp_cap, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(s_temp_cap, lv_color_hex(V2_MUTED), 0);
    lv_obj_set_style_text_letter_space(s_temp_cap, 2, 0);
    lv_obj_align(s_temp_cap, LV_ALIGN_CENTER, 0, 138);

    temp_spark_refresh(true);
}
