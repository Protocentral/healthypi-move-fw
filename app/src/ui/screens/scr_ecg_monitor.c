/*
 * HealthyPi Move — ECG monitor (v2 carousel tile)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Spot check, fully driven by the ECG SMF status (ecg_stat_chan). The monitor
 * holds NO independent measurement timer - it renders whatever phase the SMF
 * reports, so the two can never drift out of sync:
 *   IDLE        -> hero last HR + age, solid START.
 *   WAIT_LEAD   -> SpO2-measure style: pulsing title, PLACE FINGERS hint, CANCEL.
 *   STABILIZING -> same chrome + large countdown numeral.
 *   RECORDING   -> live ECG wave + progress + remaining seconds; CANCEL.
 *   COMPLETE    -> check + ECG RECORDED, then SMF returns to IDLE.
 * Losing lead contact mid-measurement aborts to IDLE in the SMF (with a
 * transient leads-off phase here).
 *
 * Layout matches the SpO2 idle/measure spacing (header / hero / CTA, and during
 * measure: title -> hint -> wave/progress -> CANCEL) with ECG green accents.
 */

#include <zephyr/kernel.h>
#include <stdio.h>
#include <lvgl.h>

#include "hpi_common_types.h"
#include "hpi_evt.h"
#include "ui/move_ui.h"
#include "ui/hpi_r0_theme.h"
#include "ui/hpi_ui_subjects.h"
#include "health/hpi_health_store.h"
#include "hpi_sys.h"

#define ECG_REC_SECS 30

/* ECG trace / accent = medical green (carousel tile accent). Amber/warning for
 * attention phases; success green for completion. */
#define ECG_ACCENT        0x34D399
#define ECG_ON_ACCENT     0x062016   /* dark text on solid START fill */
#define ECG_STAB_ACCENT   R0_ACCENT  /* Signal Amber — hold-still attention */
#define ECG_WARN_ACCENT   R0_WARNING

/* SpO2-measure plot size so the round face stays open. */
#define ECG_WAVE_W  296
#define ECG_WAVE_H   74
#define ECG_WAVE_WIN 640   /* ~5 s at 128 SPS (MAX30001 ECG rate) */

lv_obj_t *g_ecg_wave;     /* fed real ECG samples while active */
bool      g_ecg_active;

/* idle */
static lv_obj_t *s_hdr;
static lv_obj_t *s_last_hr;
static lv_obj_t *s_last_meta;
static lv_obj_t *s_start_btn;

/* measuring (shared SpO2-measure chrome) */
static lv_obj_t *s_title_row;
static lv_obj_t *s_hint_ic;
static lv_obj_t *s_hint;
static lv_obj_t *s_phase_big;    /* stabilizing countdown only */
static lv_obj_t *s_done_badge;   /* large tinted circle around the tick */
static lv_obj_t *s_done_icon;
static lv_obj_t *s_progress;
static lv_obj_t *s_count;        /* "30 S" while recording */
static lv_obj_t *s_cancel_btn;

enum ecg_view { EV_IDLE, EV_WAIT, EV_STAB, EV_REC, EV_LEADOFF, EV_DONE };

static void set_hidden(lv_obj_t *o, bool hidden)
{
    if (!o) {
        return;
    }
    hidden ? lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN)
           : lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void set_hint(const char *icon, const char *text, uint32_t color)
{
    if (s_hint_ic) {
        lv_label_set_text(s_hint_ic, icon);
        lv_obj_set_style_text_color(s_hint_ic, lv_color_hex(color), 0);
    }
    if (s_hint) {
        lv_label_set_text(s_hint, text);
        lv_obj_set_style_text_color(s_hint, lv_color_hex(color), 0);
    }
}

static void apply_view(enum ecg_view v)
{
    bool idle = (v == EV_IDLE);
    bool measuring = !idle;

    set_hidden(s_hdr, measuring);
    set_hidden(s_last_hr, measuring);
    set_hidden(s_last_meta, measuring);
    set_hidden(s_start_btn, measuring);

    set_hidden(s_title_row, idle);
    /* Icon only on coaching phases; recording / done use text (or tick) alone. */
    set_hidden(s_hint_ic, idle || v == EV_REC || v == EV_DONE);
    set_hidden(s_hint, idle);
    set_hidden(s_phase_big, v != EV_STAB);
    set_hidden(s_done_badge, v != EV_DONE);
    set_hidden(g_ecg_wave, v != EV_REC);
    set_hidden(s_progress, v != EV_REC);
    set_hidden(s_count, v != EV_REC);
    set_hidden(s_cancel_btn, idle || v == EV_DONE);

    /* Recording reuses the hint line for a quiet status under the title. */
    if (v == EV_REC && s_hint) {
        set_hidden(s_hint, false);
        set_hint(SYM_DO_NOT_TOUCH, "HOLD STILL", ECG_ACCENT);
        set_hidden(s_hint_ic, true);   /* text alone under the pulsing title */
    }
}

