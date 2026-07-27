/*
 * HealthyPi Move — SpO2 monitor (v2 design 1a, carousel tile)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Idle tile for the SpO2 spot check (the v2 design system, option
 * 1a — segmented source toggle). SpO2 is a spot check, so the tile is idle by
 * default: last reading (hero) + age, a WRIST | FINGER segmented toggle, and a
 * single START. The waveform lives only on the measuring screen
 * (SCR_SPL_SPO2_MEASURE) — it is no longer drawn here at rest. The toggle only
 * arms the source; START dispatches EVT_SPO2_START (wrist) or EVT_FI_SPO2_START
 * (finger).
 *
 * Navigation split: for the wrist source this tile opens the measure screen
 * itself (the wrist SMF never navigates). For the finger source the finger SMF
 * opens it once it has found + powered the sensor, so START only posts.
 */

#include <zephyr/kernel.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <lvgl.h>

#include "hpi_common_types.h"
#include "hpi_evt.h"
#include "ui/move_ui.h"
#include "ui/hpi_r0_theme.h"
#include "ui/hpi_ui_subjects.h"
#include "health/hpi_health_store.h"
#include "hpi_sys.h"

/* Selected segment: SpO2 blue @ ~16%, matching the MEASURE/START button tint. */
#define SPO2_SEG_TINT_OPA  41

static lv_obj_t *s_last_val;
static lv_obj_t *s_last_meta;
static lv_obj_t *s_seg_wrist;
static lv_obj_t *s_seg_finger;

/* Armed measurement source. Persists across tile rebuilds (the carousel drops
 * and repopulates tiles) and is what the measure/result screens render against.
 * Wrist is the default per the handoff. */
static int s_source = SPO2_SOURCE_PPG_WR;

int  hpi_spo2_source_get(void)      { return s_source; }
void hpi_spo2_source_set(int source)
{
    if (source == SPO2_SOURCE_PPG_WR || source == SPO2_SOURCE_PPG_FI) {
        s_source = source;
    }
}

static void spo2_monitor_del(lv_event_t *e)
{
    ARG_UNUSED(e);
    s_last_val = NULL;
    s_last_meta = NULL;
    s_seg_wrist = NULL;
    s_seg_finger = NULL;
}

/* Paint both segments from s_source: selected = blue tint + blue text. */
static void spo2_seg_paint(void)
{
    const struct { lv_obj_t *seg; bool sel; } segs[] = {
        {s_seg_wrist,  s_source == SPO2_SOURCE_PPG_WR},
        {s_seg_finger, s_source == SPO2_SOURCE_PPG_FI},
    };

    for (int i = 0; i < (int)ARRAY_SIZE(segs); i++) {
        lv_obj_t *seg = segs[i].seg;
        if (seg == NULL) {
            continue;
        }
        lv_obj_set_style_bg_opa(seg, segs[i].sel ? SPO2_SEG_TINT_OPA : LV_OPA_TRANSP, 0);

        uint32_t fg = segs[i].sel ? V2_SPO2 : V2_MUTED;
        uint32_t n = lv_obj_get_child_count(seg);
        for (uint32_t c = 0; c < n; c++) {
            lv_obj_set_style_text_color(lv_obj_get_child(seg, c), lv_color_hex(fg), 0);
        }
    }
}

static void spo2_seg_cb(lv_event_t *e)
{
    hpi_spo2_source_set((int)(intptr_t)lv_event_get_user_data(e));
    spo2_seg_paint();
}

static void spo2_start_cb(lv_event_t *e)
{
    ARG_UNUSED(e);

    if (s_source == SPO2_SOURCE_PPG_FI) {
        /* The finger SMF probes + powers the sensor, then opens the measure
         * screen itself (st_ppg_fi_sensor_check -> SCR_SPL_SPO2_MEASURE). */
        k_event_post(&fi_evt, EVT_FI_SPO2_START);
        return;
    }

    k_event_post(&spo2_evt, EVT_SPO2_START);
    hpi_load_scr_spl(SCR_SPL_SPO2_MEASURE, SCROLL_UP, SCR_SPO2, SPO2_SOURCE_PPG_WR, 0, 0);
}

