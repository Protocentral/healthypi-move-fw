/*
 * HealthyPi Move
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * SpO2 measuring screen (the v2 design system, option 1a). This is
 * the ONLY place the PPG waveform appears — the idle tile is at rest by design.
 * Center-weighted acquiring layout: pulsing-dot "SpO2" title -> source hint
 * (HOLD STILL / ATTACH FINGER SENSOR) -> live PPG wave -> linear progress + NN%
 * -> CANCEL.
 *
 * Reached from both sources: the idle tile opens it for wrist (arg2 =
 * SPO2_SOURCE_PPG_WR); the finger SMF opens it once the sensor is found (arg2 =
 * SPO2_SOURCE_PPG_FI). Completion routes to SCR_SPL_SPO2_RESULT — for wrist
 * from here (the wrist SMF never navigates), for finger from the finger SMF's
 * own done state.
 *
 * The plot is the shared R0 wave monitor (auto-scaled raw PPG), the same widget
 * + push_auto path the tile used to run inline.
 */

#include <zephyr/kernel.h>
#include "hpi_evt.h"
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <stdio.h>
#include <stdint.h>

#include "hpi_common_types.h"
#include "ui/move_ui.h"
#include "ui/hpi_r0_theme.h"

LOG_MODULE_REGISTER(hpi_disp_scr_spo2_measure, LOG_LEVEL_DBG);

/* Wrist PPG streams ~64 sps; the finger hub is slower — show a comparable ~2 s
 * of signal across the 296 px plot for each. */
#define SPO2_WAVE_WINDOW_WR  100
#define SPO2_WAVE_WINDOW_FI  128

/* Keep in step with the wrist SMF's publish gate (smf_ppg_wrist.c): a reading
 * it will not store is not a reading we may show. */
#define SPO2_MIN_CONFIDENCE  50

static lv_obj_t *scr_spo2_scr_measure;
static lv_obj_t *wave_ppg;
static lv_obj_t *bar_progress;
static lv_obj_t *label_progress;
static lv_obj_t *label_hint;
static lv_obj_t *label_hint_ic;

static int last_progress;
static int spo2_source = SPO2_SOURCE_PPG_WR;

/* Latched once this screen has asked to move on. hpi_load_scr_spl only QUEUES
 * the request, so hpi_disp_get_curr_screen() still reads SCR_SPL_SPO2_MEASURE
 * until the display thread drains it — without this, any further sample landing
 * in that window would queue a second (redundant) result screen. */
static bool routed_away;

/* Stall watchdog. The wrist decode only forwards a sample when the hub is not
 * positively OFF_SKIN (smf_ppg_wrist.c), but it sets the terminal state
 * (100% / SPO2_MEAS_TIMEOUT) and tears the hub down to CONT_HRM *before* that
 * gate. A spot check that times out *because* the watch came loose therefore
 * completes on a sample the display never receives, and no further SpO2 samples
 * are ever produced — leaving this screen frozen with CANCEL as its only exit.
 * If nothing has updated us for this long, end the measurement ourselves. */
#define SPO2_STALL_TIMEOUT_MS  15000
static lv_timer_t *stall_timer;
static uint32_t last_update_ms;

extern lv_style_t style_scr_black;

/* The screen is deleted on navigation but the display thread still holds these
 * via hpi_disp_spo2_plot_* / _update_progress. Null them on delete so a late
 * sample can't write into freed objects. */
static void spo2_measure_del(lv_event_t *e)
{
    ARG_UNUSED(e);
    if (stall_timer) {
        lv_timer_delete(stall_timer);
        stall_timer = NULL;
    }
    wave_ppg = NULL;
    bar_progress = NULL;
    label_progress = NULL;
    label_hint = NULL;
    label_hint_ic = NULL;
}

static void spo2_cancel_btn_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    gesture_down_scr_spo2_measure();
}

static void spo2_stall_cb(lv_timer_t *t)
{
    ARG_UNUSED(t);
    if (routed_away) {
        return;
    }
    if ((k_uptime_get_32() - last_update_ms) < SPO2_STALL_TIMEOUT_MS) {
        return;
    }
    LOG_WRN("SpO2 sample stream stalled - ending measurement");
    routed_away = true;
    /* Make sure the hub is torn down: if we got here the SMF has usually
     * stopped already, and a cancel on an idle measurement is a no-op. */
    if (spo2_source == SPO2_SOURCE_PPG_FI) {
        k_event_post(&fi_evt, EVT_FI_SPO2_CANCEL);
    } else {
        k_event_post(&spo2_evt, EVT_SPO2_CANCEL);
    }
    hpi_load_scr_spl(SCR_SPL_SPO2_RESULT, SCROLL_UP, (uint8_t)SCR_SPO2, HPI_SPO2_RESULT_TIMEOUT, 0, 0);
}

static void dot_opa_cb(void *var, int32_t v)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

