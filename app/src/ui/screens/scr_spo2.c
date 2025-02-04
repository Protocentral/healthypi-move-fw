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
static lv_chart_series_t *ser_spo2_trend;

// GUI Labels
static lv_obj_t *label_spo2_percent;
static lv_obj_t *label_spo2_status;
static lv_obj_t *label_spo2_last_update;

static lv_obj_t *label_spo2_min_max;
static lv_obj_t *btn_spo2_settings;

// Externs
extern lv_style_t style_lbl_white;
extern lv_style_t style_red_medium;
extern lv_style_t style_lbl_white_small;
extern lv_style_t style_white_large;
extern lv_style_t style_white_medium;

static void draw_event_cb(lv_event_t *e)
{
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (!lv_obj_draw_part_check_type(dsc, &lv_chart_class, LV_CHART_DRAW_PART_TICK_LABEL))
        return;

    if (dsc->id == LV_CHART_AXIS_PRIMARY_X && dsc->text)
    {
        const char *month[] = {"00:00", "06:00", "12:00", "18:00"};
        lv_snprintf(dsc->text, dsc->text_length, "%s", month[dsc->value]);
    }
}

static void scr_spo2_settings_btn_event_handler(lv_event_t *e)
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

    lv_obj_t *label_signal = lv_label_create(scr_spo2);
    lv_label_set_text(label_signal, "SpO2");
    lv_obj_align(label_signal, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_add_style(label_signal, &style_lbl_white_small, 0);

    label_spo2_percent = lv_label_create(scr_spo2);
    lv_label_set_text(label_spo2_percent, "00");
    lv_obj_add_style(label_spo2_percent, &style_white_large, 0);
    lv_obj_align_to(label_spo2_percent, NULL, LV_ALIGN_TOP_MID, 0, 90);

    lv_obj_t *img1 = lv_img_create(scr_spo2);
    lv_img_set_src(img1, &img_heart_48px);
    lv_obj_set_size(img1, 48, 48);
    lv_obj_align_to(img1, label_spo2_percent, LV_ALIGN_OUT_LEFT_MID, -10, 0);

    // Spo2 Sub bpm label
    lv_obj_t *label_spo2_sub = lv_label_create(scr_spo2);
    lv_label_set_text(label_spo2_sub, " %%");
    lv_obj_align_to(label_spo2_sub, label_spo2_percent, LV_ALIGN_OUT_RIGHT_MID, 0, 0);

    chart_spo2_trend = lv_chart_create(scr_spo2);
    lv_obj_set_size(chart_spo2_trend, 300, 100);
    lv_chart_set_range(chart_spo2_trend, LV_CHART_AXIS_PRIMARY_Y, 0, 150);
    lv_chart_set_point_count(chart_spo2_trend, 24);

    lv_obj_add_event_cb(chart_spo2_trend, draw_event_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);
    lv_obj_align_to(chart_spo2_trend, label_spo2_percent, LV_ALIGN_OUT_BOTTOM_MID, 10, 15);

    label_spo2_last_update = lv_label_create(scr_spo2);
    lv_label_set_text(label_spo2_last_update, LV_SYMBOL_REFRESH " 00:00");
    // lv_obj_add_style(label_spo2_last_update, &style_lbl_white_small, 0);
    lv_obj_align_to(label_spo2_last_update, label_spo2_sub, LV_ALIGN_OUT_BOTTOM_MID, 0, 25);

    // Spo2 Min/Max label
    lv_obj_t *label_spo2_min_max_title = lv_label_create(scr_spo2);
    lv_label_set_text(label_spo2_min_max_title, "Hourly Spo2 Range");
    lv_obj_add_style(label_spo2_min_max_title, &style_white_medium, 0);
    lv_obj_align_to(label_spo2_min_max_title, label_spo2_last_update, LV_ALIGN_OUT_BOTTOM_MID, 0, 50);

    hpi_disp_set_curr_screen(SCR_SPO2);
    hpi_show_screen(scr_spo2, m_scroll_dir);
}
