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

#include <zephyr/logging/log.h>

#include "hpi_common_types.h"
#include "ui/move_ui.h"
#include "trends.h"

LOG_MODULE_REGISTER(hpi_disp_scr_spo2_scr3, LOG_LEVEL_DBG);

static lv_obj_t *scr_spo2_scr3;

static lv_obj_t *chart_spo2_trend;

#define SPO2_SCR_TREND_MAX_POINTS 24

static lv_chart_series_t *ser_max_trend;
static lv_chart_series_t *ser_min_trend;

// GUI Labels
static lv_obj_t *label_spo2_percent;
static lv_obj_t *label_spo2_last_update_time;
// static lv_obj_t *label_spo2_status;

// static lv_obj_t *label_min_max;
//static lv_obj_t *btn_spo2_settings;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_large_numeric;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_tiny;

static void draw_event_cb(lv_event_t *e)
{
    // Simplified for LVGL 9 - custom tick labels not implemented
    LV_UNUSED(e);
}

static void scr_spo2_btn_live_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        hpi_load_scr_spl(SCR_SPL_RAW_PPG, SCROLL_UP, (uint8_t)SCR_SPO2, 0, 0, 0);
    }
}

void draw_scr_spo2_trends(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_spo2_scr3 = lv_obj_create(NULL);
    lv_obj_add_style(scr_spo2_scr3, &style_scr_black, 0);

    lv_obj_set_scrollbar_mode(scr_spo2_scr3, LV_SCROLLBAR_MODE_ON);

    lv_obj_t *cont_col = lv_obj_create(scr_spo2_scr3);
    lv_obj_set_size(cont_col, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_col, -1, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_top(cont_col, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(cont_col, 1, LV_PART_MAIN);
    lv_obj_add_style(cont_col, &style_scr_black, 0);

    lv_obj_t *label_signal = lv_label_create(cont_col);
    lv_label_set_text(label_signal, "SpO2");

    lv_obj_t *cont_spo2 = lv_obj_create(cont_col);
    lv_obj_set_size(cont_spo2, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_spo2, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(cont_spo2, &style_scr_black, 0);
    lv_obj_set_style_pad_all(cont_spo2, 1, LV_PART_MAIN);
    lv_obj_set_flex_align(cont_spo2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_bottom(cont_spo2, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(cont_spo2, 0, LV_PART_MAIN);

    // lv_obj_align_to(img1, label_spo2_percent, LV_ALIGN_OUT_LEFT_MID, -10, 0);

    label_spo2_percent = lv_label_create(cont_spo2);
    lv_label_set_text(label_spo2_percent, "00 %");
    lv_obj_add_style(label_spo2_percent, &style_white_medium, 0);

    lv_obj_t *cont_spo2_time = lv_obj_create(cont_col);
    lv_obj_set_size(cont_spo2_time, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_spo2_time, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(cont_spo2_time, &style_scr_black, 0);
    lv_obj_set_flex_align(cont_spo2_time, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_bottom(cont_spo2_time, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(cont_spo2_time, 0, LV_PART_MAIN);

    label_spo2_last_update_time = lv_label_create(cont_col);
    lv_label_set_text(label_spo2_last_update_time, "Last updated: 00:00");

    chart_spo2_trend = lv_chart_create(cont_col);
    lv_obj_set_size(chart_spo2_trend, 290, 170);
    lv_chart_set_type(chart_spo2_trend, LV_CHART_TYPE_LINE);
    lv_chart_set_range(chart_spo2_trend, LV_CHART_AXIS_PRIMARY_Y, 50, 100);
    lv_chart_set_point_count(chart_spo2_trend, 24);

    // Hide the lines and show the points
    lv_obj_set_style_line_width(chart_spo2_trend, 0, LV_PART_ITEMS);
    // LVGL 9: Chart point styling changed - commented out

    // lv_obj_set_style_width(...);
    // LVGL 9: Chart point styling changed - commented out

    // lv_obj_set_style_height(...);

    // Removed draw event callback for LVGL 9 compatibility
    // lv_obj_align_to(chart_spo2_trend, NULL, LV_ALIGN_CENTER, 15, 40);

    lv_obj_set_style_bg_color(chart_spo2_trend, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(chart_spo2_trend, 0, LV_PART_MAIN);
    lv_chart_set_div_line_count(chart_spo2_trend, 0, 24);

    // Note: lv_chart_set_axis_tick removed in LVGL 9 - using default axis settings
    // Note: lv_chart_set_axis_tick removed in LVGL 9 - using default axis settings

    ser_max_trend = lv_chart_add_series(chart_spo2_trend, lv_color_hex(0xFFEA00), LV_CHART_AXIS_PRIMARY_Y);
    ser_min_trend = lv_chart_add_series(chart_spo2_trend, lv_color_hex(0x00B0FF), LV_CHART_AXIS_PRIMARY_Y);

    lv_obj_t *lbl_legend = lv_label_create(cont_col);
    lv_label_set_recolor(lbl_legend, true);
    lv_label_set_text(lbl_legend, "#FFEA00 " LV_SYMBOL_STOP " Max.# #00B0FF " LV_SYMBOL_STOP "  Min.# ");
    lv_obj_set_style_pad_top(lbl_legend, 35, LV_PART_MAIN);

    lv_obj_t *lbl_gap1 = lv_label_create(cont_col);
    lv_label_set_text(lbl_gap1, " ");

    /*
    label_min_max = lv_label_create(cont_col);
    lv_label_set_text(label_min_max, "92 - 96 %");
    lv_obj_add_style(label_min_max, &style_white_medium, 0);

    lv_obj_t *lbl_minmax_title = lv_label_create(cont_col);
    lv_label_set_text(lbl_minmax_title, "Hourly SpO2 Range");(*/

    /*lv_obj_t *btn_measure = lv_btn_create(cont_col);
    //lv_obj_set_height(btn_measure, 80);
    lv_obj_add_event_cb(btn_measure, scr_spo2_measure_btn_event_handler, LV_EVENT_ALL, NULL);

    lv_obj_t *label_btn_measure = lv_label_create(btn_measure);
    lv_label_set_text(label_btn_measure, "Measure");
    lv_obj_center(label_btn_measure);

    lv_obj_t *lbl_gap = lv_label_create(cont_col);
    lv_label_set_text(lbl_gap, " ");*/

    /*btn_spo2_settings = lv_btn_create(cont_col);
    lv_obj_add_event_cb(btn_spo2_settings, scr_spo2_settings_btn_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_set_height(btn_spo2_settings, 60);

    lv_obj_t *label_btn_spo2_settings = lv_label_create(btn_spo2_settings);
    lv_label_set_text(label_btn_spo2_settings, LV_SYMBOL_SETTINGS " Settings");
    lv_obj_center(label_btn_spo2_settings);*/

    lv_obj_t *btn_spo2_live = lv_btn_create(cont_col);
    lv_obj_add_event_cb(btn_spo2_live, scr_spo2_btn_live_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_set_height(btn_spo2_live, 80);

    lv_obj_t *label_btn_spo2_live = lv_label_create(btn_spo2_live);
    lv_label_set_text(label_btn_spo2_live, LV_SYMBOL_PLAY " Raw PPG");
    lv_obj_center(label_btn_spo2_live);

    lv_obj_align_to(btn_spo2_live, NULL, LV_ALIGN_CENTER, 0, 130);

    hpi_disp_set_curr_screen(SCR_SPO2);
    hpi_show_screen(scr_spo2_scr3, m_scroll_dir);
}

/*

void hpi_disp_spo2_load_trend(void)
{
    struct hpi_hourly_trend_point_t spo2_hourly_trend_points[SPO2_SCR_TREND_MAX_POINTS];
    struct hpi_minutely_trend_point_t spo2_minutely_trend_points[SPO2_SCR_TREND_MAX_POINTS];
    if (chart_spo2_trend == NULL)
        return;

    int m_num_points = 0;

    //if(0)
    if(hpi_trend_load_trend(spo2_hourly_trend_points, spo2_minutely_trend_points, &m_num_points, TREND_SPO2) == 0)
    {
        int y_max = -1;
        int y_min = 999;

        for (int i = 0; i < SPO2_SCR_TREND_MAX_POINTS; i++)
        {
            if(spo2_hourly_trend_points[i].max > y_max)
            {
                y_max = spo2_hourly_trend_points[i].max;
            }
            if((spo2_hourly_trend_points[i].min < y_min)&&(spo2_hourly_trend_points[i].min != 0))
            {
                y_min = spo2_hourly_trend_points[i].min;
            }

            // TODO: Replace with lv_chart_set_value_by_id(chart_obj, ser_max_trend, i, spo2_hourly_trend_points[i].max);
            // TODO: Replace with lv_chart_set_value_by_id(chart_obj, ser_min_trend, i, spo2_hourly_trend_points[i].min);
            lv_chart_set_value_by_id(chart_spo2_trend, ser_max_trend, i, spo2_hourly_trend_points[i].max);
            lv_chart_set_value_by_id(chart_spo2_trend, ser_min_trend, i, spo2_hourly_trend_points[i].min);

           // LOG_DBG("SpO2 Point: %d | %d | %d | %d", spo2_hourly_trend_points[i].hour_no, spo2_hourly_trend_points[i].max, spo2_hourly_trend_points[i].min, spo2_hourly_trend_points[i].avg);

            lv_chart_set_range(chart_spo2_trend, LV_CHART_AXIS_PRIMARY_Y, y_min, y_max);
            lv_chart_refresh(chart_spo2_trend);
        }
    } else
    {
        LOG_ERR("No SpO2 data to load");
    }
}*/