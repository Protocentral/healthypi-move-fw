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
static lv_obj_t *label_scd_state;
static lv_obj_t *cont_scd_status;

static float y_max_ppg = 0;
static float y_min_ppg = 10000;
static float gx = 0;

/* Wrist PPG baseline tracking - resettable on screen entry */
static float wr_baseline_ema = 0.0f;
static bool wr_baseline_init = false;

/* Progress bar high-water mark to prevent regression */
static int last_progress = 0;

/* Finger PPG baseline tracking - resettable on screen entry */
static float fi_baseline_ema = 0.0f;
static bool fi_baseline_init = false;
static int32_t fi_last_valid_plot_val = 2048;
static int fi_warmup_samples = 0;  // Count samples for warmup period
#define FI_WARMUP_COUNT 50  // Skip first 50 samples (~0.5 sec at 100Hz) to avoid initial junk

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_large_numeric;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_tiny;

extern lv_style_t style_bg_blue;
extern lv_style_t style_bg_red;

int current_spo2_source = 0;
extern int scd_state;
extern int perfusion_state;
extern struct k_sem sem_fi_spo2_est_cancel;

void draw_scr_spo2_measure(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    int parent_screen = arg1; // Parent screen passed from the previous screen
    current_spo2_source = arg2;       // SpO2 source passed from the previous screen
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
    if (current_spo2_source == SPO2_SOURCE_PPG_FI)
    {
        lv_chart_set_point_count(chart_ppg, BPT_DISP_WINDOW_SIZE * 2);
    }
    else if (current_spo2_source == SPO2_SOURCE_PPG_WR)
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

    if(!current_spo2_source)
    {
        /* SCD status container */
        cont_scd_status = lv_obj_create(cont_col);
        lv_obj_set_size(cont_scd_status, 160, 42);
        lv_obj_set_style_radius(cont_scd_status, 20, 0);
        lv_obj_set_style_border_width(cont_scd_status, 0, 0);
        lv_obj_set_style_pad_all(cont_scd_status, 6, 0);
        lv_obj_set_style_bg_color(cont_scd_status,lv_palette_main(LV_PALETTE_GREY),0);
        lv_obj_clear_flag(cont_scd_status, LV_OBJ_FLAG_SCROLLABLE);

        /* label inside container */
        label_scd_state = lv_label_create(cont_scd_status);
        lv_label_set_text(label_scd_state, "--");
        lv_obj_center(label_scd_state);
        lv_obj_set_style_text_color(label_scd_state,lv_color_white(),0);
    }
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
/* Plot raw PPG + shared autoscale (same path as the Raw PPG screen). */
static void spo2_plot_raw(uint32_t *data, int num, int window)
{
    if (chart_ppg == NULL || num <= 0)
        return;

    uint32_t batch_min = UINT32_MAX, batch_max = 0;
    for (int i = 0; i < num; i++) {
        if (data[i] < batch_min) batch_min = data[i];
        if (data[i] > batch_max) batch_max = data[i];
    }
    if (y_min_ppg == 10000) y_min_ppg = batch_min;
    else if (batch_min < y_min_ppg) y_min_ppg = batch_min;
    if (y_max_ppg == 0) y_max_ppg = batch_max;
    else if (batch_max > y_max_ppg) y_max_ppg = batch_max;

    for (int i = 0; i < num; i++) {
        lv_chart_set_next_value(chart_ppg, ser_ppg, data[i]);
        gx += 1;
    }
    hpi_ppg_disp_do_set_scale(window);
}

void hpi_disp_spo2_plot_wrist_ppg(struct hpi_ppg_wr_data_t ppg_sensor_sample)
{
    spo2_plot_raw(ppg_sensor_sample.raw_ir, ppg_sensor_sample.ppg_num_samples, PPG_RAW_WINDOW_SIZE);
}
void hpi_disp_spo2_plot_fi_ppg(struct hpi_ppg_fi_data_t ppg_sensor_sample)
{
    spo2_plot_raw(ppg_sensor_sample.raw_ir, ppg_sensor_sample.ppg_num_samples, BPT_DISP_WINDOW_SIZE);
}

extern struct k_sem sem_spo2_cancel;

void gesture_down_scr_spo2_measure(void)
{
    // Signal cancellation to the appropriate state machine based on source
    if (current_spo2_source == SPO2_SOURCE_PPG_FI) {
        k_sem_give(&sem_fi_spo2_est_cancel);
        LOG_INF("Spo2 measurement cancelled via gesture (finger PPG)");
    } else if (current_spo2_source == SPO2_SOURCE_PPG_WR) {
        k_sem_give(&sem_spo2_cancel);
    }

    // Navigate back to main SpO2 screen (simplified flow with Option B)
    hpi_load_screen(SCR_SPO2, SCROLL_DOWN);
}
void update_scd_label_cb(void *arg)
{
    if (label_scd_state == NULL)
        return;

    switch (scd_state)
    {
        case 0:
            lv_label_set_text(label_scd_state, "UNDETECTED");
            lv_obj_set_style_bg_color(cont_scd_status,lv_palette_main(LV_PALETTE_GREY),0);
            break;
        case 1:
            lv_label_set_text(label_scd_state, "NO SKIN");
            lv_obj_set_style_bg_color(cont_scd_status,lv_palette_main(LV_PALETTE_RED),0);
            break;
        case 2:
            lv_label_set_text(label_scd_state, "INVALID");
            lv_obj_set_style_bg_color(cont_scd_status,lv_palette_main(LV_PALETTE_BLUE),0);
            break;
        case 3:
            if(perfusion_state) 
            {
                lv_label_set_text(label_scd_state, "HOLD");
                lv_obj_set_style_bg_color(cont_scd_status,lv_palette_main(LV_PALETTE_ORANGE),0);
            }
            else
            {
                lv_label_set_text(label_scd_state, "STABLE");
                lv_obj_set_style_bg_color(cont_scd_status,lv_palette_main(LV_PALETTE_GREEN),0);
            }
            break;
        default:
            lv_label_set_text(label_scd_state, "UNKNOWN");
            lv_obj_set_style_bg_color(cont_scd_status,lv_palette_main(LV_PALETTE_GREY),0);
            break;
    }
}
void update_perfusion_label_cb(void *arg)
{
    if (label_scd_state == NULL)
        return;

    if(scd_state == 3) {
        if(perfusion_state) 
        {
            lv_label_set_text(label_scd_state, "HOLD");
            lv_obj_set_style_bg_color(cont_scd_status,lv_palette_main(LV_PALETTE_ORANGE),0);
        }
        else
        {
            lv_label_set_text(label_scd_state, "STABLE");
            lv_obj_set_style_bg_color(cont_scd_status,lv_palette_main(LV_PALETTE_GREEN),0);
        }
    }
}