static void dot_opa_cb(void *var, int32_t v)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void num_opa_cb(void *var, int32_t v)
{
    lv_obj_set_style_text_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void ecg_start_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    /* Arm the SMF and optimistically show waiting-for-leads; the SMF status
     * then drives every subsequent transition. */
    k_event_post(&ecg_evt, EVT_ECG_START);
    if (g_ecg_wave) {
        hpi_wave_monitor_reset(g_ecg_wave);
    }
    g_ecg_active = true;
    set_hint(SYM_SENSORS, "PLACE FINGERS", ECG_STAB_ACCENT);
    apply_view(EV_WAIT);
}

static void ecg_cancel_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    if (!g_ecg_active) {
        return;
    }
    g_ecg_active = false;
    k_event_post(&ecg_evt, EVT_ECG_CANCEL);
    apply_view(EV_IDLE);
}

static void ecg_wave_del(lv_event_t *e)
{
    ARG_UNUSED(e);
    g_ecg_wave = NULL;
    g_ecg_active = false;
}

static void ecg_monitor_del(lv_event_t *e)
{
    ARG_UNUSED(e);
    s_hdr = NULL;
    s_last_hr = NULL;
    s_last_meta = NULL;
    s_start_btn = NULL;
    s_title_row = NULL;
    s_hint_ic = NULL;
    s_hint = NULL;
    s_phase_big = NULL;
    s_done_badge = NULL;
    s_done_icon = NULL;
    s_progress = NULL;
    s_count = NULL;
    s_cancel_btn = NULL;
    g_ecg_wave = NULL;
    g_ecg_active = false;
}

/* Called when the user navigates away from the ECG tile. Leaving abandons an
 * attended finger-on-electrode capture rather than recording in the background. */
void hpi_ecg_monitor_leave(void)
{
    if (g_ecg_active) {
        g_ecg_active = false;
        k_event_post(&ecg_evt, EVT_ECG_CANCEL);
        apply_view(EV_IDLE);
    }
}

/* Under-hero unit + age. Prefer the health store; fall back to the display
 * latch (m_disp_ecg_hr) — same split as SpO2: the store needs RTC VALID, but
 * the hero is already fed from the display path after COMPLETE. Without the
 * fallback a successful take showed a number with meta stuck on "-". */
static void ecg_update_last_meta(void)
{
    if (s_last_meta == NULL) {
        return;
    }

    int32_t value = 0;
    int64_t ts_utc = 0;
    uint32_t uptime_ms = 0;

    struct hpi_hs_sample sm;
    if (hpi_hs_get_latest(HPI_HS_T_ECG_HR, &sm) && sm.value > 0) {
        value = sm.value;
        ts_utc = sm.ts_utc;
    } else {
        uint16_t d_hr = 0;
        if (hpi_disp_get_last_ecg_hr(&d_hr, &ts_utc, &uptime_ms) && d_hr > 0) {
            value = d_hr;
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
            lv_label_set_text_fmt(s_last_meta, "BPM \xC2\xB7 %s", ago);
            return;
        }
    }

    if (uptime_ms != 0) {
        uint32_t d = (k_uptime_get_32() - uptime_ms) / 1000U;
        if (d < 60U) {
            lv_label_set_text(s_last_meta, "BPM \xC2\xB7 JUST NOW");
        } else if (d < 3600U) {
            lv_label_set_text_fmt(s_last_meta, "BPM \xC2\xB7 %uM AGO", d / 60U);
        } else if (d < 86400U) {
            lv_label_set_text_fmt(s_last_meta, "BPM \xC2\xB7 %uH AGO", d / 3600U);
        } else {
            lv_label_set_text_fmt(s_last_meta, "BPM \xC2\xB7 %uD AGO", d / 86400U);
        }
        return;
    }

    /* Value known but no usable age signal — still show the unit. */
    lv_label_set_text(s_last_meta, "BPM");
}