/* Age under the hero. Prefer the health store (durable, UTC). Fall back to the
 * display last-value path: the hero is fed by spo2_chan / hpi_disp_update_spo2
 * even when the store drops the sample (hpi_hs_record requires HPI_HS_Q_VALID,
 * i.e. RTC synced). Without that fallback a just-taken reading showed a hero
 * value with meta stuck on "-". */
static void spo2_update_last_reading(void)
{
    if (s_last_meta == NULL) {
        return;
    }

    int32_t value = 0;
    int64_t ts_utc = 0;
    uint32_t uptime_ms = 0;

    struct hpi_hs_sample sm;
    if (hpi_hs_get_latest(HPI_HS_T_SPO2, &sm) && sm.value > 0) {
        value = sm.value;
        ts_utc = sm.ts_utc;
    } else {
        uint8_t d_spo2 = 0;
        if (hpi_disp_get_last_spo2(&d_spo2, &ts_utc, &uptime_ms) && d_spo2 > 0) {
            value = d_spo2;
        }
    }

    if (value <= 0) {
        lv_label_set_text(s_last_meta, "-");
        return;
    }

    char ago[16];
    if (hpi_sys_is_time_valid() && ts_utc > 0) {
        hpi_ui_format_ago(ts_utc, ago, sizeof(ago));
        if (ago[0] != '-' || ago[1] != '-') {
            lv_label_set_text_fmt(s_last_meta, "%% \xC2\xB7 %s", ago);
            return;
        }
    }

    /* RTC not valid (or format_ago could not resolve): uptime-relative age so a
     * just-taken reading still shows "JUST NOW" instead of bare "-". */
    if (uptime_ms != 0) {
        uint32_t d = (k_uptime_get_32() - uptime_ms) / 1000U;
        if (d < 60U) {
            lv_label_set_text(s_last_meta, "% \xC2\xB7 JUST NOW");
        } else if (d < 3600U) {
            lv_label_set_text_fmt(s_last_meta, "%% \xC2\xB7 %uM AGO", d / 60U);
        } else if (d < 86400U) {
            lv_label_set_text_fmt(s_last_meta, "%% \xC2\xB7 %uH AGO", d / 3600U);
        } else {
            lv_label_set_text_fmt(s_last_meta, "%% \xC2\xB7 %uD AGO", d / 86400U);
        }
        return;
    }

    /* Value known (e.g. restored from store pre-clock) but no age signal. */
    lv_label_set_text(s_last_meta, "%");
}

void hpi_spo2_trend_refresh(void)
{
    spo2_update_last_reading();
}

