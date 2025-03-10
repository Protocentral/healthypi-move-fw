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

lv_obj_t *scr_eda;

static bool chart_eda_update = true;

static lv_obj_t *chart_eda;
static lv_chart_series_t *ser_eda;

static float y_max_eda = 0;
static float y_min_eda = 10000;

static float gx = 0;

// Externs
extern lv_style_t style_red_medium;

void draw_scr_pre(enum scroll_dir m_scroll_dir)
{
    scr_eda = lv_obj_create(NULL);
    draw_bg(scr_eda);
    draw_header_minimal(scr_eda, 10);

    lv_obj_t *label_signal = lv_label_create(scr_eda);
    lv_label_set_text(label_signal, "PRE-RELEASE FIRMWARE");
    lv_obj_set_style_text_color(label_signal, lv_palette_darken(LV_PALETTE_RED, 1), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(label_signal, LV_ALIGN_TOP_MID, 0, 75);

    lv_obj_t *label_install = lv_label_create(scr_eda);
    lv_label_set_recolor(label_install, true);     
    lv_label_set_text(label_install, "This is pre-release firmware.\nPlease update your firmware through the app.\n\nVisit #42A5F5 move.protocentral.com # \n for details.");
    lv_obj_set_style_text_font(label_install, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_width(label_install, 380);
    lv_obj_set_style_text_align(label_install, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(label_install, LV_ALIGN_CENTER, 0, 20);

    /*chart_eda = lv_chart_create(scr_eda);
    lv_obj_set_size(chart_eda, 390, 150);
    lv_obj_set_style_bg_color(chart_eda, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(chart_eda, 50, LV_PART_MAIN);
    lv_obj_set_style_size(chart_eda, 0, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(chart_eda, 0, LV_PART_MAIN);
    lv_chart_set_point_count(chart_eda, DISP_WINDOW_SIZE_EDA);
    lv_chart_set_range(chart_eda, LV_CHART_AXIS_PRIMARY_Y, -1000, 1000);
    lv_chart_set_div_line_count(chart_eda, 0, 0);
    lv_chart_set_update_mode(chart_eda, LV_CHART_UPDATE_MODE_CIRCULAR);
    lv_obj_align(chart_eda, LV_ALIGN_CENTER, 0, -35);
    ser_eda = lv_chart_add_series(chart_eda, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_line_width(chart_eda, 3, LV_PART_ITEMS);*/
   

    hpi_disp_set_curr_screen(SCR_PLOT_PRE);
    hpi_show_screen(scr_eda, m_scroll_dir);
}

static void hpi_eda_disp_do_set_scale(int disp_window_size)
{
    if (gx >= (disp_window_size))
    {
        if (chart_eda_update == true)
            lv_chart_set_range(chart_eda, LV_CHART_AXIS_PRIMARY_Y, y_min_eda, y_max_eda);

        gx = 0;

        y_max_eda = -900000;
        y_min_eda = 900000;
    }
}

static void hpi_eda_disp_add_samples(int num_samples)
{
    gx += num_samples;
}

void hpi_eda_disp_draw_plotEDA(int32_t *data_eda, int num_samples, bool eda_lead_off)
{
    if (chart_eda_update == true)
    {
        for (int i = 0; i < num_samples; i++)
        {
            int32_t data_eda_i = ((data_eda[i] / 1000));

            if (data_eda_i < y_min_eda)
            {
                y_min_eda = data_eda_i;
            }

            if (data_eda_i > y_max_eda)
            {
                y_max_eda = data_eda_i;
            }

            lv_chart_set_next_value(chart_eda, ser_eda, data_eda_i);
            hpi_eda_disp_add_samples(1);
            hpi_eda_disp_do_set_scale(DISP_WINDOW_SIZE_EDA);
        }
    }
}
