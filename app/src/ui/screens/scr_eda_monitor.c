/*
 * HealthyPi Move — EDA / GSR monitor tile (v2 design, carousel)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * In-place spot check, fully driven by the GSR SMF status (gsr_status_chan) -
 * the same shape as the inline ECG monitor. The tile holds NO independent
 * measurement timer; it renders whatever phase the SMF reports, so the two can
 * never drift out of sync:
 *   IDLE        -> last SCR count hero + "SCR / 30S" unit, MEASURE button.
 *   NO CONTACT  -> "SKIN CONTACT" prompt over the plot; the SMF freezes the
 *                  countdown and discards the partial capture until contact returns.
 *   MEASURING   -> live BioZ trace + 30s progress bar + countdown; tap to cancel.
 *   COMPLETE    -> back to idle here; the SMF's EVT_GSR_RESET routes the display
 *                  to SCR_SPL_GSR_COMPLETE, which owns the result view (SCR count,
 *                  tonic uS, DONE).
 *
 * Phase is derived from the SMF status message (status, remaining_s, contact):
 *   status != STREAMING  -> IDLE (COMPLETE lands here too; results screen takes over)
 *   !contact             -> NO CONTACT
 *   otherwise            -> MEASURING (countdown = remaining_s)
 */

#include <zephyr/kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <lvgl.h>

#include "hpi_common_types.h"
#include "hpi_evt.h"
#include "ui/move_ui.h"
#include "ui/hpi_r0_theme.h"
#include "ui/hpi_ui_subjects.h"
#include "health/hpi_health_store.h"

#define GSR_MEAS_SECS 30

/* BioZ is 32 SPS; an 8 s window keeps the slow tonic drift legible. */
#define GSR_WAVE_WINDOW_SAMPLES (8 * 32)

#define EDA_WARN_ACCENT   R0_WARNING     /* no-contact "place fingers" warning    */
#define EDA_CARD_BG       0x0B1F22       /* dark teal-tinted M3 surface-variant   */

lv_obj_t *g_gsr_wave;     /* fed real BioZ samples while active */
bool      g_gsr_active;

/* idle-view widgets */
static lv_obj_t *s_hero;
static lv_obj_t *s_unit;
static lv_obj_t *s_measure_btn;
static lv_obj_t *s_age_cap;
static lv_obj_t *s_ctx_pill;
static lv_obj_t *s_ctx_lbl;

static const char *eda_scr_context(int peaks)
{
    if (peaks <= 0) {
        return "NONE";
    }
    if (peaks <= 2) {
        return "LOW";
    }
    if (peaks <= 5) {
        return "MODERATE";
    }
    if (peaks <= 8) {
        return "ACTIVE";
    }
    return "VERY ACTIVE";
}

/* measuring-view widgets */
static lv_obj_t *s_progress;
static lv_obj_t *s_count_row;
static lv_obj_t *s_count_num;
static lv_obj_t *s_phase_card;
static lv_obj_t *s_phase_title;
static lv_obj_t *s_phase_sub;
static lv_obj_t *s_stop_hint;

enum eda_view { EV_IDLE, EV_NOCONTACT, EV_MEAS };

static void set_hidden(lv_obj_t *o, bool hidden)
{
    if (!o) {
        return;
    }
    hidden ? lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN)
           : lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void apply_view(enum eda_view v)
{
    bool measuring = (v != EV_IDLE);

    set_hidden(s_hero, measuring);
    set_hidden(s_unit, measuring);
    set_hidden(s_measure_btn, measuring);
    set_hidden(s_age_cap, measuring);
    set_hidden(s_ctx_pill, measuring);

    set_hidden(s_stop_hint, !measuring);
    set_hidden(g_gsr_wave, v != EV_MEAS);      /* live trace only with contact */
    set_hidden(s_progress, v != EV_MEAS);
    set_hidden(s_count_row, v != EV_MEAS);
    set_hidden(s_phase_card, v != EV_NOCONTACT);
}

static void eda_measure_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    /* Arm the SMF and optimistically show the waiting-for-contact view for
     * responsiveness; the SMF's status then drives every subsequent transition,
     * keeping the view in lockstep with it. */
    k_event_post(&ecg_evt, EVT_GSR_START);
    if (g_gsr_wave) {
        hpi_wave_monitor_reset(g_gsr_wave);   /* fresh scaler each capture */
    }
    g_gsr_active = true;
    apply_view(EV_NOCONTACT);
}