/* Build one WRIST/FINGER segment: icon + label inside a full-radius button. */
static lv_obj_t *spo2_seg_create(lv_obj_t *track, const char *icon, const char *text, int source)
{
    lv_obj_t *seg = lv_obj_create(track);
    lv_obj_remove_style_all(seg);
    lv_obj_set_size(seg, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(seg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(seg, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(seg, lv_color_hex(V2_SPO2), 0);
    lv_obj_set_style_bg_opa(seg, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_hor(seg, 14, 0);
    lv_obj_set_style_pad_ver(seg, 8, 0);
    lv_obj_set_style_pad_column(seg, 6, 0);
    lv_obj_set_flex_flow(seg, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(seg, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(seg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(seg, spo2_seg_cb, LV_EVENT_CLICKED, (void *)(intptr_t)source);
    /* Segments are ~46px tall; grow the hit target to the 44px+ touch floor. */
    lv_obj_set_ext_click_area(seg, 6);

    lv_obj_t *ic = lv_label_create(seg);
    lv_label_set_text(ic, icon);
    lv_obj_set_style_text_font(ic, &HPI_FONT_ICON, 0);

    lv_obj_t *tx = lv_label_create(seg);
    lv_label_set_text(tx, text);
    lv_obj_set_style_text_font(tx, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_letter_space(tx, 1, 0);

    return seg;   /* colors come from spo2_seg_paint() */
}

void hpi_spo2_monitor_into(lv_obj_t *parent)
{
    lv_obj_add_event_cb(parent, spo2_monitor_del, LV_EVENT_DELETE, NULL);

    /* header caption */
    lv_obj_t *hdr = lv_label_create(parent);
    lv_label_set_text(hdr, "BLOOD OXYGEN");
    lv_obj_align(hdr, LV_ALIGN_CENTER, 0, -128);
    lv_obj_set_style_text_font(hdr, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(hdr, lv_color_hex(V2_LABEL), 0);
    lv_obj_set_style_text_letter_space(hdr, 2, 0);

    /* hero last reading — Rubik 88, same scale as BP / result. subj_spo2
     * carries the numeral ("--" before any sample). */
    s_last_val = lv_label_create(parent);
    lv_label_set_text(s_last_val, "--");
    lv_obj_align(s_last_val, LV_ALIGN_CENTER, 0, -52);
    lv_obj_set_style_text_font(s_last_val, &HPI_FONT_HERO, 0);
    lv_obj_set_style_text_color(s_last_val, lv_color_hex(V2_SPO2), 0);
    lv_obj_set_style_text_letter_space(s_last_val, -2, 0);
    hpi_ui_bind_label(s_last_val, &subj_spo2);

    /* unit + age under the hero ("% · 2H AGO"), or "-" when none yet */
    s_last_meta = lv_label_create(parent);
    lv_label_set_text(s_last_meta, "-");
    lv_obj_align(s_last_meta, LV_ALIGN_CENTER, 0, 6);
    lv_obj_set_style_text_font(s_last_meta, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(s_last_meta, lv_color_hex(V2_MUTED), 0);
    lv_obj_set_style_text_letter_space(s_last_meta, 2, 0);

    /* segmented source toggle: WRIST | FINGER as equal peers on one track */
    lv_obj_t *track = hpi_v2_pill(parent, 0, 0);   /* soft white @5% */
    lv_obj_align(track, LV_ALIGN_CENTER, 0, 52);
    lv_obj_set_style_pad_all(track, 4, 0);
    lv_obj_set_style_pad_column(track, 4, 0);

    s_seg_wrist  = spo2_seg_create(track, SYM_WATCH, "WRIST", SPO2_SOURCE_PPG_WR);
    s_seg_finger = spo2_seg_create(track, SYM_FINGERPRINT, "FINGER", SPO2_SOURCE_PPG_FI);
    spo2_seg_paint();

    /* START — the single CTA. Solid accent fill with dark text, deliberately
     * NOT the 16% tint used by the selected segment: the toggle offers two
     * equal peers, and START has to outrank both of them. */
    lv_obj_t *btn = hpi_btn_create_secondary(parent);
    lv_obj_set_size(btn, 200, 60);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 128);
    lv_obj_set_style_bg_color(btn, lv_color_hex(V2_SPO2), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(btn, spo2_start_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *brow = hpi_btn_row_create(btn, 8);

    lv_obj_t *bic = lv_label_create(brow);
    lv_label_set_text(bic, SYM_PLAY);
    lv_obj_set_style_text_font(bic, &HPI_FONT_ICON, 0);
    lv_obj_set_style_text_color(bic, lv_color_hex(V2_ON_BP), 0);   /* dark on fill */

    lv_obj_t *blbl = lv_label_create(brow);
    lv_label_set_text(blbl, "START");
    lv_obj_set_style_text_font(blbl, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(blbl, lv_color_hex(V2_ON_BP), 0);
    lv_obj_set_style_text_letter_space(blbl, 1, 0);

    spo2_update_last_reading();
}