void hpi_ecg_trend_refresh(void)
{
    ecg_update_last_meta();
}

/*
 * Render the monitor to the ECG SMF's authoritative status. Display (LVGL)
 * thread only — single path that moves phases so SMF and UI cannot drift.
 */
void hpi_ecg_monitor_update(int status, int progress_timer)
{
    if (status == HPI_ECG_STATUS_COMPLETE) {
        if (s_hint) {
            lv_label_set_text(s_hint, "ECG RECORDED");
            lv_obj_set_style_text_color(s_hint, lv_color_hex(V2_GREEN), 0);
        }
        /* Hero is bound to subj_ecg (fed by m_disp_ecg_hr). Refresh meta from
         * store and/or the display latch stamped on this COMPLETE publish. */
        ecg_update_last_meta();
        g_ecg_active = false;
        apply_view(EV_DONE);
        return;
    }
    if (status != HPI_ECG_STATUS_STREAMING) {   /* IDLE / ERROR */
        g_ecg_active = false;
        /* Returning to idle after COMPLETE: ensure BPM · age is painted now
         * that the hero is visible again. */
        ecg_update_last_meta();
        apply_view(EV_IDLE);
        return;
    }

    if (!g_ecg_active) {
        if (g_ecg_wave) {
            hpi_wave_monitor_reset(g_ecg_wave);
        }
        g_ecg_active = true;
    }

    if (progress_timer == (int)HPI_ECG_UI_WAIT_LEADS) {
        set_hint(SYM_SENSORS, "PLACE FINGERS", ECG_STAB_ACCENT);
        apply_view(EV_WAIT);
    } else if (progress_timer == (int)HPI_ECG_UI_LEADS_OFF) {
        set_hint(SYM_WARNING, "LEADS OFF", ECG_WARN_ACCENT);
        apply_view(EV_LEADOFF);
    } else if (progress_timer > ECG_REC_SECS) {
        set_hint(SYM_DO_NOT_TOUCH, "HOLD STILL", ECG_STAB_ACCENT);
        if (s_phase_big) {
            lv_label_set_text_fmt(s_phase_big, "%d", progress_timer - ECG_REC_SECS);
        }
        apply_view(EV_STAB);
    } else {
        int countdown = progress_timer;   /* 30..0 remaining */
        if (s_progress) {
            lv_bar_set_value(s_progress,
                             (ECG_REC_SECS - countdown) * 100 / ECG_REC_SECS,
                             LV_ANIM_OFF);
        }
        if (s_count) {
            lv_label_set_text_fmt(s_count, "%d S", countdown);
        }
        apply_view(EV_REC);
    }
}

