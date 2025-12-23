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

/* Enable verbose plotting debug to trace values. Comment out to reduce log spam. */
#undef SPO2_PLOT_DEBUG

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

/* Progress bar high-water mark to prevent regression */
static int last_progress = 0;

/* Finger PPG baseline tracking - resettable on screen entry */
static float fi_baseline_ema = 0.0f;
static bool fi_baseline_init = false;
static int32_t fi_last_valid_plot_val = 2048;
static int fi_warmup_samples = 0;  // Count samples for warmup period
#define FI_WARMUP_COUNT 50  // Skip first 50 samples (~0.5 sec at 100Hz) to avoid initial junk

/* Wrist PPG baseline tracking - resettable on screen entry */
static float wr_baseline_ema = 0.0f;
static bool wr_baseline_init = false;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_large_numeric;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_tiny;

extern lv_style_t style_bg_blue;
extern lv_style_t style_bg_red;

static int spo2_source = 0;

extern struct k_sem sem_fi_spo2_est_cancel;

void draw_scr_spo2_measure(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    int parent_screen = arg1; // Parent screen passed from the previous screen
    spo2_source = arg2;       // SpO2 source passed from the previous screen

    /* Reset all plotting state on screen entry */
    hpi_ppg_autoscale_reset();
    y_min_ppg = 10000;
    y_max_ppg = 0;
    gx = 0;
    last_progress = 0;

    /* Reset finger PPG baseline */
    fi_baseline_init = false;
    fi_baseline_ema = 0.0f;
    fi_last_valid_plot_val = 2048;
    fi_warmup_samples = 0;

    /* Reset wrist PPG baseline */
    wr_baseline_init = false;
    wr_baseline_ema = 0.0f;

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

    /* Set a sensible default Y range to keep waveform visible until autoscale runs */
    lv_chart_set_range(chart_ppg, LV_CHART_AXIS_PRIMARY_Y, 2048 - 128, 2048 + 128);

    ser_ppg = lv_chart_add_series(chart_ppg, lv_palette_main(LV_PALETTE_ORANGE), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_line_width(chart_ppg, 6, LV_PART_ITEMS);

    /* Initialize chart with baseline value to show a flat line instead of junk
     * during the warmup period. The value 2048 matches the DC offset used in plotting. */
    lv_chart_set_all_value(chart_ppg, ser_ppg, 2048);

    lv_obj_t *cont_hr = lv_obj_create(cont_col);
    lv_obj_set_size(cont_hr, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_hr, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(cont_hr, &style_scr_black, 0);
    lv_obj_set_flex_align(cont_hr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    // Draw BPM
    /*lv_obj_t *img_heart = lv_img_create(cont_hr);
    lv_img_set_src(img_heart, &img_heart_48px);

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

    /* Clamp progress to valid 0-100 range.
     * The sensor may report garbage values (e.g., 178) during initialization
     * or algorithm warmup. Clamping ensures the UI always shows valid progress. */
    if (progress < 0)
    {
        progress = 0;
    }
    else if (progress > 100)
    {
        progress = 100;
    }

    /* High-water mark protection: only allow progress to increase, never decrease.
     * This prevents the progress bar from jumping back to 0 when the sensor
     * temporarily reports lower values due to algorithm resets or motion. */
    if (state == SPO2_MEAS_SUCCESS || state == SPO2_MEAS_TIMEOUT)
    {
        /* Measurement complete or failed - reset high-water mark for next measurement */
        last_progress = 0;
    }
    else
    {
        /* During active measurement, only allow progress to increase */
        if (progress < last_progress)
        {
            progress = last_progress; /* Don't allow regression */
        }
        else
        {
            last_progress = progress;
        }
    }

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
    const float alpha = 0.005f; /* small alpha for slow baseline tracking */

    /* Cache locals to reduce repeated global accesses */
    int num = ppg_sensor_sample.ppg_num_samples;
    float local_ymin = y_min_ppg;
    float local_ymax = y_max_ppg;
    float local_base = wr_baseline_ema;
    int local_spo2_source = spo2_source;

    for (int i = 0; i < num; i++)
    {
        /* Driver now provides normalized samples; use value directly. */
        int32_t scaled = (int32_t)(data_ppg[i]);

        if (!wr_baseline_init)
        {
            local_base = (float)scaled;
            wr_baseline_init = true;
        }

        float residual = (float)scaled - local_base;
        /* Increased amplification from 2x to 8x for better visibility of small signals */
        residual *= 8.0f;
        local_base = local_base * (1.0f - alpha) + ((float)scaled * alpha);

        /* Center residual to positive range for plotting */
        int32_t plot_val = (int32_t)(residual) + 2048; /* center offset */

        float fplot = (float)plot_val;

        /* Update local extrema then write sample to chart so autoscale sees newest values */
        if (fplot < local_ymin) local_ymin = fplot;
        if (fplot > local_ymax) local_ymax = fplot;

        lv_chart_set_next_value(chart_ppg, ser_ppg, plot_val);

        /* Commit extrema to globals used by the shared autoscale helper */
        y_min_ppg = local_ymin;
        y_max_ppg = local_ymax;

        /* Advance sample counter used by autoscaler and call helper */
        hpi_ppg_disp_add_samples(1);

        if (local_spo2_source == SPO2_SOURCE_PPG_WR) {
            hpi_ppg_disp_do_set_scale(PPG_RAW_WINDOW_SIZE);
        } else {
            hpi_ppg_disp_do_set_scale(SPO2_DISP_WINDOW_SIZE_FI);
        }
    }

    /* write back cached locals */
    y_min_ppg = local_ymin;
    y_max_ppg = local_ymax;
    wr_baseline_ema = local_base;
}

void hpi_disp_spo2_plot_fi_ppg(struct hpi_ppg_fi_data_t ppg_sensor_sample)
{
    uint32_t *data_ppg = ppg_sensor_sample.raw_ir;

    /* Simple DC removal for FI source similar to wrist plotting to reduce baseline wander */
    const float alpha_fi = 0.01f; /* slightly faster baseline tracking for finger */

    for (int i = 0; i < ppg_sensor_sample.ppg_num_samples; i++)
    {
        float data_ppg_i = (float)(data_ppg[i]);

        /* Guard against zero/invalid samples from driver - use last valid value instead of skipping
         * to prevent discontinuities in the waveform display */
        if (data_ppg_i == 0.0f)
        {
            /* During warmup, just count but don't plot */
            if (fi_warmup_samples < FI_WARMUP_COUNT)
            {
                fi_warmup_samples++;
                continue;
            }
            /* Plot last valid value to maintain waveform continuity */
            lv_chart_set_next_value(chart_ppg, ser_ppg, fi_last_valid_plot_val);
            hpi_ppg_disp_add_samples(1);
            hpi_ppg_disp_do_set_scale(BPT_DISP_WINDOW_SIZE * 2);
            continue;
        }

        /* Warmup period: collect samples to build baseline but don't plot yet.
         * This avoids showing initial junk data on screen. */
        if (fi_warmup_samples < FI_WARMUP_COUNT)
        {
            fi_warmup_samples++;
            /* Build baseline during warmup using faster alpha for quicker convergence */
            if (!fi_baseline_init)
            {
                fi_baseline_ema = data_ppg_i;
                fi_baseline_init = true;
            }
            else
            {
                /* Use faster alpha (0.1) during warmup for quick baseline lock */
                fi_baseline_ema = fi_baseline_ema * 0.9f + (data_ppg_i * 0.1f);
            }
            continue;  /* Skip plotting during warmup */
        }

        if (!fi_baseline_init)
        {
            fi_baseline_ema = data_ppg_i;
            fi_baseline_init = true;
        }

        float residual = data_ppg_i - fi_baseline_ema;
        fi_baseline_ema = fi_baseline_ema * (1.0f - alpha_fi) + (data_ppg_i * alpha_fi);

        /* Center residual to positive range for LVGL plotting */
        int32_t plot_val = (int32_t)(residual) + 2048;

        /* Store as last valid value for continuity on invalid samples */
        fi_last_valid_plot_val = plot_val;

        if ((float)plot_val < y_min_ppg)
        {
            y_min_ppg = (float)plot_val;
        }

        if ((float)plot_val > y_max_ppg)
        {
            y_max_ppg = (float)plot_val;
        }

        lv_chart_set_next_value(chart_ppg, ser_ppg, plot_val);

        hpi_ppg_disp_add_samples(1);
        hpi_ppg_disp_do_set_scale(BPT_DISP_WINDOW_SIZE * 2);
    }
}

extern struct k_sem sem_spo2_cancel;

void gesture_down_scr_spo2_measure(void)
{
    // Signal cancellation to the appropriate state machine based on source
    if (spo2_source == SPO2_SOURCE_PPG_FI) {
        k_sem_give(&sem_fi_spo2_est_cancel);
    } else if (spo2_source == SPO2_SOURCE_PPG_WR) {
        k_sem_give(&sem_spo2_cancel);
    }

    // Navigate back to main SpO2 screen (simplified flow with Option B)
    hpi_load_screen(SCR_SPO2, SCROLL_DOWN);
}