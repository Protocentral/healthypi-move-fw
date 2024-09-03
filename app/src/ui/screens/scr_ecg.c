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

#include "display_module.h"
#include "sampling_module.h"

#include "ui/move_ui.h"

lv_obj_t *scr_ecg;

// GUI Charts
static lv_obj_t *chart_ecg;
static lv_chart_series_t *ser_ecg;

static lv_obj_t *label_ecg_hr;
static lv_obj_t *label_ecg_lead_off;

static bool chart_ecg_update = true;
static float y_max_ecg = 0;
static float y_min_ecg = 10000;

static bool ecg_plot_hidden = false;

static float gx = 0;

// Externs
extern lv_style_t style_lbl_orange;
extern lv_style_t style_lbl_white;
extern lv_style_t style_lbl_red;
extern lv_style_t style_lbl_white_small;

extern int curr_screen;

#define DISP_WINDOW_SIZE_ECG 390

void draw_scr_ecg(enum scroll_dir m_scroll_dir)
{
    scr_ecg = lv_obj_create(NULL);
    draw_header_minimal(scr_ecg, 10);
    draw_bg(scr_ecg);

    // Create Chart 1 - ECG
    chart_ecg = lv_chart_create(scr_ecg);
    lv_obj_set_size(chart_ecg, 390, 150);
    lv_obj_set_style_bg_color(chart_ecg, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(chart_ecg, 0, LV_PART_MAIN);

    lv_obj_set_style_size(chart_ecg, 0, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(chart_ecg, 0, LV_PART_MAIN);
    lv_chart_set_point_count(chart_ecg, DISP_WINDOW_SIZE_ECG);
    // lv_chart_set_type(chart_ecg, LV_CHART_TYPE_LINE);   /*Show lines and points too*
    lv_chart_set_range(chart_ecg, LV_CHART_AXIS_PRIMARY_Y, -1000, 1000);
    // lv_chart_set_range(chart_ecg, LV_CHART_AXIS_SECONDARY_Y, 0, 1000);
    lv_chart_set_div_line_count(chart_ecg, 0, 0);
    lv_chart_set_update_mode(chart_ecg, LV_CHART_UPDATE_MODE_CIRCULAR);
    // lv_style_set_border_width(&styles->bg, LV_STATE_DEFAULT, BORDER_WIDTH);
    lv_obj_align(chart_ecg, LV_ALIGN_CENTER, 0, -35);
    ser_ecg = lv_chart_add_series(chart_ecg, lv_palette_main(LV_PALETTE_YELLOW), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_line_width(chart_ecg, 3, LV_PART_ITEMS);

    // HR Number label
    label_ecg_hr = lv_label_create(scr_ecg);
    lv_label_set_text(label_ecg_hr, "--");
    lv_obj_align_to(label_ecg_hr, NULL, LV_ALIGN_CENTER, -10, 50);
    lv_obj_add_style(label_ecg_hr, &style_lbl_white, 0);

    // HR Sub bpm label
    lv_obj_t *label_hr_sub = lv_label_create(scr_ecg);
    lv_label_set_text(label_hr_sub, " bpm");
    lv_obj_align_to(label_hr_sub, label_ecg_hr, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);

    // HR caption label
    lv_obj_t *label_hr_cap = lv_label_create(scr_ecg);
    lv_label_set_text(label_hr_cap, "HR");
    lv_obj_align_to(label_hr_cap, label_ecg_hr, LV_ALIGN_OUT_TOP_MID, -5, -5);
    lv_obj_add_style(label_hr_cap, &style_lbl_red, 0);

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

    /*LV_IMG_DECLARE(heart);
    lv_obj_t *img1 = lv_img_create(scr_ecg);
    lv_img_set_src(img1, &heart);
    lv_obj_set_size(img1, 35, 33);
    lv_obj_align_to(img1, label_hr, LV_ALIGN_OUT_LEFT_MID, 0, 0);*/

    curr_screen = SCR_PLOT_ECG;
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
            int32_t data_ecg_i = (data_ecg[i]/1000);

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
            hpi_ecg_disp_do_set_scale(DISP_WINDOW_SIZE_ECG);
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