void draw_scr_spo2_measure(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg3);
    ARG_UNUSED(arg4);

    spo2_source = (int)arg2;
    hpi_spo2_source_set(spo2_source);   /* the result screen renders the source */
    last_progress = 0;
    routed_away = false;
    last_update_ms = k_uptime_get_32();

    bool wrist = (spo2_source != SPO2_SOURCE_PPG_FI);

    scr_spo2_scr_measure = lv_obj_create(NULL);
    lv_obj_add_style(scr_spo2_scr_measure, &style_scr_black, 0);
    lv_obj_clear_flag(scr_spo2_scr_measure, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr_spo2_scr_measure, spo2_measure_del, LV_EVENT_DELETE, NULL);

    /* title row: pulsing accent dot + "SpO2" (manrope_700_22 carries U+2082) */
    lv_obj_t *trow = lv_obj_create(scr_spo2_scr_measure);
    lv_obj_remove_style_all(trow);
    lv_obj_set_size(trow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(trow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(trow, LV_ALIGN_CENTER, 0, -122);
    lv_obj_set_flex_flow(trow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(trow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(trow, 8, 0);

    lv_obj_t *dot = lv_obj_create(trow);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(V2_SPO2), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(trow);
    lv_label_set_text(title, "SpO\xE2\x82\x82");
    lv_obj_set_style_text_font(title, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(V2_LABEL), 0);
    lv_obj_set_style_text_letter_space(title, 2, 0);

    /* ~1.1 s pulse, matching the handoff's acquiring cue */
    lv_anim_t an;
    lv_anim_init(&an);
    lv_anim_set_var(&an, dot);
    lv_anim_set_exec_cb(&an, dot_opa_cb);
    lv_anim_set_values(&an, 255, 90);
    lv_anim_set_time(&an, 550);
    lv_anim_set_playback_time(&an, 550);
    lv_anim_set_repeat_count(&an, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&an);

    /* source hint: the wrist is already worn, the finger sensor is attached
     * only after start — so each source gets its own instruction. */
    lv_obj_t *hrow = lv_obj_create(scr_spo2_scr_measure);
    lv_obj_remove_style_all(hrow);
    lv_obj_set_size(hrow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(hrow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(hrow, LV_ALIGN_CENTER, 0, -88);
    lv_obj_set_flex_flow(hrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hrow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hrow, 6, 0);

    label_hint_ic = lv_label_create(hrow);
    lv_label_set_text(label_hint_ic, wrist ? SYM_DO_NOT_TOUCH : SYM_FINGERPRINT);
    lv_obj_set_style_text_font(label_hint_ic, &HPI_FONT_ICON, 0);
    lv_obj_set_style_text_color(label_hint_ic, lv_color_hex(V2_SPO2), 0);

    label_hint = lv_label_create(hrow);
    /* "ATTACH SENSOR", not the prototype's "ATTACH FINGER SENSOR": at the label
     * font (22 px floor vs the mock's 20 px) the longer string measures 313 px
     * and runs into the round bezel at this y. The fingerprint icon beside it
     * already says which sensor. */
    lv_label_set_text(label_hint, wrist ? "HOLD STILL" : "ATTACH SENSOR");
    lv_obj_set_style_text_font(label_hint, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(label_hint, lv_color_hex(V2_SPO2), 0);
    lv_obj_set_style_text_letter_space(label_hint, 1, 0);

    /* live PPG waveform — 296x74, the only place it appears in the flow */
    wave_ppg = hpi_wave_monitor_create(scr_spo2_scr_measure, 296, 74, lv_color_hex(V2_SPO2));
    lv_obj_align(wave_ppg, LV_ALIGN_CENTER, 0, -14);
    hpi_wave_monitor_set_window(wave_ppg, wrist ? SPO2_WAVE_WINDOW_WR : SPO2_WAVE_WINDOW_FI);
    hpi_wave_monitor_reset(wave_ppg);

    /* progress 220x6 + NN% caption */
    bar_progress = lv_bar_create(scr_spo2_scr_measure);
    lv_obj_set_size(bar_progress, 220, 6);
    lv_obj_align(bar_progress, LV_ALIGN_CENTER, 0, 48);
    lv_bar_set_range(bar_progress, 0, 100);
    lv_bar_set_value(bar_progress, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar_progress, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_progress, 3, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar_progress, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar_progress, 20, LV_PART_MAIN);   /* ~8% track */
    lv_obj_set_style_bg_color(bar_progress, lv_color_hex(V2_SPO2), LV_PART_INDICATOR);

    label_progress = lv_label_create(scr_spo2_scr_measure);
    lv_label_set_text(label_progress, "0%");
    lv_obj_align(label_progress, LV_ALIGN_CENTER, 0, 76);
    lv_obj_set_style_text_font(label_progress, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(label_progress, lv_color_hex(V2_MUTED2), 0);
    lv_obj_set_style_text_letter_space(label_progress, 2, 0);

    /* CANCEL — a text button, not a filled pill: it must not compete with the
     * measurement for attention. */
    lv_obj_t *btn = lv_btn_create(scr_spo2_scr_measure);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 160, 48);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 124);
    lv_obj_add_event_cb(btn, spo2_cancel_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "CANCEL");
    lv_obj_center(lbl);
    lv_obj_set_style_text_font(lbl, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(V2_MUTED), 0);
    lv_obj_set_style_text_letter_space(lbl, 2, 0);

    stall_timer = lv_timer_create(spo2_stall_cb, 1000, NULL);

    hpi_disp_set_curr_screen(SCR_SPL_SPO2_MEASURE);
    hpi_show_screen(scr_spo2_scr_measure, m_scroll_dir);
}

void hpi_disp_spo2_update_progress(int progress, enum spo2_meas_state state, int spo2, int hr, int conf)
{
    if (bar_progress == NULL || label_progress == NULL || routed_away) {
        return;
    }
    last_update_ms = k_uptime_get_32();   /* feed the stall watchdog */

    if (progress < 0) {
        progress = 0;
    } else if (progress > 100) {
        progress = 100;
    }

    /* High-water mark: don't let progress regress mid-measurement. */
    if (state == SPO2_MEAS_SUCCESS || state == SPO2_MEAS_TIMEOUT) {
        last_progress = 0;
    } else if (progress < last_progress) {
        progress = last_progress;
    } else {
        last_progress = progress;
    }

    lv_bar_set_value(bar_progress, progress, LV_ANIM_OFF);
    lv_label_set_text_fmt(label_progress, "%d%%", progress);

    /* A timeout ends the measurement whatever the source. (Only the wrist hub
     * reports it today — the finger decode emits COMPUTATION/SUCCESS only — but
     * if that ever changes this would race the finger SMF's own result load, so
     * revisit the ordering here rather than assuming.) */
    if (state == SPO2_MEAS_TIMEOUT) {
        routed_away = true;
        hpi_load_scr_spl(SCR_SPL_SPO2_RESULT, SCROLL_UP, (uint8_t)SCR_SPO2, HPI_SPO2_RESULT_TIMEOUT, 0, 0);
        return;
    }

    /* Wrist only: the finger SMF opens its own result from its done state, so
     * routing here too would double-navigate.
     *
     * Completion is "the hub said SUCCESS *or* the sample hit 100%". Both are
     * checked deliberately: percent==100 is the condition the shipping in-place
     * flow used to end a wrist spot check, so it is the proven signal, while
     * spo2_state is a raw hub byte this path has never gated on. Trusting
     * spo2_state alone would strand the user on this screen if the hub reports
     * SUCCESS only transiently (or not at all) — the wrist SMF has already torn
     * the measurement down to CONT_HRM by then, so no further samples arrive to
     * retry with. */
    if (spo2_source != SPO2_SOURCE_PPG_WR) {
        return;
    }
    if (state == SPO2_MEAS_SUCCESS || progress >= 100) {
        /* Same confidence gate the wrist SMF applies before it publishes and
         * stores the reading. Without it we would hero a value the rest of the
         * firmware threw away — the user would read "96%", tap DONE, and find
         * the tile still saying NOT MEASURED YET. A finished-but-unusable spot
         * check is a timeout: "keep still and try again". */
        bool usable = (spo2 > 0) && (conf > SPO2_MIN_CONFIDENCE);
        routed_away = true;
        hpi_load_scr_spl(SCR_SPL_SPO2_RESULT, SCROLL_UP, (uint8_t)SCR_SPO2,
                         usable ? HPI_SPO2_RESULT_SUCCESS : HPI_SPO2_RESULT_TIMEOUT,
                         (uint32_t)spo2, (uint32_t)hr);
    }
}

static void spo2_plot_raw(uint32_t *data, int num)
{
    if (wave_ppg == NULL || num <= 0) {
        return;
    }
    /* push_linear, not push_auto: this is the spot-check trace the user judges
     * the reading by, so it has to be the same faithful, linearly-scaled picture
     * of the raw IR value that v2's lv_chart drew. push_auto's AGC (high-pass +
     * instant-attack envelope + clip) is kept for the always-on HR tile. */
    for (int i = 0; i < num; i++) {
        hpi_wave_monitor_push_linear(wave_ppg, (int32_t)data[i]);
    }
}

void hpi_disp_spo2_plot_wrist_ppg(struct hpi_ppg_wr_data_t ppg_sensor_sample)
{
    spo2_plot_raw(ppg_sensor_sample.raw_ir, ppg_sensor_sample.ppg_num_samples);
}

void hpi_disp_spo2_plot_fi_ppg(struct hpi_ppg_fi_data_t ppg_sensor_sample)
{
    spo2_plot_raw(ppg_sensor_sample.raw_ir, ppg_sensor_sample.ppg_num_samples);
}

void gesture_down_scr_spo2_measure(void)
{
    if (spo2_source == SPO2_SOURCE_PPG_FI) {
        k_event_post(&fi_evt, EVT_FI_SPO2_CANCEL);
        LOG_INF("SpO2 measurement cancelled (finger)");
    } else {
        k_event_post(&spo2_evt, EVT_SPO2_CANCEL);
        LOG_INF("SpO2 measurement cancelled (wrist)");
    }
    hpi_carousel_show(SCR_SPO2, SCROLL_DOWN);
}
