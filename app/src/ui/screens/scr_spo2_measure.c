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

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <lvgl.h>
#include <stdio.h>

#include "hpi_common_types.h"
#include "ui/move_ui.h"
#include <math.h>

LOG_MODULE_REGISTER(hpi_disp_scr_spo2_measure, LOG_LEVEL_DBG);

#define PPG_RAW_WINDOW_SIZE 128

static lv_obj_t *scr_spo2_scr_measure;

// GUI components
static lv_obj_t *chart_ppg;
static lv_chart_series_t *ser_ppg;
// static lv_obj_t *label_hr;
static lv_obj_t *label_spo2_progress;
static lv_obj_t *bar_spo2_progress;
static lv_obj_t *label_spo2_status;
static lv_obj_t *cont_progress;

static float y_max_ppg = 0;
static float y_min_ppg = 10000;

static float gx = 0;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_large_numeric;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_tiny;

extern lv_style_t style_bg_blue;
extern lv_style_t style_bg_red;

static int spo2_source = 0;

void draw_scr_spo2_measure(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    int parent_screen = arg1; // Parent screen passed from the previous screen
    spo2_source = arg2;       // SpO2 source passed from the previous screen

    scr_spo2_scr_measure = lv_obj_create(NULL);
    lv_obj_add_style(scr_spo2_scr_measure, &style_scr_black, 0);
    lv_obj_clear_flag(scr_spo2_scr_measure, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    lv_obj_set_scrollbar_mode(scr_spo2_scr_measure, LV_SCROLLBAR_MODE_OFF);

    /*Create a container with COLUMN flex direction*/
    lv_obj_t *cont_col = lv_obj_create(scr_spo2_scr_measure);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(cont_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(cont_col, &style_scr_black, 0);

    lv_obj_t *label_signal = lv_label_create(cont_col);
    lv_label_set_text(label_signal, "SpO2");

    label_spo2_status = lv_label_create(cont_col);
    lv_label_set_text(label_spo2_status, "--");
    lv_obj_set_style_text_align(label_spo2_status, LV_TEXT_ALIGN_CENTER, 0);

    // Draw countdown timer container
    cont_progress = lv_obj_create(cont_col);
    lv_obj_set_size(cont_progress, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_progress, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_style(cont_progress, &style_scr_black, 0);
    lv_obj_set_flex_align(cont_progress, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    bar_spo2_progress = lv_bar_create(cont_progress);
    lv_obj_set_size(bar_spo2_progress, 200, 20);

    label_spo2_progress = lv_label_create(cont_progress);
    lv_label_set_text(label_spo2_progress, "--");
    lv_obj_set_style_text_align(label_spo2_progress, LV_TEXT_ALIGN_CENTER, 0);

    chart_ppg = lv_chart_create(cont_col);
    lv_obj_set_size(chart_ppg, 390, 140);
    lv_obj_set_style_bg_color(chart_ppg, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(chart_ppg, 0, LV_PART_MAIN);

    // LVGL 9: Chart point styling changed - commented out

    // lv_obj_set_style_width(...);
    // LVGL 9: Chart point styling changed - commented out

    // lv_obj_set_style_height(...);
    lv_obj_set_style_border_width(chart_ppg, 0, LV_PART_MAIN);

    /* Use consistent buffering/point-count choices as raw PPG screen for better visual parity
     * - FI source keeps the wider BPT window
     * - Wrist PPG uses the raw PPG window for snappier updates
     */
    if (spo2_source == SPO2_SOURCE_PPG_FI)
    {
        lv_chart_set_point_count(chart_ppg, BPT_DISP_WINDOW_SIZE * 2);
    }
    else if (spo2_source == SPO2_SOURCE_PPG_WR)
    {
        /* match raw PPG window size for wrist plotting */
        lv_chart_set_point_count(chart_ppg, PPG_RAW_WINDOW_SIZE);
    }

    lv_chart_set_div_line_count(chart_ppg, 0, 0);
    lv_chart_set_update_mode(chart_ppg, LV_CHART_UPDATE_MODE_CIRCULAR);
    lv_obj_align(chart_ppg, LV_ALIGN_CENTER, 0, -35);

    ser_ppg = lv_chart_add_series(chart_ppg, lv_palette_main(LV_PALETTE_ORANGE), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_line_width(chart_ppg, 6, LV_PART_ITEMS);

    lv_obj_t *cont_hr = lv_obj_create(cont_col);
    lv_obj_set_size(cont_hr, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_hr, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(cont_hr, &style_scr_black, 0);
    lv_obj_set_flex_align(cont_hr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    // Draw BPM
    /*lv_obj_t *img_heart = lv_img_create(cont_hr);
    lv_img_set_src(img_heart, &img_heart_35);

    label_hr = lv_label_create(cont_hr);
    lv_label_set_text(label_hr, "00");
    lv_obj_add_style(label_hr, &style_white_medium, 0);
    lv_obj_t *label_hr_sub = lv_label_create(cont_hr);
    lv_label_set_text(label_hr_sub, " bpm");
    */

    hpi_disp_set_curr_screen(SCR_SPL_SPO2_MEASURE);
    hpi_show_screen(scr_spo2_scr_measure, m_scroll_dir);
}

static void hpi_ppg_disp_add_samples(int num_samples)
{
    gx += num_samples;
}

static void hpi_ppg_disp_do_set_scale(int disp_window_size)
{
    hpi_ppg_disp_do_set_scale_shared(chart_ppg, &y_min_ppg, &y_max_ppg, &gx, disp_window_size);
}

void hpi_disp_spo2_update_progress(int progress, enum spo2_meas_state state, int spo2, int hr)
{
    if (label_spo2_progress == NULL)
        return;

    lv_label_set_text_fmt(label_spo2_progress, "%d %%", progress);
    lv_bar_set_value(bar_spo2_progress, progress, LV_ANIM_ON);

    if ((state == SPO2_MEAS_LED_ADJ) || (state == SPO2_MEAS_COMPUTATION))
    {
        lv_label_set_text(label_spo2_status, "Measuring...");
    }
    else if (state == SPO2_MEAS_SUCCESS)
    {
        lv_label_set_text(label_spo2_status, "Complete");
        // hpi_load_scr_spl(SCR_SPL_SPO2_COMPLETE, SCROLL_UP, (uint8_t)SCR_SPO2, spo2, hr, 0);
    }
    else if (state == SPO2_MEAS_TIMEOUT)
    {
        lv_label_set_text(label_spo2_status, "Timed Out");
        hpi_load_scr_spl(SCR_SPL_SPO2_TIMEOUT, SCROLL_UP, (uint8_t)SCR_SPO2, 0, 0, 0);
    }
    else if (state == SPO2_MEAS_UNK)
    {
        lv_label_set_text(label_spo2_status, "Starting...");
    }
}

void hpi_disp_spo2_plot_wrist_ppg(struct hpi_ppg_wr_data_t ppg_sensor_sample)
{
    uint32_t *data_ppg = ppg_sensor_sample.raw_green;

    /* Simple DC removal: EMA baseline and plot residual centered to avoid LVGL coord wrap. */
    static float baseline_ema = 0.0f;
    static bool baseline_init = false;
    const float alpha = 0.005f; /* small alpha for slow baseline tracking */

    /* Cache locals to reduce repeated global accesses */
    int num = ppg_sensor_sample.ppg_num_samples;
    float local_ymin = y_min_ppg;
    float local_ymax = y_max_ppg;
    float local_base = baseline_ema;
    int local_spo2_source = spo2_source;

    for (int i = 0; i < num; i++)
    {
        int32_t scaled = (int32_t)(data_ppg[i] >> 8); /* divide by 256 */

        if (!baseline_init)
        {
            local_base = (float)scaled;
            baseline_init = true;
        }

        float residual = (float)scaled - local_base;
        local_base = local_base * (1.0f - alpha) + ((float)scaled * alpha);

        /* Center residual to positive range for plotting */
        int32_t plot_val = (int32_t)(residual) + 2048; /* center offset */

        float fplot = (float)plot_val;

        if (fplot < local_ymin)
        {
            local_ymin = fplot;
        }

        if (fplot > local_ymax)
        {
            local_ymax = fplot;
        }

        lv_chart_set_next_value(chart_ppg, ser_ppg, plot_val);

        hpi_ppg_disp_add_samples(1);

        /* Use raw PPG window size for wrist plotting autoscale to match raw screen */
        if (local_spo2_source == SPO2_SOURCE_PPG_WR)
        {
            hpi_ppg_disp_do_set_scale(PPG_RAW_WINDOW_SIZE);
        }
        else
        {
            hpi_ppg_disp_do_set_scale(SPO2_DISP_WINDOW_SIZE_FI);
        }
    }

    /* write back cached locals */
    y_min_ppg = local_ymin;
    y_max_ppg = local_ymax;
    baseline_ema = local_base;
}

void hpi_disp_spo2_plot_fi_ppg(struct hpi_ppg_fi_data_t ppg_sensor_sample)
{
    uint32_t *data_ppg = ppg_sensor_sample.raw_ir;

    for (int i = 0; i < ppg_sensor_sample.ppg_num_samples; i++)
    {
        float data_ppg_i = (float)(data_ppg[i] * 1.000); // * 0.100);

        if (data_ppg_i == 0)
        {
            return;
        }

        if (data_ppg_i < y_min_ppg)
        {
            y_min_ppg = data_ppg_i;
        }

        if (data_ppg_i > y_max_ppg)
        {
            y_max_ppg = data_ppg_i;
        }

        lv_chart_set_next_value(chart_ppg, ser_ppg, data_ppg_i);

        hpi_ppg_disp_add_samples(1);
        hpi_ppg_disp_do_set_scale(BPT_DISP_WINDOW_SIZE * 2);
    }
}

void gesture_down_scr_spo2_measure(void)
{
    // Handle gesture down on SpO2 measure screen
    hpi_load_scr_spl(SCR_SPL_SPO2_SELECT, SCROLL_DOWN, 0, 0, 0, 0);
}
