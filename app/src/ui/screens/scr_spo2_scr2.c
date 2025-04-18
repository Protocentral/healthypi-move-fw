#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <lvgl.h>
#include <stdio.h>

#include <zephyr/logging/log.h>

#include "hpi_common_types.h"
#include "ui/move_ui.h"
#include "trends.h"

LOG_MODULE_REGISTER(hpi_disp_scr_spo2_scr2, LOG_LEVEL_DBG);

static lv_obj_t *scr_spo2_scr2;
static lv_obj_t *scr_spo2_scr3;

// GUI Labels
static lv_obj_t *label_spo2_percent;
static lv_obj_t *label_spo2_last_update_time;

static lv_obj_t *chart_ppg;
static lv_chart_series_t *ser_ppg;

static lv_obj_t *label_hr;
static lv_obj_t *label_hr_bpm;

static lv_obj_t *label_spo2_progress;
static lv_obj_t *bar_spo2_progress;
static lv_obj_t *label_spo2_status;

static lv_obj_t *cont_spo2_val;

// static lv_obj_t *label_min_max;
static lv_obj_t *btn_spo2_proceed;

static bool chart_ppg_update = false;
static float y_max_ppg = 0;
static float y_min_ppg = 10000;

static float gx = 0;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_large;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_tiny;

extern lv_style_t style_bg_blue;
extern lv_style_t style_bg_red;

extern struct k_sem sem_start_one_shot_spo2;

static void scr_spo2_btn_proceed_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        k_sem_give(&sem_start_one_shot_spo2);
        hpi_move_load_scr_spl(SCR_SPL_SPO2_SCR3, SCROLL_UP, (uint8_t)SCR_SPO2);
    }
}

void draw_scr_spo2_scr2(enum scroll_dir m_scroll_dir)
{
    scr_spo2_scr2 = lv_obj_create(NULL);
    lv_obj_add_style(scr_spo2_scr2, &style_scr_black, 0);
    // lv_obj_set_flag(scr_spo2, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    // draw_scr_common(scr_spo2_scr2);

    lv_obj_set_scrollbar_mode(scr_spo2_scr2, LV_SCROLLBAR_MODE_ON);

    /*Create a container with COLUMN flex direction*/
    lv_obj_t *cont_col = lv_obj_create(scr_spo2_scr2);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 25);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_col, -1, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_top(cont_col, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(cont_col, 1, LV_PART_MAIN);
    lv_obj_add_style(cont_col, &style_scr_black, 0);
    lv_obj_add_style(cont_col, &style_bg_red, 0);

    lv_obj_t *label_info = lv_label_create(cont_col);
    lv_label_set_long_mode(label_info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_info, 300);
    lv_label_set_text(label_info, "Ensure that your Move is worn on the wrist as shown, not too tight nor too loose, away from the wrist bone.");
    lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *img_spo2 = lv_img_create(cont_col);
    lv_img_set_src(img_spo2, &img_spo2_hand);

    btn_spo2_proceed = lv_btn_create(cont_col);
    lv_obj_add_event_cb(btn_spo2_proceed, scr_spo2_btn_proceed_handler, LV_EVENT_ALL, NULL);
    lv_obj_set_height(btn_spo2_proceed, 85);

    lv_obj_t *label_btn = lv_label_create(btn_spo2_proceed);
    lv_label_set_text(label_btn, LV_SYMBOL_PLAY " Proceed");
    lv_obj_center(label_btn);

    hpi_disp_set_curr_screen(SCR_SPL_SPO2_SCR2);
    hpi_show_screen(scr_spo2_scr2, m_scroll_dir);
}