static void eda_cancel_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    if (!g_gsr_active) {
        return;
    }
    g_gsr_active = false;
    k_event_post(&ecg_evt, EVT_GSR_CANCEL);
    apply_view(EV_IDLE);   /* SMF confirms with an IDLE status shortly after */
}

static void eda_wave_del(lv_event_t *e)
{
    ARG_UNUSED(e);
    g_gsr_wave = NULL;
    g_gsr_active = false;
}

/* Called when the user navigates away from the EDA tile. A GSR spot check is a
 * focused, attended measurement (fingers on the electrodes); leaving abandons it,
 * so cancel rather than leave it capturing invisibly in the background. */
void hpi_eda_monitor_leave(void)
{
    if (g_gsr_active) {
        g_gsr_active = false;
        k_event_post(&ecg_evt, EVT_GSR_CANCEL);
        apply_view(EV_IDLE);
    }
}

/*
 * Render the monitor to the GSR SMF's authoritative status. Called from the
 * display (LVGL) thread whenever the status / countdown / contact changes - see
 * disp_gsr_status_listener + the monitor edge block in smf_display_thread.
 */
void hpi_eda_monitor_update(int status, int remaining_s, bool contact)
{
    if (status != HPI_GSR_STATUS_STREAMING) {   /* IDLE / COMPLETE */
        g_gsr_active = false;
        apply_view(EV_IDLE);
        return;
    }

    if (!g_gsr_active) {
        /* SMF started a capture we didn't initiate from the button (or we
         * returned to the tile mid-capture): sync up and reset the trace. */
        if (g_gsr_wave) {
            hpi_wave_monitor_reset(g_gsr_wave);
        }
        g_gsr_active = true;
    }

    if (!contact) {
        apply_view(EV_NOCONTACT);
        return;
    }

    if (s_progress) {
        lv_bar_set_value(s_progress,
                         (GSR_MEAS_SECS - remaining_s) * 100 / GSR_MEAS_SECS,
                         LV_ANIM_OFF);
    }
    if (s_count_num) {
        lv_label_set_text_fmt(s_count_num, "%d", remaining_s);
    }
    apply_view(EV_MEAS);
}

