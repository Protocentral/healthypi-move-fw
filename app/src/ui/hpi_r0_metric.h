/*
 * HealthyPi Move — R0 metric-screen template (P6 redesign engine)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Config-driven builder for the repeated measurement/monitor screens. A screen
 * is a DECLARATION (r0_metric_cfg_t), not a hand-laid-out layout: the builder
 * assembles title -> hero value -> kind-specific middle (waveform/ring/none) ->
 * footer over the motif, using the central tokens/roles/anchors in
 * hpi_r0_theme.h. Change the look for the whole family by editing the builder or
 * the theme header - this is the single point the final redesign plugs into.
 */
#ifndef HPI_R0_METRIC_H
#define HPI_R0_METRIC_H

#include <lvgl.h>
#include "ui/hpi_r0_theme.h"

enum r0_metric_kind {
    R0_KIND_SPOT,      /* value + unit only (e.g. Temp)                    */
    R0_KIND_WAVEFORM,  /* value + unit + live monitor waveform (HR/SpO2)   */
    R0_KIND_RING,      /* value inside a progress ring (timed capture)     */
    R0_KIND_DUAL,      /* two values, e.g. BP "120/80"                     */
};

typedef struct {
    const char          *title;      /* section label, e.g. "HEART RATE"   */
    uint32_t             accent;     /* accent hex (R0_ACCENT, R0_SPO2, ...)*/
    lv_subject_t        *value;      /* string subject bound to hero value  */
    const char          *unit;       /* e.g. "BPM" (NULL = none)            */
    enum r0_metric_kind  kind;
    int                  wave_w;     /* WAVEFORM: width  (0 -> 296)         */
    int                  wave_h;     /* WAVEFORM: height (0 -> 74)          */
    /* structured footer (handoff): accent status word + up to 2 stat columns */
    const char          *status;     /* accent status word (NULL = none)    */
    uint32_t             status_color;/* 0 -> accent                        */
    const char          *stat1_val, *stat1_cap;  /* left column (NULL=none) */
    const char          *stat2_val, *stat2_cap;  /* right column            */
    /* optional live bindings: if set, the value tracks the subject via an
     * observer instead of the static string (which is then just the seed). */
    lv_subject_t        *status_subj;
    lv_subject_t        *stat1_subj, *stat2_subj;
} r0_metric_cfg_t;

typedef struct {
    lv_obj_t *screen;   /* the created screen (caller shows it)            */
    lv_obj_t *title;    /* title label (or NULL) - for extra decoration    */
    lv_obj_t *value;    /* hero value label                                */
    lv_obj_t *unit;     /* unit label (or NULL)                            */
    lv_obj_t *wave;     /* WAVEFORM kind: monitor widget (else NULL)       */
    lv_obj_t *ring;     /* RING kind: arc (else NULL)                      */
} r0_metric_ui_t;

/* Build the screen skeleton from cfg (layout computed before return). The
 * caller sets the current-screen id, starts any data feed (ui.wave / ui.ring),
 * and calls hpi_show_screen(ui.screen, dir). */
r0_metric_ui_t hpi_r0_metric_build(const r0_metric_cfg_t *cfg);

/* Same, but populates an existing parent (e.g. a carousel tile) instead of
 * creating a screen. Caller owns the parent's bg/scroll flags. */
r0_metric_ui_t hpi_r0_metric_build_into(lv_obj_t *parent, const r0_metric_cfg_t *cfg);

#endif /* HPI_R0_METRIC_H */
