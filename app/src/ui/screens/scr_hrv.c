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

lv_obj_t *scr_hrv;

// GUI Charts
static lv_obj_t *chart_hrv;
static lv_chart_series_t *ser_hrv;

// GUI Labels
static lv_obj_t *label_hrv_rri;
static lv_obj_t *label_hrv_sdnn;

static bool chart_hrv_update = true;

static float y_max_hrv = 0;
static float y_min_hrv = 10000;

static float gx = 0;

extern lv_style_t style_lbl_white;
extern lv_style_t style_lbl_red;
extern lv_style_t style_lbl_white_small;

extern int curr_screen;

void draw_scr_hrv(enum scroll_dir m_scroll_dir)
{
    scr_hrv = lv_obj_create(NULL);
    lv_obj_clear_flag(scr_hrv, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    draw_bg(scr_hrv);
    draw_header_minimal(scr_hrv);

    // Create Chart 1 - HRV
    chart_hrv = lv_chart_create(scr_hrv);
    lv_obj_set_size(chart_hrv, 200, 100);
    lv_obj_set_style_bg_color(chart_hrv, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(chart_hrv, 50, LV_PART_MAIN);

    lv_obj_set_style_size(chart_hrv, 0, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(chart_hrv, 0, LV_PART_MAIN);
    lv_chart_set_point_count(chart_hrv, 60);
    // lv_chart_set_type(chart_hrv, LV_CHART_TYPE_LINE);
    lv_chart_set_range(chart_hrv, LV_CHART_AXIS_PRIMARY_Y, -1000, 1000);
    // lv_chart_set_range(chart_hrv, LV_CHART_AXIS_SECONDARY_Y, 0, 1000);
    lv_chart_set_div_line_count(chart_hrv, 0, 0);
    lv_chart_set_update_mode(chart_hrv, LV_CHART_UPDATE_MODE_CIRCULAR);
    // lv_style_set_border_width(&styles->bg, LV_STATE_DEFAULT, BORDER_WIDTH);
    lv_obj_align(chart_hrv, LV_ALIGN_CENTER, 0, -35);
    ser_hrv = lv_chart_add_series(chart_hrv, lv_palette_main(LV_PALETTE_ORANGE), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_line_width(chart_hrv, 3, LV_PART_ITEMS);

    // RR Int Number label
    label_hrv_rri = lv_label_create(scr_hrv);
    lv_label_set_text(label_hrv_rri, "--");
    lv_obj_align_to(label_hrv_rri,  NULL, LV_ALIGN_CENTER, -50, 50);
    lv_obj_add_style(label_hrv_rri, &style_lbl_white, 0);

    // RR Int caption label
    lv_obj_t *label_hr_cap = lv_label_create(scr_hrv);
    lv_label_set_text(label_hr_cap, "RR Int");
    lv_obj_align_to(label_hr_cap, label_hrv_rri, LV_ALIGN_OUT_TOP_MID, -5, -5);
    lv_obj_add_style(label_hr_cap, &style_lbl_red, 0);

    // RR Int bpm label
    lv_obj_t *label_hr_sub = lv_label_create(scr_hrv);
    lv_label_set_text(label_hr_sub, "ms");
    lv_obj_align_to(label_hr_sub, label_hrv_rri, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);

    // SDNN Number label
    label_hrv_sdnn = lv_label_create(scr_hrv);
    lv_label_set_text(label_hrv_sdnn, "--");
    lv_obj_align_to(label_hrv_sdnn, NULL, LV_ALIGN_CENTER, 30, 50);
    lv_obj_add_style(label_hrv_sdnn, &style_lbl_white, 0);

    // SDNN caption label
    lv_obj_t *label_sdnn_cap = lv_label_create(scr_hrv);
    lv_label_set_text(label_sdnn_cap, "SDNN");
    lv_obj_align_to(label_sdnn_cap, label_hrv_sdnn, LV_ALIGN_OUT_TOP_MID, -5, -5);
    lv_obj_add_style(label_sdnn_cap, &style_lbl_red, 0);

    // SDNN bpm label
    lv_obj_t *label_sdnn_sub = lv_label_create(scr_hrv);
    lv_label_set_text(label_sdnn_sub, "ms");
    lv_obj_align_to(label_sdnn_sub, label_hrv_sdnn, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);


    // Bottom signal label
    lv_obj_t *label_signal = lv_label_create(scr_hrv);
    lv_label_set_text(label_signal, "HRV");
    lv_obj_align(label_signal, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_add_style(label_signal, &style_lbl_white_small, 0);

    curr_screen = SCR_PLOT_HRV;

    // Add screen to display
    hpi_show_screen(scr_hrv, m_scroll_dir);
}

static void hpi_hrv_disp_do_set_scale(int disp_window_size)
{
    if (gx >= (disp_window_size))
    {
        if (chart_hrv_update == true)
            lv_chart_set_range(chart_hrv, LV_CHART_AXIS_PRIMARY_Y, y_min_hrv, y_max_hrv);

        gx = 0;

        y_max_hrv = -900000;
        y_min_hrv = 900000;
    }
}

void hpi_disp_hrv_draw_plot_rtor(float rtor)
{
    if (chart_hrv_update == true)
    {
        if (rtor < y_min_hrv)
        {
            y_min_hrv = rtor;
        }

        if (rtor > y_max_hrv)
        {
            y_max_hrv = rtor;
        }

        // printk("E");
        lv_chart_set_next_value(chart_hrv, ser_hrv, rtor);

        gx += 1;
        hpi_hrv_disp_do_set_scale(PPG_DISP_WINDOW_SIZE);
    }
}

void hpi_disp_hrv_update_rtor(int rtor)
{
     if (label_hrv_rri == NULL)
        return;

    char buf[32];
    if (rtor == 0)
    {
        sprintf(buf, "--");
    }
    else
    {
        sprintf(buf, "%d", rtor);
    }

    lv_label_set_text(label_hrv_rri, buf);

}

void hpi_disp_hrv_update_sdnn(int sdnn)
{
    if (label_hrv_sdnn == NULL)
        return;

    char buf[32];
    if (sdnn == 0)
    {
        sprintf(buf, "--");
    }
    else
    {
        sprintf(buf, "%d", sdnn);
    }

    lv_label_set_text(label_hrv_sdnn, buf);
}