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

lv_obj_t *scr_ppg;

// GUI Charts
static lv_obj_t *chart_ppg;
static lv_chart_series_t *ser_ppg;

// GUI Labels
static lv_obj_t *label_ppg_hr;
static lv_obj_t *label_ppg_spo2;
static lv_obj_t *label_status;
static lv_obj_t *label_ppg_no_signal;

static float y_max_ppg = 0;
static float y_min_ppg = 10000;

float y2_max = 0;
float y2_min = 10000;

float y3_max = 0;
float y3_min = 10000;

static float gx = 0;

extern lv_style_t style_red_medium;
extern lv_style_t style_white_medium;

#define PPG_SIGNAL_RED 0
#define PPG_SIGNAL_IR 1
#define PPG_SIGNAL_GREEN 2

uint8_t ppg_disp_signal_type = PPG_SIGNAL_RED;

LOG_MODULE_REGISTER(ppg_scr);

static lv_obj_t *btn_settings_close;

static lv_obj_t *msg_box_settings;

static void event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = lv_event_get_target(e);
    if (code == LV_EVENT_VALUE_CHANGED)
    {
        char buf[32];
        lv_dropdown_get_selected_str(obj, buf, sizeof(buf));
        LOG_DBG("Option: %s", buf);
    }
}

static void btn_close_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        lv_msgbox_close(msg_box_settings);
    }
}

