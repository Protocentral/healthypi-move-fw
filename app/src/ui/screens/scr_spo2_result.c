/*
 * HealthyPi Move
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * SpO2 result screen (the v2 design system, option 1a). One
 * outcome-driven template renders success / timeout / cancelled — the outcome
 * arrives as an argument rather than as three near-identical screens.
 *
 * Success: hero value + source icon, then DONE (back to the idle tile) / AGAIN
 * (straight back into measuring, same source). Timeout/cancelled keep the same
 * skeleton with a reason line and DONE + AGAIN, so a failed spot check is one
 * tap from a retry.
 */

#include <zephyr/kernel.h>
#include <lvgl.h>
#include <stdio.h>

#include "hpi_common_types.h"
#include "hpi_evt.h"
#include "hpi_sys.h"
#include "ui/move_ui.h"
#include "ui/hpi_r0_theme.h"

/* outcome codes (arg2) come from move_ui.h: HPI_SPO2_RESULT_{SUCCESS,TIMEOUT,CANCELLED} */

lv_obj_t *scr_spo2_result = NULL;

extern lv_style_t style_scr_black;

static void spo2_result_done_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    hpi_carousel_show(SCR_SPO2, SCROLL_DOWN);
}

/* AGAIN — restart the same source, mirroring the idle tile's START branch. */
static void spo2_result_again_cb(lv_event_t *e)
{
    ARG_UNUSED(e);

    if (hpi_spo2_source_get() == SPO2_SOURCE_PPG_FI) {
        k_event_post(&fi_evt, EVT_FI_SPO2_START);   /* finger SMF navigates */
        hpi_carousel_show(SCR_SPO2, SCROLL_DOWN);
        return;
    }
    k_event_post(&spo2_evt, EVT_SPO2_START);
    hpi_load_scr_spl(SCR_SPL_SPO2_MEASURE, SCROLL_UP, SCR_SPO2, SPO2_SOURCE_PPG_WR, 0, 0);
}

void gesture_down_scr_spo2_result(void)
{
    hpi_carousel_show(SCR_SPO2, SCROLL_DOWN);
}

