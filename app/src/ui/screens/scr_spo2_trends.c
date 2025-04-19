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
extern lv_style_t style_white_large;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_tiny;

static void draw_event_cb(lv_event_t *e)
{
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (!lv_obj_draw_part_check_type(dsc, &lv_chart_class, LV_CHART_DRAW_PART_TICK_LABEL))
        return;

    if (dsc->id == LV_CHART_AXIS_PRIMARY_X && dsc->text)
    {
        const char *hour[] = {"00", "06", "12", "18", "23"};
        lv_snprintf(dsc->text, dsc->text_length, "%s", hour[dsc->value]);
    }
}

static void scr_spo2_btn_live_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        hpi_move_load_scr_spl(SCR_SPL_RAW_PPG, SCROLL_UP, (uint8_t)SCR_SPO2);
    }
}

void draw_scr_spo2_trends(enum scroll_dir m_scroll_dir)
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
    lv_obj_set_style_size(chart_spo2_trend, 8, LV_PART_INDICATOR);

    lv_obj_add_event_cb(chart_spo2_trend, draw_event_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);
    // lv_obj_align_to(chart_spo2_trend, NULL, LV_ALIGN_CENTER, 15, 40);

    lv_obj_set_style_bg_color(chart_spo2_trend, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(chart_spo2_trend, 0, LV_PART_MAIN);
    lv_chart_set_div_line_count(chart_spo2_trend, 0, 24);

    lv_chart_set_axis_tick(chart_spo2_trend, LV_CHART_AXIS_PRIMARY_X, 10, 5, 5, 5, true, 40);
    lv_chart_set_axis_tick(chart_spo2_trend, LV_CHART_AXIS_PRIMARY_Y, 0, 0, 3, 2, true, 80);

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

            ser_max_trend->y_points[i] = spo2_hourly_trend_points[i].max;
            ser_min_trend->y_points[i] = spo2_hourly_trend_points[i].min;

           // LOG_DBG("SpO2 Point: %d | %d | %d | %d", spo2_hourly_trend_points[i].hour_no, spo2_hourly_trend_points[i].max, spo2_hourly_trend_points[i].min, spo2_hourly_trend_points[i].avg);

            lv_chart_set_range(chart_spo2_trend, LV_CHART_AXIS_PRIMARY_Y, y_min, y_max);
            lv_chart_refresh(chart_spo2_trend);
        }
    } else
    {
        LOG_ERR("No SpO2 data to load");
    }
}*/