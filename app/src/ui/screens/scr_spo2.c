#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <lvgl.h>
#include <stdio.h>

#include <zephyr/logging/log.h>

#include "hpi_common_types.h"
#include "ui/move_ui.h"

lv_obj_t *scr_spo2;

static lv_obj_t *chart_spo2_trend;

static lv_chart_series_t *ser_spo2_max_trend;
static lv_chart_series_t *ser_spo2_min_trend;

// GUI Labels
static lv_obj_t *label_spo2_percent;
//static lv_obj_t *label_spo2_status;
static lv_obj_t *label_spo2_last_update;

//static lv_obj_t *label_spo2_min_max;
static lv_obj_t *btn_spo2_settings;

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

static void scr_spo2_settings_btn_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
    }
}

static void scr_spo2_btn_live_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        hpi_move_load_scr_spl(SCR_SPL_PLOT_PPG, SCROLL_UP, (uint8_t) SCR_SPO2);
    }
}

static void scr_spo2_measure_btn_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
    }
}

void draw_scr_spo2(enum scroll_dir m_scroll_dir)
{
    scr_spo2 = lv_obj_create(NULL);
    // lv_obj_set_flag(scr_spo2, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    draw_header_minimal(scr_spo2, 10);

    lv_obj_set_scrollbar_mode(scr_spo2, LV_SCROLLBAR_MODE_ON);

    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_flex_flow(&style, LV_FLEX_FLOW_ROW_WRAP);
    lv_style_set_flex_main_place(&style, LV_FLEX_ALIGN_SPACE_EVENLY);
    lv_style_set_flex_cross_place(&style, LV_FLEX_ALIGN_CENTER);

    /*Create a container with COLUMN flex direction*/
    lv_obj_t *cont_col = lv_obj_create(scr_spo2);
    lv_obj_set_size(cont_col, lv_pct(100), LV_SIZE_CONTENT);
    // lv_obj_set_width(cont_col, lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_col, -1, LV_PART_SCROLLBAR);
    lv_obj_add_style(cont_col, &style_scr_black, 0);
    lv_obj_add_style(cont_col, &style, 0);

    lv_obj_t *label_signal = lv_label_create(cont_col);
    lv_label_set_text(label_signal, "SpO2");
    //lv_obj_align(label_signal, LV_ALIGN_TOP_MID, 0, 60);
    //lv_obj_add_style(label_signal, &style_lbl_white_small, 0);

    lv_obj_t *cont_spo2 = lv_obj_create(cont_col);
    lv_obj_set_size(cont_spo2, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_spo2, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(cont_spo2, &style_scr_black, 0);
    lv_obj_set_style_pad_all(cont_spo2, 1, LV_PART_MAIN);
    lv_obj_set_flex_align(cont_spo2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    LV_IMG_DECLARE(icon_spo2_30x35);
    lv_obj_t *img1 = lv_img_create(cont_spo2);
    lv_img_set_src(img1, &icon_spo2_30x35);
    lv_obj_align_to(img1, label_spo2_percent, LV_ALIGN_OUT_LEFT_MID, -10, 0);

    label_spo2_percent = lv_label_create(cont_spo2);
    lv_label_set_text(label_spo2_percent, "00 %");
    lv_obj_add_style(label_spo2_percent, &style_white_medium, 0);
  
    label_spo2_last_update = lv_label_create(cont_col);
    lv_label_set_text(label_spo2_last_update, LV_SYMBOL_REFRESH " 00:00");
    lv_obj_add_style(label_spo2_last_update, &style_lbl_white_small, 0);
    lv_obj_align_to(label_spo2_last_update, label_spo2_percent, LV_ALIGN_OUT_BOTTOM_MID, 0, 3);

    chart_spo2_trend = lv_chart_create(cont_col);
    lv_obj_set_size(chart_spo2_trend, 290, 130);
    lv_chart_set_type(chart_spo2_trend, LV_CHART_TYPE_LINE);
    lv_chart_set_range(chart_spo2_trend, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_point_count(chart_spo2_trend, 24);

    // Hide the lines and show the points
    lv_obj_set_style_line_width(chart_spo2_trend, 0, LV_PART_ITEMS);
    lv_obj_set_style_size(chart_spo2_trend, 5, LV_PART_INDICATOR);

    lv_obj_add_event_cb(chart_spo2_trend, draw_event_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);
    //lv_obj_align_to(chart_spo2_trend, NULL, LV_ALIGN_CENTER, 15, 40);

    lv_obj_set_style_bg_color(chart_spo2_trend, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(chart_spo2_trend, 0, LV_PART_MAIN);

    //lv_obj_set_style_border_width(chart_hr_trend_up, 1, LV_PART_MAIN);
    lv_chart_set_div_line_count(chart_spo2_trend, 0, 0);

    lv_chart_set_axis_tick(chart_spo2_trend, LV_CHART_AXIS_PRIMARY_X, 10, 5, 5, 5, true, 40);
    lv_chart_set_axis_tick(chart_spo2_trend, LV_CHART_AXIS_PRIMARY_Y, 10, 5, 3, 2, true, 60);

    ser_spo2_max_trend = lv_chart_add_series(chart_spo2_trend, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
    ser_spo2_min_trend = lv_chart_add_series(chart_spo2_trend, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);

    lv_obj_t *lbl_gap1 = lv_label_create(cont_col);
    lv_label_set_text(lbl_gap1, " ");

    /*
    label_spo2_min_max = lv_label_create(cont_col);
    lv_label_set_text(label_spo2_min_max, "92 - 96 %");
    lv_obj_add_style(label_spo2_min_max, &style_white_medium, 0);
  
    lv_obj_t *lbl_minmax_title = lv_label_create(cont_col);
    lv_label_set_text(lbl_minmax_title, "Hourly SpO2 Range");
    lv_obj_add_style(lbl_minmax_title, &style_lbl_white_small, 0);*/

    lv_obj_t *btn_measure = lv_btn_create(cont_col);
    //lv_obj_set_height(btn_measure, 80);
    lv_obj_add_event_cb(btn_measure, scr_spo2_measure_btn_event_handler, LV_EVENT_ALL, NULL);

    lv_obj_t *label_btn_measure = lv_label_create(btn_measure);
    lv_label_set_text(label_btn_measure, "Measure");
    lv_obj_center(label_btn_measure);

    lv_obj_t *lbl_gap = lv_label_create(cont_col);
    lv_label_set_text(lbl_gap, " ");

    btn_spo2_settings = lv_btn_create(cont_col);
    lv_obj_add_event_cb(btn_spo2_settings, scr_spo2_settings_btn_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_set_height(btn_spo2_settings, 60);

    lv_obj_t *label_btn_spo2_settings = lv_label_create(btn_spo2_settings);
    lv_label_set_text(label_btn_spo2_settings, LV_SYMBOL_SETTINGS " Settings");
    lv_obj_center(label_btn_spo2_settings);

    lv_obj_t *btn_spo2_live = lv_btn_create(cont_col);
    lv_obj_add_event_cb(btn_spo2_live, scr_spo2_btn_live_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_set_height(btn_spo2_live, 60);

    lv_obj_t *label_btn_spo2_live = lv_label_create(btn_spo2_live);
    lv_label_set_text(label_btn_spo2_live, LV_SYMBOL_PLAY " Live PPG");
    lv_obj_center(label_btn_spo2_live);
    
    hpi_disp_set_curr_screen(SCR_SPO2);
    hpi_show_screen(scr_spo2, m_scroll_dir);
}

void hpi_disp_update_spo2(int spo2)
{
    if (label_spo2_percent == NULL)
        return;

    char buf[32];
    if (spo2 == 0)
    {
        sprintf(buf, "00");
    }
    else
    {
        sprintf(buf, "%d",spo2);
    }

    lv_label_set_text(label_spo2_percent, buf);
}