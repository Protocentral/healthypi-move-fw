#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <lvgl.h>
#include <stdio.h>

#include <time.h>

#include <zephyr/logging/log.h>

#include "hpi_common_types.h"
#include "ui/move_ui.h"
#include "trends.h"
#include "hpi_user_settings_api.h"

LOG_MODULE_REGISTER(hpi_disp_scr_hr_scr2, LOG_LEVEL_DBG);

#define HR_SCR_TREND_MAX_POINTS 24

lv_obj_t *scr_hr_scr2;

static lv_obj_t *chart_hr_hour_trend;
static lv_obj_t *chart_hr_day_trend;

static lv_chart_series_t *ser_hr_hour_trend;

static lv_chart_series_t *ser_hr_max_trend;
static lv_chart_series_t *ser_hr_min_trend;



// GUI Labels
static lv_obj_t *label_hr_bpm;
static lv_obj_t *label_hr_min_max;
static lv_obj_t *btn_hr_settings;

static lv_obj_t *label_hr_last_update_time;
static lv_obj_t *label_hr_previous_hr;
// Externs
extern lv_style_t style_scr_container;
extern lv_style_t style_red_medium;
extern lv_style_t style_white_large;
extern lv_style_t style_white_medium;
extern lv_style_t style_white_small;

extern lv_style_t style_scr_black;
extern lv_style_t style_bg_red;
extern lv_style_t style_bg_blue;

extern lv_style_t style_tiny;

static void draw_event_cb_day(lv_event_t *e)
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

static void draw_event_cb_hour(lv_event_t *e)
{
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (!lv_obj_draw_part_check_type(dsc, &lv_chart_class, LV_CHART_DRAW_PART_TICK_LABEL))
        return;

    if (dsc->id == LV_CHART_AXIS_PRIMARY_X && dsc->text)
    {
        const char *hour[] = {"00", "15", "30", "45", "60"};
        lv_snprintf(dsc->text, dsc->text_length, "%s", hour[dsc->value]);
    }
}

static void scr_hr_btn_live_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        hpi_load_scr_spl(SCR_SPL_RAW_PPG, SCROLL_UP, (uint8_t)SCR_HR, 0, 0, 0);
    }
}

