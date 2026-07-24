/*
 * HealthyPi Move
 * 
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Author: Ashwin Whitchurch, Protocentral Electronics
 * Contact: ashwin@protocentral.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


#include <lvgl.h>
#include <stdio.h>
#include <zephyr/logging/log.h>

#include "hpi_common_types.h"
#include "hw_module.h"
#include "ui/move_ui.h"
#include "ui/hpi_r0_theme.h"
#include "ui/hpi_ui_subjects.h"

LOG_MODULE_REGISTER(scr_bpt_est_complete, LOG_LEVEL_DBG);

lv_obj_t *scr_bpt_est_complete;

int mode = 0;

static void scr_btn_close_handler(lv_event_t *e)
{
    ARG_UNUSED(e);
    hpi_load_screen(SCR_BPT, SCROLL_UP);
}

/* v2 BP result (the v2 design system, BP screen 4 "Result"):
 * sys/dia in the BP-blue hero, a green check_circle "COMPLETE · HR NN" pill, and
 * a DONE button. arg1=sys, arg2=dia, arg3=hr, arg4=mode (0 est / 1 cal). */
void draw_scr_bpt_est_complete(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    mode = arg4;

    /* Refresh the BP tile's bound subject so the last reading shows on return
     * (subj_bp was otherwise only set from the store at boot). Estimation only —
     * a calibration result (mode 1) is not a BP reading. */
    if (mode == 0 && arg1 > 0) {
        hpi_ui_subj_set_bp((int)arg1, (int)arg2);
    }

    scr_bpt_est_complete = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_bpt_est_complete, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_bpt_est_complete, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label_title = lv_label_create(scr_bpt_est_complete);
    lv_label_set_text(label_title, (mode == 1) ? "CALIBRATED" : "BLOOD PRESSURE");
    lv_obj_align(label_title, LV_ALIGN_CENTER, 0, -128);
    lv_obj_set_style_text_font(label_title, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(label_title, lv_color_hex(V2_MUTED2), 0);
    lv_obj_set_style_text_letter_space(label_title, 2, 0);

    /* sys / dia hero row (Rubik-88 blue, dim slash) */
    lv_obj_t *row = lv_obj_create(scr_bpt_est_complete);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(row, LV_ALIGN_CENTER, 0, -52);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 4, 0);

    lv_obj_t *l_sys = lv_label_create(row);
    lv_label_set_text_fmt(l_sys, "%u", (unsigned)arg1);
    lv_obj_set_style_text_font(l_sys, &HPI_FONT_HERO, 0);
    lv_obj_set_style_text_color(l_sys, lv_color_hex(V2_BP), 0);
    lv_obj_set_style_text_letter_space(l_sys, -1, 0);

    lv_obj_t *l_slash = lv_label_create(row);
    lv_label_set_text(l_slash, "/");
    lv_obj_set_style_text_font(l_slash, &HPI_FONT_HERO, 0);
    lv_obj_set_style_text_color(l_slash, lv_color_hex(V2_BP_SLASH), 0);

    lv_obj_t *l_dia = lv_label_create(row);
    lv_label_set_text_fmt(l_dia, "%u", (unsigned)arg2);
    lv_obj_set_style_text_font(l_dia, &HPI_FONT_HERO, 0);
    lv_obj_set_style_text_color(l_dia, lv_color_hex(V2_BP), 0);
    lv_obj_set_style_text_letter_space(l_dia, -1, 0);

    lv_obj_t *unit = lv_label_create(scr_bpt_est_complete);
    lv_label_set_text(unit, "MMHG");
    lv_obj_align(unit, LV_ALIGN_CENTER, 0, 6);
    lv_obj_set_style_text_font(unit, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(unit, lv_color_hex(V2_MUTED), 0);
    lv_obj_set_style_text_letter_space(unit, 2, 0);

    /* green check_circle "COMPLETE (· HR NN)" pill */
    lv_obj_t *pill = hpi_v2_pill(scr_bpt_est_complete, V2_GREEN, V2_TINT_OPA);
    lv_obj_align(pill, LV_ALIGN_CENTER, 0, 52);
    lv_obj_set_style_pad_hor(pill, 16, 0);

    lv_obj_t *pic = lv_label_create(pill);
    lv_label_set_text(pic, SYM_CHECK_CIRCLE);
    lv_obj_set_style_text_font(pic, &HPI_FONT_ICON, 0);
    lv_obj_set_style_text_color(pic, lv_color_hex(V2_GREEN), 0);

    lv_obj_t *ptx = lv_label_create(pill);
    if (arg3 > 0) {
        lv_label_set_text_fmt(ptx, "COMPLETE \xC2\xB7 HR %u", (unsigned)arg3);
    } else {
        lv_label_set_text(ptx, "COMPLETE");
    }
    lv_obj_set_style_text_font(ptx, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(ptx, lv_color_hex(V2_GREEN), 0);
    lv_obj_set_style_text_letter_space(ptx, 1, 0);

    /* DONE button */
    lv_obj_t *btn = hpi_btn_create_secondary(scr_bpt_est_complete);
    lv_obj_set_size(btn, 160, 56);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 128);
    lv_obj_add_event_cb(btn, scr_btn_close_handler, LV_EVENT_CLICKED, NULL);
    lv_obj_t *blbl = lv_label_create(btn);
    lv_label_set_text(blbl, "DONE");
    lv_obj_center(blbl);
    lv_obj_set_style_text_font(blbl, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(blbl, lv_color_hex(V2_VALUE), 0);
    lv_obj_set_style_text_letter_space(blbl, 1, 0);

    hpi_disp_set_curr_screen(SCR_SPL_BPT_EST_COMPLETE);
    hpi_show_screen(scr_bpt_est_complete, m_scroll_dir);
}

void gesture_down_scr_bpt_est_complete(void)
{
    hpi_load_screen(SCR_BPT, SCROLL_DOWN);
}