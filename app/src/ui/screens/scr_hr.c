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

LOG_MODULE_REGISTER(hpi_disp_scr_hr, LOG_LEVEL_DBG);

#define HR_SCR_TREND_MAX_POINTS 24

lv_obj_t *scr_hr;

static lv_obj_t *chart_hr_trend;

static lv_chart_series_t *ser_hr_max_trend;
static lv_chart_series_t *ser_hr_min_trend;

static uint16_t hr_trend_max[HR_SCR_TREND_MAX_POINTS] = {0};
static uint16_t hr_trend_min[HR_SCR_TREND_MAX_POINTS] = {0};

// static lv_chart_series_t *ser_hr_trend;
// static lv_chart_series_t *ser_hr_max_trend;
// static lv_chart_series_t *ser_hr_min_trend;

// GUI Labels
static lv_obj_t *label_hr_bpm;
static lv_obj_t *label_hr_min_max;
static lv_obj_t *btn_hr_settings;

static lv_obj_t *label_hr_last_update_time;

// Externs
extern lv_style_t style_scr_container;
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

static void scr_ecg_start_btn_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
    }
}

static void scr_hr_btn_live_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        hpi_move_load_scr_spl(SCR_SPL_PLOT_PPG, SCROLL_UP, (uint8_t)SCR_HR);
    }
}

static void hpi_set_chart_points(lv_chart_series_t *ser, uint16_t *points, int num_points)
{
    for (int i = 0; i < num_points; i++)
    {
        ser->y_points[i] = points[i];
    }
}

