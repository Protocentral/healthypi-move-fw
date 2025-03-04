#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <lvgl.h>
#include <stdio.h>

#include <zephyr/logging/log.h>

#include "hpi_common_types.h"
#include "ui/move_ui.h"

lv_obj_t *scr_temp;

static lv_obj_t *chart_temp_trend;
//static lv_chart_series_t *ser_temp_trend;

// GUI Labels
static lv_obj_t *label_temp_f;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_white_medium;

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

void draw_scr_temp(enum scroll_dir m_scroll_dir)
{
    scr_temp = lv_obj_create(NULL);
    lv_obj_clear_flag(scr_temp, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    draw_header_minimal(scr_temp, 10);

    lv_obj_t *cont_col = lv_obj_create(scr_temp);
    lv_obj_set_size(cont_col, lv_pct(100), LV_SIZE_CONTENT);
    // lv_obj_set_width(cont_col, lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    //lv_obj_set_style_pad_right(cont_col, -1, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_left(cont_col, 0, LV_PART_MAIN);
    lv_obj_add_style(cont_col, &style_scr_black, 0);
    
    lv_obj_t *lbl_gap = lv_label_create(cont_col);
    lv_label_set_text(lbl_gap, " ");
    
    lv_obj_t *label_signal = lv_label_create(cont_col);
    lv_label_set_text(label_signal, "Skin Temperature");

    label_temp_f = lv_label_create(cont_col);
    lv_label_set_text(label_temp_f, "00.00 °F");
    lv_obj_add_style(label_temp_f, &style_white_medium, 0);

    lv_obj_t *cont_chart = lv_obj_create(cont_col);
    lv_obj_set_size(cont_chart, lv_pct(100), 150);
    lv_obj_set_style_pad_left(cont_chart, 50, LV_PART_MAIN);
    
    lv_obj_align_to(cont_chart, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_style(cont_chart, &style_scr_black, 0);

    chart_temp_trend = lv_chart_create(cont_chart);
    lv_obj_set_size(chart_temp_trend, 290, 130);
    lv_obj_center(chart_temp_trend);
    lv_chart_set_type(chart_temp_trend, LV_CHART_TYPE_BAR);
    lv_chart_set_range(chart_temp_trend, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_point_count(chart_temp_trend, 24);

    lv_obj_add_event_cb(chart_temp_trend, draw_event_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);

    lv_obj_set_style_bg_color(chart_temp_trend, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(chart_temp_trend, 0, LV_PART_MAIN);
    //lv_obj_set_style_border_width(chart_temp_trend, 0, LV_PART_MAIN);
    lv_chart_set_div_line_count(chart_temp_trend, 0, 0);

    /*Add ticks and label to every axis*/
    lv_chart_set_axis_tick(chart_temp_trend, LV_CHART_AXIS_PRIMARY_X, 10, 5, 5, 5, true, 80);
    lv_chart_set_axis_tick(chart_temp_trend, LV_CHART_AXIS_PRIMARY_Y, 10, 5, 3, 2, true, 80);
    //lv_chart_set_axis_tick(chart_temp_trend, LV_CHART_AXIS_SECONDARY_Y, 10, 5, 3, 4, true, 50);

    /*Zoom in a little in X*/
    //lv_chart_set_zoom_x(chart_temp_trend, 800);

    /*Add two data series*/
    lv_chart_series_t *ser2 = lv_chart_add_series(chart_temp_trend, lv_palette_darken(LV_PALETTE_ORANGE, 2), LV_CHART_AXIS_PRIMARY_Y);

    lv_coord_t *ser2_array = lv_chart_get_y_array(chart_temp_trend, ser2);
    /*Directly set points on 'ser2'*/
    ser2_array[0] = 92;
    ser2_array[1] = 71;
    ser2_array[2] = 61;
    ser2_array[3] = 15;
    ser2_array[4] = 21;
    ser2_array[5] = 35;
    ser2_array[6] = 35;
    ser2_array[7] = 58;
    ser2_array[8] = 31;
    ser2_array[9] = 53;
    ser2_array[10] = 33;
    ser2_array[11] = 73;

    lv_chart_refresh(chart_temp_trend); /*Required after direct set*/

    hpi_disp_set_curr_screen(SCR_TEMP);
    hpi_show_screen(scr_temp, m_scroll_dir);
}

void hpi_temp_disp_update_temp_f(float temp_f)
{
    if (label_temp_f == NULL)
        return;

    lv_label_set_text_fmt(label_temp_f, "%.2f °F", temp_f);
}