void draw_scr_hr_scr2(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_hr_scr2 = lv_obj_create(NULL);
    // lv_obj_set_flag(scr_hr, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    draw_scr_common(scr_hr_scr2);

    static lv_point_t line_points[] = { {10, 0}, {240, 0}};

    lv_obj_set_scrollbar_mode(scr_hr_scr2, LV_SCROLLBAR_MODE_ON);

    /*Create a container with COLUMN flex direction*/
    lv_obj_t *cont_col = lv_obj_create(scr_hr_scr2);
    lv_obj_set_size(cont_col, lv_pct(100), LV_SIZE_CONTENT);
    // lv_obj_set_width(cont_col, lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_col, -1, LV_PART_SCROLLBAR);
    //lv_obj_set_style_pad_top(cont_col, 5, LV_PART_MAIN);
    //lv_obj_set_style_pad_bottom(cont_col, 1, LV_PART_MAIN);
    lv_obj_add_style(cont_col, &style_scr_black, 0);
    
    lv_obj_t *label_signal = lv_label_create(cont_col);
    lv_label_set_text(label_signal, "Heart Rate");
    lv_obj_add_style(label_signal, &style_white_small, 0);

    // Draw a horizontal line
    /*lv_obj_t * line1 = lv_line_create(cont_col);
    lv_line_set_points(line1, line_points, 2);*/     
       
    lv_obj_t *lbl_l1 = lv_label_create(cont_col);
    lv_label_set_text(lbl_l1, "Last hour trend");
    lv_obj_add_style(lbl_l1, &style_white_small, 0);

    chart_hr_hour_trend = lv_chart_create(cont_col);
    lv_obj_set_size(chart_hr_hour_trend, 270, 110);
    lv_chart_set_type(chart_hr_hour_trend, LV_CHART_TYPE_LINE);
    lv_chart_set_update_mode(chart_hr_hour_trend, LV_CHART_UPDATE_MODE_CIRCULAR);
    lv_chart_set_range(chart_hr_hour_trend, LV_CHART_AXIS_PRIMARY_Y, 30, 150);
    lv_chart_set_point_count(chart_hr_hour_trend, 60);

    lv_obj_set_style_line_width(chart_hr_hour_trend, 0, LV_PART_ITEMS);
    lv_obj_set_style_size(chart_hr_hour_trend, 6, LV_PART_INDICATOR);

    lv_obj_add_event_cb(chart_hr_hour_trend, draw_event_cb_hour, LV_EVENT_DRAW_PART_BEGIN, NULL);

    lv_obj_set_style_bg_color(chart_hr_hour_trend, lv_color_black(), LV_STATE_DEFAULT);
    //lv_obj_set_style_bg_opa(chart_hr_hour_trend, 0, LV_PART_MAIN);

    lv_obj_set_style_border_width(chart_hr_hour_trend, 2, LV_PART_MAIN);
    lv_chart_set_div_line_count(chart_hr_hour_trend, 0, 0);

    lv_chart_set_axis_tick(chart_hr_hour_trend, LV_CHART_AXIS_PRIMARY_X, 10, 5, 5, 2, true, 40);
    lv_chart_set_axis_tick(chart_hr_hour_trend, LV_CHART_AXIS_PRIMARY_Y, 0, 0, 3, 2, true, 10);

    ser_hr_hour_trend = lv_chart_add_series(chart_hr_hour_trend, lv_color_hex(0xFFEA00), LV_CHART_AXIS_PRIMARY_Y);

    lv_obj_t *lbl_l3 = lv_label_create(cont_col);
    lv_label_set_text(lbl_l3,  " \n");

    lv_obj_t * line2 = lv_line_create(cont_col);
    lv_line_set_points(line2, line_points, 2);     
    
    lv_obj_t *lbl_l2 = lv_label_create(cont_col);
    lv_label_set_text(lbl_l2, "Last day trend");

    chart_hr_day_trend = lv_chart_create(cont_col);
    lv_obj_set_size(chart_hr_day_trend, 270, 140);
    lv_chart_set_type(chart_hr_day_trend, LV_CHART_TYPE_LINE);
    lv_chart_set_range(chart_hr_day_trend, LV_CHART_AXIS_PRIMARY_Y, 30, 150);
    lv_chart_set_point_count(chart_hr_day_trend, 60);

    lv_obj_set_style_line_width(chart_hr_day_trend, 0, LV_PART_ITEMS);
    lv_obj_set_style_size(chart_hr_day_trend, 8, LV_PART_INDICATOR);

    lv_obj_add_event_cb(chart_hr_day_trend, draw_event_cb_day, LV_EVENT_DRAW_PART_BEGIN, NULL);

    lv_obj_set_style_bg_color(chart_hr_day_trend, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(chart_hr_day_trend, 0, LV_PART_MAIN);

    lv_obj_set_style_border_width(chart_hr_day_trend, 2, LV_PART_MAIN);
    lv_chart_set_div_line_count(chart_hr_day_trend, 0, 24);

    lv_chart_set_axis_tick(chart_hr_day_trend, LV_CHART_AXIS_PRIMARY_X, 10, 5, 5, 5, true, 40);
    lv_chart_set_axis_tick(chart_hr_day_trend, LV_CHART_AXIS_PRIMARY_Y, 0, 0, 3, 2, true, 10);

    ser_hr_max_trend = lv_chart_add_series(chart_hr_day_trend, lv_color_hex(0xFFEA00), LV_CHART_AXIS_PRIMARY_Y);
    ser_hr_min_trend = lv_chart_add_series(chart_hr_day_trend, lv_color_hex(0x00B0FF), LV_CHART_AXIS_PRIMARY_Y);

    lv_obj_t *lbl_legend = lv_label_create(cont_col);
    lv_label_set_recolor(lbl_legend, true);
    lv_label_set_text(lbl_legend, "#FFEA00 " LV_SYMBOL_STOP " Max.# #00B0FF  " LV_SYMBOL_STOP " Min.# ");
    lv_obj_set_style_pad_top(lbl_legend, 35, LV_PART_MAIN);

    lv_obj_t *lbl_gap1 = lv_label_create(cont_col);
    lv_label_set_text(lbl_gap1, " ");
    lv_obj_align_to(lbl_gap1, btn_hr_settings, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);

    lv_obj_t *btn_hr_live = lv_btn_create(cont_col);
    lv_obj_add_event_cb(btn_hr_live, scr_hr_btn_live_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_set_height(btn_hr_live, 80);

    lv_obj_t *label_btn_bpt_measure1 = lv_label_create(btn_hr_live);
    lv_label_set_text(label_btn_bpt_measure1, LV_SYMBOL_PLAY " Raw PPG");
    lv_obj_center(label_btn_bpt_measure1);

    /*btn_hr_settings = lv_btn_create(cont_col);
    lv_obj_add_event_cb(btn_hr_settings, scr_ecg_start_btn_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_set_height(btn_hr_settings, 80);

    lv_obj_t *lbl_btn_settings = lv_label_create(btn_hr_settings);
    lv_label_set_text(lbl_btn_settings, LV_SYMBOL_SETTINGS " Settings");
    lv_obj_center(lbl_btn_settings);*/

    hpi_disp_set_curr_screen(SCR_SPL_HR_SCR2);
    hpi_show_screen(scr_hr_scr2, m_scroll_dir);
}

void hpi_disp_hr_load_trend(void)
{
    struct hpi_hourly_trend_point_t hr_hourly_trend_points[HR_SCR_TREND_MAX_POINTS];
    struct hpi_minutely_trend_point_t hr_minutely_trend_points[60];

    for(int i=0; i<60; i++)
    {
        hr_minutely_trend_points[i].avg = 0;
    }

    int m_num_points = 0;

    if (hpi_trend_load_trend(hr_hourly_trend_points, hr_minutely_trend_points, &m_num_points, TREND_HR) == 0)
    {
        if (chart_hr_day_trend == NULL)
        {
            return;
        }
        int y_max = -1;
        int y_min = 999;

        for (int i = 0; i < 24; i++)
        {
            // LOG_DBG("HR Point: %" PRIx64 "| %d | %d | %d", hr_trend_points[i].timestamp, hr_trend_points[i].hr_max, hr_trend_points[i].hr_min, hr_trend_points[i].hr_avg);
            ser_hr_max_trend->y_points[i] = hr_hourly_trend_points[i].max;
            ser_hr_min_trend->y_points[i] = hr_hourly_trend_points[i].min;

            if (hr_hourly_trend_points[i].max > y_max)
            {
                y_max = hr_hourly_trend_points[i].max;
            }
            if ((hr_hourly_trend_points[i].min < y_min) && (hr_hourly_trend_points[i].min != 0))
            {
                y_min = hr_hourly_trend_points[i].min;
            }
        }

        for(int i=0; i<60; i++)
        {
            ser_hr_hour_trend->y_points[i] = hr_minutely_trend_points[i].max;
        }

        //lv_chart_set_range(chart_hr_day_trend, LV_CHART_AXIS_PRIMARY_Y, y_min, y_max);
        lv_chart_refresh(chart_hr_hour_trend);
        lv_chart_refresh(chart_hr_day_trend);
    }
    else
    {
        LOG_ERR("No data to load");
    }
}