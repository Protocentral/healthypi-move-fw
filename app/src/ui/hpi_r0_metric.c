/*
 * HealthyPi Move — R0 metric-screen template builder (P6 redesign engine)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 */

#include <zephyr/kernel.h>
#include <lvgl.h>

#include "ui/hpi_r0_metric.h"
#include "ui/hpi_ui_subjects.h"
#include "ui/move_ui.h"
#include "ui/hpi_r0_theme.h"

extern lv_style_t style_scr_black;

/* hero value + unit in a flex row so the unit re-flows as the value width changes. */
static void build_value_row(lv_obj_t *scr, const r0_metric_cfg_t *cfg, r0_metric_ui_t *ui)
{
    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(row, LV_ALIGN_CENTER, 0, R0_Y_VALUE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);

    lv_obj_t *val = lv_label_create(row);
    lv_label_set_text(val, "--");
    lv_obj_set_style_text_font(val, &HPI_FONT_HERO, 0);   /* Rubik 88 */
    lv_obj_set_style_text_color(val, lv_color_hex(cfg->accent), 0);
    lv_obj_set_style_text_letter_space(val, -2, 0);       /* v2 hero tracking -0.025em */
    if (cfg->value != NULL) {
        hpi_ui_bind_label(val, cfg->value);
    }
    ui->value = val;

    if (cfg->unit != NULL) {
        lv_obj_t *u = lv_label_create(row);
        lv_label_set_text(u, cfg->unit);
        lv_obj_set_style_text_font(u, &HPI_FONT_LABEL, 0); /* Manrope 22 */
        lv_obj_set_style_text_color(u, lv_color_hex(V2_MUTED), 0);
        lv_obj_set_style_pad_bottom(u, 20, 0);   /* lift off the hero baseline */
        ui->unit = u;
    }
}

/* one footer stat as a v2 chip: Value numeral over a Label caption. If `subj`
 * is non-NULL the value label tracks it live (observer); else `val` is static. */
static void stat_col(lv_obj_t *row, const char *val, const char *cap, lv_subject_t *subj)
{
    lv_obj_t *c = hpi_v2_chip(row);                       /* soft rounded chip */
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_radius(c, 16, 0);                    /* v2 stat-chip radius */
    lv_obj_set_style_pad_hor(c, 18, 0);
    lv_obj_set_style_pad_ver(c, 8, 0);
    lv_obj_set_style_pad_row(c, 2, 0);

    lv_obj_t *v = lv_label_create(c);
    lv_obj_set_style_text_font(v, &HPI_FONT_VALUE, 0);    /* Rubik 32 */
    lv_obj_set_style_text_color(v, lv_color_hex(V2_VALUE), 0);
    lv_obj_set_style_text_letter_space(v, -1, 0);
    if (subj != NULL) {
        hpi_ui_bind_label(v, subj);
    } else {
        lv_label_set_text(v, val);
    }

    lv_obj_t *cp = lv_label_create(c);
    lv_label_set_text(cp, cap);
    lv_obj_set_style_text_font(cp, &HPI_FONT_LABEL, 0);   /* Manrope 22 */
    lv_obj_set_style_text_color(cp, lv_color_hex(V2_MUTED), 0);
    lv_obj_set_style_text_letter_space(cp, 2, 0);
}

/* status word (accent) + up to two stat columns */
static void build_footer(lv_obj_t *scr, const r0_metric_cfg_t *cfg)
{
    if (cfg->status != NULL || cfg->status_subj != NULL) {
        lv_obj_t *s = lv_label_create(scr);
        lv_obj_align(s, LV_ALIGN_CENTER, 0, R0_Y_STATUS);
        lv_obj_set_style_text_font(s, &R0_FONT_LABEL, 0);
        lv_obj_set_style_text_color(s, lv_color_hex(cfg->status_color ? cfg->status_color : cfg->accent), 0);
        lv_obj_set_style_text_letter_space(s, 3, 0);
        if (cfg->status_subj != NULL) {
            hpi_ui_bind_label(s, cfg->status_subj);
        } else {
            lv_label_set_text(s, cfg->status);
        }
    }
    if (cfg->stat1_val != NULL || cfg->stat1_subj != NULL) {
        lv_obj_t *row = lv_obj_create(scr);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_align(row, LV_ALIGN_CENTER, 0, R0_Y_STATS);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 14, 0);   /* v2 chip gap */
        stat_col(row, cfg->stat1_val, cfg->stat1_cap, cfg->stat1_subj);
        if (cfg->stat2_val != NULL || cfg->stat2_subj != NULL) {
            stat_col(row, cfg->stat2_val, cfg->stat2_cap, cfg->stat2_subj);
        }
    }
}

/* Populate an existing parent (a screen or a carousel tile) with the template. */
r0_metric_ui_t hpi_r0_metric_build_into(lv_obj_t *scr, const r0_metric_cfg_t *cfg)
{
    r0_metric_ui_t ui = {0};
    ui.screen = scr;

    /* section title */
    if (cfg->title != NULL) {
        lv_obj_t *lbl = lv_label_create(scr);
        lv_label_set_text(lbl, cfg->title);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, R0_Y_TITLE);
        lv_obj_set_style_text_font(lbl, &HPI_FONT_LABEL, 0);      /* Manrope 22 */
        lv_obj_set_style_text_color(lbl, lv_color_hex(V2_LABEL), 0);
        lv_obj_set_style_text_letter_space(lbl, 2, 0);
        ui.title = lbl;
    }

    /* hero value + unit */
    build_value_row(scr, cfg, &ui);

    /* kind-specific middle element */
    switch (cfg->kind) {
    case R0_KIND_WAVEFORM: {
        int w = cfg->wave_w ? cfg->wave_w : 296;
        int h = cfg->wave_h ? cfg->wave_h : 74;
        ui.wave = hpi_wave_monitor_create(scr, w, h, lv_color_hex(cfg->accent));
        lv_obj_align(ui.wave, LV_ALIGN_CENTER, 0, R0_Y_PLOT);
        break;
    }
    case R0_KIND_SPOT:
    case R0_KIND_RING:   /* ring/gauge components land in R2 */
    case R0_KIND_DUAL:
    default:
        break;
    }

    build_footer(scr, cfg);

    lv_obj_update_layout(scr);   /* compute flex/align before first show */
    return ui;
}

/* Standalone: create a black screen and populate it. */
r0_metric_ui_t hpi_r0_metric_build(const r0_metric_cfg_t *cfg)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_add_style(scr, &style_scr_black, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    return hpi_r0_metric_build_into(scr, cfg);
}
