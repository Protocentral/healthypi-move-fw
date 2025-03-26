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
extern lv_style_t style_tiny;


extern lv_style_t style_scr_black;
extern lv_style_t style_bg_red;
extern lv_style_t style_bg_blue;
extern lv_style_t style_bg_green;

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
        hpi_move_load_scr_spl(SCR_SPL_PLOT_PPG, SCROLL_UP, (uint8_t)SCR_HR);
    }
}

void draw_scr_hr(enum scroll_dir m_scroll_dir)
{
    scr_hr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr_hr, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    //draw_scr_common(scr_hr);

    //lv_obj_set_scrollbar_mode(scr_hr, LV_SCROLLBAR_MODE_ON);

    lv_obj_t *cont_col = lv_obj_create(scr_hr);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    // lv_obj_set_width(cont_col, lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_col, -1, LV_PART_SCROLLBAR);
    //lv_obj_set_style_pad_top(cont_col, 5, LV_PART_MAIN);
    //lv_obj_set_style_pad_bottom(cont_col, 1, LV_PART_MAIN);
    lv_obj_add_style(cont_col, &style_scr_black, 0);
    lv_obj_add_style(cont_col, &style_bg_green, 0);

    lv_obj_t *label_signal = lv_label_create(cont_col);
    lv_label_set_text(label_signal, "Heart Rate");
    lv_obj_add_style(label_signal, &style_white_small, 0);

    lv_obj_t *img_hr = lv_img_create(cont_col);
    lv_img_set_src(img_hr, &img_heart_120);

    lv_obj_t *cont_hr = lv_obj_create(cont_col);
    lv_obj_set_size(cont_hr, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_hr, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(cont_hr, &style_scr_black, 0);
    lv_obj_set_flex_align(cont_hr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_bottom(cont_hr, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(cont_hr, 0, LV_PART_MAIN);

    lv_obj_set_style_bg_opa(cont_hr, 0, 0);

    //lv_obj_t *img1 = lv_img_create(cont_hr);
    //lv_img_set_src(img1, &img_heart_35);
    // lv_obj_align_to(img1, label_hr_bpm, LV_ALIGN_OUT_LEFT_MID, -10, 0);

    label_hr_bpm = lv_label_create(cont_hr);
    lv_label_set_text(label_hr_bpm, "78");
    lv_obj_add_style(label_hr_bpm, &style_white_large, 0);

    // HR Sub bpm label
    lv_obj_t *label_hr_sub = lv_label_create(cont_hr);
    lv_label_set_text(label_hr_sub, " bpm");

    label_hr_last_update_time = lv_label_create(cont_col);
    struct tm last_update_ts = disp_get_hr_last_update_ts();
    lv_label_set_text_fmt(label_hr_last_update_time, "Last updated: %02d:%02d", last_update_ts.tm_hour, last_update_ts.tm_min);

    hpi_disp_set_curr_screen(SCR_HR);
    hpi_show_screen(scr_hr, m_scroll_dir);
}

void hpi_disp_hr_update_hr(uint16_t hr, struct tm hr_tm_last_update)
{
    if (label_hr_bpm == NULL)
        return;

    if (hr == 0)
    {
        lv_label_set_text(label_hr_bpm, "--");
    }
    else
    {
        lv_label_set_text_fmt(label_hr_bpm, "%d", hr);
    }
    lv_label_set_text_fmt(label_hr_last_update_time, "Last updated: %02d:%02d", hr_tm_last_update.tm_hour, hr_tm_last_update.tm_min);
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
