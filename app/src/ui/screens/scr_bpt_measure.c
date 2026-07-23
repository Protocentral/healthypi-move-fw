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

/*
 * BP measurement screen — v2 (the v2 design system §4 "Measuring").
 * Pulsing blue dot + BLOOD PRESSURE header; MEASURING · KEEP STILL status;
 * live PPG waveform (hpi_wave_monitor, auto-scaled); status pill built like the
 * ECG monitor: 66 px progress ring (percent centered) + heart + live HR + BPM;
 * CANCEL below. Fed by the display SMF via hpi_disp_bpt_draw_plotPPG /
 * hpi_disp_bpt_update_progress; swipe-down or CANCEL post EVT_FI_BPT_EST_CANCEL.
 */

#include <zephyr/kernel.h>
#include "hpi_evt.h"
#include <lvgl.h>
#include <stdio.h>
#include <zephyr/logging/log.h>

#include "hpi_common_types.h"
#include "hw_module.h"
#include "ui/move_ui.h"
#include "ui/hpi_r0_theme.h"

LOG_MODULE_REGISTER(scr_bpt_measure, LOG_LEVEL_DBG);
lv_obj_t *scr_bpt_measure;

/* ~3 s of finger PPG (100 SPS) spread across the 296 px trace. */
#define BPT_WAVE_W   296
#define BPT_WAVE_H    74
#define BPT_WAVE_WIN 300

static lv_obj_t *bpt_wave;
static lv_obj_t *ring_progress;
static lv_obj_t *label_ring_pct;
static lv_obj_t *label_hr_bpm;

/* The display thread keeps pointers to these widgets via the draw/update hooks.
 * Null them on delete so a late sample can't write into freed objects. */
static void bpt_measure_del(lv_event_t *e)
{
    ARG_UNUSED(e);
    bpt_wave = NULL;
    ring_progress = NULL;
    label_ring_pct = NULL;
    label_hr_bpm = NULL;
}

static void dot_opa_cb(void *var, int32_t v)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void bpt_cancel_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    gesture_down_scr_bpt_measure();
}

/* Compact 66 px progress ring with the percent numeral centred (NUM_SM has no
 * '%' glyph, so the ring shows the number alone — the ring is the "percent"). */
static lv_obj_t *hpi_bpt_make_progress_ring(lv_obj_t *parent, lv_obj_t **out_pct)
{
    lv_obj_t *ring = lv_arc_create(parent);
    lv_obj_set_size(ring, 66, 66);
    lv_arc_set_rotation(ring, 270);
    lv_arc_set_bg_angles(ring, 0, 360);
    lv_arc_set_range(ring, 0, 100);
    lv_arc_set_value(ring, 0);
    lv_obj_set_style_arc_width(ring, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(ring, 26, LV_PART_MAIN);            /* ~10% track */
    lv_obj_set_style_arc_width(ring, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ring, lv_color_hex(V2_BP), LV_PART_INDICATOR);
    lv_obj_remove_style(ring, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *pct = lv_label_create(ring);
    lv_label_set_text(pct, "0");
    lv_obj_center(pct);
    lv_obj_set_style_text_font(pct, &HPI_FONT_NUM_SM, 0);
    lv_obj_set_style_text_color(pct, lv_color_hex(V2_VALUE), 0);
    if (out_pct) {
        *out_pct = pct;
    }
    return ring;
}

/* The pulsing-dot + header title row shared by the measure/cal chrome. Returns
 * the row; the title text is set by the caller. */
lv_obj_t *hpi_bpt_make_title_row(lv_obj_t *parent, const char *title)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(row, LV_ALIGN_CENTER, 0, -132);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);

    lv_obj_t *dot = lv_obj_create(row);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(V2_BP), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_font(lbl, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(V2_LABEL), 0);
    lv_obj_set_style_text_letter_space(lbl, 2, 0);

    lv_anim_t an;
    lv_anim_init(&an);
    lv_anim_set_var(&an, dot);
    lv_anim_set_exec_cb(&an, dot_opa_cb);
    lv_anim_set_values(&an, 255, 90);
    lv_anim_set_time(&an, 550);
    lv_anim_set_playback_time(&an, 550);
    lv_anim_set_repeat_count(&an, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&an);

    return row;
}

/* Status pill: [ring+%] [heart] [HR] [BPM] on a soft chip. Publishes the ring,
 * percent and HR labels to the caller-supplied out-pointers. */
