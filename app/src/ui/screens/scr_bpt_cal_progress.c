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
 * BP calibration-progress screen — v2. The design handoff punts a dedicated cal
 * screen and says "reuse the measuring layout with a CALIBRATING status", so
 * this shares the measure chrome (title row / waveform / ring+HR pill / CANCEL
 * from scr_bpt_measure.c) and adds a POINT n/3 line. A full calibration is three
 * points; the index comes from the finger SMF (hpi_bpt_cal_status). Fed by the
 * display SMF via hpi_disp_bpt_cal_draw_plotPPG / _update_progress.
 */

#include <lvgl.h>
#include "hpi_evt.h"
#include <stdio.h>
#include <zephyr/logging/log.h>

#include "hpi_common_types.h"
#include "hw_module.h"
#include "ui/move_ui.h"
#include "ui/hpi_r0_theme.h"

LOG_MODULE_REGISTER(scr_bpt_cal_progress, LOG_LEVEL_DBG);

lv_obj_t *scr_bpt_cal_progress;

#define BPT_CAL_WAVE_W   296
#define BPT_CAL_WAVE_H    74
#define BPT_CAL_WAVE_WIN 300
#define BPT_CAL_POINTS     3

static lv_obj_t *cal_wave;
static lv_obj_t *cal_ring;
static lv_obj_t *cal_ring_pct;
static lv_obj_t *cal_hr;
static lv_obj_t *cal_point_lbl;

static void bpt_cal_del(lv_event_t *e)
{
    ARG_UNUSED(e);
    cal_wave = NULL;
    cal_ring = NULL;
    cal_ring_pct = NULL;
    cal_hr = NULL;
    cal_point_lbl = NULL;
}

static void cal_cancel_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    gesture_down_scr_bpt_cal_progress();
}

void draw_scr_bpt_cal_progress(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_bpt_cal_progress = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_bpt_cal_progress, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_bpt_cal_progress, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr_bpt_cal_progress, bpt_cal_del, LV_EVENT_DELETE, NULL);

    /* header: pulsing dot + BLOOD PRESSURE */
    hpi_bpt_make_title_row(scr_bpt_cal_progress, "BLOOD PRESSURE");

    /* status line — CALIBRATING (blue) */
    lv_obj_t *status = lv_label_create(scr_bpt_cal_progress);
    lv_label_set_text(status, "CALIBRATING \xC2\xB7 KEEP STILL");
    lv_obj_align(status, LV_ALIGN_CENTER, 0, -100);
    lv_obj_set_style_text_font(status, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(status, lv_color_hex(V2_BP), 0);
    lv_obj_set_style_text_letter_space(status, 1, 0);

    /* which of the three calibration points */
    cal_point_lbl = lv_label_create(scr_bpt_cal_progress);
    lv_label_set_text(cal_point_lbl, "POINT 1 / 3");
    lv_obj_align(cal_point_lbl, LV_ALIGN_CENTER, 0, -74);
    lv_obj_set_style_text_font(cal_point_lbl, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(cal_point_lbl, lv_color_hex(V2_MUTED2), 0);
    lv_obj_set_style_text_letter_space(cal_point_lbl, 2, 0);

    /* live PPG trace */
    cal_wave = hpi_wave_monitor_create(scr_bpt_cal_progress, BPT_CAL_WAVE_W, BPT_CAL_WAVE_H,
                                       lv_color_hex(V2_BP));
    lv_obj_align(cal_wave, LV_ALIGN_CENTER, 0, -20);
    hpi_wave_monitor_set_window(cal_wave, BPT_CAL_WAVE_WIN);

    /* status pill: ring + heart + HR + BPM */
    lv_obj_t *pill = hpi_bpt_make_status_pill(scr_bpt_cal_progress, &cal_ring,
                                              &cal_ring_pct, &cal_hr);
    lv_obj_align(pill, LV_ALIGN_CENTER, 0, 62);

    hpi_bpt_make_cancel(scr_bpt_cal_progress, cal_cancel_cb);

    hpi_disp_set_curr_screen(SCR_SPL_BPT_CAL_PROGRESS);
    hpi_show_screen(scr_bpt_cal_progress, m_scroll_dir);
}

void hpi_disp_bpt_cal_draw_plotPPG(struct hpi_ppg_fi_data_t ppg_sensor_sample)
{
    if (cal_wave == NULL || cal_hr == NULL) {
        return;
    }

    uint32_t *data_ppg = ppg_sensor_sample.raw_red;
    uint16_t n_sample = ppg_sensor_sample.ppg_num_samples;

    for (int i = 0; i < n_sample; i++) {
        hpi_wave_monitor_push_auto(cal_wave, (int32_t)data_ppg[i]);
    }

    if (ppg_sensor_sample.hr > 0) {
        lv_label_set_text_fmt(cal_hr, "%d", ppg_sensor_sample.hr);
    } else {
        lv_label_set_text(cal_hr, "--");
    }
}

void hpi_disp_bpt_cal_update_progress(int point_idx, int progress)
{
    if (cal_ring == NULL || cal_ring_pct == NULL) {
        return;
    }
    if (progress < 0) {
        progress = 0;
    } else if (progress > 100) {
        progress = 100;
    }
    lv_arc_set_value(cal_ring, progress);
    lv_label_set_text_fmt(cal_ring_pct, "%d", progress);

    if (cal_point_lbl != NULL) {
        int p = point_idx + 1;   /* 0-based index -> 1-based label */
        if (p < 1) {
            p = 1;
        } else if (p > BPT_CAL_POINTS) {
            p = BPT_CAL_POINTS;
        }
        lv_label_set_text_fmt(cal_point_lbl, "POINT %d / %d", p, BPT_CAL_POINTS);
    }
}

void gesture_down_scr_bpt_cal_progress(void)
{
    LOG_INF("Cancel BPT calibration (swipe/CANCEL) - posting EVT_FI_BPT_CAL_CANCEL");
    k_event_post(&fi_evt, EVT_FI_BPT_CAL_CANCEL);
}
