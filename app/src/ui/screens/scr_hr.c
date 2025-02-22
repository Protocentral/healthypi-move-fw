#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <lvgl.h>
#include <stdio.h>

#include <zephyr/logging/log.h>

#include "hpi_common_types.h"
#include "ui/move_ui.h"

#define HR_TREND_POINTS 24

lv_obj_t *scr_hr;

static lv_obj_t *chart_hr_trend;

static lv_chart_series_t *ser_hr_max_trend;
static lv_chart_series_t *ser_hr_min_trend;

static uint16_t hr_trend_max[HR_TREND_POINTS] = {70, 75, 80, 85, 90, 95, 100, 105, 110, 115, 120, 125, 130, 135, 140, 145, 150, 155, 160, 165, 170, 175, 180, 185};
static uint16_t hr_trend_min[HR_TREND_POINTS] = {40, 45, 50, 55, 60, 65, 70, 75, 80, 85, 90, 95, 100, 105, 110, 115, 120, 125, 130, 135, 140, 145, 150, 155};

// static lv_chart_series_t *ser_hr_trend;
// static lv_chart_series_t *ser_hr_max_trend;
// static lv_chart_series_t *ser_hr_min_trend;

// GUI Labels
static lv_obj_t *label_hr_bpm;
static lv_obj_t *label_hr_status;
static lv_obj_t *label_hr_last_update;

static lv_obj_t *label_hr_min_max;
static lv_obj_t *btn_hr_settings;

// Externs
extern lv_style_t style_lbl_white;
extern lv_style_t style_red_medium;
extern lv_style_t style_lbl_white_small;
extern lv_style_t style_white_large;
extern lv_style_t style_white_medium;

extern lv_style_t style_scr_black;

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

static void scr_hr_settings_btn_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
    }
}

