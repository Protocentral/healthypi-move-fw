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

lv_obj_t *scr_ecg;

// GUI Charts
static lv_obj_t *chart_ecg;
static lv_chart_series_t *ser_ecg;

static lv_obj_t *label_ecg_hr;
static lv_obj_t *label_ecg_lead_off;

static lv_obj_t *btn_hr_settings;

static bool chart_ecg_update = true;
static float y_max_ecg = 0;
static float y_min_ecg = 10000;

// static bool ecg_plot_hidden = false;

static float gx = 0;

// Externs
extern lv_style_t style_lbl_orange;
extern lv_style_t style_lbl_white;
extern lv_style_t style_red_medium;
extern lv_style_t style_lbl_white_small;



static void scr_hr_settings_btn_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        lv_obj_add_flag(btn_hr_settings, LV_OBJ_FLAG_HIDDEN);
        hw_max30001_ecg_enable(true);
        //lv_example_msgbox_2();
    }
}

void hpi_disp_scr_ecg_exit_handler(void)
{
    hw_max30001_ecg_enable(false);
}

void draw_scr_ecg(enum scroll_dir m_scroll_dir)
{
    scr_ecg = lv_obj_create(NULL);
    draw_header_minimal(scr_ecg, 10);
    // draw_bg(scr_ecg);

    // Create Chart 1 - ECG
    chart_ecg = lv_chart_create(scr_ecg);
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
    lv_obj_set_style_line_width(chart_ecg, 4, LV_PART_ITEMS);

    // HR Number label
    label_ecg_hr = lv_label_create(scr_ecg);
    lv_label_set_text(label_ecg_hr, "--");
    lv_obj_align_to(label_ecg_hr, NULL, LV_ALIGN_CENTER, -10, 60);
    lv_obj_add_style(label_ecg_hr, &style_lbl_white, 0);

    // HR Sub bpm label
    lv_obj_t *label_hr_sub = lv_label_create(scr_ecg);
    lv_label_set_text(label_hr_sub, " bpm");
    lv_obj_align_to(label_hr_sub, label_ecg_hr, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);

    // HR caption label
    lv_obj_t *label_hr_cap = lv_label_create(scr_ecg);
    lv_label_set_text(label_hr_cap, "HR");
    lv_obj_align_to(label_hr_cap, label_ecg_hr, LV_ALIGN_OUT_LEFT_MID, -5, -5);
    lv_obj_add_style(label_hr_cap, &style_red_medium, 0);

    // Bottom signal label
    lv_obj_t *label_signal = lv_label_create(scr_ecg);
    lv_label_set_text(label_signal, "ECG");
    lv_obj_align(label_signal, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_add_style(label_signal, &style_lbl_white_small, 0);

    label_ecg_lead_off = lv_label_create(scr_ecg);
    lv_label_set_text(label_ecg_lead_off, LV_SYMBOL_UP "\nPlace finger \non sensor \nto start ECG");
    lv_obj_align_to(label_ecg_lead_off, NULL, LV_ALIGN_CENTER, -20, -40);
    lv_obj_set_style_text_align(label_ecg_lead_off, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_style(label_ecg_lead_off, &style_lbl_orange, 0);
    lv_obj_add_flag(label_ecg_lead_off, LV_OBJ_FLAG_HIDDEN);

    btn_hr_settings = lv_btn_create(scr_ecg);
    lv_obj_add_event_cb(btn_hr_settings, scr_hr_settings_btn_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_align(btn_hr_settings, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_height(btn_hr_settings, 80);

    lv_obj_t *label_btn_bpt_measure = lv_label_create(btn_hr_settings);
    lv_label_set_text(label_btn_bpt_measure, LV_SYMBOL_PLAY " Measure ECG");
    lv_obj_center(label_btn_bpt_measure);

    hpi_disp_set_curr_screen(SCR_PLOT_ECG);
    hpi_show_screen(scr_ecg, m_scroll_dir);
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

void hpi_ecg_disp_add_samples(int num_samples)
{
    gx += num_samples;
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