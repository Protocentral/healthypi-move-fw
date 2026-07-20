/*
 * HealthyPi Move — R0 reusable widgets (P6 redesign spike)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Waveform monitor: a bedside-monitor trace (fixed columns, sweeping erase
 * gap ahead of the pen). RAM-light — one int16 per column, drawn on the
 * object's own layer, opaque black bg.
 */

#include <zephyr/kernel.h>
#include <lvgl.h>
#include <string.h>
#include <math.h>

#include "ui/hpi_r0_theme.h"

/* --------------------------------------------------------- waveform monitor */

#define WM_MAX_W 700   /* max samples (buffer is allocated to the actual window) */

typedef struct {
    int16_t    *y;             /* trace y per sample (n entries), rel. to top */
    int         w, h, head, mid, amp;
    int         n;             /* number of samples displayed across w px */
    lv_color_t  color;
    lv_timer_t *demo;          /* synthetic feed timer (or NULL) */
    float       dphase;
    int         dbpm;
    int         demo_ecg;      /* 0 = PPG morphology, 1 = ECG */
    float       amin, amax;    /* auto-scale running range */
    int         auto_init;
    float       ecg_base, ecg_env, ecg_amp;  /* ECG baseline(HP), peak env, long-term amp */
    int         ecg_init, ecg_n;
} wave_monitor_t;

static void wave_delete_cb(lv_event_t *e)
{
    wave_monitor_t *wm = lv_event_get_user_data(e);
    if (wm->demo) {
        lv_timer_delete(wm->demo);
    }
    lv_free(wm->y);
    lv_free(wm);
}

static void wave_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    wave_monitor_t *wm = lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    const int ox = a.x1, oy = a.y1;

    /* trace with a monitor erase gap just ahead of the pen (head) */
    lv_draw_line_dsc_t t;
    lv_draw_line_dsc_init(&t);
    t.color = wm->color;
    t.width = 2;
    t.round_start = 1;
    t.round_end = 1;
    const int n = wm->n;
    const int gap = (n / 18 < 1) ? 1 : n / 18;   /* erase band, in samples */
    for (int i = 0; i < n - 1; i++) {
        int dist = (i - wm->head + n) % n;
        if (dist < gap) {
            continue;   /* blanked sweep band ahead of the pen */
        }
        t.p1.x = ox + i * (wm->w - 1) / (n - 1);       t.p1.y = oy + wm->y[i];
        t.p2.x = ox + (i + 1) * (wm->w - 1) / (n - 1); t.p2.y = oy + wm->y[i + 1];
        lv_draw_line(layer, &t);
    }
}

lv_obj_t *hpi_wave_monitor_create(lv_obj_t *parent, int w, int h, lv_color_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, w, h);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(obj, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);

    wave_monitor_t *wm = lv_malloc(sizeof(*wm));
    memset(wm, 0, sizeof(*wm));
    wm->w = w;
    wm->h = h;
    wm->n = w;                 /* default: 1 sample/px (override w/ set_window) */
    wm->color = color;
    wm->mid = h / 2;
    wm->amp = (int)(h * 0.34f);
    wm->y = lv_malloc(sizeof(int16_t) * w);
    for (int i = 0; i < w; i++) {
        wm->y[i] = wm->mid;
    }

    lv_obj_set_user_data(obj, wm);
    lv_obj_add_event_cb(obj, wave_draw_cb, LV_EVENT_DRAW_MAIN, wm);
    lv_obj_add_event_cb(obj, wave_delete_cb, LV_EVENT_DELETE, wm);
    return obj;
}

void hpi_wave_monitor_push(lv_obj_t *obj, float sample)
{
    wave_monitor_t *wm = lv_obj_get_user_data(obj);
    if (wm == NULL) {
        return;
    }
    int y = wm->mid - (int)(sample * wm->amp);
    if (y < 1) {
        y = 1;
    } else if (y > wm->h - 2) {
        y = wm->h - 2;
    }
    wm->y[wm->head] = (int16_t)y;
    wm->head = (wm->head + 1) % wm->n;
    lv_obj_invalidate(obj);
}

