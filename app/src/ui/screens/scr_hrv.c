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
extern lv_style_t style_red_medium;

static void anim_x_cb(void * var, int32_t v)
{
    lv_obj_set_x(var, v);
}

static void anim_size_cb(void * var, int32_t v)
{
    lv_obj_set_size(var, v, v);
}

void draw_scr_hrv(enum scroll_dir m_scroll_dir)
{
    scr_hrv = lv_obj_create(NULL);
    lv_obj_clear_flag(scr_hrv, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    draw_bg(scr_hrv);
    draw_header_minimal(scr_hrv,10);

    // Create Chart 1 - HRV
    chart_hrv = lv_chart_create(scr_hrv);
    lv_obj_set_size(chart_hrv, 200, 30);
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
    //5rjdfedjgfxz lv_obj_align(chart_hrv, LV_ALIGN_CENTER, 0, -35);
    ser_hrv = lv_chart_add_series(chart_hrv, lv_palette_main(LV_PALETTE_ORANGE), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_line_width(chart_hrv, 3, LV_PART_ITEMS);

    // RR Int Number label
    label_hrv_rri = lv_label_create(scr_hrv);
    lv_label_set_text(label_hrv_rri, "--");
    lv_obj_align_to(label_hrv_rri, NULL, LV_ALIGN_CENTER, -50, 50);


    // RR Int caption label
    /*lv_obj_t *label_hr_cap = lv_label_create(scr_hrv);
    lv_label_set_text(label_hr_cap, "RR Int");
    lv_obj_align_to(label_hr_cap, label_hrv_rri, LV_ALIGN_OUT_TOP_MID, -5, -5);
    lv_obj_add_style(label_hr_cap, &style_lbl_red, 0);

    // RR Int bpm label
    lv_obj_t *label_hr_sub = lv_label_create(scr_hrv);
    lv_label_set_text(label_hr_sub, "ms");
    lv_obj_align_to(label_hr_sub, label_hrv_rri, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    */

    // SDNN Number label
    label_hrv_sdnn = lv_label_create(scr_hrv);
    lv_label_set_text(label_hrv_sdnn, "--");
    lv_obj_align_to(label_hrv_sdnn, NULL, LV_ALIGN_CENTER, 30, 50);

    // SDNN caption label
    /*lv_obj_t *label_sdnn_cap = lv_label_create(scr_hrv);
    lv_label_set_text(label_sdnn_cap, "SDNN");
    lv_obj_align_to(label_sdnn_cap, label_hrv_sdnn, LV_ALIGN_OUT_TOP_MID, -5, -5);
    lv_obj_add_style(label_sdnn_cap, &style_lbl_red, 0);

    // SDNN bpm label
    lv_obj_t *label_sdnn_sub = lv_label_create(scr_hrv);
    lv_label_set_text(label_sdnn_sub, "ms");
    lv_obj_align_to(label_sdnn_sub, label_hrv_sdnn, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    */

    // Bottom signal label
    lv_obj_t *label_signal = lv_label_create(scr_hrv);
    lv_label_set_text(label_signal, "HRV");
    lv_obj_align(label_signal, LV_ALIGN_BOTTOM_MID, 0, -5);

    lv_obj_t *obj = lv_obj_create(scr_hrv);
    lv_obj_set_style_bg_color(obj, lv_palette_main(LV_PALETTE_DEEP_PURPLE), 0);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);

    lv_obj_align(obj, LV_ALIGN_LEFT_MID, 10, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, 70, 200);
    lv_anim_set_time(&a, 1000);
    lv_anim_set_playback_delay(&a, 100);
    lv_anim_set_playback_time(&a, 400);
    lv_anim_set_repeat_delay(&a, 500);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_bounce);

    lv_anim_set_exec_cb(&a, anim_size_cb);
    lv_anim_start(&a);
    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_set_values(&a, 10, 300);
    lv_anim_start(&a);

    hpi_disp_set_curr_screen(SCR_SPL_PLOT_HRV);

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