static void hpi_disp_ppg_settings_open(void)
{
    msg_box_settings = lv_msgbox_create(scr_ppg, "PPG Settings", NULL, NULL, false);
    lv_obj_set_style_clip_corner(msg_box_settings, true, 0);

    // /lv_obj_set_size(msg_box_settings, 320, 320);
    lv_obj_center(msg_box_settings);

    lv_obj_t *content = lv_msgbox_get_content(msg_box_settings);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(content, -1, LV_PART_SCROLLBAR);

    lv_obj_t *lbl_plot_signal = lv_label_create(content);
    lv_label_set_text(lbl_plot_signal, "Plot Signal : ");

    /*Create a normal drop down list*/
    lv_obj_t *dd = lv_dropdown_create(content);
    lv_dropdown_set_options(dd, "Red\n"
                                "IR\n"
                                "Green");

    lv_obj_align(dd, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_add_event_cb(dd, event_handler, LV_EVENT_ALL, NULL);

    /*lv_obj_t *cont_speed = lv_obj_create(content);
    lv_obj_set_size(cont_speed, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_speed, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_speed, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lb_speed = lv_label_create(cont_speed);
    lv_label_set_text(lb_speed, "Speed : ");
    lv_obj_t *slider_speed = lv_slider_create(cont_speed);
    lv_obj_set_width(slider_speed, lv_pct(100));
    lv_slider_set_value(slider_speed, 80, LV_ANIM_OFF);
    */

    btn_settings_close = lv_btn_create(content);
    lv_obj_add_event_cb(btn_settings_close, btn_close_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_align(btn_settings_close, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_height(btn_settings_close, 80);

    lv_obj_t *label_btn_bpt_measure = lv_label_create(btn_settings_close);
    lv_label_set_text(label_btn_bpt_measure, "Close");
    lv_obj_center(label_btn_bpt_measure);
}

static void ppg_settings_button_cb(lv_event_t *e)
{
    hpi_disp_ppg_settings_open();
}

void draw_scr_spl_plot_ppg(enum scroll_dir m_scroll_dir, uint8_t scr_parent)
{
    scr_ppg = lv_obj_create(NULL);
    lv_obj_clear_flag(scr_ppg, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    // draw_bg(scr_ppg);
    draw_scr_common(scr_ppg);

    // Bottom signal label
    lv_obj_t *label_signal = lv_label_create(scr_ppg);
    lv_label_set_text(label_signal, "PPG");
    lv_obj_align(label_signal, LV_ALIGN_TOP_MID, 0, 5);

    chart_ppg = lv_chart_create(scr_ppg);
    lv_obj_set_size(chart_ppg, 380, 130);
    lv_obj_set_style_bg_color(chart_ppg, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(chart_ppg, 0, LV_PART_MAIN);
    lv_obj_set_style_size(chart_ppg, 0, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(chart_ppg, 0, LV_PART_MAIN);
    lv_chart_set_point_count(chart_ppg, PPG_DISP_WINDOW_SIZE);
    lv_chart_set_div_line_count(chart_ppg, 0, 0);
    lv_chart_set_update_mode(chart_ppg, LV_CHART_UPDATE_MODE_CIRCULAR);
    lv_obj_align(chart_ppg, LV_ALIGN_CENTER, 0, -35);

    ser_ppg = lv_chart_add_series(chart_ppg, lv_palette_main(LV_PALETTE_ORANGE), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_line_width(chart_ppg, 5, LV_PART_ITEMS);

    label_ppg_no_signal = lv_label_create(scr_ppg);
    lv_label_set_text(label_ppg_no_signal, LV_SYMBOL_UP "\nPlace device \non wrist \nto start PPG");
    lv_obj_align_to(label_ppg_no_signal, NULL, LV_ALIGN_CENTER, -20, -40);
    lv_obj_set_style_text_align(label_ppg_no_signal, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_style(label_ppg_no_signal, &style_red_medium, 0);
    lv_obj_add_flag(label_ppg_no_signal, LV_OBJ_FLAG_HIDDEN);

    // HR Number label
    label_ppg_hr = lv_label_create(scr_ppg);
    lv_label_set_text(label_ppg_hr, "--");
    lv_obj_align_to(label_ppg_hr, chart_ppg, LV_ALIGN_OUT_BOTTOM_MID, -100, 60);
    lv_obj_add_style(label_ppg_hr, &style_white_medium, 0);

    // HR Sub bpm label
    lv_obj_t *label_hr_sub = lv_label_create(scr_ppg);
    lv_label_set_text(label_hr_sub, " bpm");
    lv_obj_align_to(label_hr_sub, label_ppg_hr, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);

    // HR caption label
    lv_obj_t *label_hr_cap = lv_label_create(scr_ppg);
    lv_label_set_text(label_hr_cap, "HR");
    lv_obj_align_to(label_hr_cap, label_ppg_hr, LV_ALIGN_OUT_TOP_MID, 0, -20);
    lv_obj_add_style(label_hr_cap, &style_red_medium, 0);

    /*LV_IMG_DECLARE(heart);
    lv_obj_t *img1 = lv_img_create(scr_ppg);
    lv_img_set_src(img1, &heart);
    lv_obj_set_size(img1, 35, 33);
    lv_obj_align_to(img1, label_ppg_hr, LV_ALIGN_OUT_LEFT_MID, -5, 0);*/

    // SpO2 Number label
    label_ppg_spo2 = lv_label_create(scr_ppg);
    lv_label_set_text(label_ppg_spo2, "--");
    lv_obj_align_to(label_ppg_spo2, chart_ppg,  LV_ALIGN_OUT_BOTTOM_MID, 100, 60);
    lv_obj_add_style(label_ppg_spo2, &style_white_medium, 0);

    // SpO2 Sub % label
    lv_obj_t *label_spo2_sub = lv_label_create(scr_ppg);
    lv_label_set_text(label_spo2_sub, " %");
    lv_obj_align_to(label_spo2_sub, label_ppg_spo2, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);

    // SpO2 caption label
    lv_obj_t *label_spo2_cap = lv_label_create(scr_ppg);
    lv_label_set_text(label_spo2_cap, "SpO2");
    lv_obj_align_to(label_spo2_cap, label_ppg_spo2,LV_ALIGN_OUT_TOP_MID, 0, -20);
    lv_obj_add_style(label_spo2_cap, &style_red_medium, 0);

    /*lv_obj_t *btn_settings = lv_btn_create(scr_ppg);
    lv_obj_set_width(btn_settings, 80);
    lv_obj_set_height(btn_settings, 80);
    lv_obj_set_x(btn_settings, 0);
    lv_obj_align_to(btn_settings, NULL, LV_ALIGN_CENTER, 0, 150);
    lv_obj_add_flag(btn_settings, LV_OBJ_FLAG_SCROLL_ON_FOCUS); /// Flags
    lv_obj_clear_flag(btn_settings, LV_OBJ_FLAG_SCROLLABLE);    /// Flags
    lv_obj_set_style_radius(btn_settings, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn_settings, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn_settings, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(btn_settings, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_settings, ppg_settings_button_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ui_hr_number = lv_label_create(btn_settings);
    lv_obj_set_width(ui_hr_number, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_hr_number, LV_SIZE_CONTENT); /// 1
    lv_obj_set_align(ui_hr_number, LV_ALIGN_BOTTOM_MID);
    lv_label_set_text(ui_hr_number, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(ui_hr_number, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_hr_number, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    //lv_obj_set_style_text_font(ui_hr_number, &lv_font_montserrat_42, LV_PART_MAIN | LV_STATE_DEFAULT);
    */
    // PPG Sensor Status label
    label_status = lv_label_create(scr_ppg);
    lv_label_set_text(label_status, "--");
    lv_obj_align_to(label_status, chart_ppg, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_obj_set_style_text_align(label_status, LV_TEXT_ALIGN_CENTER, 0);

    hpi_disp_set_curr_screen(SCR_SPL_PLOT_PPG);
    hpi_show_screen_spl(scr_ppg, m_scroll_dir, scr_parent);
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

static void hpi_scr_ppg_hide_plot(bool hide)
{
    if (hide)
    {
        lv_obj_add_flag(chart_ppg, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(label_ppg_no_signal, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_clear_flag(chart_ppg, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(label_ppg_no_signal, LV_OBJ_FLAG_HIDDEN);
    }
}

void hpi_ppg_disp_update_status(uint8_t status)
{
    if (label_status == NULL)
        return;

    char stat_str[16];

    switch (status)
    {
    case HPI_PPG_SCD_STATUS_UNKNOWN:
    case HPI_PPG_SCD_OFF_SKIN:
        sprintf(stat_str, "Off Skin");
        hpi_scr_ppg_hide_plot(true);
        break;
    case HPI_PPG_SCD_ON_OBJ:
        sprintf(stat_str, "On Obj.");
        break;
    case HPI_PPG_SCD_ON_SKIN:
        sprintf(stat_str, "On Skin");
        hpi_scr_ppg_hide_plot(false);
        break;
    default:
        sprintf(stat_str, "UNK.");
        break;
    }

    lv_label_set_text(label_status, stat_str);
}

static void hpi_ppg_disp_add_samples(int num_samples)
{
    gx += num_samples;
}

static void hpi_ppg_disp_do_set_scale(int disp_window_size)
{
    if (gx >= (disp_window_size / 4))
    {

        lv_chart_set_range(chart_ppg, LV_CHART_AXIS_PRIMARY_Y, y_min_ppg, y_max_ppg);

        gx = 0;
        y_max_ppg = -900000;
        y_min_ppg = 900000;
    }
}

void hpi_disp_ppg_draw_plotPPG(struct hpi_ppg_wr_data_t ppg_sensor_sample)
{
    uint32_t *data_ppg = ppg_sensor_sample.raw_green;

    for (int i = 0; i < ppg_sensor_sample.ppg_num_samples; i++)
    {
        if (data_ppg[i] < y_min_ppg)
        {
            y_min_ppg = data_ppg[i];
        }

        if (data_ppg[i] > y_max_ppg)
        {
            y_max_ppg = data_ppg[i];
        }

        lv_chart_set_next_value(chart_ppg, ser_ppg, data_ppg[i]);

        hpi_ppg_disp_add_samples(1);
        hpi_ppg_disp_do_set_scale(PPG_DISP_WINDOW_SIZE);
    }
}