/* Set the displayed window in samples (spread across the pixel width). */
void hpi_wave_monitor_set_window(lv_obj_t *obj, int n_samples)
{
    wave_monitor_t *wm = lv_obj_get_user_data(obj);
    if (wm == NULL) {
        return;
    }
    if (n_samples < 8) {
        n_samples = 8;
    } else if (n_samples > WM_MAX_W) {
        n_samples = WM_MAX_W;
    }
    int16_t *ny = lv_malloc(sizeof(int16_t) * n_samples);
    if (ny == NULL) {
        return;
    }
    lv_free(wm->y);
    wm->y = ny;
    wm->n = n_samples;
    wm->head = 0;
    for (int i = 0; i < n_samples; i++) {
        wm->y[i] = wm->mid;
    }
    lv_obj_invalidate(obj);
}

/* Push a raw PPG sample, AC-coupled + AGC-scaled. PPG is a small pulsatile AC
 * riding a large, drifting DC, and on the finger/wrist hubs that DC jumps hugely
 * at the not-worn -> worn transition. The old running-min/max envelope handled
 * neither: on a flat/unworn trace its slow decay dragged amin and amax onto the
 * sample, the range hit the 1-count floor, and LSB noise was amplified to a
 * full-scale SQUARE WAVE; and the unworn->worn DC step inflated the range so the
 * real pulse showed compressed and "never scaled down". Use the same robust path
 * as the ECG scaler instead:
 *   1. remove DC with a high-pass so the trace centres and a contact DC step is
 *      rejected within ~1 s (pulse ~1-3 Hz sits well above the cutoff);
 *   2. scale by an amplitude envelope (instant attack, slow release), capped at
 *      3x a slow amplitude reference so a motion/saturation burst can't blow up
 *      the gain and flatten the pulse;
 *   3. floor the envelope RELATIVE TO THE DC LEVEL (PPG raw magnitude + LSB noise
 *      scale with LED drive), so a flat / not-worn trace stays flat instead of
 *      amplifying noise.
 * Reuses the ecg_* AGC state (no wave monitor drives both PPG and ECG). */
void hpi_wave_monitor_push_auto(lv_obj_t *obj, int32_t raw)
{
    wave_monitor_t *wm = lv_obj_get_user_data(obj);
    if (wm == NULL) {
        return;
    }
    float v = (float)raw;
    if (!wm->ecg_init) {
        wm->ecg_base = v;
        wm->ecg_env = 1.0f;
        wm->ecg_amp = 1.0f;
        wm->ecg_n = 0;
        wm->ecg_init = 1;
    }
    wm->ecg_n++;

    /* 1. high-pass baseline: removes DC + the unworn->worn step + slow wander.
     *    tau ~= 50 samples (0.5-2 s across the 25-100 SPS PPG rates). */
    wm->ecg_base += (v - wm->ecg_base) * 0.02f;
    float ac = v - wm->ecg_base;
    float a = fabsf(ac);

    /* 2. amplitude envelope: instant attack to a new peak, slow release so the
     *    pulse fills the plot and it recovers after an artifact. */
    if (a > wm->ecg_env) {
        wm->ecg_env = a;
    } else {
        wm->ecg_env *= 0.995f;
    }

    /* 3. slow amplitude reference + 3x ceiling: a sustained motion/saturation
     *    burst can't inflate the scale into a flat line - worst case is a
     *    1/3-height trace that snaps back once the artifact passes. Warm-up. */
    if (wm->ecg_n < 64) {
        wm->ecg_amp = wm->ecg_env;
    } else {
        wm->ecg_amp += (wm->ecg_env - wm->ecg_amp) * 0.002f;
        float ceiling = wm->ecg_amp * 3.0f;
        if (ceiling > 1.0f && wm->ecg_env > ceiling) {
            wm->ecg_env = ceiling;
        }
    }

    /* 4. DC-relative envelope floor: keeps a flat / not-worn trace from being
     *    amplified onto LSB noise (the square-wave bug). ~0.15% of the DC level
     *    sits above sensor noise (~0.03% of DC) and below a real pulse (>~0.5%),
     *    so a genuine pulse still fills the plot but noise stays flat. */
    float floor_env = fabsf(wm->ecg_base) * 0.0015f;
    if (floor_env < 1.0f) {
        floor_env = 1.0f;
    }
    float env = (wm->ecg_env < floor_env) ? floor_env : wm->ecg_env;

    float norm = (ac / env) * 1.1f;
    if (norm > 1.3f) {
        norm = 1.3f;
    } else if (norm < -1.3f) {
        norm = -1.3f;
    }
    hpi_wave_monitor_push(obj, norm);
}