lv_obj_t *hpi_bpt_make_status_pill(lv_obj_t *parent, lv_obj_t **out_ring,
                                   lv_obj_t **out_pct, lv_obj_t **out_hr)
{
    lv_obj_t *pill = lv_obj_create(parent);
    lv_obj_remove_style_all(pill);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(pill, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(pill, 13, 0);                        /* ~5% chip */
    lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(pill, 14, 0);
    lv_obj_set_style_pad_ver(pill, 8, 0);
    lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pill, 10, 0);

    lv_obj_t *ring = hpi_bpt_make_progress_ring(pill, out_pct);
    if (out_ring) {
        *out_ring = ring;
    }

    lv_obj_t *ic_hr = lv_label_create(pill);
    lv_label_set_text(ic_hr, SYM_HR);
    lv_obj_set_style_text_font(ic_hr, &HPI_FONT_ICON, 0);
    lv_obj_set_style_text_color(ic_hr, lv_color_hex(V2_BP), 0);
    lv_obj_set_style_pad_left(ic_hr, 4, 0);

    lv_obj_t *hr = lv_label_create(pill);
    lv_label_set_text(hr, "--");
    lv_obj_set_style_text_font(hr, &HPI_FONT_VALUE, 0);
    lv_obj_set_style_text_color(hr, lv_color_hex(V2_VALUE), 0);
    if (out_hr) {
        *out_hr = hr;
    }

    lv_obj_t *unit = lv_label_create(pill);
    lv_label_set_text(unit, "BPM");
    lv_obj_set_style_text_font(unit, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(unit, lv_color_hex(V2_MUTED), 0);

    return pill;
}

/* Quiet text CANCEL control, same treatment as the ECG monitor. */
lv_obj_t *hpi_bpt_make_cancel(lv_obj_t *parent, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 160, 48);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 140);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "CANCEL");
    lv_obj_center(lbl);
    lv_obj_set_style_text_font(lbl, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(V2_MUTED), 0);
    lv_obj_set_style_text_letter_space(lbl, 2, 0);
    return btn;
}

void draw_scr_bpt_measure(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_bpt_measure = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_bpt_measure, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_bpt_measure, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr_bpt_measure, bpt_measure_del, LV_EVENT_DELETE, NULL);

    /* header: pulsing dot + BLOOD PRESSURE */
    hpi_bpt_make_title_row(scr_bpt_measure, "BLOOD PRESSURE");

    /* status line */
    lv_obj_t *status = lv_label_create(scr_bpt_measure);
    lv_label_set_text(status, "MEASURING \xC2\xB7 KEEP STILL");
    lv_obj_align(status, LV_ALIGN_CENTER, 0, -100);
    lv_obj_set_style_text_font(status, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(status, lv_color_hex(V2_BP), 0);
    lv_obj_set_style_text_letter_space(status, 1, 0);

    /* live PPG trace — auto-scaled monitor waveform */
    bpt_wave = hpi_wave_monitor_create(scr_bpt_measure, BPT_WAVE_W, BPT_WAVE_H,
                                       lv_color_hex(V2_BP));
    lv_obj_align(bpt_wave, LV_ALIGN_CENTER, 0, -30);
    hpi_wave_monitor_set_window(bpt_wave, BPT_WAVE_WIN);

    /* status pill: ring + heart + HR + BPM */
    lv_obj_t *pill = hpi_bpt_make_status_pill(scr_bpt_measure, &ring_progress,
                                              &label_ring_pct, &label_hr_bpm);
    lv_obj_align(pill, LV_ALIGN_CENTER, 0, 58);

    hpi_bpt_make_cancel(scr_bpt_measure, bpt_cancel_cb);

    hpi_disp_set_curr_screen(SCR_SPL_BPT_MEASURE);
    hpi_show_screen(scr_bpt_measure, m_scroll_dir);
}

void hpi_disp_bpt_update_progress(int progress)
{
    if (label_ring_pct == NULL || ring_progress == NULL) {
        return;
    }
    if (progress < 0) {
        progress = 0;
    } else if (progress > 100) {
        progress = 100;
    }
    lv_arc_set_value(ring_progress, progress);
    lv_label_set_text_fmt(label_ring_pct, "%d", progress);
}

void hpi_disp_bpt_draw_plotPPG(struct hpi_ppg_fi_data_t ppg_sensor_sample)
{
    if (bpt_wave == NULL || label_hr_bpm == NULL) {
        return;   /* screen torn down between the msgq drain and this call */
    }

    uint32_t *data_ppg = ppg_sensor_sample.raw_red;
    uint16_t n_sample = ppg_sensor_sample.ppg_num_samples;

    for (int i = 0; i < n_sample; i++) {
        hpi_wave_monitor_push_auto(bpt_wave, (int32_t)data_ppg[i]);
    }

    if (ppg_sensor_sample.hr > 0) {
        lv_label_set_text_fmt(label_hr_bpm, "%d", ppg_sensor_sample.hr);
    } else {
        lv_label_set_text(label_hr_bpm, "--");
    }
}

void gesture_down_scr_bpt_measure(void)
{
    LOG_INF("Cancel BPT measurement (swipe/CANCEL) - posting EVT_FI_BPT_EST_CANCEL");
    k_event_post(&fi_evt, EVT_FI_BPT_EST_CANCEL);
    /* Defer, not hpi_load_screen(): this runs inside LVGL input dispatch (swipe
     * gesture or the CANCEL button), and this screen carries a live PPG wave
     * monitor. Rebuilding synchronously here frees this screen and the child
     * under the finger from within the event still walking it -- the same
     * reboot the SpO2 measure screen hit. Queue it; the display loop draws it on
     * a clean stack. */
    hpi_load_scr_spl(SCR_BPT, SCROLL_DOWN, 0, 0, 0, 0);
}