static void scr_ht_btn_live_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
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
    draw_header_minimal(scr_hr, 10);

    lv_obj_set_scrollbar_mode(scr_hr, LV_SCROLLBAR_MODE_ON);

    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_flex_flow(&style, LV_FLEX_FLOW_ROW_WRAP);
    lv_style_set_flex_main_place(&style, LV_FLEX_ALIGN_SPACE_EVENLY);
    lv_style_set_flex_cross_place(&style, LV_FLEX_ALIGN_CENTER);

    /*Create a container with COLUMN flex direction*/
    lv_obj_t *cont_col = lv_obj_create(scr_hr);
    lv_obj_set_size(cont_col, lv_pct(100), LV_SIZE_CONTENT);
    // lv_obj_set_width(cont_col, lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_col, -1, LV_PART_SCROLLBAR);
    lv_obj_add_style(cont_col, &style_scr_black, 0);
    lv_obj_add_style(cont_col, &style, 0);

    lv_obj_t *label_signal = lv_label_create(cont_col);
    lv_label_set_text(label_signal, "Heart Rate");
    // lv_obj_align(label_signal, LV_ALIGN_TOP_MID, 0, 10);
    // lv_obj_add_style(label_signal, &style_lbl_white_small, 0);

    lv_obj_t *cont_hr = lv_obj_create(cont_col);
    lv_obj_set_size(cont_hr, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_hr, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(cont_hr, &style_scr_black, 0);
    lv_obj_set_flex_align(cont_hr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

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

    chart_hr_trend = lv_chart_create(cont_col);
    lv_obj_set_size(chart_hr_trend, 290, 130);
    lv_chart_set_type(chart_hr_trend, LV_CHART_TYPE_LINE);
    lv_chart_set_range(chart_hr_trend, LV_CHART_AXIS_PRIMARY_Y, 0, 150);
    lv_chart_set_point_count(chart_hr_trend, 24);

    // Hide the lines and show the points
    lv_obj_set_style_line_width(chart_hr_trend, 0, LV_PART_ITEMS);
    lv_obj_set_style_size(chart_hr_trend, 5, LV_PART_INDICATOR);

    lv_obj_add_event_cb(chart_hr_trend, draw_event_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);
    // lv_obj_align_to(chart_hr_trend, NULL, LV_ALIGN_CENTER, 15, 0);

    lv_obj_set_style_bg_color(chart_hr_trend, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(chart_hr_trend, 0, LV_PART_MAIN);

    // lv_obj_set_style_border_width(chart_hr_trend_up, 1, LV_PART_MAIN);
    lv_chart_set_div_line_count(chart_hr_trend, 0, 0);

    lv_chart_set_axis_tick(chart_hr_trend, LV_CHART_AXIS_PRIMARY_X, 10, 5, 5, 5, true, 40);
    lv_chart_set_axis_tick(chart_hr_trend, LV_CHART_AXIS_PRIMARY_Y, 10, 5, 3, 2, true, 60);

    // ser_hr_trend = lv_chart_add_series(chart_hr_trend, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);
    // lv_obj_set_style_line_width(chart_hr_trend, 3, LV_PART_ITEMS);
    // ser_hr_trend = lv_chart_add_series(chart_hr_trend, lv_palette_darken(LV_PALETTE_GREEN, 2), LV_CHART_AXIS_SECONDARY_Y);

    ser_hr_max_trend = lv_chart_add_series(chart_hr_trend, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
    ser_hr_min_trend = lv_chart_add_series(chart_hr_trend, lv_palette_main(LV_PALETTE_GREEN), LV_CHART_AXIS_PRIMARY_Y);

    hpi_set_chart_points(ser_hr_max_trend, hr_trend_max, HR_TREND_POINTS);
    hpi_set_chart_points(ser_hr_min_trend, hr_trend_min, HR_TREND_POINTS);

    lv_chart_refresh(chart_hr_trend);

    // HR Min/Max label
    /*lv_obj_t *label_hr_min_max_title = lv_label_create(scr_hr);
    lv_label_set_text(label_hr_min_max_title, "Hourly HR Range");
    lv_obj_add_style(label_hr_min_max_title, &style_white_medium, 0);
    lv_obj_align_to(label_hr_min_max_title, chart_hr_trend, LV_ALIGN_OUT_BOTTOM_MID, 0, 50);
    */

    lv_obj_t *lbl_gap = lv_label_create(cont_col);
    lv_label_set_text(lbl_gap, " ");
    lv_obj_align_to(lbl_gap, btn_hr_settings, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);

    label_hr_min_max = lv_label_create(cont_col);
    lv_label_set_text(label_hr_min_max, "80 - 113 bpm");
    lv_obj_add_style(label_hr_min_max, &style_white_medium, 0);
    // lv_obj_add_style(label_hr_min_max, &style_gap, LV_PART_MAIN);

    // lv_obj_align_to(label_hr_min_max, NULL, LV_ALIGN_CENTER, 0, 150);

    label_hr_last_update = lv_label_create(cont_col);
    lv_label_set_text(label_hr_last_update, "Hourly HR Range");
    lv_obj_add_style(label_hr_last_update, &style_lbl_white_small, 0);
    // lv_obj_align_to(label_hr_last_update, label_hr_min_max, LV_ALIGN_OUT_TOP_MID , 0, 0);

    lv_obj_t *lbl_gap1 = lv_label_create(cont_col);
    lv_label_set_text(lbl_gap1, " ");
    lv_obj_align_to(lbl_gap1, btn_hr_settings, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);

    btn_hr_settings = lv_btn_create(cont_col);
    lv_obj_add_event_cb(btn_hr_settings, scr_hr_settings_btn_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_set_height(btn_hr_settings, 80);

    lv_obj_t *label_btn_bpt_measure = lv_label_create(btn_hr_settings);
    lv_label_set_text(label_btn_bpt_measure, LV_SYMBOL_SETTINGS " Settings");
    lv_obj_center(label_btn_bpt_measure);

    lv_obj_t *btn_hr_live = lv_btn_create(cont_col);
    lv_obj_add_event_cb(btn_hr_live, scr_ht_btn_live_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_set_height(btn_hr_live, 80);

    lv_obj_t *label_btn_bpt_measure1 = lv_label_create(btn_hr_live);
    lv_label_set_text(label_btn_bpt_measure1, LV_SYMBOL_PLAY " Live");
    lv_obj_center(label_btn_bpt_measure1);

    // lv_obj_align_to(btn_hr_settings, label_hr_min_max, LV_ALIGN_OUT_BOTTOM_MID, 0, 30);

    /*lv_obj_t *lbl_gap = lv_label_create(scr_hr);
    lv_label_set_text(lbl_gap, " ");
    lv_obj_align_to(lbl_gap, btn_hr_settings, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);
    */

    // hpi_disp_hr_load_trend

    hpi_disp_set_curr_screen(SCR_HR);
    hpi_show_screen(scr_hr, m_scroll_dir);
}

void hpi_disp_hr_update_hr(uint16_t hr, uint16_t min, uint16_t max, uint16_t hr_mean)
{
    if (label_hr_bpm == NULL)
        return;

    char buf[32];
    if (hr == 0)
    {
        sprintf(buf, "00");
    }
    else
    {
        sprintf(buf, "%d", hr);
    }

    lv_label_set_text(label_hr_bpm, buf);

    char buf_min_max[32];
    sprintf(buf_min_max, "%d - %d bpm", min, max);
    lv_label_set_text(label_hr_min_max, buf_min_max);
}

void hpi_disp_hr_load_trend(void)
{
    uint16_t hr_max_trend[24];
    uint16_t hr_min_trend[24];

    // Load full HR data from file

    if (chart_hr_trend == NULL)
        return;

    for (int i = 0; i < 24; i++)
    {
        ser_hr_max_trend->y_points[i] = hr_max_trend[i];
        ser_hr_min_trend->y_points[i] = hr_min_trend[i];
    }

    lv_chart_refresh(chart_hr_trend);
}