/* EDA/BioZ scaler. push_auto's envelope is tuned for pulsatile PPG: at 32 SPS on
 * a slow, near-flat tonic level its 0.02/sample decay drags amin and amax onto
 * the sample itself, the range collapses to the 1-count floor, and LSB noise is
 * then amplified to full scale. Track the envelope over roughly the displayed
 * window instead, and hold a minimum span so a quiet trace stays quiet. The
 * window is re-centred on the envelope, so a large DC offset (skin impedance)
 * doesn't push the trace off-screen. */
#define WM_EDA_DECAY     0.004f   /* ~1/256: tau ~= the 8 s displayed window @ 32 SPS */
#define WM_EDA_MIN_SPAN  20.0f    /* counts; BioZ is uS x100, so 0.2 uS of headroom */

void hpi_wave_monitor_push_eda(lv_obj_t *obj, int32_t raw)
{
    wave_monitor_t *wm = lv_obj_get_user_data(obj);
    if (wm == NULL) {
        return;
    }
    float v = (float)raw;
    if (!wm->auto_init) {
        wm->amin = v;
        wm->amax = v;
        wm->auto_init = 1;
    }

    /* expand immediately to admit a new extreme, contract slowly */
    if (v < wm->amin) {
        wm->amin = v;
    } else {
        wm->amin += (v - wm->amin) * WM_EDA_DECAY;
    }
    if (v > wm->amax) {
        wm->amax = v;
    } else {
        wm->amax -= (wm->amax - v) * WM_EDA_DECAY;
    }

    float mid = 0.5f * (wm->amin + wm->amax);
    float range = wm->amax - wm->amin;
    if (range < WM_EDA_MIN_SPAN) {
        range = WM_EDA_MIN_SPAN;
    }

    float norm = (v - (mid - 0.5f * range)) / range;      /* 0..1 about the envelope */
    hpi_wave_monitor_push(obj, (norm - 0.5f) * 2.6f);     /* centered, ~88% fill */
}

/* ECG-specific scaler. Unlike push_auto (tuned for pulsatile PPG), ECG is a flat
 * baseline with a sharp QRS spike, so a running min/max gets pinned by the R/S
 * peaks and squashes everything else. Instead:
 *   1. remove baseline/DC with a slow high-pass (~0.5 Hz) so the trace centres
 *      and slow wander / lead-on DC steps don't drift it off-screen;
 *   2. drive the gain from an amplitude *envelope* (fast attack, slow release)
 *      that is outlier-clamped, so one big QRS or a motion spike can't blow up
 *      the scale and flatten the following beats;
 *   3. soft-clip the peaks so a tall QRS bounds gracefully at the plot edge
 *      instead of railing.
 * Tuned for the MAX30001 ECG at 128 SPS. */
void hpi_wave_monitor_push_ecg(lv_obj_t *obj, int32_t raw)
{
    wave_monitor_t *wm = lv_obj_get_user_data(obj);
    if (wm == NULL) {
        return;
    }
    float v = (float)raw;
    if (!wm->ecg_init) {
        wm->ecg_base = v;
        wm->ecg_env = 1.0f;
        wm->ecg_amp = 1.0f;
        wm->ecg_n = 0;
        wm->ecg_init = 1;
    }
    wm->ecg_n++;

    /* 1. baseline high-pass (~0.5 Hz at 128 SPS): centre + de-wander */
    wm->ecg_base += (v - wm->ecg_base) * 0.025f;
    float ac = v - wm->ecg_base;
    float a = fabsf(ac);

    /* 2. envelope = decaying peak of |ac|. A QRS instantly sets the peak (so its
     *    tip always maps to ~full scale), then it decays ~2.6 s between beats so
     *    the P/T waves and baseline stay visible and it recovers after an
     *    artifact. No multiplicative attack -> it can't run away. */
    if (a > wm->ecg_env) {
        wm->ecg_env = a;
    } else {
        wm->ecg_env *= 0.997f;
    }

    /* 3. long-term amplitude reference + hard ceiling. A ~1 s warm-up lets amp
     *    converge on the real ECG amplitude; after that it holds slow, and the
     *    envelope is capped at 3x it. So a sustained motion burst can't inflate
     *    the scale into a flat line - worst case is a 1/3-height trace that snaps
     *    back the moment the artifact passes. */
    if (wm->ecg_n < 128) {
        wm->ecg_amp = wm->ecg_env;                                  /* warm-up */
    } else {
        wm->ecg_amp += (wm->ecg_env - wm->ecg_amp) * 0.002f;        /* slow track */
        float ceiling = wm->ecg_amp * 3.0f;
        if (ceiling > 1.0f && wm->ecg_env > ceiling) {
            wm->ecg_env = ceiling;
        }
    }

    float env = (wm->ecg_env < 1.0f) ? 1.0f : wm->ecg_env;
    float norm = (ac / env) * 1.1f;
    if (norm > 1.3f) {
        norm = 1.3f;
    } else if (norm < -1.3f) {
        norm = -1.3f;
    }
    hpi_wave_monitor_push(obj, norm);
}

