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
static lv_chart_series_t *ser_temp_trend;

// GUI Labels
static lv_obj_t *label_temp_f;
static lv_obj_t *label_temp_c;

// Externs
extern lv_style_t style_lbl_white;
extern lv_style_t style_lbl_red;
extern lv_style_t style_lbl_white_small;

void draw_scr_temp(enum scroll_dir m_scroll_dir)
{
    scr_temp = lv_obj_create(NULL);
    lv_obj_clear_flag(scr_temp, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    draw_header_minimal(scr_temp, 10);

    lv_obj_t *label_signal = lv_label_create(scr_temp);
    lv_label_set_text(label_signal, "Body Temperature");
    lv_obj_align(label_signal, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_add_style(label_signal, &style_lbl_white_small, 0);

    chart_temp_trend = lv_chart_create(scr_temp);
    lv_obj_set_size(chart_temp_trend, 390, 100);
    lv_obj_set_style_bg_color(chart_temp_trend, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(chart_temp_trend, 0, LV_PART_MAIN);
    lv_obj_set_style_size(chart_temp_trend, 0, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(chart_temp_trend, 0, LV_PART_MAIN);
    lv_chart_set_point_count(chart_temp_trend, PPG_DISP_WINDOW_SIZE);
    lv_chart_set_div_line_count(chart_temp_trend, 0, 0);
    lv_chart_set_update_mode(chart_temp_trend, LV_CHART_UPDATE_MODE_CIRCULAR);
    lv_obj_align(chart_temp_trend, LV_ALIGN_CENTER, 0, -35);

    ser_temp_trend = lv_chart_add_series(chart_temp_trend, lv_palette_main(LV_PALETTE_ORANGE), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_line_width(chart_temp_trend, 3, LV_PART_ITEMS);

    label_temp_f = lv_label_create(scr_temp);
    lv_label_set_text(label_temp_f, "--");
    lv_obj_align_to(label_temp_f, NULL, LV_ALIGN_CENTER, -100, 40);
    lv_obj_add_style(label_temp_f, &style_lbl_white, 0);

    lv_obj_t *label_temp_f_sub = lv_label_create(scr_temp);
    lv_label_set_text(label_temp_f_sub, " F");
    lv_obj_align_to(label_temp_f_sub, label_temp_f, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);

    hpi_disp_set_curr_screen(SCR_TEMP);
    hpi_show_screen(scr_temp, m_scroll_dir);
}

void hpi_temp_disp_update_temp_f(double temp_f)
{
    if (label_temp_f == NULL)
        return;

    char buf[32];
    sprintf(buf, "%.2f", temp_f);

    lv_label_set_text(label_temp_f, buf);
}