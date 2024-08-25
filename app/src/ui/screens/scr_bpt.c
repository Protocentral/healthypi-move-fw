#include <zephyr/kernel.h>
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
#include "hw_module.h"

#include "ui/move_ui.h"

lv_obj_t *scr_bpt_home;
lv_obj_t *scr_bpt_calibrate;
lv_obj_t *scr_bpt_measure;

lv_obj_t *label_cal_status;
lv_obj_t *btn_bpt_cal_start;
lv_obj_t *btn_bpt_cal_exit;
lv_obj_t *btn_bpt_measure_exit;

static lv_obj_t *chart_bpt;
static lv_chart_series_t *ser_bpt;

bool bpt_start_flag = false;
lv_obj_t *label_btn_bpt;
lv_obj_t *bar_bpt_progress;
lv_obj_t *label_progress;
lv_obj_t *btn_bpt_start_cal;

lv_obj_t *btn_bpt_measure_start;

lv_obj_t *label_bp_sys_sub;
lv_obj_t *label_bp_sys_cap;
static lv_obj_t *label_bp_val;
static lv_obj_t *label_bp_dia_val;

static bool bpt_meas_done_flag = false;

bool bpt_meas_started = false;

// Externs
extern lv_style_t style_lbl_orange;
extern lv_style_t style_lbl_white;
extern lv_style_t style_lbl_red;
extern lv_style_t style_lbl_white_small;
extern lv_style_t style_lbl_white_14;
extern lv_style_t style_lbl_black_small;

extern int curr_screen;

static float y_max_ppg = 0;
static float y_min_ppg = 10000;
static float gx = 0;

static bool chart_ppg_update = true;
static void hpi_bpt_disp_do_set_scale(int disp_window_size);

static void scr_bpt_btn_measure_exit_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        // bpt_meas_done_flag = false;
        // bpt_meas_last_progress = 0;
        // bpt_meas_last_status = 0;
        // bpt_meas_started = false;

        draw_scr_bpt_home(SCROLL_DOWN);
    }
}

static void scr_bpt_calib_btn_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        draw_scr_bpt_calibrate();
    }
}

static void scr_bpt_measure_btn_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {

        draw_scr_bpt_measure();
    }
}

static void scr_bpt_btn_cal_start_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED)
    {
        hw_bpt_start_cal();
    }
}

static void scr_bpt_btn_cal_exit_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED)
    {
        draw_scr_bpt_home(SCROLL_RIGHT);
    }
}