/* Reset the adaptive scaler + clear the drawn trace. Called on a fresh Start so
 * an inflated envelope from a previous capture doesn't carry over. */
void hpi_wave_monitor_reset(lv_obj_t *obj)
{
    wave_monitor_t *wm = lv_obj_get_user_data(obj);
    if (wm == NULL) {
        return;
    }
    wm->ecg_init = 0;
    wm->auto_init = 0;
    wm->head = 0;
    for (int i = 0; i < wm->n; i++) {
        wm->y[i] = wm->mid;
    }
    lv_obj_invalidate(obj);
}

/* synthetic PPG morphology: systolic upstroke + decay + dicrotic notch */
static float ppg_demo(float p)
{
    if (p < 0.13f) {
        return sinf(p / 0.13f * 1.5707964f);
    }
    float d = expf(-(p - 0.13f) * 2.6f);
    float notch = 0.16f * expf(-((p - 0.45f) * (p - 0.45f)) / (2.0f * 0.02f * 0.02f));
    return d + notch;
}

/* synthetic ECG morphology: P/Q/R/S/T (sum of Gaussians, handoff) */
static float ecg_demo(float p)
{
#define B(c, w) expf(-((p - (c)) * (p - (c))) / (2.0f * (w) * (w)))
    return 0.14f * B(0.16f, 0.03f) - 0.05f * B(0.275f, 0.008f) + 1.0f * B(0.30f, 0.006f)
           - 0.20f * B(0.325f, 0.012f) + 0.26f * B(0.52f, 0.045f);
#undef B
}

static void wave_demo_cb(lv_timer_t *t)
{
    lv_obj_t *obj = lv_timer_get_user_data(t);
    wave_monitor_t *wm = lv_obj_get_user_data(obj);
    if (wm == NULL) {
        return;
    }
    float hz = wm->dbpm / 60.0f;
    for (int i = 0; i < 4; i++) {   /* ~116 px/s */
        wm->dphase += hz / 116.0f;
        if (wm->dphase > 1.0f) {
            wm->dphase -= 1.0f;
        }
        hpi_wave_monitor_push(obj, wm->demo_ecg ? ecg_demo(wm->dphase) : ppg_demo(wm->dphase));
    }
}

static void wave_demo_start(lv_obj_t *obj, int bpm, int ecg)
{
    wave_monitor_t *wm = lv_obj_get_user_data(obj);
    if (wm == NULL) {
        return;
    }
    wm->dbpm = bpm;
    wm->dphase = 0;
    wm->demo_ecg = ecg;
    if (wm->demo == NULL) {
        wm->demo = lv_timer_create(wave_demo_cb, 33, obj);
    }
}

/* Attach a self-managing synthetic feed (auto-freed with the widget). */
void hpi_wave_monitor_demo(lv_obj_t *obj, int bpm)     { wave_demo_start(obj, bpm, 0); }
void hpi_wave_monitor_demo_ecg(lv_obj_t *obj, int bpm) { wave_demo_start(obj, bpm, 1); }

/* ------------------------------------------------------------- range bar */

/* SpO2-style range indicator: red/amber/green zones + a white marker at
 * frac [0..1] (negative = no marker). */
lv_obj_t *hpi_r0_range_bar_create(lv_obj_t *parent, int w, float frac)
{
    static const struct { float a, b; uint32_t c; } zones[3] = {
        {0.00f, 0.33f, 0xDC2626}, {0.33f, 0.60f, 0xEA580C}, {0.60f, 1.00f, 0x16A34A},
    };
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, w, 7);
    lv_obj_set_style_radius(bar, 4, 0);
    lv_obj_set_style_clip_corner(bar, true, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 3; i++) {
        lv_obj_t *z = lv_obj_create(bar);
        lv_obj_remove_style_all(z);
        lv_obj_set_pos(z, (int)(zones[i].a * w), 0);
        lv_obj_set_size(z, (int)((zones[i].b - zones[i].a) * w) + 1, 7);
        lv_obj_set_style_bg_color(z, lv_color_hex(zones[i].c), 0);
        lv_obj_set_style_bg_opa(z, LV_OPA_COVER, 0);
    }
    if (frac >= 0.0f) {
        lv_obj_t *dot = lv_obj_create(bar);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 11, 11);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 2, 0);
        lv_obj_set_style_border_color(dot, lv_color_black(), 0);
        lv_obj_align(dot, LV_ALIGN_LEFT_MID, (int)(frac * w) - 5, 0);
    }
    return bar;
}

