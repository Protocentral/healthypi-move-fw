/*
 * HealthyPi Move — Blood-pressure (BPT) monitor tile (v2 design, carousel)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * v2 idle state for the BP carousel tile (the v2 design system,
 * screen 4). Shows the last estimate (sys/dia, Rubik-66 blue), the finger-
 * sensor requirement pill, and a MEASURE button. MEASURE launches the proven
 * finger-sensor measurement flow (SCR_SPL_FI_SENS_WEAR -> BPT_MEASURE ->
 * BPT_EST_COMPLETE), which is driven by the finger SMF over fi_evt/bpt_chan.
 * The sensor-wear / measuring / result states live in those special screens
 * (hardware-gated on the finger sensor); the tile owns only the idle view.
 */

#include <zephyr/kernel.h>
#include <stdio.h>
#include <lvgl.h>

#include "hpi_common_types.h"
#include "ui/move_ui.h"
#include "ui/hpi_r0_theme.h"
#include "ui/hpi_ui_subjects.h"
#include "health/hpi_health_store.h"

static void bpt_measure_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    /* Enter the finger-sensor guidance screen; the finger SMF powers + probes
     * the sensor on start, so no boot-time presence check is needed here. */
    hpi_load_scr_spl(SCR_SPL_FI_SENS_WEAR, SCROLL_UP, SCR_BPT, SCR_BPT, 0, 0);
}

void hpi_bpt_monitor_into(lv_obj_t *parent)
{
    /* header caption */
    lv_obj_t *hdr = lv_label_create(parent);
    lv_label_set_text(hdr, "BLOOD PRESSURE");
    lv_obj_align(hdr, LV_ALIGN_CENTER, 0, -128);
    lv_obj_set_style_text_font(hdr, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(hdr, lv_color_hex(V2_MUTED2), 0);
    lv_obj_set_style_text_letter_space(hdr, 2, 0);

    /* last reading: "118/76" (sys/dia), Rubik-66 in BP blue. subj_bp carries
     * the combined "sys/dia" string ("--" before any estimate). */
    lv_obj_t *val = lv_label_create(parent);
    lv_label_set_text(val, "--");
    lv_obj_align(val, LV_ALIGN_CENTER, 0, -52);
    lv_obj_set_style_text_font(val, &HPI_FONT_HERO, 0);   /* Rubik 88 (shared hero) */
    lv_obj_set_style_text_color(val, lv_color_hex(V2_BP), 0);
    lv_obj_set_style_text_letter_space(val, -1, 0);
    hpi_ui_bind_label(val, &subj_bp);

    lv_obj_t *unit = lv_label_create(parent);
    {
        char ago[16];
        struct hpi_hs_sample sm;
        if (hpi_hs_get_latest(HPI_HS_T_BP_SYS, &sm) && sm.value > 0) {
            hpi_ui_format_ago(sm.ts_utc, ago, sizeof(ago));
            lv_label_set_text_fmt(unit, "MMHG · %s", ago);
        } else {
            lv_label_set_text(unit, "MMHG");
        }
    }
    lv_obj_align(unit, LV_ALIGN_CENTER, 0, 6);
    lv_obj_set_style_text_font(unit, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(unit, lv_color_hex(V2_MUTED), 0);
    lv_obj_set_style_text_letter_space(unit, 2, 0);

    /* "FINGER SENSOR REQUIRED" pill (sensors icon + label) */
    lv_obj_t *pill = hpi_v2_pill(parent, V2_BP, V2_TINT_OPA);
    lv_obj_align(pill, LV_ALIGN_CENTER, 0, 52);
    lv_obj_set_style_pad_hor(pill, 16, 0);
    lv_obj_set_style_pad_column(pill, 8, 0);

    lv_obj_t *pic = lv_label_create(pill);
    lv_label_set_text(pic, SYM_SENSORS);
    lv_obj_set_style_text_font(pic, &HPI_FONT_ICON, 0);
    lv_obj_set_style_text_color(pic, lv_color_hex(V2_BP), 0);

    lv_obj_t *ptx = lv_label_create(pill);
    lv_label_set_text(ptx, "FINGER SENSOR REQUIRED");
    lv_obj_set_style_text_font(ptx, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(ptx, lv_color_hex(V2_LABEL), 0);
    lv_obj_set_style_text_letter_space(ptx, 1, 0);

    /* MEASURE button (play icon + label) on a blue tint */
    lv_obj_t *btn = hpi_btn_create_secondary(parent);
    lv_obj_set_size(btn, 200, 60);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 128);
    lv_obj_set_style_bg_color(btn, lv_color_hex(V2_BP), 0);
    lv_obj_set_style_bg_opa(btn, 41, 0);   /* ~16% tint */
    lv_obj_add_event_cb(btn, bpt_measure_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *brow = hpi_btn_row_create(btn, 8);

    lv_obj_t *bic = lv_label_create(brow);
    lv_label_set_text(bic, SYM_PLAY);
    lv_obj_set_style_text_font(bic, &HPI_FONT_ICON, 0);
    lv_obj_set_style_text_color(bic, lv_color_hex(V2_BP), 0);

    lv_obj_t *blbl = lv_label_create(brow);
    lv_label_set_text(blbl, "MEASURE");
    lv_obj_set_style_text_font(blbl, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(blbl, lv_color_hex(V2_BP), 0);
    lv_obj_set_style_text_letter_space(blbl, 1, 0);
}