void draw_scr_spo2_scr3(enum scroll_dir m_scroll_dir)
{
    scr_spo2_scr3 = lv_obj_create(NULL);
    lv_obj_add_style(scr_spo2_scr3, &style_scr_black, 0);
    lv_obj_clear_flag(scr_spo2_scr3, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    // draw_scr_common(scr_spo2);

    lv_obj_set_scrollbar_mode(scr_spo2_scr3, LV_SCROLLBAR_MODE_OFF);

    /*Create a container with COLUMN flex direction*/
    lv_obj_t *cont_col = lv_obj_create(scr_spo2_scr3);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(cont_col, LV_OBJ_FLAG_SCROLLABLE);
    //lv_obj_set_style_pad_top(cont_col, 5, LV_PART_MAIN);
    //lv_obj_set_style_pad_bottom(cont_col, 1, LV_PART_MAIN);
    lv_obj_add_style(cont_col, &style_scr_black, 0);
    // lv_obj_add_style(cont_col, &style_bg_red, 0);

    lv_obj_t *label_signal = lv_label_create(cont_col);
    lv_label_set_text(label_signal, "SpO2");

    // Draw countdown timer container
    lv_obj_t *cont_status = lv_obj_create(cont_col);
    lv_obj_set_size(cont_status, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_status, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_style(cont_status, &style_scr_black, 0);
    lv_obj_set_flex_align(cont_status, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    label_spo2_status = lv_label_create(cont_status);
    lv_label_set_text(label_spo2_status, "--");
    lv_obj_set_style_text_align(label_spo2_status, LV_TEXT_ALIGN_CENTER, 0);

    bar_spo2_progress = lv_bar_create(cont_status);
    lv_obj_set_size(bar_spo2_progress, 200, 20);

    label_spo2_progress = lv_label_create(cont_status);
    lv_label_set_text(label_spo2_progress, "--");
    lv_obj_set_style_text_align(label_spo2_progress, LV_TEXT_ALIGN_CENTER, 0);

    chart_ppg = lv_chart_create(cont_col);
    lv_obj_set_size(chart_ppg, 390, 140);
    lv_obj_set_style_bg_color(chart_ppg, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(chart_ppg, 0, LV_PART_MAIN);

    lv_obj_set_style_size(chart_ppg, 0, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(chart_ppg, 0, LV_PART_MAIN);
    lv_chart_set_point_count(chart_ppg, ECG_DISP_WINDOW_SIZE);
    lv_chart_set_div_line_count(chart_ppg, 0, 0);
    lv_chart_set_update_mode(chart_ppg, LV_CHART_UPDATE_MODE_CIRCULAR);
    lv_obj_align(chart_ppg, LV_ALIGN_CENTER, 0, -35);

    cont_spo2_val = lv_obj_create(cont_col);
    lv_obj_set_size(cont_spo2_val, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_spo2_val, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(cont_spo2_val, &style_scr_black, 0);
    lv_obj_set_flex_align(cont_spo2_val, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(cont_spo2_val, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *img_spo2 = lv_img_create(cont_spo2_val);
    lv_img_set_src(img_spo2, &icon_spo2_100);
    
    label_spo2_percent = lv_label_create(cont_spo2_val);
    lv_label_set_text(label_spo2_percent, "--");
    lv_obj_set_style_text_align(label_spo2_percent, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_style(label_spo2_percent, &style_white_large, 0);
    
    lv_obj_t *label_spo2_percent_sign = lv_label_create(cont_spo2_val);
    lv_label_set_text(label_spo2_percent_sign, " %");

    ser_ppg = lv_chart_add_series(chart_ppg, lv_palette_main(LV_PALETTE_ORANGE), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_line_width(chart_ppg, 6, LV_PART_ITEMS);

    /*label_ecg_lead_off = lv_label_create(scr_ecg);
    lv_label_set_text(label_ecg_lead_off, LV_SYMBOL_UP "\nPlace finger \non sensor \nto start ECG");
    lv_obj_align_to(label_ecg_lead_off, NULL, LV_ALIGN_CENTER, -20, -40);
    lv_obj_set_style_text_align(label_ecg_lead_off, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_style(label_ecg_lead_off, &style_lbl_orange, 0);
    lv_obj_add_flag(label_ecg_lead_off, LV_OBJ_FLAG_HIDDEN);*/

    lv_obj_t *cont_hr = lv_obj_create(cont_col);
    lv_obj_set_size(cont_hr, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_hr, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(cont_hr, &style_scr_black, 0);
    lv_obj_set_flex_align(cont_hr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    hpi_disp_set_curr_screen(SCR_SPL_SPO2_SCR3);
    hpi_show_screen(scr_spo2_scr3, m_scroll_dir);
}

void hpi_disp_update_spo2(uint8_t spo2, struct tm tm_last_update)
{
    if (label_spo2_percent == NULL)
        return;

    if (spo2 == 0)
    {
        lv_label_set_text(label_spo2_percent, "-- %");
    }
    else
    {
        lv_label_set_text_fmt(label_spo2_percent, "%d %", spo2);
    }
    lv_label_set_text_fmt(label_spo2_last_update_time, "Last updated: %02d:%02d", tm_last_update.tm_hour, tm_last_update.tm_min);
}

static void hpi_ppg_disp_add_samples(int num_samples)
{
    gx += num_samples;
}

static void hpi_ppg_disp_do_set_scale(int disp_window_size)
{
    if (gx >= (disp_window_size / 4))
    {

        lv_chart_set_range(chart_ppg, LV_CHART_AXIS_PRIMARY_Y, y_min_ppg, y_max_ppg);

        gx = 0;
        y_max_ppg = -900000;
        y_min_ppg = 900000;
    }
}

void hpi_disp_spo2_update_progress(int progress, int status, int spo2)
{
    if (label_spo2_progress == NULL)
        return;

    lv_label_set_text_fmt(label_spo2_progress, "%d %%", progress);
    lv_bar_set_value(bar_spo2_progress, progress, LV_ANIM_ON);

    if ((status == 0) || (status == 1))
    {
        lv_label_set_text(label_spo2_status, "Measuring...");
    }
    else if (status == 2)
    {
        lv_label_set_text(label_spo2_status, "Complete");
    }
    else
    {
        lv_label_set_text(label_spo2_status, "Timeout");
    }

    if(status == 2)
    {
        lv_label_set_text_fmt(label_spo2_percent, "%d", spo2);
        lv_obj_add_flag(chart_ppg, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(cont_spo2_val, LV_OBJ_FLAG_HIDDEN);
    }
}

void hpi_disp_spo2_plotPPG(struct hpi_ppg_wr_data_t ppg_sensor_sample)
{
    uint32_t *data_ppg = ppg_sensor_sample.raw_green;

    for (int i = 0; i < ppg_sensor_sample.ppg_num_samples; i++)
    {
        if (data_ppg[i] < y_min_ppg)
        {
            y_min_ppg = data_ppg[i];
        }

        if (data_ppg[i] > y_max_ppg)
        {
            y_max_ppg = data_ppg[i];
        }

        lv_chart_set_next_value(chart_ppg, ser_ppg, data_ppg[i]);

        hpi_ppg_disp_add_samples(1);
        hpi_ppg_disp_do_set_scale(SPO2_DISP_WINDOW_SIZE);
    }
}