void hpi_ecg_monitor_into(lv_obj_t *parent)
{
    lv_obj_add_event_cb(parent, ecg_monitor_del, LV_EVENT_DELETE, NULL);

    /* ---- idle: SpO2-tile rhythm (header / hero / meta / START) ---- */

    s_hdr = lv_label_create(parent);
    lv_label_set_text(s_hdr, "ECG");
    lv_obj_align(s_hdr, LV_ALIGN_CENTER, 0, -128);
    lv_obj_set_style_text_font(s_hdr, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(s_hdr, lv_color_hex(V2_LABEL), 0);
    lv_obj_set_style_text_letter_space(s_hdr, 2, 0);

    s_last_hr = lv_label_create(parent);
    lv_label_set_text(s_last_hr, "--");
    lv_obj_align(s_last_hr, LV_ALIGN_CENTER, 0, -52);
    lv_obj_set_style_text_font(s_last_hr, &HPI_FONT_HERO, 0);
    lv_obj_set_style_text_color(s_last_hr, lv_color_hex(ECG_ACCENT), 0);
    lv_obj_set_style_text_letter_space(s_last_hr, -2, 0);
    hpi_ui_bind_label(s_last_hr, &subj_ecg);

    s_last_meta = lv_label_create(parent);
    lv_label_set_text(s_last_meta, "-");
    lv_obj_align(s_last_meta, LV_ALIGN_CENTER, 0, 6);
    lv_obj_set_style_text_font(s_last_meta, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(s_last_meta, lv_color_hex(V2_MUTED), 0);
    lv_obj_set_style_text_letter_space(s_last_meta, 2, 0);

    s_start_btn = hpi_btn_create_secondary(parent);
    lv_obj_set_size(s_start_btn, 200, 68);   /* a bit taller — open idle layout */
    lv_obj_align(s_start_btn, LV_ALIGN_CENTER, 0, 128);
    lv_obj_set_style_bg_color(s_start_btn, lv_color_hex(ECG_ACCENT), 0);
    lv_obj_set_style_bg_opa(s_start_btn, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(s_start_btn, ecg_start_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *brow = lv_obj_create(s_start_btn);
    lv_obj_remove_style_all(brow);
    lv_obj_set_size(brow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(brow);
    lv_obj_set_flex_flow(brow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(brow, 8, 0);

    lv_obj_t *bic = lv_label_create(brow);
    lv_label_set_text(bic, SYM_PLAY);
    lv_obj_set_style_text_font(bic, &HPI_FONT_ICON, 0);
    lv_obj_set_style_text_color(bic, lv_color_hex(ECG_ON_ACCENT), 0);

    lv_obj_t *blbl = lv_label_create(brow);
    lv_label_set_text(blbl, "START");
    lv_obj_set_style_text_font(blbl, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(blbl, lv_color_hex(ECG_ON_ACCENT), 0);
    lv_obj_set_style_text_letter_space(blbl, 1, 0);

    /* ---- measuring: SpO2-measure chrome (title / hint / wave / progress / CANCEL) ---- */

    s_title_row = lv_obj_create(parent);
    lv_obj_remove_style_all(s_title_row);
    lv_obj_set_size(s_title_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(s_title_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_title_row, LV_ALIGN_CENTER, 0, -122);
    lv_obj_set_flex_flow(s_title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_title_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_title_row, 8, 0);
    lv_obj_add_flag(s_title_row, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *dot = lv_obj_create(s_title_row);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(ECG_ACCENT), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(s_title_row);
    lv_label_set_text(title, "ECG");
    lv_obj_set_style_text_font(title, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(V2_LABEL), 0);
    lv_obj_set_style_text_letter_space(title, 2, 0);

    lv_anim_t an;
    lv_anim_init(&an);
    lv_anim_set_var(&an, dot);
    lv_anim_set_exec_cb(&an, dot_opa_cb);
    lv_anim_set_values(&an, 255, 90);
    lv_anim_set_time(&an, 550);
    lv_anim_set_playback_time(&an, 550);
    lv_anim_set_repeat_count(&an, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&an);

    /* hint row (icon + uppercase coaching) */
    lv_obj_t *hrow = lv_obj_create(parent);
    lv_obj_remove_style_all(hrow);
    lv_obj_set_size(hrow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(hrow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(hrow, LV_ALIGN_CENTER, 0, -88);
    lv_obj_set_flex_flow(hrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hrow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hrow, 6, 0);

    s_hint_ic = lv_label_create(hrow);
    lv_label_set_text(s_hint_ic, SYM_SENSORS);   /* matsym_24 — touch_app is only in matsym_28 */
    lv_obj_set_style_text_font(s_hint_ic, &HPI_FONT_ICON, 0);
    lv_obj_set_style_text_color(s_hint_ic, lv_color_hex(ECG_STAB_ACCENT), 0);

    s_hint = lv_label_create(hrow);
    lv_label_set_text(s_hint, "PLACE FINGERS");
    lv_obj_set_style_text_font(s_hint, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(ECG_STAB_ACCENT), 0);
    lv_obj_set_style_text_letter_space(s_hint, 1, 0);

    /* stabilizing numeral — center of the open plot slot, breathing opacity */
    s_phase_big = lv_label_create(parent);
    lv_label_set_text(s_phase_big, "5");
    lv_obj_align(s_phase_big, LV_ALIGN_CENTER, 0, -8);
    lv_obj_set_style_text_font(s_phase_big, &HPI_FONT_HERO, 0);
    lv_obj_set_style_text_color(s_phase_big, lv_color_hex(ECG_STAB_ACCENT), 0);
    lv_obj_set_style_text_letter_space(s_phase_big, -2, 0);
    lv_obj_add_flag(s_phase_big, LV_OBJ_FLAG_HIDDEN);
    {
        lv_anim_t pa;
        lv_anim_init(&pa);
        lv_anim_set_var(&pa, s_phase_big);
        lv_anim_set_exec_cb(&pa, num_opa_cb);
        lv_anim_set_values(&pa, 255, 90);
        lv_anim_set_time(&pa, 650);
        lv_anim_set_playback_time(&pa, 650);
        lv_anim_set_repeat_count(&pa, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&pa);
    }

    /* completion badge — check_circle only lives in matsym_24 (24 px). A bare
     * 24 px glyph is nearly invisible on the 390 px face; put it in a large
     * green-tinted disc and scale the label ~2.5× so the tick reads at a glance. */
    s_done_badge = lv_obj_create(parent);
    lv_obj_remove_style_all(s_done_badge);
    lv_obj_set_size(s_done_badge, 88, 88);
    lv_obj_align(s_done_badge, LV_ALIGN_CENTER, 0, -14);
    lv_obj_set_style_radius(s_done_badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_done_badge, lv_color_hex(V2_GREEN), 0);
    lv_obj_set_style_bg_opa(s_done_badge, 48, 0);   /* ~19% tint */
    lv_obj_clear_flag(s_done_badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_done_badge, LV_OBJ_FLAG_HIDDEN);

    s_done_icon = lv_label_create(s_done_badge);
    lv_label_set_text(s_done_icon, SYM_CHECK_CIRCLE);
    lv_obj_center(s_done_icon);
    lv_obj_set_style_text_font(s_done_icon, &HPI_FONT_ICON, 0);
    lv_obj_set_style_text_color(s_done_icon, lv_color_hex(V2_GREEN), 0);
    /* LVGL scale: 256 = 1.0×. 640 ≈ 2.5× → ~60 px effective glyph. */
    lv_obj_set_style_transform_scale(s_done_icon, 640, 0);
    lv_obj_set_style_transform_pivot_x(s_done_icon, LV_PCT(50), 0);
    lv_obj_set_style_transform_pivot_y(s_done_icon, LV_PCT(50), 0);

    /* live ECG waveform — same footprint as SpO2 measure; tap cancels */
    g_ecg_wave = hpi_wave_monitor_create(parent, ECG_WAVE_W, ECG_WAVE_H,
                                         lv_color_hex(ECG_ACCENT));
    lv_obj_align(g_ecg_wave, LV_ALIGN_CENTER, 0, -14);
    hpi_wave_monitor_set_window(g_ecg_wave, ECG_WAVE_WIN);
    lv_obj_add_flag(g_ecg_wave, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(g_ecg_wave, ecg_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(g_ecg_wave, ecg_wave_del, LV_EVENT_DELETE, NULL);
    g_ecg_active = false;

    /* progress 220x6 — SpO2-measure track/indicator treatment */
    s_progress = lv_bar_create(parent);
    lv_obj_set_size(s_progress, 220, 6);
    lv_obj_align(s_progress, LV_ALIGN_CENTER, 0, 48);
    lv_bar_set_range(s_progress, 0, 100);
    lv_bar_set_value(s_progress, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(s_progress, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(s_progress, 3, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_progress, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_progress, 20, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_progress, lv_color_hex(ECG_ACCENT), LV_PART_INDICATOR);
    lv_obj_add_flag(s_progress, LV_OBJ_FLAG_HIDDEN);

    s_count = lv_label_create(parent);
    lv_label_set_text(s_count, "30 S");
    lv_obj_align(s_count, LV_ALIGN_CENTER, 0, 76);
    lv_obj_set_style_text_font(s_count, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(s_count, lv_color_hex(V2_MUTED2), 0);
    lv_obj_set_style_text_letter_space(s_count, 2, 0);
    lv_obj_add_flag(s_count, LV_OBJ_FLAG_HIDDEN);

    /* CANCEL — quiet text control, same as SpO2 measure */
    s_cancel_btn = lv_btn_create(parent);
    lv_obj_remove_style_all(s_cancel_btn);
    lv_obj_set_size(s_cancel_btn, 160, 48);
    lv_obj_align(s_cancel_btn, LV_ALIGN_CENTER, 0, 124);
    lv_obj_add_event_cb(s_cancel_btn, ecg_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_cancel_btn, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *clbl = lv_label_create(s_cancel_btn);
    lv_label_set_text(clbl, "CANCEL");
    lv_obj_center(clbl);
    lv_obj_set_style_text_font(clbl, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(clbl, lv_color_hex(V2_MUTED), 0);
    lv_obj_set_style_text_letter_space(clbl, 2, 0);

    ecg_update_last_meta();
    g_ecg_active = false;
    apply_view(EV_IDLE);
}