void hpi_eda_monitor_into(lv_obj_t *parent)
{
    /* header caption */
    lv_obj_t *hdr = lv_label_create(parent);
    lv_label_set_text(hdr, "EDA/GSR");
    lv_obj_align(hdr, LV_ALIGN_CENTER, 0, -128);
    lv_obj_set_style_text_font(hdr, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(hdr, lv_color_hex(V2_MUTED2), 0);
    lv_obj_set_style_text_letter_space(hdr, 2, 0);

    /* live BioZ trace (EDA teal; hidden until measuring); tap to cancel. Uses the
     * auto-scaled push (not the ECG one): EDA is a slow tonic level, so the ECG
     * baseline high-pass + QRS envelope AGC would flatten it. */
    g_gsr_wave = hpi_wave_monitor_create(parent, 304, 120, lv_color_hex(V2_EDA));
    lv_obj_align(g_gsr_wave, LV_ALIGN_CENTER, 0, -20);
    hpi_wave_monitor_set_window(g_gsr_wave, GSR_WAVE_WINDOW_SAMPLES);
    lv_obj_add_flag(g_gsr_wave, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(g_gsr_wave, eda_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(g_gsr_wave, eda_wave_del, LV_EVENT_DELETE, NULL);

    /* no-contact card (M3 tonal), shown in place of the trace */
    s_phase_card = lv_obj_create(parent);
    lv_obj_set_size(s_phase_card, 250, 132);
    lv_obj_align(s_phase_card, LV_ALIGN_CENTER, 0, -14);
    lv_obj_clear_flag(s_phase_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(s_phase_card, 24, 0);
    lv_obj_set_style_bg_color(s_phase_card, lv_color_hex(EDA_CARD_BG), 0);
    lv_obj_set_style_bg_opa(s_phase_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_phase_card, lv_color_hex(EDA_WARN_ACCENT), 0);
    lv_obj_set_style_border_opa(s_phase_card, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_phase_card, 2, 0);
    lv_obj_set_style_pad_all(s_phase_card, 8, 0);
    lv_obj_set_flex_flow(s_phase_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_phase_card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_phase_card, 6, 0);
    lv_obj_add_flag(s_phase_card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_phase_card, eda_cancel_cb, LV_EVENT_CLICKED, NULL);

    s_phase_title = lv_label_create(s_phase_card);
    lv_label_set_text(s_phase_title, "SKIN CONTACT");
    lv_obj_set_style_text_font(s_phase_title, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(s_phase_title, lv_color_hex(EDA_WARN_ACCENT), 0);
    lv_obj_set_style_text_letter_space(s_phase_title, 3, 0);

    s_phase_sub = lv_label_create(s_phase_card);
    lv_label_set_text(s_phase_sub, "Place fingers\non the electrodes");
    lv_obj_set_style_text_align(s_phase_sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_phase_sub, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(s_phase_sub, lv_color_hex(V2_MUTED), 0);

    /* countdown row (seconds remaining) above the progress bar. Split into a
     * digits-only Value numeral + a Label "s" because the Value font has no
     * lowercase 's' glyph. */
    s_count_row = lv_obj_create(parent);
    lv_obj_remove_style_all(s_count_row);
    lv_obj_set_size(s_count_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(s_count_row, LV_ALIGN_CENTER, 0, 56);
    lv_obj_set_flex_flow(s_count_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_count_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_count_row, 4, 0);
    lv_obj_add_flag(s_count_row, LV_OBJ_FLAG_HIDDEN);

    s_count_num = lv_label_create(s_count_row);
    lv_label_set_text(s_count_num, "30");
    lv_obj_set_style_text_font(s_count_num, &R0_FONT_VALUE, 0);
    lv_obj_set_style_text_color(s_count_num, lv_color_hex(V2_EDA), 0);

    lv_obj_t *count_unit = lv_label_create(s_count_row);
    lv_label_set_text(count_unit, "s");
    lv_obj_set_style_text_font(count_unit, &R0_FONT_LABEL, 0);   /* has lowercase glyphs */
    lv_obj_set_style_text_color(count_unit, lv_color_hex(V2_EDA), 0);
    lv_obj_set_style_pad_bottom(count_unit, 6, 0);   /* baseline-align under the numeral */

    /* 30s capture progress bar (shown only while measuring) */
    s_progress = lv_bar_create(parent);
    lv_obj_set_size(s_progress, 200, 6);
    lv_obj_align(s_progress, LV_ALIGN_CENTER, 0, 96);
    lv_bar_set_range(s_progress, 0, 100);
    lv_bar_set_value(s_progress, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(s_progress, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(s_progress, 3, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_progress, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_progress, lv_color_hex(V2_EDA), LV_PART_INDICATOR);
    lv_obj_add_flag(s_progress, LV_OBJ_FLAG_HIDDEN);

    /* "tap to cancel" hint, clear of the progress bar (y 96, 6 px tall) */
    s_stop_hint = lv_label_create(parent);
    lv_label_set_text(s_stop_hint, "tap to cancel");
    lv_obj_align(s_stop_hint, LV_ALIGN_CENTER, 0, 130);
    lv_obj_set_style_text_font(s_stop_hint, &R0_FONT_LABEL, 0);
    lv_obj_set_style_text_color(s_stop_hint, lv_color_hex(R0_MUTED), 0);
    lv_obj_add_flag(s_stop_hint, LV_OBJ_FLAG_HIDDEN);

    /* ---- idle view ---- */

    /* last SCR count, Rubik-66 in EDA teal. subj_gsr carries the count string
     * ("--" before any capture). */
    s_hero = lv_label_create(parent);
    lv_label_set_text(s_hero, "--");
    lv_obj_align(s_hero, LV_ALIGN_CENTER, 0, -52);
    lv_obj_set_style_text_font(s_hero, &HPI_FONT_HERO, 0);   /* Rubik 88 (shared hero) */
    lv_obj_set_style_text_color(s_hero, lv_color_hex(V2_EDA), 0);
    lv_obj_set_style_text_letter_space(s_hero, -1, 0);
    hpi_ui_bind_label(s_hero, &subj_gsr);

    s_unit = lv_label_create(parent);
    lv_label_set_text(s_unit, "SCR / 30S");
    lv_obj_align(s_unit, LV_ALIGN_CENTER, 0, 6);
    lv_obj_set_style_text_font(s_unit, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(s_unit, lv_color_hex(V2_MUTED), 0);
    lv_obj_set_style_text_letter_space(s_unit, 2, 0);

    s_age_cap = lv_label_create(parent);
    {
        char ago[16];
        struct hpi_hs_sample sm;
        if (hpi_hs_get_latest(HPI_HS_T_EDA_SCR_RATE, &sm)) {
            hpi_ui_format_ago(sm.ts_utc, ago, sizeof(ago));
            lv_label_set_text_fmt(s_age_cap, "SKIN RESPONSE · %s", ago);
        } else {
            lv_label_set_text(s_age_cap, "SKIN RESPONSE");
        }
    }
    lv_obj_align(s_age_cap, LV_ALIGN_CENTER, 0, 28);
    lv_obj_set_style_text_font(s_age_cap, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(s_age_cap, lv_color_hex(V2_MUTED), 0);
    lv_obj_set_style_text_letter_space(s_age_cap, 1, 0);

    s_ctx_pill = hpi_v2_pill(parent, V2_EDA, V2_TINT_OPA);
    lv_obj_align(s_ctx_pill, LV_ALIGN_CENTER, 0, 58);
    lv_obj_set_style_pad_hor(s_ctx_pill, 14, 0);
    lv_obj_set_style_pad_ver(s_ctx_pill, 6, 0);
    lv_obj_set_style_pad_column(s_ctx_pill, 6, 0);

    lv_obj_t *wic = lv_label_create(s_ctx_pill);
    lv_label_set_text(wic, SYM_WATER_DROP);
    lv_obj_set_style_text_font(wic, &HPI_FONT_ICON, 0);
    lv_obj_set_style_text_color(wic, lv_color_hex(V2_EDA), 0);

    s_ctx_lbl = lv_label_create(s_ctx_pill);
    {
        int peaks = 0;
        const char *gs = lv_subject_get_string(&subj_gsr);
        if (gs && gs[0] != '-' && gs[0] != '\0') {
            peaks = atoi(gs);
        }
        lv_label_set_text(s_ctx_lbl, eda_scr_context(peaks));
    }
    lv_obj_set_style_text_font(s_ctx_lbl, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(s_ctx_lbl, lv_color_hex(V2_EDA), 0);
    lv_obj_set_style_text_letter_space(s_ctx_lbl, 1, 0);

    /* MEASURE button (play icon + label) on a teal tint */
    s_measure_btn = hpi_btn_create_secondary(parent);
    lv_obj_set_size(s_measure_btn, 200, 56);
    lv_obj_align(s_measure_btn, LV_ALIGN_CENTER, 0, 118);
    lv_obj_set_style_bg_color(s_measure_btn, lv_color_hex(V2_EDA), 0);
    lv_obj_set_style_bg_opa(s_measure_btn, 41, 0);   /* ~16% tint */
    lv_obj_add_event_cb(s_measure_btn, eda_measure_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *brow = lv_obj_create(s_measure_btn);
    lv_obj_remove_style_all(brow);
    lv_obj_set_size(brow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(brow);
    lv_obj_set_flex_flow(brow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(brow, 8, 0);

    lv_obj_t *bic = lv_label_create(brow);
    lv_label_set_text(bic, SYM_PLAY);
    lv_obj_set_style_text_font(bic, &HPI_FONT_ICON, 0);
    lv_obj_set_style_text_color(bic, lv_color_hex(V2_EDA), 0);

    lv_obj_t *blbl = lv_label_create(brow);
    lv_label_set_text(blbl, "MEASURE");
    lv_obj_set_style_text_font(blbl, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(blbl, lv_color_hex(V2_EDA), 0);
    lv_obj_set_style_text_letter_space(blbl, 1, 0);

    /* the tile is rebuilt on carousel rebuild - start from whatever the SMF says */
    g_gsr_active = false;
    apply_view(EV_IDLE);
}
