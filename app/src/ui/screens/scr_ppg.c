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

// #include "sys_sm_module.h"
// #include "
#include "display_module.h"
#include "sampling_module.h"

#include "sys_sm_module.h"

#include "ui/move_ui.h"

lv_obj_t *scr_ppg;

// GUI Charts
static lv_obj_t *chart_ppg;
static lv_chart_series_t *ser_ppg;

// GUI Labels
static lv_obj_t *label_ppg_hr;
static lv_obj_t *label_ppg_spo2;

static bool chart_ppg_update = true;

static float y_max_ppg = 0;
static float y_min_ppg = 10000;

float y2_max = 0;
float y2_min = 10000;

float y3_max = 0;
float y3_min = 10000;

static float gx = 0;

extern lv_style_t style_lbl_white;
extern lv_style_t style_lbl_red;
extern lv_style_t style_lbl_white_small;

extern int curr_screen;

void draw_scr_ppg(enum scroll_dir m_scroll_dir)
{
    scr_ppg = lv_obj_create(NULL);
    lv_obj_clear_flag(scr_ppg, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    draw_bg(scr_ppg);
    draw_header_minimal(scr_ppg);

    // Create Chart 1 - ECG
    chart_ppg = lv_chart_create(scr_ppg);
    lv_obj_set_size(chart_ppg, 200, 100);
    lv_obj_set_style_bg_color(chart_ppg, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(chart_ppg, 50, LV_PART_MAIN);

    lv_obj_set_style_size(chart_ppg, 0, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(chart_ppg, 0, LV_PART_MAIN);
    lv_chart_set_point_count(chart_ppg, PPG_DISP_WINDOW_SIZE);
    // lv_chart_set_type(chart_ppg, LV_CHART_TYPE_LINE);
    lv_chart_set_range(chart_ppg, LV_CHART_AXIS_PRIMARY_Y, -1000, 1000);
    // lv_chart_set_range(chart_ppg, LV_CHART_AXIS_SECONDARY_Y, 0, 1000);
    lv_chart_set_div_line_count(chart_ppg, 0, 0);
    lv_chart_set_update_mode(chart_ppg, LV_CHART_UPDATE_MODE_CIRCULAR);
    // lv_style_set_border_width(&styles->bg, LV_STATE_DEFAULT, BORDER_WIDTH);
    lv_obj_align(chart_ppg, LV_ALIGN_CENTER, 0, -35);
    ser_ppg = lv_chart_add_series(chart_ppg, lv_palette_main(LV_PALETTE_ORANGE), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_line_width(chart_ppg, 3, LV_PART_ITEMS);

    // HR Number label
    label_ppg_hr = lv_label_create(scr_ppg);
    lv_label_set_text(label_ppg_hr, "--");
    lv_obj_align_to(label_ppg_hr, NULL, LV_ALIGN_CENTER, -50, 50);
    lv_obj_add_style(label_ppg_hr, &style_lbl_white, 0);

    // HR Sub bpm label
    lv_obj_t *label_hr_sub = lv_label_create(scr_ppg);
    lv_label_set_text(label_hr_sub, " bpm");
    lv_obj_align_to(label_hr_sub, label_ppg_hr, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);

    // HR caption label
    lv_obj_t *label_hr_cap = lv_label_create(scr_ppg);
    lv_label_set_text(label_hr_cap, "HR");
    lv_obj_align_to(label_hr_cap, label_ppg_hr, LV_ALIGN_OUT_TOP_MID, -5, -5);
    lv_obj_add_style(label_hr_cap, &style_lbl_red, 0);

    /*LV_IMG_DECLARE(heart);
    lv_obj_t *img1 = lv_img_create(scr_ppg);
    lv_img_set_src(img1, &heart);
    lv_obj_set_size(img1, 35, 33);
    lv_obj_align_to(img1, label_ppg_hr, LV_ALIGN_OUT_LEFT_MID, -5, 0);*/

    // SpO2 Number label
    label_ppg_spo2 = lv_label_create(scr_ppg);
    lv_label_set_text(label_ppg_spo2, "--");
    lv_obj_align_to(label_ppg_spo2, NULL, LV_ALIGN_CENTER, 30, 50);
    lv_obj_add_style(label_ppg_spo2, &style_lbl_white, 0);

    // SpO2 Sub % label
    lv_obj_t *label_spo2_sub = lv_label_create(scr_ppg);
    lv_label_set_text(label_spo2_sub, " %");
    lv_obj_align_to(label_spo2_sub, label_ppg_spo2, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);

    // SpO2 caption label
    lv_obj_t *label_spo2_cap = lv_label_create(scr_ppg);
    lv_label_set_text(label_spo2_cap, "SpO2");
    lv_obj_align_to(label_spo2_cap, label_ppg_spo2, LV_ALIGN_OUT_TOP_MID, -5, -5);
    lv_obj_add_style(label_spo2_cap, &style_lbl_red, 0);

    // Bottom signal label
    lv_obj_t *label_signal = lv_label_create(scr_ppg);
    lv_label_set_text(label_signal, "PPG");
    lv_obj_align(label_signal, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_add_style(label_signal, &style_lbl_white_small, 0);

    /*LV_IMG_DECLARE(o2);
    lv_obj_t *img2 = lv_img_create(scr_ppg);
    lv_img_set_src(img2, &o2);
    lv_obj_set_size(img2, 22, 35);
    lv_obj_align_to(img2, label_ppg_spo2, LV_ALIGN_OUT_LEFT_MID, -5, 0);*/

    curr_screen = SCR_PLOT_PPG;

    // lv_obj_add_event_cb(scr_ppg, ppg_screen_event, LV_EVENT_GESTURE, NULL);
    hpi_show_screen(scr_ppg, m_scroll_dir);
}

void hpi_ppg_disp_update_hr(int hr)
{
    if (label_ppg_hr == NULL)
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

    lv_label_set_text(label_ppg_hr, buf);
}

void hpi_ppg_disp_update_spo2(int spo2)
{
    if (label_ppg_spo2 == NULL)
        return;

    char buf[32];
    if (spo2 == 0)
    {
        sprintf(buf, "--");
    }
    else
    {
        sprintf(buf, "%d", spo2);
    }
    // sprintf(buf, "%d", spo2);
    lv_label_set_text(label_ppg_spo2, buf);
}

void hpi_disp_ppg_draw_plotPPG(float data_ppg)
{
    if (chart_ppg_update == true)
    {
        if (data_ppg < y_min_ppg)
        {
            y_min_ppg = data_ppg;
        }

        if (data_ppg > y_max_ppg)
        {
            y_max_ppg = data_ppg;
        }

        // printk("E");
        lv_chart_set_next_value(chart_ppg, ser_ppg, data_ppg);

        gx += 1;
        hpi_ppg_disp_do_set_scale(PPG_DISP_WINDOW_SIZE);
    }
}

void hpi_ppg_disp_do_set_scale(int disp_window_size)
{
    if (gx >= (disp_window_size))
    {
        if (chart_ppg_update == true)
            lv_chart_set_range(chart_ppg, LV_CHART_AXIS_PRIMARY_Y, y_min_ppg, y_max_ppg);

        gx = 0;

        y_max_ppg = -900000;
        y_min_ppg = 900000;
    }
}