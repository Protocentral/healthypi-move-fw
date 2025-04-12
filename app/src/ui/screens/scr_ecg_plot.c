#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <lvgl.h>
#include <stdio.h>
#include <app_version.h>
#include <zephyr/logging/log.h>

#include "hpi_common_types.h"
#include "ui/move_ui.h"
#include "hw_module.h"

lv_obj_t *scr_plot_ecg;

// GUI Charts
static lv_obj_t *chart_ecg;
static lv_chart_series_t *ser_ecg;

static lv_obj_t *label_ecg_hr;
static lv_obj_t *label_timer;
static lv_obj_t *label_ecg_lead_off;

static bool chart_ecg_update = false;
static float y_max_ecg = 0;
static float y_min_ecg = 10000;

// static bool ecg_plot_hidden = false;

static float gx = 0;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;

// void scr_plot_ecg_update_timer(
void draw_scr_spl_plot_ecg(enum scroll_dir m_scroll_dir, uint8_t scr_parent)
{
    scr_plot_ecg = lv_obj_create(NULL);
    draw_scr_common(scr_plot_ecg);

    /*Create a container with COLUMN flex direction*/
    lv_obj_t *cont_col = lv_obj_create(scr_plot_ecg);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    // lv_obj_set_width(cont_col, lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_col, -1, LV_PART_SCROLLBAR);
    lv_obj_add_style(cont_col, &style_scr_black, 0);

    lv_obj_t *label_signal = lv_label_create(cont_col);
    lv_label_set_text(label_signal, "ECG");

    // Draw countdown timer container
    lv_obj_t *cont_timer = lv_obj_create(cont_col);
    lv_obj_set_size(cont_timer, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_timer, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(cont_timer, &style_scr_black, 0);
    lv_obj_set_flex_align(cont_timer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    // Draw Countdown Timer
    LV_IMG_DECLARE(timer_32);
    lv_obj_t *img_timer = lv_img_create(cont_timer);
    lv_img_set_src(img_timer, &timer_32);

    label_timer = lv_label_create(cont_timer);
    lv_label_set_text(label_timer, "00");
    lv_obj_add_style(label_timer, &style_white_medium, 0);
    lv_obj_t *label_timer_sub = lv_label_create(cont_timer);
    lv_label_set_text(label_timer_sub, " secs");
    lv_obj_add_style(label_timer_sub, &style_white_medium, 0);

    // Create Chart 1 - ECG
    chart_ecg = lv_chart_create(cont_col);
    lv_obj_set_size(chart_ecg, 390, 140);
    lv_obj_set_style_bg_color(chart_ecg, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(chart_ecg, 0, LV_PART_MAIN);

    lv_obj_set_style_size(chart_ecg, 0, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(chart_ecg, 0, LV_PART_MAIN);
    lv_chart_set_point_count(chart_ecg, ECG_DISP_WINDOW_SIZE);
    lv_chart_set_div_line_count(chart_ecg, 0, 0);
    lv_chart_set_update_mode(chart_ecg, LV_CHART_UPDATE_MODE_CIRCULAR);
    lv_obj_align(chart_ecg, LV_ALIGN_CENTER, 0, -35);
    ser_ecg = lv_chart_add_series(chart_ecg, lv_palette_darken(LV_PALETTE_ORANGE, 2), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_line_width(chart_ecg, 6, LV_PART_ITEMS);

    label_ecg_lead_off = lv_label_create(cont_col);
    lv_label_set_text(label_ecg_lead_off, "Do not remove finger");
    //lv_obj_align_to(label_ecg_lead_off, NULL, LV_ALIGN_CENTER, -20, -40);
    lv_obj_set_style_text_align(label_ecg_lead_off, LV_TEXT_ALIGN_CENTER, 0);
    // lv_obj_add_style(label_ecg_lead_off, &style_lbl_orange, 0);
    lv_obj_add_flag(label_ecg_lead_off, LV_OBJ_FLAG_HIDDEN);

    // Draw BPM container
    lv_obj_t *cont_hr = lv_obj_create(cont_col);
    lv_obj_set_size(cont_hr, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_hr, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(cont_hr, &style_scr_black, 0);
    lv_obj_set_flex_align(cont_hr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    
    lv_obj_t *img_heart = lv_img_create(cont_hr);
    lv_img_set_src(img_heart, &img_heart_35);

    label_ecg_hr = lv_label_create(cont_hr);
    lv_label_set_text(label_ecg_hr, "00");
    lv_obj_add_style(label_ecg_hr, &style_white_medium, 0);
    lv_obj_t *label_hr_sub = lv_label_create(cont_hr);
    lv_label_set_text(label_hr_sub, " bpm");

    hpi_disp_set_curr_screen(SCR_SPL_PLOT_ECG);
    hpi_show_screen_spl(scr_plot_ecg, m_scroll_dir, scr_parent);
    chart_ecg_update = true;
}

