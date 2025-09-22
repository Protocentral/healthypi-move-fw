/*
 * HealthyPi Move
 * 
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Author: Ashwin Whitchurch, Protocentral Electronics
 * Contact: ashwin@protocentral.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


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

// Externs - Modern style system
extern lv_style_t style_body_medium;
extern lv_style_t style_numeric_large;
extern lv_style_t style_caption;

static void anim_x_cb(void * var, int32_t v)
{
    lv_obj_set_x(var, v);
}

static void anim_size_cb(void * var, int32_t v)
{
    lv_obj_set_size(var, v, v);
}

void draw_scr_hrv(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_hrv = lv_obj_create(NULL);
    // AMOLED OPTIMIZATION: Pure black background for power efficiency
    lv_obj_set_style_bg_color(scr_hrv, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_hrv, LV_OBJ_FLAG_SCROLLABLE);

    // CIRCULAR AMOLED-OPTIMIZED HRV SCREEN
    // Display center: (195, 195), Usable radius: ~185px
    
    // Screen title - clean and simple positioning
    lv_obj_t *label_title = lv_label_create(scr_hrv);
    lv_label_set_text(label_title, "Heart Rate Variability");
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 40);  // Centered at top, clear of arc
    lv_obj_add_style(label_title, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);

    // CENTRAL AREA: HRV Chart - modernized for circular display
    chart_hrv = lv_chart_create(scr_hrv);
    lv_obj_set_size(chart_hrv, 300, 120);  // Larger chart for better visibility
    lv_obj_align(chart_hrv, LV_ALIGN_CENTER, 0, -20);  // Slightly above center
    lv_obj_set_style_bg_color(chart_hrv, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(chart_hrv, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(chart_hrv, 0, LV_PART_MAIN);
    lv_chart_set_point_count(chart_hrv, 60);
    lv_chart_set_type(chart_hrv, LV_CHART_TYPE_LINE);
    lv_chart_set_range(chart_hrv, LV_CHART_AXIS_PRIMARY_Y, -1000, 1000);
    lv_chart_set_div_line_count(chart_hrv, 0, 0);
    lv_chart_set_update_mode(chart_hrv, LV_CHART_UPDATE_MODE_CIRCULAR);
    ser_hrv = lv_chart_add_series(chart_hrv, lv_color_hex(COLOR_WARNING_AMBER), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_line_width(chart_hrv, 3, LV_PART_ITEMS);

    // LOWER LEFT: RR Interval Value Display
    label_hrv_rri = lv_label_create(scr_hrv);
    lv_label_set_text(label_hrv_rri, "--");
    lv_obj_align(label_hrv_rri, LV_ALIGN_CENTER, -60, 80);  // Lower left position
    lv_obj_add_style(label_hrv_rri, &style_numeric_large, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_hrv_rri, lv_color_white(), LV_PART_MAIN);

    // RR Interval caption
    lv_obj_t *label_rri_caption = lv_label_create(scr_hrv);
    lv_label_set_text(label_rri_caption, "RR Int (ms)");
    lv_obj_align_to(label_rri_caption, label_hrv_rri, LV_ALIGN_OUT_TOP_MID, 0, -5);
    lv_obj_add_style(label_rri_caption, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_rri_caption, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);

    // LOWER RIGHT: SDNN Value Display  
    label_hrv_sdnn = lv_label_create(scr_hrv);
    lv_label_set_text(label_hrv_sdnn, "--");
    lv_obj_align(label_hrv_sdnn, LV_ALIGN_CENTER, 60, 80);  // Lower right position
    lv_obj_add_style(label_hrv_sdnn, &style_numeric_large, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_hrv_sdnn, lv_color_white(), LV_PART_MAIN);

    // SDNN caption
    lv_obj_t *label_sdnn_caption = lv_label_create(scr_hrv);
    lv_label_set_text(label_sdnn_caption, "SDNN (ms)");
    lv_obj_align_to(label_sdnn_caption, label_hrv_sdnn, LV_ALIGN_OUT_TOP_MID, 0, -5);
    lv_obj_add_style(label_sdnn_caption, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_sdnn_caption, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);

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