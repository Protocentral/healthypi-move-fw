#include <zephyr/kernel.h>
#include <lvgl.h>
#include <stdio.h>
#include <zephyr/logging/log.h>

#include "hpi_common_types.h"
#include "hw_module.h"
#include "ui/move_ui.h"

lv_obj_t *scr_bpt_scr4;

static lv_obj_t *chart_bpt_ppg;
static lv_chart_series_t *ser_bpt_ppg;

static lv_obj_t *label_hr_bpm;
static lv_obj_t *bar_bpt_progress;
static lv_obj_t *label_progress;

static float y_max_ppg = 0;
static float y_min_ppg = 10000;

static float gx = 0;

// Externs
extern lv_style_t style_lbl_orange;
extern lv_style_t style_red_medium;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;

void draw_scr_bpt_scr4(enum scroll_dir m_scroll_dir)
{
    scr_bpt_scr4 = lv_obj_create(NULL);
    draw_scr_common(scr_bpt_scr4);

    /*Create a container with COLUMN flex direction*/
    lv_obj_t *cont_col = lv_obj_create(scr_bpt_scr4);
    lv_obj_clear_flag(cont_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    // lv_obj_set_width(cont_col, lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_col, -1, LV_PART_SCROLLBAR);
    lv_obj_add_style(cont_col, &style_scr_black, 0);

    lv_obj_t *label_signal = lv_label_create(cont_col);
    lv_label_set_text(label_signal, "Blood Pressure");

    lv_obj_t *lbl_measure = lv_label_create(cont_col);
    lv_label_set_text(lbl_measure, "Measuring...");

    // Draw Progress bar
    bar_bpt_progress = lv_bar_create(cont_col);
    lv_obj_set_size(bar_bpt_progress, 200, 5);
    lv_bar_set_value(bar_bpt_progress, 50, LV_ANIM_OFF);

    // Draw Progress bar label
    label_progress = lv_label_create(cont_col);
    lv_label_set_text(label_progress, "50%");
    lv_obj_align_to(label_progress, bar_bpt_progress, LV_ALIGN_OUT_BOTTOM_MID, 0, 3);

    // Create Chart 1 - ECG
    chart_bpt_ppg = lv_chart_create(cont_col);
    lv_obj_set_size(chart_bpt_ppg, 390, 140);
    lv_obj_set_style_bg_color(chart_bpt_ppg, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(chart_bpt_ppg, 0, LV_PART_MAIN);

    lv_obj_set_style_size(chart_bpt_ppg, 0, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(chart_bpt_ppg, 0, LV_PART_MAIN);
    lv_chart_set_point_count(chart_bpt_ppg, ECG_DISP_WINDOW_SIZE);
    lv_chart_set_div_line_count(chart_bpt_ppg, 0, 0);
    lv_chart_set_update_mode(chart_bpt_ppg, LV_CHART_UPDATE_MODE_CIRCULAR);
    lv_obj_align(chart_bpt_ppg, LV_ALIGN_CENTER, 0, -35);
    ser_bpt_ppg = lv_chart_add_series(chart_bpt_ppg, lv_palette_darken(LV_PALETTE_ORANGE, 2), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_line_width(chart_bpt_ppg, 6, LV_PART_ITEMS);

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

    // Draw BPM
    lv_obj_t *img1 = lv_img_create(cont_hr);
    lv_img_set_src(img1, &img_heart_35);
    label_hr_bpm = lv_label_create(cont_hr);
    lv_label_set_text(label_hr_bpm, "00");
    lv_obj_add_style(label_hr_bpm, &style_white_medium, 0);
    lv_obj_t *label_hr_sub = lv_label_create(cont_hr);
    lv_label_set_text(label_hr_sub, " bpm");

    hpi_disp_set_curr_screen(SCR_SPL_BPT_SCR4);
    hpi_show_screen_spl(scr_bpt_scr4, m_scroll_dir, SCR_BPT);
}

void hpi_disp_bpt_update_progress(int progress)
{
    if (label_progress == NULL || bar_bpt_progress == NULL)
    {
        return;
    }

    lv_bar_set_value(bar_bpt_progress, progress, LV_ANIM_OFF);
    lv_label_set_text_fmt(label_progress, "Progress: %d %%", progress);

    if (progress == 100)
    {
        // lv_obj_add_flag(chart_bpt, LV_OBJ_FLAG_HIDDEN);
        // lv_obj_clear_flag(label_cal_status, LV_OBJ_FLAG_HIDDEN);

        // lv_obj_add_flag(btn_bpt_cal_start, LV_OBJ_FLAG_HIDDEN);
        // lv_obj_clear_flag(btn_bpt_cal_exit, LV_OBJ_FLAG_HIDDEN);
    }
}

static void hpi_bpt_disp_do_set_scale(int disp_window_size)
{
    if (gx >= (disp_window_size))
    {

        lv_chart_set_range(chart_bpt_ppg, LV_CHART_AXIS_PRIMARY_Y, y_min_ppg, y_max_ppg);

        gx = 0;

        y_max_ppg = -900000;
        y_min_ppg = 900000;
    }
}

static void hpi_bpt_disp_add_samples(int num_samples)
{
    gx += num_samples;
}

void hpi_disp_bpt_draw_plotPPG(struct hpi_ppg_fi_data_t ppg_sensor_sample)
{
    uint32_t *data_ppg = ppg_sensor_sample.raw_red;

    uint16_t n_sample = ppg_sensor_sample.ppg_num_samples;

    for (int i = 0; i < n_sample; i++)
    {
        float data_ppg_i = (float)(data_ppg[i] * 1.000); // * 0.100);

        if (data_ppg_i == 0)
        {
            return;
        }

        if (data_ppg_i < y_min_ppg)
        {
            y_min_ppg = data_ppg_i;
        }

        if (data_ppg_i > y_max_ppg)
        {
            y_max_ppg = data_ppg_i;
        }

        lv_chart_set_next_value(chart_bpt_ppg, ser_bpt_ppg, data_ppg_i);

        hpi_bpt_disp_add_samples(1);
        hpi_bpt_disp_do_set_scale(BPT_DISP_WINDOW_SIZE);
    }
}