/* -------------------------------------------------------------- sparkline */

typedef struct {
    float      pts[HPI_R0_SPARK_MAX];   /* normalized 0..1 */
    uint32_t   valid;                   /* bit i = pts[i] is real data        */
    int        n, w, h;
    lv_color_t color;
} sparkline_t;

static void sparkline_del(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

static void sparkline_draw(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target(e);
    sparkline_t *s = lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);

    if (s->n < 2) {
        return;
    }

    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = s->color;
    d.width = 3;
    d.round_start = 1;
    d.round_end = 1;

    /* Only join points that are BOTH real. A gap (watch off-skin, charging) must
     * read as a break in the line - bridging it would invent data, and treating
     * it as 0 would draw a dive to 0 bpm that looks like a physiological event. */
    for (int i = 0; i < s->n - 1; i++) {
        if (!(s->valid & (1u << i)) || !(s->valid & (1u << (i + 1)))) {
            continue;
        }
        d.p1.x = a.x1 + i * s->w / (s->n - 1);
        d.p1.y = a.y1 + (int)((1.0f - s->pts[i]) * (s->h - 1));
        d.p2.x = a.x1 + (i + 1) * s->w / (s->n - 1);
        d.p2.y = a.y1 + (int)((1.0f - s->pts[i + 1]) * (s->h - 1));
        lv_draw_line(layer, &d);
    }

    /* An isolated real point between two gaps would otherwise draw nothing at
     * all - mark it with a dot so a sparse trend is still visible. */
    lv_draw_rect_dsc_t dot;
    lv_draw_rect_dsc_init(&dot);
    dot.bg_color = s->color;
    dot.bg_opa = LV_OPA_COVER;
    dot.radius = LV_RADIUS_CIRCLE;
    for (int i = 0; i < s->n; i++) {
        bool me = s->valid & (1u << i);
        bool prev = (i > 0) && (s->valid & (1u << (i - 1)));
        bool next = (i < s->n - 1) && (s->valid & (1u << (i + 1)));
        if (!me || prev || next) {
            continue;
        }
        int x = a.x1 + i * s->w / (s->n - 1);
        int y = a.y1 + (int)((1.0f - s->pts[i]) * (s->h - 1));
        lv_area_t r = { x - 1, y - 1, x + 1, y + 1 };
        lv_draw_rect(layer, &dot, &r);
    }
}

void hpi_r0_sparkline_set(lv_obj_t *o, const float *pts, uint32_t valid, int n)
{
    if (o == NULL) {
        return;
    }
    sparkline_t *s = lv_obj_get_user_data(o);
    if (s == NULL) {
        return;
    }
    if (n > HPI_R0_SPARK_MAX) {
        n = HPI_R0_SPARK_MAX;
    }
    if (n < 0) {
        n = 0;
    }
    s->n = n;
    s->valid = valid;
    for (int i = 0; i < n; i++) {
        s->pts[i] = pts[i];
    }
    lv_obj_invalidate(o);
}

/* Trend polyline from normalized points [0..1]. `valid` marks which points hold
 * real data (bit i); pass ~0u when every point is real. */
lv_obj_t *hpi_r0_sparkline_create(lv_obj_t *parent, int w, int h, uint32_t color,
                                  const float *pts, int n)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);

    sparkline_t *s = lv_malloc(sizeof(*s));
    if (s == NULL) {
        return o;
    }
    memset(s, 0, sizeof(*s));
    s->n = (n > HPI_R0_SPARK_MAX) ? HPI_R0_SPARK_MAX : n;
    s->w = w;
    s->h = h;
    s->color = lv_color_hex(color);
    for (int i = 0; i < s->n; i++) {
        s->pts[i] = pts[i];
        s->valid |= (1u << i);
    }
    lv_obj_set_user_data(o, s);   /* so hpi_r0_sparkline_set() can find it */
    lv_obj_add_event_cb(o, sparkline_draw, LV_EVENT_DRAW_MAIN, s);
    lv_obj_add_event_cb(o, sparkline_del, LV_EVENT_DELETE, s);
    return o;
}
