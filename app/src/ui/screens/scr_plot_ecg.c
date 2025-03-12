#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/dac.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>
#include <stdio.h>
#include <zephyr/smf.h>
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
static lv_obj_t *label_hr_bpm;
static lv_obj_t *label_timer;
//static lv_obj_t *label_ecg_lead_off;

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
    ser_ecg = lv_chart_add_series(chart_ecg, lv_palette_main(LV_PALETTE_YELLOW), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_line_width(chart_ecg, 6, LV_PART_ITEMS);

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

    hpi_disp_set_curr_screen(SCR_SPL_PLOT_ECG);
    hpi_show_screen_spl(scr_plot_ecg, m_scroll_dir, scr_parent);
    chart_ecg_update = true;
}

void hpi_ecg_disp_do_set_scale(int disp_window_size)
{
    if (gx >= (disp_window_size))
    {
        if (chart_ecg_update == true)
        {
            lv_chart_set_range(chart_ecg, LV_CHART_AXIS_PRIMARY_Y, y_min_ecg, y_max_ecg);
        }

        gx = 0;

        y_max_ecg = -900000;
        y_min_ecg = 900000;
    }
}

void hpi_ecg_disp_add_samples(int num_samples)
{
    gx += num_samples;
}

void hpi_ecg_disp_update_hr(int hr)
{
    if (label_ecg_hr == NULL)
        return;

    char buf[32];
    if (hr == 0)
    {
        sprintf(buf, "--");
    }
    else
    {
        sprintf(buf, "%d", hr);
    }

    lv_label_set_text(label_ecg_hr, buf);
}

void hpi_ecg_disp_update_timer(int time_left)
{
    if (label_timer == NULL)
        return;

    lv_label_set_text_fmt(label_timer, "%d" , time_left);
}

void hpi_ecg_disp_draw_plotECG(int32_t *data_ecg, int num_samples, bool ecg_lead_off)
{
    if (chart_ecg_update == true) // && ecg_lead_off == false)
    {
        for (int i = 0; i < num_samples; i++)
        {
            int32_t data_ecg_i = ((data_ecg[i] * 1000) / 5242880) * 10; // in mV// (data_ecg[i]);

            if (data_ecg_i < y_min_ecg)
            {
                y_min_ecg = data_ecg_i;
            }

            if (data_ecg_i > y_max_ecg)
            {
                y_max_ecg = data_ecg_i;
            }

            /*if (ecg_plot_hidden == true)
            {
                lv_obj_add_flag(label_ecg_lead_off, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(chart_ecg, LV_OBJ_FLAG_HIDDEN);
                ecg_plot_hidden = false;
            }*/

            // printk("E");

            lv_chart_set_next_value(chart_ecg, ser_ecg, data_ecg_i);
            hpi_ecg_disp_add_samples(1);
            hpi_ecg_disp_do_set_scale(ECG_DISP_WINDOW_SIZE);
        }
        // lv_chart_set_next_value(chart_ecg, ser_ecg, data_ecg);
        // hpi_ecg_disp_add_samples(1);
        // hpi_ecg_disp_do_set_scale(DISP_WINDOW_SIZE_ECG);
    }
    /*else if (ecg_lead_off == true)
    {
        lv_obj_add_flag(chart_ecg, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(label_ecg_lead_off, LV_OBJ_FLAG_HIDDEN);
        ecg_plot_hidden = true;
    }*/
}