/* One pill: icon + text on a tint of the text color. */
static lv_obj_t *spo2_result_pill(lv_obj_t *parent, const char *icon, const char *text, uint32_t color)
{
    lv_obj_t *pill = hpi_v2_pill(parent, color, 36);   /* ~14% tint */
    lv_obj_set_style_pad_hor(pill, 18, 0);
    lv_obj_set_style_pad_column(pill, 6, 0);

    lv_obj_t *ic = lv_label_create(pill);
    lv_label_set_text(ic, icon);
    lv_obj_set_style_text_font(ic, &HPI_FONT_ICON, 0);
    lv_obj_set_style_text_color(ic, lv_color_hex(color), 0);

    lv_obj_t *tx = lv_label_create(pill);
    lv_label_set_text(tx, text);
    lv_obj_set_style_text_font(tx, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(tx, lv_color_hex(color), 0);

    return pill;
}

/* arg1 = parent (unused, carousel), arg2 = outcome, arg3 = spo2 value, arg4 = hr */
void draw_scr_spo2_result(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg4);
    int outcome = (int)arg2;
    int spo2 = (int)arg3;
    bool wrist = (hpi_spo2_source_get() != SPO2_SOURCE_PPG_FI);
    bool ok = (outcome == HPI_SPO2_RESULT_SUCCESS && spo2 > 0);

    scr_spo2_result = lv_obj_create(NULL);
    lv_obj_add_style(scr_spo2_result, &style_scr_black, 0);
    lv_obj_clear_flag(scr_spo2_result, LV_OBJ_FLAG_SCROLLABLE);

    /* header caption — same words as the idle tile, so the result reads as a
     * continuation of it rather than a different place. */
    lv_obj_t *hdr = lv_label_create(scr_spo2_result);
    lv_label_set_text(hdr, "BLOOD OXYGEN");
    lv_obj_align(hdr, LV_ALIGN_CENTER, 0, -128);
    lv_obj_set_style_text_font(hdr, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(hdr, lv_color_hex(V2_LABEL), 0);
    lv_obj_set_style_text_letter_space(hdr, 2, 0);

    if (ok) {
        /* hero value + unit */
        lv_obj_t *row = lv_obj_create(scr_spo2_result);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(row, LV_ALIGN_CENTER, 0, -40);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 7, 0);

        lv_obj_t *hero = lv_label_create(row);
        lv_label_set_text_fmt(hero, "%d", spo2);
        lv_obj_set_style_text_font(hero, &HPI_FONT_HERO, 0);   /* Rubik 88 */
        lv_obj_set_style_text_color(hero, lv_color_hex(V2_SPO2), 0);
        lv_obj_set_style_text_letter_space(hero, -2, 0);

        lv_obj_t *unit = lv_label_create(row);
        lv_label_set_text(unit, "%");
        lv_obj_set_style_text_font(unit, &HPI_FONT_LABEL, 0);
        lv_obj_set_style_text_color(unit, lv_color_hex(V2_SPO2), 0);
        lv_obj_set_style_pad_top(unit, 18, 0);

        /* Keep the idle-tile hero + age in sync even if the store rejected the
         * sample (RTC not VALID) or the finger path had not published yet. */
        hpi_disp_update_spo2((uint8_t)spo2, hw_get_sys_time_ts());
    } else {
        bool timed_out = (outcome == HPI_SPO2_RESULT_TIMEOUT);
        lv_obj_t *pill = spo2_result_pill(scr_spo2_result,
                                          timed_out ? SYM_DO_NOT_TOUCH : SYM_BACK,
                                          timed_out ? "TIMED OUT" : "CANCELLED",
                                          timed_out ? R0_WARNING : V2_MUTED2);
        lv_obj_align(pill, LV_ALIGN_CENTER, 0, -50);

        lv_obj_t *msg = lv_label_create(scr_spo2_result);
        lv_label_set_text(msg, (outcome == HPI_SPO2_RESULT_TIMEOUT)
                               ? "KEEP STILL AND TRY AGAIN"
                               : "NO READING TAKEN");
        lv_obj_align(msg, LV_ALIGN_CENTER, 0, 6);
        lv_obj_set_style_text_font(msg, &HPI_FONT_LABEL, 0);
        lv_obj_set_style_text_color(msg, lv_color_hex(V2_MUTED), 0);
        lv_obj_set_style_text_letter_space(msg, 1, 0);
    }

    /* source: icon only (watch / fingerprint) — text was dropped as redundant
     * with the source the user just chose on the idle tile. */
    lv_obj_t *sic = lv_label_create(scr_spo2_result);
    lv_label_set_text(sic, wrist ? SYM_WATCH : SYM_FINGERPRINT);
    lv_obj_align(sic, LV_ALIGN_CENTER, 0, ok ? 40 : 52);
    lv_obj_set_style_text_font(sic, &HPI_FONT_ICON, 0);
    lv_obj_set_style_text_color(sic, lv_color_hex(V2_MUTED), 0);

    /* actions: DONE (neutral) + AGAIN (accent tint) */
    lv_obj_t *arow = lv_obj_create(scr_spo2_result);
    lv_obj_remove_style_all(arow);
    lv_obj_set_size(arow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(arow, LV_OBJ_FLAG_SCROLLABLE);
    /* y and widths are bezel-driven: at 268 px wide and y=126 the row's bottom
     * corners fell outside the 390 px circle. 240 px at y=120 clears it. */
    lv_obj_align(arow, LV_ALIGN_CENTER, 0, 120);
    lv_obj_set_flex_flow(arow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(arow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(arow, 12, 0);

    lv_obj_t *btn_done = lv_btn_create(arow);
    lv_obj_remove_style_all(btn_done);
    lv_obj_set_size(btn_done, 104, 52);
    lv_obj_set_style_radius(btn_done, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn_done, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(btn_done, 20, 0);   /* ~8% neutral surface */
    lv_obj_add_event_cb(btn_done, spo2_result_done_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ldone = lv_label_create(btn_done);
    lv_label_set_text(ldone, "DONE");
    lv_obj_center(ldone);
    lv_obj_set_style_text_font(ldone, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(ldone, lv_color_hex(V2_VALUE), 0);
    lv_obj_set_style_text_letter_space(ldone, 2, 0);

    lv_obj_t *btn_again = lv_btn_create(arow);
    lv_obj_remove_style_all(btn_again);
    lv_obj_set_size(btn_again, 124, 52);
    lv_obj_set_style_radius(btn_again, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn_again, lv_color_hex(V2_SPO2), 0);
    lv_obj_set_style_bg_opa(btn_again, 41, 0);   /* ~16% accent tint */
    lv_obj_add_event_cb(btn_again, spo2_result_again_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *arow_in = hpi_btn_row_create(btn_again, 7);

    lv_obj_t *aic = lv_label_create(arow_in);
    lv_label_set_text(aic, SYM_REFRESH);
    lv_obj_set_style_text_font(aic, &HPI_FONT_ICON, 0);
    lv_obj_set_style_text_color(aic, lv_color_hex(V2_SPO2), 0);

    lv_obj_t *atx = lv_label_create(arow_in);
    lv_label_set_text(atx, "AGAIN");
    lv_obj_set_style_text_font(atx, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(atx, lv_color_hex(V2_SPO2), 0);
    lv_obj_set_style_text_letter_space(atx, 1, 0);

    hpi_disp_set_curr_screen(SCR_SPL_SPO2_RESULT);
    hpi_show_screen(scr_spo2_result, m_scroll_dir);
}
