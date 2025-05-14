#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <lvgl.h>
#include <stdio.h>
#include <zephyr/zbus/zbus.h>

#include "hpi_common_types.h"
#include "ui/move_ui.h"
#include "hw_module.h"

static lv_obj_t *scr_ecg_scr2;

static lv_obj_t *btn_ecg_cancel;

// GUI Charts
static lv_obj_t *chart_ecg;
static lv_chart_series_t *ser_ecg;

static lv_obj_t *label_ecg_hr;
static lv_obj_t *label_timer;
static lv_obj_t *label_ecg_lead_off;
static lv_obj_t *label_info;

static bool chart_ecg_update = true;
static float y_max_ecg = 0;
static float y_min_ecg = 10000;

// static bool ecg_plot_hidden = false;

static float gx = 0;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_large;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_tiny;

extern lv_style_t style_bg_blue;
extern lv_style_t style_bg_red;

static void btn_ecg_cancel_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        hpi_load_screen(SCR_ECG, SCROLL_DOWN);
    }
}

void draw_scr_ecg_scr2(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_ecg_scr2 = lv_obj_create(NULL);
    lv_obj_add_style(scr_ecg_scr2, &style_scr_black, 0);
    lv_obj_clear_flag(scr_ecg_scr2, LV_OBJ_FLAG_SCROLLABLE);
    // draw_scr_common(scr_ecg_scr2);

    /*Create a container with COLUMN flex direction*/
    lv_obj_t *cont_col = lv_obj_create(scr_ecg_scr2);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 25);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_col, -1, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_top(cont_col, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(cont_col, 1, LV_PART_MAIN);
    lv_obj_add_style(cont_col, &style_scr_black, 0);
    // lv_obj_add_style(cont_col, &style_bg_red, 0);

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

    /*label_info = lv_label_create(cont_col);
    lv_label_set_long_mode(label_info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_info, 300);
    lv_label_set_text(label_info, "Touch the bezel to start");
    lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_style(label_info, &style_white_medium, 0);*/

    /*btn_ecg_cancel = lv_btn_create(cont_col);
    lv_obj_add_event_cb(btn_ecg_cancel, btn_ecg_cancel_handler, LV_EVENT_ALL, NULL);
    // lv_obj_set_height(btn_ecg_cancel, 85);

    lv_obj_t *label_btn = lv_label_create(btn_ecg_cancel);
    lv_label_set_text(label_btn, LV_SYMBOL_CLOSE);
    lv_obj_center(label_btn);*/

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
    ser_ecg = lv_chart_add_series(chart_ecg, lv_palette_darken(LV_PALETTE_ORANGE, 2), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_line_width(chart_ecg, 6, LV_PART_ITEMS);
    // lv_obj_add_flag(chart_ecg, LV_OBJ_FLAG_HIDDEN);

    // Draw Lead off label
    label_ecg_lead_off = lv_label_create(cont_col);
    lv_label_set_long_mode(label_ecg_lead_off, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_ecg_lead_off, 300);
    lv_label_set_text(label_ecg_lead_off, "--");
    lv_obj_set_style_text_align(label_ecg_lead_off, LV_TEXT_ALIGN_CENTER, 0);

    // Draw BPM container
    lv_obj_t *cont_hr = lv_obj_create(cont_col);
    lv_obj_set_size(cont_hr, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_hr, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(cont_hr, &style_scr_black, 0);
    lv_obj_set_flex_align(cont_hr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *img_heart = lv_img_create(cont_hr);
    lv_img_set_src(img_heart, &img_heart_35);

    label_ecg_hr = lv_label_create(cont_hr);
    lv_label_set_text(label_ecg_hr, "00");
    lv_obj_add_style(label_ecg_hr, &style_white_medium, 0);
    lv_obj_t *label_hr_sub = lv_label_create(cont_hr);
    lv_label_set_text(label_hr_sub, " bpm");

    hpi_disp_set_curr_screen(SCR_SPL_ECG_SCR2);
    hpi_show_screen(scr_ecg_scr2, m_scroll_dir);
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

    lv_label_set_text_fmt(label_timer, "%d", time_left);
}

static bool prev_lead_off_status = true;

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

            lv_chart_set_next_value(chart_ecg, ser_ecg, data_ecg_i);
            hpi_ecg_disp_add_samples(1);
            hpi_ecg_disp_do_set_scale(ECG_DISP_WINDOW_SIZE);
        }

        /*if (ecg_lead_off == true)
        {
            //lv_obj_add_flag(chart_ecg, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(label_ecg_lead_off, LV_OBJ_FLAG_HIDDEN);
            // ecg_plot_hidden = true;
        }
        else
        {
            lv_obj_add_flag(label_ecg_lead_off, LV_OBJ_FLAG_HIDDEN);
            // ecg_plot_hidden = false;
        }*/

        if ((ecg_lead_off == true) && (prev_lead_off_status == false))
        {
            lv_label_set_text(label_ecg_lead_off, "Lead Off");
            lv_obj_add_style(label_ecg_lead_off, &style_red_medium, 0);
            prev_lead_off_status = true;
        }
        else if((ecg_lead_off == false) && (prev_lead_off_status == true))
        {
            lv_label_set_text(label_ecg_lead_off, "Lead On");
            lv_obj_add_style(label_ecg_lead_off, &style_white_medium, 0);
            prev_lead_off_status = false;
        }
    }
}

void scr_ecg_lead_on_off_handler(bool lead_on_off)
{
    if (label_info == NULL)
        return;

    if (lead_on_off == false)
    {
        lv_obj_clear_flag(label_info, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(chart_ecg, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(label_info, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(chart_ecg, LV_OBJ_FLAG_HIDDEN);
    }
}
