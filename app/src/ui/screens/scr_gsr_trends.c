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
#include "trends.h"
#include "hpi_sys.h"

LOG_MODULE_REGISTER(hpi_disp_scr_gsr_trends, LOG_LEVEL_DBG);

#define GSR_TRENDS_MAX_POINTS 24

static lv_obj_t *scr_gsr_trends;
static lv_obj_t *chart_gsr_trends;
static lv_chart_series_t *ser_gsr_trends;

// GUI Labels
static lv_obj_t *label_avg_baseline;
static lv_obj_t *label_stress_events;
static lv_obj_t *label_stress_score;
static lv_obj_t *label_last_peak;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_large;
extern lv_style_t style_white_medium;
extern lv_style_t style_white_small;
extern lv_style_t style_scr_black;
extern lv_style_t style_tiny;

static void scr_gsr_btn_live_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        hpi_load_scr_spl(SCR_SPL_GSR_MEASURE, SCROLL_UP, (uint8_t)SCR_GSR, 0, 0, 0);
    }
}

void draw_scr_gsr_trends(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_gsr_trends = lv_obj_create(NULL);
    lv_obj_add_style(scr_gsr_trends, &style_scr_black, 0);
    lv_obj_clear_flag(scr_gsr_trends, LV_OBJ_FLAG_SCROLLABLE);

    // Main container optimized for 390x390 round display
    lv_obj_t *cont_main = lv_obj_create(scr_gsr_trends);
    lv_obj_clear_flag(cont_main, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(cont_main, 370, 370);  // Leave margin for round display
    lv_obj_center(cont_main);
    lv_obj_add_style(cont_main, &style_scr_black, 0);
    lv_obj_set_style_radius(cont_main, 185, 0);  // Circular layout
    lv_obj_set_style_border_width(cont_main, 0, LV_PART_MAIN);  // Hide border
    lv_obj_set_style_pad_all(cont_main, 15, 0);

    // Title at top
    lv_obj_t *label_title = lv_label_create(cont_main);
    lv_label_set_text(label_title, "Stress Trends");
    lv_obj_add_style(label_title, &style_white_medium, 0);
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 10);

    // Trends chart - larger and centered for round display
    chart_gsr_trends = lv_chart_create(cont_main);
    lv_obj_set_size(chart_gsr_trends, 320, 160);  // Optimized for round display
    lv_obj_align(chart_gsr_trends, LV_ALIGN_TOP_MID, 0, 45);
    lv_chart_set_type(chart_gsr_trends, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart_gsr_trends, GSR_TRENDS_MAX_POINTS);
    lv_chart_set_range(chart_gsr_trends, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(chart_gsr_trends, 3, 4);  // Grid for better readability
    lv_obj_set_style_bg_opa(chart_gsr_trends, LV_OPA_10, LV_PART_MAIN);
    lv_obj_set_style_bg_color(chart_gsr_trends, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_border_width(chart_gsr_trends, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(chart_gsr_trends, lv_color_hex(0x404040), LV_PART_MAIN);
    lv_obj_set_style_radius(chart_gsr_trends, 10, LV_PART_MAIN);

    // Amber series for stress trends
    ser_gsr_trends = lv_chart_add_series(chart_gsr_trends, lv_palette_main(LV_PALETTE_AMBER), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_line_width(chart_gsr_trends, 3, LV_PART_ITEMS);

    // Initialize with dummy data (replace with actual trend data)
    lv_chart_set_all_value(chart_gsr_trends, ser_gsr_trends, 30);

    // Statistics container at bottom - using flex layout instead of grid
    lv_obj_t *cont_stats = lv_obj_create(cont_main);
    lv_obj_set_size(cont_stats, 340, 110);
    lv_obj_align(cont_stats, LV_ALIGN_BOTTOM_MID, 0, -50);
    lv_obj_set_style_bg_opa(cont_stats, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_stats, 0, LV_PART_MAIN);  // Hide border
    lv_obj_clear_flag(cont_stats, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cont_stats, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_stats, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Top row of statistics
    lv_obj_t *cont_stats_top = lv_obj_create(cont_stats);
    lv_obj_set_size(cont_stats_top, 320, 40);
    lv_obj_set_style_bg_opa(cont_stats_top, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_stats_top, 0, LV_PART_MAIN);  // Hide border
    lv_obj_clear_flag(cont_stats_top, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cont_stats_top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_stats_top, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Bottom row of statistics
    lv_obj_t *cont_stats_bottom = lv_obj_create(cont_stats);
    lv_obj_set_size(cont_stats_bottom, 320, 40);
    lv_obj_set_style_bg_opa(cont_stats_bottom, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_stats_bottom, 0, LV_PART_MAIN);  // Hide border
    lv_obj_clear_flag(cont_stats_bottom, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cont_stats_bottom, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_stats_bottom, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Peak events (top-left)
    lv_obj_t *cont_events = lv_obj_create(cont_stats_top);
    lv_obj_set_size(cont_events, 150, 35);
    lv_obj_set_style_bg_opa(cont_events, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_events, 0, LV_PART_MAIN);  // Hide border
    lv_obj_clear_flag(cont_events, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cont_events, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_events, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *label_events_title = lv_label_create(cont_events);
    lv_label_set_text(label_events_title, "Peak Events");
    lv_obj_add_style(label_events_title, &style_white_small, 0);

    label_stress_events = lv_label_create(cont_events);
    lv_label_set_text(label_stress_events, "3 today");
    lv_obj_add_style(label_stress_events, &style_white_medium, 0);

    // Average baseline (top-right)
    lv_obj_t *cont_baseline = lv_obj_create(cont_stats_top);
    lv_obj_set_size(cont_baseline, 150, 35);
    lv_obj_set_style_bg_opa(cont_baseline, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_baseline, 0, LV_PART_MAIN);  // Hide border
    lv_obj_clear_flag(cont_baseline, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cont_baseline, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_baseline, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *label_baseline_title = lv_label_create(cont_baseline);
    lv_label_set_text(label_baseline_title, "Avg Baseline");
    lv_obj_add_style(label_baseline_title, &style_white_small, 0);

    label_avg_baseline = lv_label_create(cont_baseline);
    lv_label_set_text(label_avg_baseline, "1.8 µS");
    lv_obj_add_style(label_avg_baseline, &style_white_medium, 0);

    // Stress score (bottom-left)
    lv_obj_t *cont_score = lv_obj_create(cont_stats_bottom);
    lv_obj_set_size(cont_score, 150, 35);
    lv_obj_set_style_bg_opa(cont_score, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_score, 0, LV_PART_MAIN);  // Hide border
    lv_obj_clear_flag(cont_score, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cont_score, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_score, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *label_score_title = lv_label_create(cont_score);
    lv_label_set_text(label_score_title, "Stress Score");
    lv_obj_add_style(label_score_title, &style_white_small, 0);

    label_stress_score = lv_label_create(cont_score);
    lv_label_set_text(label_stress_score, "6/10");
    lv_obj_add_style(label_stress_score, &style_white_medium, 0);

    // Time range indicator (bottom-right)
    lv_obj_t *cont_time = lv_obj_create(cont_stats_bottom);
    lv_obj_set_size(cont_time, 150, 35);
    lv_obj_set_style_bg_opa(cont_time, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_time, 0, LV_PART_MAIN);  // Hide border
    lv_obj_clear_flag(cont_time, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cont_time, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_time, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *label_time_title = lv_label_create(cont_time);
    lv_label_set_text(label_time_title, "Time Range");
    lv_obj_add_style(label_time_title, &style_white_small, 0);

    lv_obj_t *label_time_range = lv_label_create(cont_time);
    lv_label_set_text(label_time_range, "24 Hours");
    lv_obj_add_style(label_time_range, &style_white_medium, 0);

    // Live monitoring button at bottom - compact
    lv_obj_t *btn_gsr_live = lv_btn_create(cont_main);
    lv_obj_add_event_cb(btn_gsr_live, scr_gsr_btn_live_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_set_size(btn_gsr_live, 200, 40);
    lv_obj_align(btn_gsr_live, LV_ALIGN_BOTTOM_MID, 0, -10);

    lv_obj_t *label_btn_gsr_live = lv_label_create(btn_gsr_live);
    lv_label_set_text(label_btn_gsr_live, LV_SYMBOL_PLAY " Live Monitor");
    lv_obj_center(label_btn_gsr_live);
    lv_obj_set_style_text_font(label_btn_gsr_live, &lv_font_montserrat_20, 0);

    hpi_disp_set_curr_screen(SCR_SPL_GSR_TRENDS);
    hpi_show_screen(scr_gsr_trends, m_scroll_dir);
}

void hpi_disp_gsr_trends_update_stats(uint8_t peak_events, float avg_baseline, uint8_t stress_score)
{
    if (label_stress_events != NULL)
    {
        lv_label_set_text_fmt(label_stress_events, "%d today", peak_events);
    }

    if (label_avg_baseline != NULL)
    {
        lv_label_set_text_fmt(label_avg_baseline, "%.1f µS", avg_baseline);
    }

    if (label_stress_score != NULL)
    {
        lv_label_set_text_fmt(label_stress_score, "%d/10", stress_score);
    }
}

void hpi_disp_gsr_trends_add_point(uint8_t stress_value)
{
    if (chart_gsr_trends != NULL && ser_gsr_trends != NULL)
    {
        lv_chart_set_next_value(chart_gsr_trends, ser_gsr_trends, stress_value);
    }
}
