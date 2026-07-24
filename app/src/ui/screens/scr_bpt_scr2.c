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
#include "hpi_evt.h"
#include <stdio.h>
#include <zephyr/logging/log.h>

#include "hpi_common_types.h"
#include "hw_module.h"
#include "ui/move_ui.h"
#include "ui/hpi_r0_theme.h"

LOG_MODULE_REGISTER(scr_bpt_scr2, LOG_LEVEL_DBG);

lv_obj_t *scr_bpt_scr2;
static lv_obj_t *btn_spo2_proceed;

static int next_screen =0;
static int parent_screen = 0;

static void bpt_fp_opa_cb(void *var, int32_t v)
{
    lv_obj_set_style_text_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void scr_bpt_btn_proceed_handler(lv_event_t *e)
{
    ARG_UNUSED(e);
    // Only signal the state machine - it handles the screen transitions
    // (this avoids a race when calibration is required).
    k_event_post(&fi_evt, EVT_BPT_EST_START);
}

static void scr_bpt_btn_cancel_handler(lv_event_t *e)
{
    ARG_UNUSED(e);
    hpi_load_screen(parent_screen, SCROLL_DOWN);
}

/* v2 finger-sensor wear screen (the v2 design system, BP screen 4
 * "Sensor-wear"): a big pulsing fingerprint in BP blue, the wear instruction,
 * a filled-blue PROCEED button (posts EVT_BPT_EST_START), and a text CANCEL. */
void draw_scr_fi_sens_wear(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    ARG_UNUSED(arg3); ARG_UNUSED(arg4);
    next_screen = arg1;
    parent_screen = arg2;

    scr_bpt_scr2 = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_bpt_scr2, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_bpt_scr2, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label_title = lv_label_create(scr_bpt_scr2);
    lv_label_set_text(label_title, "FINGER SENSOR");
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_style_text_font(label_title, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(label_title, lv_color_hex(V2_MUTED2), 0);
    lv_obj_set_style_text_letter_space(label_title, 2, 0);

    /* pulsing fingerprint glyph (matsym_fp_72, BP blue) */
    lv_obj_t *fp = lv_label_create(scr_bpt_scr2);
    lv_label_set_text(fp, SYM_FINGERPRINT);
    lv_obj_align(fp, LV_ALIGN_CENTER, 0, -46);
    lv_obj_set_style_text_font(fp, &HPI_FONT_ICON_XL, 0);
    lv_obj_set_style_text_color(fp, lv_color_hex(V2_BP), 0);
    lv_anim_t pa;
    lv_anim_init(&pa);
    lv_anim_set_var(&pa, fp);
    lv_anim_set_exec_cb(&pa, bpt_fp_opa_cb);
    lv_anim_set_values(&pa, 255, 90);
    lv_anim_set_time(&pa, 800);
    lv_anim_set_playback_time(&pa, 800);
    lv_anim_set_repeat_count(&pa, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&pa);

    lv_obj_t *label_info = lv_label_create(scr_bpt_scr2);
    lv_label_set_long_mode(label_info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_info, 290);
    lv_label_set_text(label_info, "Wear the finger sensor now,\nkeep your hand relaxed");
    lv_obj_align(label_info, LV_ALIGN_CENTER, 0, 44);
    lv_obj_set_style_text_font(label_info, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_info, lv_color_hex(V2_LABEL), 0);

    /* filled-blue PROCEED */
    btn_spo2_proceed = hpi_btn_create_primary(scr_bpt_scr2);
    lv_obj_add_event_cb(btn_spo2_proceed, scr_bpt_btn_proceed_handler, LV_EVENT_CLICKED, NULL);
    lv_obj_set_size(btn_spo2_proceed, 190, 56);
    lv_obj_align(btn_spo2_proceed, LV_ALIGN_BOTTOM_MID, 0, -58);
    lv_obj_set_style_radius(btn_spo2_proceed, 28, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn_spo2_proceed, lv_color_hex(V2_BP), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_spo2_proceed, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *label_btn = lv_label_create(btn_spo2_proceed);
    lv_label_set_text(label_btn, "PROCEED");
    lv_obj_center(label_btn);
    lv_obj_set_style_text_font(label_btn, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(label_btn, lv_color_hex(V2_ON_BP), 0);
    lv_obj_set_style_text_letter_space(label_btn, 1, 0);

    /* text-only CANCEL */
    lv_obj_t *btn_cancel = lv_button_create(scr_bpt_scr2);
    lv_obj_remove_style_all(btn_cancel);
    lv_obj_set_size(btn_cancel, 140, 34);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_add_event_cb(btn_cancel, scr_bpt_btn_cancel_handler, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(label_cancel, "CANCEL");
    lv_obj_center(label_cancel);
    lv_obj_set_style_text_font(label_cancel, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(label_cancel, lv_color_hex(V2_MUTED), 0);
    lv_obj_set_style_text_letter_space(label_cancel, 1, 0);

    hpi_disp_set_curr_screen(SCR_SPL_FI_SENS_WEAR);
    hpi_show_screen(scr_bpt_scr2, m_scroll_dir);
}

void gesture_down_scr_fi_sens_wear(void)
{
    hpi_load_screen(parent_screen, SCROLL_DOWN);
}