static void scr_bpt_btn_measure_start_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        // lv_obj_add_flag(btn_bpt_measure_start, LV_OBJ_FLAG_HIDDEN);

        bpt_meas_done_flag = false;
        // bpt_meas_last_progress = 0;
        // bpt_meas_last_status = 0;

        lv_obj_add_flag(label_bp_val, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(label_bp_sys_sub, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(label_bp_sys_cap, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(chart_bpt, LV_OBJ_FLAG_HIDDEN);
        // lv_obj__flag(btn_bpt_measure_start, LV_OBJ_FLAG_HIDDEN);

        bpt_meas_started = true;
        hw_bpt_start_est();
    }
}

void draw_scr_bpt_measure(void)
{
    scr_bpt_measure = lv_obj_create(NULL);
    draw_header_minimal(scr_bpt_measure,0);

    // Draw Blood Pressure label

    lv_obj_t *label_bp = lv_label_create(scr_bpt_measure);
    lv_label_set_text(label_bp, "BP Measurement");
    lv_obj_align(label_bp, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_add_style(label_bp, &style_lbl_white_14, 0);

    // Create Chart 1 - ECG
    chart_bpt = lv_chart_create(scr_bpt_measure);
    lv_obj_set_size(chart_bpt, 390, 150);
    lv_obj_set_style_bg_color(chart_bpt, lv_color_black(), LV_STATE_DEFAULT);

    lv_obj_set_style_size(chart_bpt, 0, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(chart_bpt, 0, LV_PART_MAIN);
    lv_chart_set_point_count(chart_bpt, PPG_DISP_WINDOW_SIZE);
    // lv_chart_set_type(chart_bpt, LV_CHART_TYPE_LINE);   /*Show lines and points too*
    lv_chart_set_range(chart_bpt, LV_CHART_AXIS_PRIMARY_Y, -1000, 1000);
    // lv_chart_set_range(chart_bpt, LV_CHART_AXIS_SECONDARY_Y, 0, 1000);
    lv_chart_set_div_line_count(chart_bpt, 0, 0);
    lv_chart_set_update_mode(chart_bpt, LV_CHART_UPDATE_MODE_CIRCULAR);
    // lv_style_set_border_width(&styles->bg, LV_STATE_DEFAULT, BORDER_WIDTH);
    lv_obj_align_to(chart_bpt, label_bp, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
    ser_bpt = lv_chart_add_series(chart_bpt, lv_palette_main(LV_PALETTE_ORANGE), LV_CHART_AXIS_PRIMARY_Y);

    // BP Systolic Number label
    label_bp_val = lv_label_create(scr_bpt_measure);
    lv_label_set_text(label_bp_val, "-- / --");
    lv_obj_align_to(label_bp_val, NULL, LV_ALIGN_CENTER, -30, -25);
    lv_obj_add_style(label_bp_val, &style_lbl_white, 0);
    lv_obj_add_flag(label_bp_val, LV_OBJ_FLAG_HIDDEN);

    // BP Systolic Sub mmHg label
    label_bp_sys_sub = lv_label_create(scr_bpt_measure);
    lv_label_set_text(label_bp_sys_sub, " mmHg");
    lv_obj_align_to(label_bp_sys_sub, label_bp_val, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(label_bp_sys_sub, LV_OBJ_FLAG_HIDDEN);

    // BP Systolic caption label
    label_bp_sys_cap = lv_label_create(scr_bpt_measure);
    lv_label_set_text(label_bp_sys_cap, "BP(Sys/Dia)");
    lv_obj_align_to(label_bp_sys_cap, label_bp_val, LV_ALIGN_OUT_TOP_MID, -5, -5);
    lv_obj_add_style(label_bp_sys_cap, &style_lbl_red, 0);
    lv_obj_add_flag(label_bp_sys_cap, LV_OBJ_FLAG_HIDDEN);

    // Draw Progress bar
    bar_bpt_progress = lv_bar_create(scr_bpt_measure);
    lv_obj_set_size(bar_bpt_progress, 200, 5);
    lv_obj_align_to(bar_bpt_progress, chart_bpt, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
    lv_bar_set_value(bar_bpt_progress, 0, LV_ANIM_OFF);

    // Draw Progress bar label
    label_progress = lv_label_create(scr_bpt_measure);
    lv_label_set_text(label_progress, "Progress: --");
    lv_obj_align_to(label_progress, bar_bpt_progress, LV_ALIGN_OUT_BOTTOM_MID, 0, 3);

    // Draw button to start BP measurement

    btn_bpt_measure_start = lv_btn_create(scr_bpt_measure);
    lv_obj_add_event_cb(btn_bpt_measure_start, scr_bpt_btn_measure_start_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_align_to(btn_bpt_measure_start, NULL, LV_ALIGN_BOTTOM_MID, -110, -120);
    lv_obj_set_height(btn_bpt_measure_start, 55);
    lv_obj_set_width(btn_bpt_measure_start, 240);
    lv_obj_set_style_bg_color(btn_bpt_measure_start, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN);

    lv_obj_t *label_btn_bpt = lv_label_create(btn_bpt_measure_start);
    lv_label_set_text(label_btn_bpt, "Start");
    // lv_obj_add_style(label_btn_bpt, &style_lbl_white_small, 0);
    lv_obj_center(label_btn_bpt);

    btn_bpt_measure_exit = lv_btn_create(scr_bpt_measure);
    lv_obj_add_event_cb(btn_bpt_measure_exit, scr_bpt_btn_measure_exit_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_align_to(btn_bpt_measure_exit, NULL, LV_ALIGN_BOTTOM_MID, -110, -40);
    lv_obj_set_height(btn_bpt_measure_exit, 55);
    lv_obj_set_width(btn_bpt_measure_exit, 240);
    lv_obj_set_style_bg_color(btn_bpt_measure_exit, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);

    lv_obj_t *label_btn_bpt_exit = lv_label_create(btn_bpt_measure_exit);
    lv_label_set_text(label_btn_bpt_exit, "Exit");
    lv_obj_add_style(label_btn_bpt_exit, &style_lbl_white_small, 0);
    lv_obj_center(label_btn_bpt_exit);

    // Hide exit button by default
    lv_obj_add_flag(btn_bpt_measure_exit, LV_OBJ_FLAG_HIDDEN);

    // lv_obj_add_event_cb(scr_bpt_measure, disp_screen_event, LV_EVENT_CLICKED, NULL);
    curr_screen = SUBSCR_BPT_MEASURE;

    lv_scr_load_anim(scr_bpt_measure, LV_SCR_LOAD_ANIM_MOVE_BOTTOM, SCREEN_TRANS_TIME, 0, true);
}

void draw_scr_bpt_calibrate(void)
{
    scr_bpt_calibrate = lv_obj_create(NULL);
    draw_header_minimal(scr_bpt_calibrate,0);

    draw_bg(scr_bpt_calibrate);

    // Draw Blood Pressure label
    lv_obj_t *label_bp = lv_label_create(scr_bpt_calibrate);
    lv_label_set_text(label_bp, "BP Calibration");
    lv_obj_align(label_bp, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_add_style(label_bp, &style_lbl_white_14, 0);

    // Create Chart 1 - ECG
    chart_bpt = lv_chart_create(scr_bpt_calibrate);
    lv_obj_set_size(chart_bpt, 390, 150);
    lv_obj_set_style_bg_color(chart_bpt, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(chart_bpt, 0, LV_PART_MAIN);

    lv_obj_set_style_size(chart_bpt, 0, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(chart_bpt, 0, LV_PART_MAIN);
    lv_chart_set_point_count(chart_bpt, PPG_DISP_WINDOW_SIZE);
    lv_chart_set_range(chart_bpt, LV_CHART_AXIS_PRIMARY_Y, -1000, 1000);
    lv_chart_set_div_line_count(chart_bpt, 0, 0);
    lv_chart_set_update_mode(chart_bpt, LV_CHART_UPDATE_MODE_CIRCULAR);
    lv_obj_align_to(chart_bpt, label_bp, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
    ser_bpt = lv_chart_add_series(chart_bpt, lv_palette_main(LV_PALETTE_ORANGE), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_line_width(chart_bpt, 3, LV_PART_ITEMS);

    label_cal_status = lv_label_create(scr_bpt_calibrate);
    lv_label_set_text(label_cal_status, "Calibration\nDone");
    lv_obj_align_to(label_cal_status, NULL, LV_ALIGN_CENTER, -30, -25);
    lv_obj_set_style_text_align(label_cal_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_style(label_cal_status, &style_lbl_white, 0);
    lv_obj_add_flag(label_cal_status, LV_OBJ_FLAG_HIDDEN);

    // Draw Progress bar
    bar_bpt_progress = lv_bar_create(scr_bpt_calibrate);
    lv_obj_set_size(bar_bpt_progress, 200, 5);
    lv_obj_align_to(bar_bpt_progress, chart_bpt, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_bar_set_value(bar_bpt_progress, 0, LV_ANIM_OFF);

    // Draw Progress bar label
    label_progress = lv_label_create(scr_bpt_calibrate);
    lv_label_set_text(label_progress, "Progress: --");
    lv_obj_align_to(label_progress, bar_bpt_progress, LV_ALIGN_OUT_BOTTOM_MID, 0, 3);
    lv_obj_add_style(label_progress, &style_lbl_white_14, 0);

    // Draw button to start BP calibration

    btn_bpt_cal_start = lv_btn_create(scr_bpt_calibrate);
    lv_obj_add_event_cb(btn_bpt_cal_start, scr_bpt_btn_cal_start_handler, LV_EVENT_ALL, NULL);
    lv_obj_align_to(btn_bpt_cal_start, NULL, LV_ALIGN_BOTTOM_LEFT, 0, -60);
    lv_obj_set_height(btn_bpt_cal_start, 90);
    lv_obj_set_width(btn_bpt_cal_start, 390);
    lv_obj_set_style_bg_color(btn_bpt_cal_start, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN);

    lv_obj_t *label_btn_bpt = lv_label_create(btn_bpt_cal_start);
    lv_label_set_text(label_btn_bpt, "Start");
    lv_obj_add_style(label_btn_bpt, &style_lbl_black_small, 0);
    lv_obj_center(label_btn_bpt);

    btn_bpt_cal_exit = lv_btn_create(scr_bpt_calibrate);
    lv_obj_add_event_cb(btn_bpt_cal_exit, scr_bpt_btn_cal_exit_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_align_to(btn_bpt_cal_exit, NULL, LV_ALIGN_BOTTOM_LEFT, 0, -60);
    lv_obj_set_height(btn_bpt_cal_exit, 90);
    lv_obj_set_width(btn_bpt_cal_exit, 390);
    lv_obj_set_style_bg_color(btn_bpt_cal_exit, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);

    lv_obj_t *label_btn_bpt_exit = lv_label_create(btn_bpt_cal_exit);
    lv_label_set_text(label_btn_bpt_exit, "Exit");
    // lv_obj_add_style(label_btn_bpt_exit, &style_lbl_white_small, 0);
    lv_obj_center(label_btn_bpt_exit);

    // Hide exit button by default
    lv_obj_add_flag(btn_bpt_cal_exit, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_event_cb(scr_bpt_calibrate, disp_screen_event, LV_EVENT_GESTURE, NULL);
    curr_screen = SUBSCR_BPT_CALIBRATE;

    lv_scr_load_anim(scr_bpt_calibrate, LV_SCR_LOAD_ANIM_MOVE_TOP, SCREEN_TRANS_TIME, 0, true);
}

void draw_scr_bpt_home(enum scroll_dir m_scroll_dir)
{
    scr_bpt_home = lv_obj_create(NULL);
    draw_header_minimal(scr_bpt_home,0);

    // Draw button to measure BP

    lv_obj_t *btn_bpt_measure_start = lv_btn_create(scr_bpt_home);
    lv_obj_add_event_cb(btn_bpt_measure_start, scr_bpt_measure_btn_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_align(btn_bpt_measure_start, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_height(btn_bpt_measure_start, 80);

    lv_obj_t *label_btn_bpt_measure = lv_label_create(btn_bpt_measure_start);
    lv_label_set_text(label_btn_bpt_measure, "Measure BP");
    lv_obj_center(label_btn_bpt_measure);

    // Draw button to calibrate BP

    lv_obj_t *btn_bpt_calibrate = lv_btn_create(scr_bpt_home);
    lv_obj_add_event_cb(btn_bpt_calibrate, scr_bpt_calib_btn_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_align_to(btn_bpt_calibrate, btn_bpt_measure_start, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 20);
    lv_obj_set_height(btn_bpt_calibrate, 80);

    lv_obj_t *label_btn_bpt_calibrate = lv_label_create(btn_bpt_calibrate);
    lv_label_set_text(label_btn_bpt_calibrate, "Calibrate BP");
    lv_obj_center(label_btn_bpt_calibrate);

    curr_screen = SCR_BPT_HOME;
    hpi_show_screen(scr_bpt_home, m_scroll_dir);

    // hw_bpt_stop();
}

void hpi_disp_bpt_update_progress(int progress)
{
    if (label_progress == NULL || bar_bpt_progress == NULL)
    {
        return;
    }

    lv_bar_set_value(bar_bpt_progress, progress, LV_ANIM_OFF);
    lv_label_set_text_fmt(label_progress, "Progress: %d %%", progress);

    if (progress == 100)
    {
        lv_obj_add_flag(chart_bpt, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(label_cal_status, LV_OBJ_FLAG_HIDDEN);

        lv_obj_add_flag(btn_bpt_cal_start, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(btn_bpt_cal_exit, LV_OBJ_FLAG_HIDDEN);
    }
}

void hpi_disp_bpt_update_status(int status)
{
    if (label_cal_status == NULL)
    {
        return;
    }

    if (status == 1)
    {
        lv_label_set_text(label_cal_status, "Calibration\nDone");
    }
    else
    {
        lv_label_set_text(label_cal_status, "Calibration\nFailed");
    }
}

void hpi_disp_update_bp(int sys, int dia)
{
    if (label_bp_val == NULL)
        return;

    char buf[32];
    if (sys == 0 || dia == 0)
    {
        sprintf(buf, "-- / --");
    }
    else
    {
        sprintf(buf, "%d / %d", sys, dia);
    }

    lv_label_set_text(label_bp_val, buf);
}

void hpi_disp_bpt_draw_plotPPG(float data_ppg)
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
        lv_chart_set_next_value(chart_bpt, ser_bpt, data_ppg);

        gx += 1;
        hpi_bpt_disp_do_set_scale(PPG_DISP_WINDOW_SIZE);
    }
}

static void hpi_bpt_disp_do_set_scale(int disp_window_size)
{
    if (gx >= (disp_window_size))
    {
        if (chart_ppg_update == true)
            lv_chart_set_range(chart_bpt, LV_CHART_AXIS_PRIMARY_Y, y_min_ppg, y_max_ppg);

        gx = 0;

        y_max_ppg = -900000;
        y_min_ppg = 900000;
    }
}