void draw_scr_hr(enum scroll_dir m_scroll_dir)
{
    scr_hr = lv_obj_create(NULL);
    // lv_obj_set_flag(scr_hr, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    draw_scr_common(scr_hr);

    lv_obj_set_scrollbar_mode(scr_hr, LV_SCROLLBAR_MODE_ON);

    /*Create a container with COLUMN flex direction*/
    lv_obj_t *cont_col = lv_obj_create(scr_hr);
    lv_obj_set_size(cont_col, lv_pct(100), LV_SIZE_CONTENT);
    // lv_obj_set_width(cont_col, lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_col, -1, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_top(cont_col, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(cont_col, 1, LV_PART_MAIN);
    lv_obj_add_style(cont_col, &style_scr_black, 0);

    lv_obj_t *label_signal = lv_label_create(cont_col);
    lv_label_set_text(label_signal, "Heart Rate");

    lv_obj_t *cont_hr = lv_obj_create(cont_col);
    lv_obj_set_size(cont_hr, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_hr, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(cont_hr, &style_scr_black, 0);
    lv_obj_set_flex_align(cont_hr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_bottom(cont_hr, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(cont_hr, 0, LV_PART_MAIN);

    lv_obj_t *img1 = lv_img_create(cont_hr);
    lv_img_set_src(img1, &img_heart_35);
    // lv_obj_align_to(img1, label_hr_bpm, LV_ALIGN_OUT_LEFT_MID, -10, 0);

    label_hr_bpm = lv_label_create(cont_hr);
    lv_label_set_text(label_hr_bpm, "00");
    lv_obj_add_style(label_hr_bpm, &style_white_medium, 0);
    // lv_obj_align_to(label_hr_bpm, NULL, LV_ALIGN_TOP_MID, 0, 90);

    // HR Sub bpm label
    lv_obj_t *label_hr_sub = lv_label_create(cont_hr);
    lv_label_set_text(label_hr_sub, " bpm");
    // lv_obj_align_to(label_hr_sub, label_hr_bpm, LV_ALIGN_OUT_RIGHT_MID, 0, 0);

    lv_obj_t *cont_hr_time = lv_obj_create(cont_col);
    lv_obj_set_size(cont_hr_time, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_hr_time, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(cont_hr_time, &style_scr_black, 0);
    lv_obj_set_flex_align(cont_hr_time, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_bottom(cont_hr_time, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(cont_hr_time, 0, LV_PART_MAIN);

    lv_obj_t *label_hr_last_update = lv_label_create(cont_hr_time);
    lv_label_set_text(label_hr_last_update, "Last update:");
    lv_obj_add_style(label_hr_last_update, &style_tiny, 0);
    //lv_obj_set_style_text_color(label_hr_last_update, lv_color_hex(0xFFFF00), 0);

    label_hr_last_update_time = lv_label_create(cont_hr_time);
    struct tm last_update_ts = disp_get_hr_last_update_ts();
    
    lv_label_set_text_fmt(label_hr_last_update_time, "%d:%d", last_update_ts.tm_hour, last_update_ts.tm_min);
    //lv_obj_add_style(label_hr_last_update_time, &style_tiny, 0);
    //lv_obj_set_style_text_color(label_hr_last_update_time, lv_color_hex(0xFFFF00), 0);

    chart_hr_trend = lv_chart_create(cont_col);
    lv_obj_set_size(chart_hr_trend, 290, 170);
    //lv_obj_set_style_pad_left(chart_hr_trend, 100, LV_PART_MAIN);
    lv_chart_set_type(chart_hr_trend, LV_CHART_TYPE_LINE);
    lv_chart_set_range(chart_hr_trend, LV_CHART_AXIS_PRIMARY_Y, 30, 150);
    lv_chart_set_point_count(chart_hr_trend, 24);
    //lv_chart_set_zoom_x(chart_hr_trend, 512);
    //lv_obj_set_style_pad_column(chart_hr_trend, 1, LV_PART_MAIN);
    //lv_obj_set_style_pad_left(chart_hr_trend, -1, LV_PART_TICKS);
    //lv_obj_set_style_pad_right(chart_hr_trend, -10, LV_PART_TICKS);

    // Hide the lines and show the points
    lv_obj_set_style_line_width(chart_hr_trend, 0, LV_PART_ITEMS);
    lv_obj_set_style_size(chart_hr_trend, 8, LV_PART_INDICATOR);

    lv_obj_add_event_cb(chart_hr_trend, draw_event_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);
    // lv_obj_align_to(chart_hr_trend, NULL, LV_ALIGN_CENTER, 15, 0);

    lv_obj_set_style_bg_color(chart_hr_trend, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(chart_hr_trend, 0, LV_PART_MAIN);

    lv_obj_set_style_border_width(chart_hr_trend, 0, LV_PART_MAIN);
    lv_chart_set_div_line_count(chart_hr_trend, 0, 24);

    lv_chart_set_axis_tick(chart_hr_trend, LV_CHART_AXIS_PRIMARY_X, 10, 5, 5, 5, true, 40);
    lv_chart_set_axis_tick(chart_hr_trend, LV_CHART_AXIS_PRIMARY_Y, 0, 0, 3, 2, true, 10);

    // ser_hr_trend = lv_chart_add_series(chart_hr_trend, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);
    // lv_obj_set_style_line_width(chart_hr_trend, 3, LV_PART_ITEMS);
    // ser_hr_trend = lv_chart_add_series(chart_hr_trend, lv_palette_darken(LV_PALETTE_GREEN, 2), LV_CHART_AXIS_SECONDARY_Y);

    ser_hr_max_trend = lv_chart_add_series(chart_hr_trend, lv_color_hex(0xFFEA00), LV_CHART_AXIS_PRIMARY_Y);
    ser_hr_min_trend = lv_chart_add_series(chart_hr_trend, lv_color_hex(0x00B0FF), LV_CHART_AXIS_PRIMARY_Y);

    // hpi_set_chart_points(ser_hr_max_trend, hr_trend_max, HR_SCR_TREND_MAX_POINTS);
    // hpi_set_chart_points(ser_hr_min_trend, hr_trend_min, HR_SCR_TREND_MAX_POINTS);

    // lv_chart_refresh(chart_hr_trend);

    // HR Min/Max label
    /*lv_obj_t *label_hr_min_max_title = lv_label_create(scr_hr);
    lv_label_set_text(label_hr_min_max_title, "Hourly HR Range");
    lv_obj_add_style(label_hr_min_max_title, &style_white_medium, 0);
    lv_obj_align_to(label_hr_min_max_title, chart_hr_trend, LV_ALIGN_OUT_BOTTOM_MID, 0, 50);
    */
   
   /* lv_obj_t *cont_legend = lv_obj_create(cont_col);
   lv_obj_set_size(cont_legend, lv_pct(100), LV_SIZE_CONTENT);
   lv_obj_set_flex_flow(cont_legend, LV_FLEX_FLOW_ROW);
   lv_obj_add_style(cont_legend, &style_scr_black, 0);
   lv_obj_set_flex_align(cont_legend, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
   lv_obj_set_style_pad_bottom(cont_legend, 0, LV_PART_MAIN);
   lv_obj_set_style_pad_top(cont_legend, 0, LV_PART_MAIN);*/

    /*label_hr_min_max = lv_label_create(cont_col);
    lv_label_set_text(label_hr_min_max, "80 - 113 bpm");
    lv_obj_add_style(label_hr_min_max, &style_white_medium, 0);
    // lv_obj_add_style(label_hr_min_max, &style_gap, LV_PART_MAIN);

    // lv_obj_align_to(label_hr_min_max, NULL, LV_ALIGN_CENTER, 0, 150);

    label_hr_last_update = lv_label_create(cont_col);
    lv_label_set_text(label_hr_last_update, "Hourly HR Range");
    // lv_obj_align_to(label_hr_last_update, label_hr_min_max, LV_ALIGN_OUT_TOP_MID , 0, 0);
    */

    lv_obj_t *lbl_legend = lv_label_create(cont_col);
    lv_label_set_recolor(lbl_legend, true);
    lv_label_set_text(lbl_legend, "#FFEA00 " LV_SYMBOL_STOP " Max.# #00B0FF " LV_SYMBOL_STOP "   Min.# ");
    lv_obj_set_style_pad_top(lbl_legend, 35, LV_PART_MAIN);

    lv_obj_t *lbl_gap1 = lv_label_create(cont_col);
    lv_label_set_text(lbl_gap1, " ");
    lv_obj_align_to(lbl_gap1, btn_hr_settings, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);

    /*btn_hr_settings = lv_btn_create(cont_col);
    lv_obj_add_event_cb(btn_hr_settings, scr_ecg_start_btn_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_set_height(btn_hr_settings, 80);

    lv_obj_t *lbl_btn_settings = lv_label_create(btn_hr_settings);
    lv_label_set_text(lbl_btn_settings, LV_SYMBOL_SETTINGS " Settings");
    lv_obj_center(lbl_btn_settings);*/

    lv_obj_t *btn_hr_live = lv_btn_create(cont_col);
    lv_obj_add_event_cb(btn_hr_live, scr_hr_btn_live_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_set_height(btn_hr_live, 80);

    lv_obj_t *label_btn_bpt_measure1 = lv_label_create(btn_hr_live);
    lv_label_set_text(label_btn_bpt_measure1, LV_SYMBOL_PLAY " Raw PPG");
    lv_obj_center(label_btn_bpt_measure1);

    hpi_disp_set_curr_screen(SCR_HR);
    hpi_show_screen(scr_hr, m_scroll_dir);
}

void hpi_disp_hr_update_hr(uint16_t hr, struct tm hr_tm_last_update)
{
    if (label_hr_bpm == NULL)
        return;

    char buf[32];
    if (hr == 0)
    {
        sprintf(buf, "---");
    }
    else
    {
        sprintf(buf, "%d", hr);
    }

    lv_label_set_text(label_hr_bpm, buf);
   
    lv_label_set_text_fmt(label_hr_last_update_time, "%d:%d", hr_tm_last_update.tm_hour, hr_tm_last_update.tm_min);

    /*char buf_min_max[32];
    sprintf(buf_min_max, "%d - %d bpm", min, max);
    lv_label_set_text(label_hr_min_max, buf_min_max);
    */
}

void hpi_disp_hr_load_trend(void)
{
    struct hpi_hourly_trend_point_t hr_hourly_trend_points[HR_SCR_TREND_MAX_POINTS];

    int m_num_points = 0;

    hpi_trend_load_day_trend(hr_hourly_trend_points, &m_num_points);

    if (chart_hr_trend == NULL)
    {
        return;
    }

    int y_max = -1;
    int y_min = 999;

    for (int i = 0; i < 24; i++)
    {
        // LOG_DBG("HR Point: %" PRIx64 "| %d | %d | %d", hr_trend_points[i].timestamp, hr_trend_points[i].hr_max, hr_trend_points[i].hr_min, hr_trend_points[i].hr_avg);
        ser_hr_max_trend->y_points[i] = hr_hourly_trend_points[i].hr_max;
        ser_hr_min_trend->y_points[i] = hr_hourly_trend_points[i].hr_min;

        if (hr_hourly_trend_points[i].hr_max > y_max)
        {
            y_max = hr_hourly_trend_points[i].hr_max;
        }
        if ((hr_hourly_trend_points[i].hr_min < y_min) && (hr_hourly_trend_points[i].hr_min != 0))
        {
            y_min = hr_hourly_trend_points[i].hr_min;
        }
    }

    lv_chart_set_range(chart_hr_trend, LV_CHART_AXIS_PRIMARY_Y, y_min, y_max);
    lv_chart_refresh(chart_hr_trend);
}
