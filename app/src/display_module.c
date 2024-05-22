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

#include "sys_sm_module.h"
#include "hw_module.h"
#include "display_module.h"
#include "sampling_module.h"

#include "sys_sm_module.h"

#define SAMPLE_RATE 125

#define PPG_DISP_WINDOW_SIZE 256 // SAMPLE_RATE * 4
#define DISP_WINDOW_SIZE_EDA 250
#define DISP_WINDOW_SIZE_ECG 256

#define SCREEN_TRANS_TIME 300
#define DISP_THREAD_REFRESH_INT_MS 10

LOG_MODULE_REGISTER(display_module, LOG_LEVEL_WRN);

lv_obj_t *btn_start_session;
lv_obj_t *btn_return;

// LVGL Common Objects
static lv_indev_drv_t m_keypad_drv;
static lv_indev_t *m_keypad_indev = NULL;
extern uint8_t m_key_pressed;

const struct device *display_dev;
lv_indev_t *touch_indev;

// GUI Charts
static lv_obj_t *chart1;
static lv_chart_series_t *ser1;

// GUI Labels
static lv_obj_t *label_hr;
static lv_obj_t *label_spo2;
static lv_obj_t *label_temp;

static lv_obj_t *label_bp_val;
static lv_obj_t *label_bp_dia_val;

// LVGL Screens
lv_obj_t *scr_splash;
lv_obj_t *scr_home;
lv_obj_t *scr_menu;
lv_obj_t *scr_clock;
lv_obj_t *scr_ppg;
lv_obj_t *scr_ecg;
lv_obj_t *scr_eda;
lv_obj_t *scr_vitals_home;

lv_obj_t *scr_bpt_home;
lv_obj_t *scr_bpt_calibrate;
lv_obj_t *scr_bpt_measure;

static lv_obj_t *lbl_time;
static lv_obj_t *lbl_date;

// LVGL Styles
static lv_style_t style_sub;
static lv_style_t style_hr;
static lv_style_t style_spo2;
static lv_style_t style_rr;
static lv_style_t style_temp;
static lv_style_t style_scr_back;
static lv_style_t style_batt_sym;
static lv_style_t style_batt_percent;
static lv_style_t style_h1;
static lv_style_t style_h2;
static lv_style_t style_info;
static lv_style_t style_icon;
static lv_style_t style_scr_black;

static lv_style_t style_lbl_red;
static lv_style_t style_lbl_red_small;
static lv_style_t style_lbl_white;
static lv_style_t style_lbl_white_small;
static lv_style_t style_lbl_orange;
static lv_style_t style_lbl_white_tiny;
static lv_style_t style_lbl_white_14;

bool bpt_start_flag = false;
lv_obj_t *label_btn_bpt;
lv_obj_t *bar_bpt_progress;
lv_obj_t *label_progress;

lv_obj_t *btn_bpt_start_cal;

static lv_obj_t *roller_session_select;

static lv_obj_t *label_current_mode;
static lv_style_t style_scr_back;

static lv_obj_t *label_ecg_lead_off;

lv_obj_t *btn_bpt_start_stop;

static lv_obj_t *label_batt_level;
static lv_obj_t *label_batt_level_val;
static lv_obj_t *label_sym_ble;

lv_obj_t *btn_bpt_measure_start;

lv_obj_t *label_bp_sys_sub;
lv_obj_t *label_bp_sys_cap;

// Function declarations

void draw_scr_ppg(enum scroll_dir m_scroll_dir);
void m_cycle_screens(bool scr_back);

bool chart1_update = true;
bool chart2_update = true;
bool chart3_update = false;

float y1_max = 0;
float y1_min = 10000;

float y2_max = 0;
float y2_min = 10000;

float y3_max = 0;
float y3_min = 10000;
static float gx = 0;

int curr_mode = MODE_STANDBY;

bool display_inited = false;

static int curr_screen = SCR_VITALS;

K_SEM_DEFINE(sem_disp_inited, 0, 1);
K_MSGQ_DEFINE(q_plot_ecg_bioz, sizeof(struct hpi_ecg_bioz_sensor_data_t), 100, 1);
K_MSGQ_DEFINE(q_plot_ppg, sizeof(struct hpi_ppg_sensor_data_t), 100, 1);

static const struct device *touch_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(cst816s));

static bool bpt_meas_done_flag = false;
static bool bpt_cal_done_flag = false;

static int bpt_meas_last_progress = 0;
static int bpt_meas_last_status = 0;
bool bpt_meas_started = false;

static int bpt_cal_last_progress = 0;
static int bpt_cal_last_status = 0;
bool bpt_cal_started = false;

int global_bp_sys = 0;
int global_bp_dia = 0;

extern struct k_sem sem_hw_inited;
extern struct k_sem sem_sampling_start;

void display_init_styles()
{
    // Subscript (Unit) label style
    lv_style_init(&style_sub);
    lv_style_set_text_color(&style_sub, lv_color_white());
    lv_style_set_text_font(&style_sub, &lv_font_montserrat_16);

    lv_style_init(&style_batt_sym);
    lv_style_set_text_color(&style_batt_sym, lv_color_white());
    lv_style_set_text_font(&style_batt_sym, &lv_font_montserrat_24);

    lv_style_init(&style_batt_percent);
    lv_style_set_text_color(&style_batt_percent, lv_color_white());
    lv_style_set_text_font(&style_batt_percent, &lv_font_montserrat_12);

    // HR Number label style
    lv_style_init(&style_hr);
    lv_style_set_text_color(&style_hr, lv_palette_main(LV_PALETTE_ORANGE));
    lv_style_set_text_font(&style_hr, &lv_font_montserrat_42);

    // SpO2 label style
    lv_style_init(&style_spo2);
    lv_style_set_text_color(&style_spo2, lv_palette_main(LV_PALETTE_YELLOW));
    lv_style_set_text_font(&style_spo2, &lv_font_montserrat_42);

    // RR label style
    lv_style_init(&style_rr);
    lv_style_set_text_color(&style_rr, lv_palette_main(LV_PALETTE_BLUE));
    lv_style_set_text_font(&style_rr, &lv_font_montserrat_42);

    // Temp label style
    lv_style_init(&style_temp);
    lv_style_set_text_color(&style_temp, lv_palette_main(LV_PALETTE_ORANGE));
    lv_style_set_text_font(&style_temp, &lv_font_montserrat_20);

    // Icon welcome screen style
    lv_style_init(&style_icon);
    lv_style_set_text_color(&style_icon, lv_palette_main(LV_PALETTE_YELLOW));
    lv_style_set_text_font(&style_icon, &lv_font_montserrat_42);

    // H1 welcome screen style
    lv_style_init(&style_h1);
    lv_style_set_text_color(&style_h1, lv_color_white());
    lv_style_set_text_font(&style_h1, &lv_font_montserrat_34);

    // H2 welcome screen style
    lv_style_init(&style_h2);
    lv_style_set_text_color(&style_h2, lv_palette_main(LV_PALETTE_ORANGE));
    lv_style_set_text_font(&style_h2, &lv_font_montserrat_28);

    // Info welcome screen style
    lv_style_init(&style_info);
    lv_style_set_text_color(&style_info, lv_color_white());
    lv_style_set_text_font(&style_info, &lv_font_montserrat_16);

    // Label Red
    lv_style_init(&style_lbl_red);
    lv_style_set_text_color(&style_lbl_red, lv_palette_main(LV_PALETTE_RED));
    lv_style_set_text_font(&style_lbl_red, &lv_font_montserrat_20);

    // Label White
    lv_style_init(&style_lbl_white);
    lv_style_set_text_color(&style_lbl_white, lv_color_white());
    lv_style_set_text_font(&style_lbl_white, &lv_font_montserrat_28);

    // Label Orange
    lv_style_init(&style_lbl_orange);
    lv_style_set_text_color(&style_lbl_orange, lv_palette_main(LV_PALETTE_YELLOW));
    lv_style_set_text_font(&style_lbl_orange, &lv_font_montserrat_20);

    // Label White Small
    lv_style_init(&style_lbl_white_small);
    lv_style_set_text_color(&style_lbl_white_small, lv_color_white());
    lv_style_set_text_font(&style_lbl_white_small, &lv_font_montserrat_20);

    // Label Red Small
    lv_style_init(&style_lbl_red_small);
    lv_style_set_text_color(&style_lbl_red_small, lv_palette_main(LV_PALETTE_RED));
    lv_style_set_text_font(&style_lbl_red_small, &lv_font_montserrat_16);

    // Label White Tiny
    lv_style_init(&style_lbl_white_tiny);
    lv_style_set_text_color(&style_lbl_white_tiny, lv_color_white());
    lv_style_set_text_font(&style_lbl_white_tiny, &lv_font_montserrat_12);

    // Label White 14
    lv_style_init(&style_lbl_white_14);
    lv_style_set_text_color(&style_lbl_white_14, lv_color_white());
    lv_style_set_text_font(&style_lbl_white_14, &lv_font_montserrat_14);

    // Screen background style
    lv_style_init(&style_scr_back);
    // lv_style_set_radius(&style, 5);

    /*Make a gradient*/
    lv_style_set_bg_opa(&style_scr_back, LV_OPA_COVER);
    lv_style_set_border_width(&style_scr_back, 0);

    static lv_grad_dsc_t grad;
    grad.dir = LV_GRAD_DIR_VER;
    grad.stops_count = 2;
    grad.stops[0].color = lv_color_hex(0x003a57); // lv_palette_lighten(LV_PALETTE_GREY, 1);
    grad.stops[1].color = lv_color_black();       // lv_palette_main(LV_PALETTE_BLUE);

    /*Shift the gradient to the bottom*/
    grad.stops[0].frac = 128;
    grad.stops[1].frac = 192;

    // lv_style_set_bg_color(&style_scr_back, lv_color_black());
    // lv_style_set_bg_grad(&style_scr_back, &grad);
    lv_style_init(&style_scr_black);
    lv_style_set_radius(&style_scr_black, 1);

    lv_style_set_bg_opa(&style_scr_black, LV_OPA_COVER);
    lv_style_set_border_width(&style_scr_black, 0);
    lv_style_set_bg_color(&style_scr_black, lv_color_black());
}
static void keypad_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    static int call_count = 0;

    switch (m_key_pressed)
    {
    case GPIO_KEYPAD_KEY_OK:
        printk("K OK");
        data->key = LV_KEY_ENTER;
        break;
    case GPIO_KEYPAD_KEY_UP:
        printk("K UP");
        data->key = LV_KEY_UP;
        break;
    case GPIO_KEYPAD_KEY_DOWN:
        printk("K DOWN");
        data->key = LV_KEY_DOWN;
        break;
    default:
        break;
    }

    /* key press */
    if (m_key_pressed != GPIO_KEYPAD_KEY_NONE)
    {
        if (call_count == 0)
        {
            data->state = LV_INDEV_STATE_PR;
            call_count = 1;
        }
        else if (call_count == 1)
        {
            call_count = 2;
            data->state = LV_INDEV_STATE_REL;
        }
    }

    /* reset the keys */
    if ((m_key_pressed != GPIO_KEYPAD_KEY_NONE))
    {
        call_count = 0;
        m_key_pressed = GPIO_KEYPAD_KEY_NONE;
        // m_press_type = UNKNOWN;
    }
}

void menu_roller_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *roller = lv_event_get_target(e);
    if (code == LV_EVENT_VALUE_CHANGED)
    {
        uint16_t sel = lv_roller_get_selected(roller);
        printk("Roller changed: %d\n", sel);
        switch (sel)
        {

            break;
        }

        // display_load_session_preview(sel);
    }
}

void menu_roller_remove_event(void)
{
    lv_obj_remove_event_cb(roller_session_select, menu_roller_event_handler);
}

void draw_header_minimal(lv_obj_t *parent)
{
    lv_obj_add_style(parent, &style_scr_black, 0);

    LV_IMG_DECLARE(logo_round_white);
    lv_obj_t *img_logo = lv_img_create(parent);
    lv_img_set_src(img_logo, &logo_round_white);
    lv_obj_set_size(img_logo, 25, 25);
    lv_obj_align_to(img_logo, NULL, LV_ALIGN_TOP_MID, -30, 5);

    // Battery Level
    label_batt_level = lv_label_create(parent);
    lv_label_set_text(label_batt_level, LV_SYMBOL_BATTERY_FULL);
    lv_obj_add_style(label_batt_level, &style_batt_sym, LV_STATE_DEFAULT);
    lv_obj_align(label_batt_level, LV_ALIGN_TOP_MID, 20, -2);

    label_batt_level_val = lv_label_create(parent);
    lv_label_set_text(label_batt_level_val, "--");
    lv_obj_add_style(label_batt_level_val, &style_batt_percent, LV_STATE_DEFAULT);
    lv_obj_align_to(label_batt_level_val, label_batt_level, LV_ALIGN_OUT_BOTTOM_MID, 0, -2);
}

void draw_header(lv_obj_t *parent, bool showFWVersion)
{
    lbl_time = lv_label_create(parent);
    lv_label_set_text(lbl_time, "12:00");
    lv_obj_align(lbl_time, LV_ALIGN_TOP_MID, -45, 23);

    // lbl_date = lv_label_create(scr_vitals_home);
    // lv_label_set_text(lbl_date, "16/Mar/2024");
    // lv_obj_align(lbl_date, LV_ALIGN_TOP_MID, 0, 20);

    // ProtoCentral logo
    LV_IMG_DECLARE(logo_oneline);
    lv_obj_t *img1 = lv_img_create(parent);
    lv_img_set_src(img1, &logo_oneline);
    lv_obj_align(img1, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_size(img1, 104, 10);

    LV_IMG_DECLARE(logo_round_white);
    lv_obj_t *img_logo = lv_img_create(parent);
    lv_img_set_src(img_logo, &logo_round_white);
    lv_obj_set_size(img_logo, 25, 25);
    lv_obj_align_to(img_logo, NULL, LV_ALIGN_TOP_MID, 0, 30);

    static lv_style_t style_bg;
    lv_style_init(&style_bg);
    lv_style_set_radius(&style_bg, 5);

    // Battery Level
    label_batt_level = lv_label_create(parent);
    lv_label_set_text(label_batt_level, LV_SYMBOL_BATTERY_FULL);
    lv_obj_add_style(label_batt_level, &style_batt_sym, LV_STATE_DEFAULT);
    lv_obj_align(label_batt_level, LV_ALIGN_TOP_MID, 45, 20);

    /*label_batt_level_val = lv_label_create(parent);
    lv_label_set_text(label_batt_level_val, "100%");
    lv_obj_add_style(label_batt_level, &style_batt_sym, LV_STATE_DEFAULT);
    lv_obj_align_to(label_batt_level_val, label_batt_level, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    */

    /*Make a gradient*/
    /*lv_style_set_bg_opa(&style_bg, LV_OPA_COVER);
    static lv_grad_dsc_t grad;
    grad.dir = LV_GRAD_DIR_VER;
    grad.stops_count = 2;
    grad.stops[0].color = lv_color_hex(0x003a57); // lv_palette_lighten(LV_PALETTE_GREY, 1);
    // grad.stops[0].opa = LV_OPA_COVER;
    grad.stops[1].color = lv_color_black(); // lv_palette_main(LV_PALETTE_BLUE);
    // grad.stops[1].opa = LV_OPA_COVER;


    grad.stops[0].frac = 100;
    grad.stops[1].frac = 140;

    lv_style_set_bg_grad(&style_bg, &grad);
    lv_obj_add_style(parent, &style_bg, 0);
    */
}

void hpi_disp_add_samples(int num_samples)
{
    gx += num_samples;
}

void hpi_disp_do_set_scale(int disp_window_size)
{
    if (gx >= (disp_window_size))
    {
        if (chart1_update == true)
            lv_chart_set_range(chart1, LV_CHART_AXIS_PRIMARY_Y, y1_min, y1_max);

        // if (chart2_update == true)
        // lv_chart_set_range(chart2, LV_CHART_AXIS_PRIMARY_Y, y2_min, y2_max);

        gx = 0;

        y1_max = -900000;
        y1_min = 900000;
    }
}

bool ecg_plot_hidden = false;

void hpi_disp_draw_plotECG(float data_ecg, bool ecg_lead_off)
{
    if (chart1_update == true && ecg_lead_off == false)
    {
        if (data_ecg < y1_min)
        {
            y1_min = data_ecg;
        }

        if (data_ecg > y1_max)
        {
            y1_max = data_ecg;
        }

        if (ecg_plot_hidden == true)
        {
            lv_obj_add_flag(label_ecg_lead_off, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(chart1, LV_OBJ_FLAG_HIDDEN);
            ecg_plot_hidden = false;
        }

        // printk("E");
        lv_chart_set_next_value(chart1, ser1, data_ecg);
        hpi_disp_add_samples(1);
        hpi_disp_do_set_scale(DISP_WINDOW_SIZE_ECG);
    }
    else if (ecg_lead_off == true)
    {
        lv_obj_add_flag(chart1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(label_ecg_lead_off, LV_OBJ_FLAG_HIDDEN);
        ecg_plot_hidden = true;
    }
}

void hpi_disp_draw_plotEDA(float data_eda)
{
    if (chart1_update == true)
    {
        if (data_eda < y1_min)
        {
            y1_min = data_eda;
        }

        if (data_eda > y1_max)
        {
            y1_max = data_eda;
        }

        // printk("E");
        lv_chart_set_next_value(chart1, ser1, data_eda);
        hpi_disp_add_samples(1);
        hpi_disp_do_set_scale(DISP_WINDOW_SIZE_EDA);
    }
}

void hpi_disp_draw_plotPPG(float data_ppg)
{
    if (chart1_update == true)
    {
        if (data_ppg < y1_min)
        {
            y1_min = data_ppg;
        }

        if (data_ppg > y1_max)
        {
            y1_max = data_ppg;
        }

        // printk("E");
        lv_chart_set_next_value(chart1, ser1, data_ppg);
        hpi_disp_add_samples(1);
        hpi_disp_do_set_scale(PPG_DISP_WINDOW_SIZE);
    }
}

void hpi_disp_update_temp(int temp)
{
    if (label_temp == NULL)
        return;

    char buf[32];
    double temp_d = (double)(temp / 1000.00);
    sprintf(buf, "%.1f", temp_d);
    lv_label_set_text(label_temp, buf);
}

void hpi_disp_update_hr(int hr)
{
    if (label_hr == NULL)
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

    lv_label_set_text(label_hr, buf);
}

void hpi_disp_update_spo2(int spo2)
{
    if (label_spo2 == NULL)
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
    lv_label_set_text(label_spo2, buf);
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

void hpi_disp_update_batt_level(int batt_level)
{
    if (label_batt_level == NULL || label_batt_level_val == NULL)
    {
        return;
    }

    char buf[32];
    sprintf(buf, "%d %% ", batt_level);
    lv_label_set_text(label_batt_level_val, buf);

    if (batt_level > 75)
    {
        lv_label_set_text(label_batt_level, LV_SYMBOL_BATTERY_FULL);
    }
    else if (batt_level > 50)
    {
        lv_label_set_text(label_batt_level, LV_SYMBOL_BATTERY_3);
    }
    else if (batt_level > 25)
    {
        lv_label_set_text(label_batt_level, LV_SYMBOL_BATTERY_2);
    }
    else if (batt_level > 10)
    {
        lv_label_set_text(label_batt_level, LV_SYMBOL_BATTERY_1);
    }
    else
    {
        lv_label_set_text(label_batt_level, LV_SYMBOL_BATTERY_EMPTY);
    }
}

static void event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        LV_LOG_USER("Clicked");
    }
    else if (code == LV_EVENT_VALUE_CHANGED)
    {
        LV_LOG_USER("Toggled");
    }
}

void screen_event(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(touch_indev);

    switch (dir)
    {
    case LV_DIR_LEFT:
        printf("Left\n");
        m_cycle_screens(true);
        break;
    case LV_DIR_RIGHT:
        printf("Right\n");
        m_cycle_screens(false);
        break;
    case LV_DIR_TOP:
        printf("Top\n");
        break;
    case LV_DIR_BOTTOM:
        printf("Bottom\n");
        break;
    }
}

void draw_scr_vitals_home(enum scroll_dir m_scroll_dir)
{
    scr_vitals_home = lv_obj_create(NULL);

    draw_header_minimal(scr_vitals_home);

    // HR Number label
    label_hr = lv_label_create(scr_vitals_home);
    lv_label_set_text(label_hr, "120");
    lv_obj_align_to(label_hr, NULL, LV_ALIGN_CENTER, -60, -30);
    lv_obj_add_style(label_hr, &style_lbl_white, 0);

    // HR Sub bpm label
    lv_obj_t *label_hr_sub = lv_label_create(scr_vitals_home);
    lv_label_set_text(label_hr_sub, " bpm");
    lv_obj_align_to(label_hr_sub, label_hr, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);

    // HR caption label
    lv_obj_t *label_hr_cap = lv_label_create(scr_vitals_home);
    lv_label_set_text(label_hr_cap, "HR");
    lv_obj_align_to(label_hr_cap, label_hr, LV_ALIGN_OUT_TOP_MID, -5, -5);
    lv_obj_add_style(label_hr_cap, &style_lbl_red, 0);

    /*LV_IMG_DECLARE(heart);
    lv_obj_t *img1 = lv_img_create(scr_vitals_home);
    lv_img_set_src(img1, &heart);
    lv_obj_set_size(img1, 35, 33);
    lv_obj_align_to(img1, label_hr, LV_ALIGN_OUT_LEFT_MID, -5, 0);
    */

    // SpO2 Number label
    label_spo2 = lv_label_create(scr_vitals_home);
    lv_label_set_text(label_spo2, "98");
    lv_obj_align_to(label_spo2, NULL, LV_ALIGN_CENTER, 40, -30);
    lv_obj_add_style(label_spo2, &style_lbl_white, 0);
    // lv_obj_add_style(label_spo2, &style_spo2, 0);

    // SpO2 Sub % label
    lv_obj_t *label_spo2_sub = lv_label_create(scr_vitals_home);
    lv_label_set_text(label_spo2_sub, " %");
    lv_obj_align_to(label_spo2_sub, label_spo2, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);

    // SpO2 caption label
    lv_obj_t *label_spo2_cap = lv_label_create(scr_vitals_home);
    lv_label_set_text(label_spo2_cap, "SpO2");
    lv_obj_align_to(label_spo2_cap, label_spo2, LV_ALIGN_OUT_TOP_MID, -5, -5);
    lv_obj_add_style(label_spo2_cap, &style_lbl_red, 0);

    /*LV_IMG_DECLARE(o2);
    lv_obj_t *img2 = lv_img_create(scr_vitals_home);
    lv_img_set_src(img2, &o2);
    lv_obj_set_size(img2, 22, 35);
    lv_obj_align_to(img2, label_spo2, LV_ALIGN_OUT_LEFT_MID, -5, 0);
    */

    // BP Systolic Number label
    label_bp_val = lv_label_create(scr_vitals_home);
    lv_label_set_text(label_bp_val, "120 / 80");
    lv_obj_align_to(label_bp_val, NULL, LV_ALIGN_CENTER, -30, 45);
    lv_obj_add_style(label_bp_val, &style_lbl_white, 0);

    // BP Systolic Sub mmHg label
    lv_obj_t *label_bp_sys_sub = lv_label_create(scr_vitals_home);
    lv_label_set_text(label_bp_sys_sub, " mmHg");
    lv_obj_align_to(label_bp_sys_sub, label_bp_val, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);

    // BP Systolic caption label
    lv_obj_t *label_bp_sys_cap = lv_label_create(scr_vitals_home);
    lv_label_set_text(label_bp_sys_cap, "BP(Sys/Dia)");
    lv_obj_align_to(label_bp_sys_cap, label_bp_val, LV_ALIGN_OUT_TOP_MID, -5, -5);
    lv_obj_add_style(label_bp_sys_cap, &style_lbl_red, 0);

    // Temp Number label
    label_temp = lv_label_create(scr_vitals_home);
    lv_label_set_text(label_temp, "36.7");
    lv_obj_align_to(label_temp, NULL, LV_ALIGN_CENTER, 10, 95);
    lv_obj_add_style(label_temp, &style_lbl_white_small, 0);

    // Temp Sub deg C label
    lv_obj_t *label_temp_sub = lv_label_create(scr_vitals_home);
    lv_label_set_text(label_temp_sub, " °C");
    lv_obj_align_to(label_temp_sub, label_temp, LV_ALIGN_OUT_RIGHT_MID, 5, 0);

    // Temp caption label
    lv_obj_t *label_temp_cap = lv_label_create(scr_vitals_home);
    lv_label_set_text(label_temp_cap, "Temp");
    lv_obj_align_to(label_temp_cap, label_temp, LV_ALIGN_OUT_LEFT_MID, -10, 0);
    lv_obj_add_style(label_temp_cap, &style_lbl_red_small, 0);

    lv_obj_add_event_cb(scr_vitals_home, screen_event, LV_EVENT_GESTURE, NULL);

    curr_screen = SCR_VITALS;

    if (m_scroll_dir == SCROLL_LEFT)
        lv_scr_load_anim(scr_vitals_home, LV_SCR_LOAD_ANIM_MOVE_RIGHT, SCREEN_TRANS_TIME, 0, true);
    else
        lv_scr_load_anim(scr_vitals_home, LV_SCR_LOAD_ANIM_MOVE_LEFT, SCREEN_TRANS_TIME, 0, true);
}

void draw_scr_splash(void)
{
    scr_splash = lv_obj_create(NULL);

    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, 1);

    lv_style_set_bg_opa(&style, LV_OPA_COVER);
    lv_style_set_border_width(&style, 0);

    // lv_style_set_bg_grad(&style, &grad);
    lv_style_set_bg_color(&style, lv_color_black());

    lv_obj_add_style(scr_splash, &style, 0);

    LV_IMG_DECLARE(logo_round_50x50);
    lv_obj_t *img1 = lv_img_create(scr_splash);
    lv_img_set_src(img1, &logo_round_50x50);
    lv_obj_align(img1, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_size(img1, 50, 50);

    LV_IMG_DECLARE(logo_oneline);
    lv_obj_t *img_logo = lv_img_create(scr_splash);
    lv_img_set_src(img_logo, &logo_oneline);
    lv_obj_align_to(img_logo, img1, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);

    lv_obj_t *label_hpi = lv_label_create(scr_splash);
    lv_label_set_text(label_hpi, "HealthyPi Move");
    // lv_obj_add_style(label_hpi, &style_h1, 0);
    // lv_obj_center(label_hpi);
    lv_obj_align(label_hpi, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *label_version = lv_label_create(scr_splash);
    lv_label_set_text(label_version, "v" APP_VERSION_STRING);
    lv_obj_align(label_version, LV_ALIGN_BOTTOM_MID, 0, -20);

    lv_scr_load_anim(scr_splash, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, true);
}

void draw_scr_ppg(enum scroll_dir m_scroll_dir)
{
    scr_ppg = lv_obj_create(NULL);
    draw_header_minimal(scr_ppg);

    // Create Chart 1 - ECG
    chart1 = lv_chart_create(scr_ppg);
    lv_obj_set_size(chart1, 200, 100);
    lv_obj_set_style_bg_color(chart1, lv_color_black(), LV_STATE_DEFAULT);

    lv_obj_set_style_size(chart1, 0, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(chart1, 0, LV_PART_MAIN);
    lv_chart_set_point_count(chart1, PPG_DISP_WINDOW_SIZE);
    // lv_chart_set_type(chart1, LV_CHART_TYPE_LINE);   /*Show lines and points too*
    lv_chart_set_range(chart1, LV_CHART_AXIS_PRIMARY_Y, -1000, 1000);
    // lv_chart_set_range(chart1, LV_CHART_AXIS_SECONDARY_Y, 0, 1000);
    lv_chart_set_div_line_count(chart1, 0, 0);
    lv_chart_set_update_mode(chart1, LV_CHART_UPDATE_MODE_CIRCULAR);
    // lv_style_set_border_width(&styles->bg, LV_STATE_DEFAULT, BORDER_WIDTH);
    lv_obj_align(chart1, LV_ALIGN_CENTER, 0, -35);
    ser1 = lv_chart_add_series(chart1, lv_palette_main(LV_PALETTE_ORANGE), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_line_width(chart1, 3, LV_PART_ITEMS);

    // HR Number label
    label_hr = lv_label_create(scr_ppg);
    lv_label_set_text(label_hr, "--");
    lv_obj_align_to(label_hr, NULL, LV_ALIGN_CENTER, -50, 50);
    lv_obj_add_style(label_hr, &style_lbl_white, 0);

    // HR Sub bpm label
    lv_obj_t *label_hr_sub = lv_label_create(scr_ppg);
    lv_label_set_text(label_hr_sub, " bpm");
    lv_obj_align_to(label_hr_sub, label_hr, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);

    // HR caption label
    lv_obj_t *label_hr_cap = lv_label_create(scr_ppg);
    lv_label_set_text(label_hr_cap, "HR");
    lv_obj_align_to(label_hr_cap, label_hr, LV_ALIGN_OUT_TOP_MID, -5, -5);
    lv_obj_add_style(label_hr_cap, &style_lbl_red, 0);

    /*LV_IMG_DECLARE(heart);
    lv_obj_t *img1 = lv_img_create(scr_ppg);
    lv_img_set_src(img1, &heart);
    lv_obj_set_size(img1, 35, 33);
    lv_obj_align_to(img1, label_hr, LV_ALIGN_OUT_LEFT_MID, -5, 0);*/

    // SpO2 Number label
    label_spo2 = lv_label_create(scr_ppg);
    lv_label_set_text(label_spo2, "--");
    lv_obj_align_to(label_spo2, NULL, LV_ALIGN_CENTER, 30, 50);
    lv_obj_add_style(label_spo2, &style_lbl_white, 0);

    // SpO2 Sub % label
    lv_obj_t *label_spo2_sub = lv_label_create(scr_ppg);
    lv_label_set_text(label_spo2_sub, " %");
    lv_obj_align_to(label_spo2_sub, label_spo2, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);

    // SpO2 caption label
    lv_obj_t *label_spo2_cap = lv_label_create(scr_ppg);
    lv_label_set_text(label_spo2_cap, "SpO2");
    lv_obj_align_to(label_spo2_cap, label_spo2, LV_ALIGN_OUT_TOP_MID, -5, -5);
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
    lv_obj_align_to(img2, label_spo2, LV_ALIGN_OUT_LEFT_MID, -5, 0);*/

    lv_obj_add_event_cb(scr_ppg, screen_event, LV_EVENT_GESTURE, NULL);
    curr_screen = SCR_PLOT_PPG;

    if (m_scroll_dir == SCROLL_LEFT)
        lv_scr_load_anim(scr_ppg, LV_SCR_LOAD_ANIM_MOVE_RIGHT, SCREEN_TRANS_TIME, 0, true);
    else
        lv_scr_load_anim(scr_ppg, LV_SCR_LOAD_ANIM_MOVE_LEFT, SCREEN_TRANS_TIME, 0, true);

    hw_bpt_start_est();
}

static void draw_scr_ecg(enum scroll_dir m_scroll_dir)
{
    scr_ecg = lv_obj_create(NULL);
    draw_header_minimal(scr_ecg);

    // Create Chart 1 - ECG
    chart1 = lv_chart_create(scr_ecg);
    lv_obj_set_size(chart1, 200, 100);
    lv_obj_set_style_bg_color(chart1, lv_color_black(), LV_STATE_DEFAULT);

    lv_obj_set_style_size(chart1, 0, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(chart1, 0, LV_PART_MAIN);
    lv_chart_set_point_count(chart1, DISP_WINDOW_SIZE_ECG);
    // lv_chart_set_type(chart1, LV_CHART_TYPE_LINE);   /*Show lines and points too*
    lv_chart_set_range(chart1, LV_CHART_AXIS_PRIMARY_Y, -1000, 1000);
    // lv_chart_set_range(chart1, LV_CHART_AXIS_SECONDARY_Y, 0, 1000);
    lv_chart_set_div_line_count(chart1, 0, 0);
    lv_chart_set_update_mode(chart1, LV_CHART_UPDATE_MODE_CIRCULAR);
    // lv_style_set_border_width(&styles->bg, LV_STATE_DEFAULT, BORDER_WIDTH);
    lv_obj_align(chart1, LV_ALIGN_CENTER, 0, -35);
    ser1 = lv_chart_add_series(chart1, lv_palette_main(LV_PALETTE_YELLOW), LV_CHART_AXIS_PRIMARY_Y);

    // HR Number label
    label_hr = lv_label_create(scr_ecg);
    lv_label_set_text(label_hr, "--");
    lv_obj_align_to(label_hr, NULL, LV_ALIGN_CENTER, -10, 50);
    lv_obj_add_style(label_hr, &style_lbl_white, 0);

    // HR Sub bpm label
    lv_obj_t *label_hr_sub = lv_label_create(scr_ecg);
    lv_label_set_text(label_hr_sub, " bpm");
    lv_obj_align_to(label_hr_sub, label_hr, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);

    // HR caption label
    lv_obj_t *label_hr_cap = lv_label_create(scr_ecg);
    lv_label_set_text(label_hr_cap, "HR");
    lv_obj_align_to(label_hr_cap, label_hr, LV_ALIGN_OUT_TOP_MID, -5, -5);
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

    lv_obj_add_event_cb(scr_ecg, screen_event, LV_EVENT_GESTURE, NULL);
    curr_screen = SCR_PLOT_ECG;

    if (m_scroll_dir == SCROLL_LEFT)
        lv_scr_load_anim(scr_ecg, LV_SCR_LOAD_ANIM_MOVE_RIGHT, SCREEN_TRANS_TIME, 0, true);
    else
        lv_scr_load_anim(scr_ecg, LV_SCR_LOAD_ANIM_MOVE_LEFT, SCREEN_TRANS_TIME, 0, true);
}

static void draw_scr_eda(enum scroll_dir m_scroll_dir)
{
    scr_eda = lv_obj_create(NULL);
    draw_header_minimal(scr_eda);

    // Create Chart 1 - ECG
    chart1 = lv_chart_create(scr_eda);
    lv_obj_set_size(chart1, 200, 100);
    lv_obj_set_style_bg_color(chart1, lv_color_black(), LV_STATE_DEFAULT);

    lv_obj_set_style_size(chart1, 0, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(chart1, 0, LV_PART_MAIN);
    lv_chart_set_point_count(chart1, DISP_WINDOW_SIZE_EDA);
    lv_chart_set_range(chart1, LV_CHART_AXIS_PRIMARY_Y, -1000, 1000);
    lv_chart_set_div_line_count(chart1, 0, 0);
    lv_chart_set_update_mode(chart1, LV_CHART_UPDATE_MODE_CIRCULAR);
    lv_obj_align(chart1, LV_ALIGN_CENTER, 0, -35);
    ser1 = lv_chart_add_series(chart1, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);

    // Bottom signal label
    lv_obj_t *label_signal = lv_label_create(scr_eda);
    lv_label_set_text(label_signal, "EDA/GSR");
    lv_obj_align(label_signal, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_add_style(label_signal, &style_lbl_white_small, 0);

    lv_obj_add_event_cb(scr_eda, screen_event, LV_EVENT_GESTURE, NULL);
    curr_screen = SCR_PLOT_EDA;

    if (m_scroll_dir == SCROLL_LEFT)
    {
        lv_scr_load_anim(scr_eda, LV_SCR_LOAD_ANIM_MOVE_RIGHT, SCREEN_TRANS_TIME, 0, true);
    }
    else
    {
        lv_scr_load_anim(scr_eda, LV_SCR_LOAD_ANIM_MOVE_LEFT, SCREEN_TRANS_TIME, 0, true);
    }
}

static void hpi_disp_bpt_update_progress(int progress)
{
    if (label_progress == NULL || bar_bpt_progress == NULL)
    {
        return;
    }

    lv_bar_set_value(bar_bpt_progress, progress, LV_ANIM_OFF);
    lv_label_set_text_fmt(label_progress, "Progress: %d %%", progress);
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
        draw_scr_bpt_home();
    }
}

lv_obj_t *label_cal_done;
lv_obj_t *btn_bpt_cal_start;
lv_obj_t *btn_bpt_cal_exit;

static void draw_scr_bpt_calibrate(void)
{
    scr_bpt_calibrate = lv_obj_create(NULL);
    draw_header_minimal(scr_bpt_calibrate);

    // Draw Blood Pressure label

    lv_obj_t *label_bp = lv_label_create(scr_bpt_calibrate);
    lv_label_set_text(label_bp, "BP Calibration");
    lv_obj_align(label_bp, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_add_style(label_bp, &style_lbl_white_14, 0);

    // Create Chart 1 - ECG
    chart1 = lv_chart_create(scr_bpt_calibrate);
    lv_obj_set_size(chart1, 200, 75);
    lv_obj_set_style_bg_color(chart1, lv_color_black(), LV_STATE_DEFAULT);

    lv_obj_set_style_size(chart1, 0, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(chart1, 0, LV_PART_MAIN);
    lv_chart_set_point_count(chart1, PPG_DISP_WINDOW_SIZE);
    // lv_chart_set_type(chart1, LV_CHART_TYPE_LINE);   /*Show lines and points too*
    lv_chart_set_range(chart1, LV_CHART_AXIS_PRIMARY_Y, -1000, 1000);
    // lv_chart_set_range(chart1, LV_CHART_AXIS_SECONDARY_Y, 0, 1000);
    lv_chart_set_div_line_count(chart1, 0, 0);
    lv_chart_set_update_mode(chart1, LV_CHART_UPDATE_MODE_CIRCULAR);
    // lv_style_set_border_width(&styles->bg, LV_STATE_DEFAULT, BORDER_WIDTH);
    lv_obj_align_to(chart1, label_bp, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
    ser1 = lv_chart_add_series(chart1, lv_palette_main(LV_PALETTE_ORANGE), LV_CHART_AXIS_PRIMARY_Y);

    label_cal_done = lv_label_create(scr_bpt_calibrate);
    lv_label_set_text(label_cal_done, "Calibration\nDone");
    lv_obj_align_to(label_cal_done, NULL, LV_ALIGN_CENTER, -30, -25);
    lv_obj_set_style_text_align(label_cal_done, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_style(label_cal_done, &style_lbl_white, 0);
    lv_obj_add_flag(label_cal_done, LV_OBJ_FLAG_HIDDEN);

    // Draw Progress bar
    bar_bpt_progress = lv_bar_create(scr_bpt_calibrate);
    lv_obj_set_size(bar_bpt_progress, 200, 5);
    lv_obj_align_to(bar_bpt_progress, chart1, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
    lv_bar_set_value(bar_bpt_progress, 0, LV_ANIM_OFF);

    // Draw Progress bar label
    label_progress = lv_label_create(scr_bpt_calibrate);
    lv_label_set_text(label_progress, "Progress: --");
    lv_obj_align_to(label_progress, bar_bpt_progress, LV_ALIGN_OUT_BOTTOM_MID, 0, 3);
    lv_obj_add_style(label_progress, &style_lbl_white_14, 0);

    // Draw button to start BP calibration

    btn_bpt_cal_start = lv_btn_create(scr_bpt_calibrate);
    lv_obj_add_event_cb(btn_bpt_cal_start, scr_bpt_btn_cal_start_handler, LV_EVENT_ALL, NULL);
    lv_obj_align_to(btn_bpt_cal_start, NULL, LV_ALIGN_BOTTOM_MID, -110, -40);
    lv_obj_set_height(btn_bpt_cal_start, 55);
    lv_obj_set_width(btn_bpt_cal_start, 240);
    lv_obj_set_style_bg_color(btn_bpt_cal_start, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN);

    lv_obj_t *label_btn_bpt = lv_label_create(btn_bpt_cal_start);
    lv_label_set_text(label_btn_bpt, "Start");
    lv_obj_add_style(label_btn_bpt, &style_lbl_white_small, 0);
    lv_obj_center(label_btn_bpt);

    btn_bpt_cal_exit = lv_btn_create(scr_bpt_calibrate);
    lv_obj_add_event_cb(btn_bpt_cal_exit, scr_bpt_btn_cal_exit_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_align_to(btn_bpt_cal_exit, NULL, LV_ALIGN_BOTTOM_MID, -110, -40);
    lv_obj_set_height(btn_bpt_cal_exit, 55);
    lv_obj_set_width(btn_bpt_cal_exit, 240);
    lv_obj_set_style_bg_color(btn_bpt_cal_exit, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);

    lv_obj_t *label_btn_bpt_exit = lv_label_create(btn_bpt_cal_exit);
    lv_label_set_text(label_btn_bpt_exit, "Exit");
    lv_obj_add_style(label_btn_bpt_exit, &style_lbl_white_small, 0);
    lv_obj_center(label_btn_bpt_exit);

    // Hide exit button by default
    lv_obj_add_flag(btn_bpt_cal_exit, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_event_cb(scr_bpt_calibrate, screen_event, LV_EVENT_GESTURE, NULL);
    curr_screen = SCR_BPT_CALIBRATE;

    lv_scr_load_anim(scr_bpt_calibrate, LV_SCR_LOAD_ANIM_MOVE_TOP, SCREEN_TRANS_TIME, 0, true);
}

lv_obj_t *btn_bpt_measure_exit;

static void scr_bpt_btn_measure_exit_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        bpt_meas_done_flag = false;
        bpt_meas_last_progress = 0;
        bpt_meas_last_status = 0;
        bpt_meas_started = false;

        draw_scr_bpt_home();
    }
}

static void scr_bpt_btn_measure_start_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        // lv_obj_add_flag(btn_bpt_measure_start, LV_OBJ_FLAG_HIDDEN);

        bpt_meas_done_flag = false;
        bpt_meas_last_progress = 0;
        bpt_meas_last_status = 0;

        lv_obj_add_flag(label_bp_val, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(label_bp_sys_sub, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(label_bp_sys_cap, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(chart1, LV_OBJ_FLAG_HIDDEN);
        // lv_obj__flag(btn_bpt_measure_start, LV_OBJ_FLAG_HIDDEN);

        bpt_meas_started = true;
        hw_bpt_start_est();
    }
}

static void draw_scr_bpt_measure(void)
{
    scr_bpt_measure = lv_obj_create(NULL);
    draw_header_minimal(scr_bpt_measure);

    // Draw Blood Pressure label

    lv_obj_t *label_bp = lv_label_create(scr_bpt_measure);
    lv_label_set_text(label_bp, "BP Measurement");
    lv_obj_align(label_bp, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_add_style(label_bp, &style_lbl_white_14, 0);

    // Create Chart 1 - ECG
    chart1 = lv_chart_create(scr_bpt_measure);
    lv_obj_set_size(chart1, 200, 75);
    lv_obj_set_style_bg_color(chart1, lv_color_black(), LV_STATE_DEFAULT);

    lv_obj_set_style_size(chart1, 0, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(chart1, 0, LV_PART_MAIN);
    lv_chart_set_point_count(chart1, PPG_DISP_WINDOW_SIZE);
    // lv_chart_set_type(chart1, LV_CHART_TYPE_LINE);   /*Show lines and points too*
    lv_chart_set_range(chart1, LV_CHART_AXIS_PRIMARY_Y, -1000, 1000);
    // lv_chart_set_range(chart1, LV_CHART_AXIS_SECONDARY_Y, 0, 1000);
    lv_chart_set_div_line_count(chart1, 0, 0);
    lv_chart_set_update_mode(chart1, LV_CHART_UPDATE_MODE_CIRCULAR);
    // lv_style_set_border_width(&styles->bg, LV_STATE_DEFAULT, BORDER_WIDTH);
    lv_obj_align_to(chart1, label_bp, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
    ser1 = lv_chart_add_series(chart1, lv_palette_main(LV_PALETTE_ORANGE), LV_CHART_AXIS_PRIMARY_Y);

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
    lv_obj_align_to(bar_bpt_progress, chart1, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
    lv_bar_set_value(bar_bpt_progress, 0, LV_ANIM_OFF);

    // Draw Progress bar label
    label_progress = lv_label_create(scr_bpt_measure);
    lv_label_set_text(label_progress, "Progress: --");
    lv_obj_align_to(label_progress, bar_bpt_progress, LV_ALIGN_OUT_BOTTOM_MID, 0, 3);

    // Draw button to start BP measurement

    btn_bpt_measure_start = lv_btn_create(scr_bpt_measure);
    lv_obj_add_event_cb(btn_bpt_measure_start, scr_bpt_btn_measure_start_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_align_to(btn_bpt_measure_start, NULL, LV_ALIGN_BOTTOM_MID, -110, -40);
    lv_obj_set_height(btn_bpt_measure_start, 55);
    lv_obj_set_width(btn_bpt_measure_start, 240);
    lv_obj_set_style_bg_color(btn_bpt_measure_start, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN);

    lv_obj_t *label_btn_bpt = lv_label_create(btn_bpt_measure_start);
    lv_label_set_text(label_btn_bpt, "Start");
    lv_obj_add_style(label_btn_bpt, &style_lbl_white_small, 0);
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

    lv_obj_add_event_cb(scr_bpt_measure, screen_event, LV_EVENT_GESTURE, NULL);
    curr_screen = SCR_BPT_MEASURE;

    lv_scr_load_anim(scr_bpt_measure, LV_SCR_LOAD_ANIM_MOVE_BOTTOM, SCREEN_TRANS_TIME, 0, true);
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

void draw_scr_bpt_home(void)
{
    scr_bpt_home = lv_obj_create(NULL);
    draw_header_minimal(scr_bpt_home);

    // Draw button to measure BP

    lv_obj_t *btn_bpt_measure_start = lv_btn_create(scr_bpt_home);
    lv_obj_add_event_cb(btn_bpt_measure_start, scr_bpt_measure_btn_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_align(btn_bpt_measure_start, LV_ALIGN_CENTER, 0, -40);
    lv_obj_set_height(btn_bpt_measure_start, 50);

    lv_obj_t *label_btn_bpt_measure = lv_label_create(btn_bpt_measure_start);
    lv_label_set_text(label_btn_bpt_measure, "Measure BP");
    lv_obj_center(label_btn_bpt_measure);

    // Draw button to calibrate BP

    lv_obj_t *btn_bpt_calibrate = lv_btn_create(scr_bpt_home);
    lv_obj_add_event_cb(btn_bpt_calibrate, scr_bpt_calib_btn_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_align(btn_bpt_calibrate, LV_ALIGN_CENTER, 0, 40);
    lv_obj_set_height(btn_bpt_calibrate, 50);

    lv_obj_t *label_btn_bpt_calibrate = lv_label_create(btn_bpt_calibrate);
    lv_label_set_text(label_btn_bpt_calibrate, "Calibrate BP");
    lv_obj_center(label_btn_bpt_calibrate);

    lv_obj_add_event_cb(scr_bpt_home, screen_event, LV_EVENT_GESTURE, NULL);
    curr_screen = SCR_BPT_HOME;

    lv_scr_load_anim(scr_bpt_home, LV_SCR_LOAD_ANIM_MOVE_LEFT, SCREEN_TRANS_TIME, 0, true);

    // hw_bpt_stop();
}

static lv_obj_t *meter;

static void set_value(void *indic, int32_t v)
{
    lv_meter_set_indicator_end_value(meter, (lv_meter_indicator_t *)indic, v);
}

void draw_scr_clockface(void)
{
    scr_clock = lv_obj_create(NULL);
    meter = lv_meter_create(scr_clock);
    lv_obj_set_size(meter, 220, 220);
    lv_obj_center(meter);
    lv_obj_set_style_bg_color(meter, lv_color_white(), LV_STATE_DEFAULT);

    /*Create a scale for the minutes*/
    /*61 ticks in a 360 degrees range (the last and the first line overlaps)*/
    lv_meter_scale_t *scale_min = lv_meter_add_scale(meter);
    lv_meter_set_scale_ticks(meter, scale_min, 61, 1, 10, lv_color_make(22, 83, 105));
    lv_meter_set_scale_range(meter, scale_min, 0, 60, 360, 270);

    /*Create another scale for the hours. It's only visual and contains only major ticks*/
    lv_meter_scale_t *scale_hour = lv_meter_add_scale(meter);
    lv_meter_set_scale_ticks(meter, scale_hour, 12, 0, 0, lv_color_make(22, 83, 105));           /*12 ticks*/
    lv_meter_set_scale_major_ticks(meter, scale_hour, 1, 2, 20, lv_color_make(22, 83, 105), 10); /*Every tick is major*/
    lv_meter_set_scale_range(meter, scale_hour, 1, 12, 330, 300);                                /*[1..12] values in an almost full circle*/

    LV_IMG_DECLARE(img_hand);

    /*Add a the hands from images*/
    lv_meter_indicator_t *indic_min = lv_meter_add_needle_img(meter, scale_min, &img_hand, 5, 5);
    lv_meter_indicator_t *indic_hour = lv_meter_add_needle_img(meter, scale_min, &img_hand, 5, 5);

    /*Create an animation to set the value*/
    /*
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_exec_cb(&a, set_value);
    lv_anim_set_values(&a, 0, 60);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_time(&a, 2000); //2 sec for 1 turn of the minute hand (1 hour)
    lv_anim_set_var(&a, indic_min);
    lv_anim_start(&a);

    lv_anim_set_var(&a, indic_hour);
    lv_anim_set_time(&a, 24000); //24 sec for 1 turn of the hour hand
    lv_anim_set_values(&a, 0, 60);
    lv_anim_start(&a);
    */

    // ProtoCentral logo
    /* LV_IMG_DECLARE(logo_oneline);
     lv_obj_t *img1 = lv_img_create(scr_clock);
     lv_img_set_src(img1, &logo_oneline);
     lv_obj_align_to(img1, meter, LV_ALIGN_CENTER, 0, 30);
     lv_obj_set_size(img1, 150, 10);

     // ProtoCentral round logo
     LV_IMG_DECLARE(logo_round_50x50);
     lv_obj_t *img2 = lv_img_create(scr_clock);
     lv_img_set_src(img2, &logo_round_50x50);
     lv_obj_align_to(img2, meter, LV_ALIGN_CENTER, 0, -30);
     lv_obj_set_size(img2, 50, 50);
     */

    curr_screen = SCR_CLOCK;

    lv_scr_load_anim(scr_clock, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
}

void m_cycle_screens(bool scr_back)
{
    switch (curr_screen)
    {
    case SCR_VITALS:
        if (scr_back)
        {
            draw_scr_eda(SCROLL_LEFT);
        }
        else
        {
            draw_scr_ppg(SCROLL_RIGHT);
        }
        break;
    case SCR_PLOT_PPG:
        if (scr_back)
        {
            draw_scr_vitals_home(SCROLL_LEFT);
        }
        else
        {
            draw_scr_ecg(SCROLL_RIGHT);
        }
        // draw_scr_ecg();
        break;
    case SCR_PLOT_ECG:
        if (scr_back)
        {
            draw_scr_ppg(SCROLL_LEFT);
        }
        else
        {
            draw_scr_bpt_home();
        }
        break;
    case SCR_BPT_HOME:
        if (scr_back)
        {
            draw_scr_ecg(SCROLL_LEFT);
        }
        else
        {
            draw_scr_eda(SCROLL_RIGHT);
        }

        break;
    case SCR_BPT_CALIBRATE:
    case SCR_BPT_MEASURE:
        if (scr_back)
        {
            draw_scr_eda(SCROLL_LEFT);
        }
        else
        {
            draw_scr_bpt_home();
        }
        break;
    case SCR_PLOT_EDA:
        if (scr_back)
        {
            draw_scr_bpt_home();
        }
        else
        {
            draw_scr_vitals_home(SCROLL_RIGHT);
        }
        break;
    }
}

void display_screens_thread(void)
{
    k_sem_take(&sem_hw_inited, K_FOREVER);

    display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display_dev))
    {
        // LOG_ERR("Device not ready, aborting test");
        return;
    }

    display_set_brightness(display_dev, 20);

    if (!device_is_ready(touch_dev))
    {
        LOG_WRN("Device touch not ready.");
    }

    // Init all styles globally
    display_init_styles();

    // Setup LVGL Input Device
    lv_indev_drv_init(&m_keypad_drv);
    m_keypad_drv.type = LV_INDEV_TYPE_KEYPAD;
    m_keypad_drv.read_cb = keypad_read;
    m_keypad_indev = lv_indev_drv_register(&m_keypad_drv);

    touch_indev = lv_indev_get_next(NULL);
    while (touch_indev)
    {
        if (lv_indev_get_type(touch_indev) == LV_INDEV_TYPE_POINTER)
        {

            break;
        }
        touch_indev = lv_indev_get_next(touch_indev);
    }

    display_blanking_off(display_dev);

    printk("Display screens inited");

    // k_sem_give(&sem_disp_inited);
    // draw_scr_menu("A\nB\n");
    // draw_scr_home();
    // draw_scr_splash();
    // lv_task_handler();
    // k_sleep(K_MSEC(2000));

    // draw_scr_vitals_home();
    //  draw_scr_clockface();
    //   draw_scr_charts();

    draw_scr_ppg(SCROLL_RIGHT);
    //    draw_scr_ecg();
    // draw_scr_bpt_home();
    //   draw_scr_eda();

    struct hpi_ecg_bioz_sensor_data_t ecg_bioz_sensor_sample;
    struct hpi_ppg_sensor_data_t ppg_sensor_sample;

    uint8_t batt_level;
    int32_t temp_val;

    int temp_disp_counter = 0;
    int batt_refresh_counter = 0;

    int m_disp_inact_refresh_counter = 0;
    int m_disp_status_off = false;

    int scr_ppg_hr_spo2_refresh_counter = 0;

    k_sem_give(&sem_sampling_start);

    while (1)
    {

        if (curr_screen == SCR_PLOT_PPG)
        {
            if (k_msgq_get(&q_plot_ppg, &ppg_sensor_sample, K_NO_WAIT) == 0)
            {
                hpi_disp_draw_plotPPG((float)((ppg_sensor_sample.raw_red * 1.0000)));
                if (scr_ppg_hr_spo2_refresh_counter >= (1000 / DISP_THREAD_REFRESH_INT_MS))
                {
                    hpi_disp_update_hr(ppg_sensor_sample.hr);
                    hpi_disp_update_spo2(ppg_sensor_sample.spo2);
                    scr_ppg_hr_spo2_refresh_counter = 0;
                }
                else
                {
                    scr_ppg_hr_spo2_refresh_counter++;
                }
                // hpi_disp_update_hr(ppg_sensor_sample.hr);
                // hpi_disp_update_spo2(ppg_sensor_sample.spo2);
            }
        }

        if (curr_screen == SCR_BPT_MEASURE)
        {
            if (k_msgq_get(&q_plot_ppg, &ppg_sensor_sample, K_NO_WAIT) == 0)
            {
                if (bpt_meas_started == true)
                {
                    hpi_disp_draw_plotPPG((float)((ppg_sensor_sample.raw_red * 1.0000)));

                    if (bpt_meas_done_flag == false)
                    {
                        if (bpt_meas_last_status != ppg_sensor_sample.bpt_status)
                        {
                            bpt_meas_last_status = ppg_sensor_sample.bpt_status;
                            printk("BPT Status: %d", ppg_sensor_sample.bpt_status);
                        }
                        if (bpt_meas_last_progress != ppg_sensor_sample.bpt_progress)
                        {
                            hpi_disp_bpt_update_progress(ppg_sensor_sample.bpt_progress);
                            bpt_meas_last_progress = ppg_sensor_sample.bpt_progress;
                        }
                        if (bpt_meas_last_progress >= 100)
                        {
                            printk("BPT Meas progress 100");

                            global_bp_dia = ppg_sensor_sample.bp_dia;
                            global_bp_sys = ppg_sensor_sample.bp_sys;

                            hw_bpt_stop();
                            ppg_data_stop();

                            printk("BPT Done: %d / %d", global_bp_sys, global_bp_dia);
                            bpt_meas_done_flag = true;
                            bpt_meas_started = false;
                            hpi_disp_update_bp(global_bp_sys, global_bp_dia);

                            if (curr_screen == SCR_BPT_MEASURE)
                            {
                                lv_obj_clear_flag(label_bp_val, LV_OBJ_FLAG_HIDDEN);
                                lv_obj_clear_flag(label_bp_sys_sub, LV_OBJ_FLAG_HIDDEN);
                                lv_obj_clear_flag(label_bp_sys_cap, LV_OBJ_FLAG_HIDDEN);
                                lv_obj_add_flag(chart1, LV_OBJ_FLAG_HIDDEN);

                                lv_obj_add_flag(btn_bpt_measure_start, LV_OBJ_FLAG_HIDDEN);
                                lv_obj_clear_flag(btn_bpt_measure_exit, LV_OBJ_FLAG_HIDDEN);
                            }
                            hpi_disp_bpt_update_progress(ppg_sensor_sample.bpt_progress);
                        }
                    }
                    lv_disp_trig_activity(NULL);
                }
            }
        }

        if (curr_screen == SCR_BPT_CALIBRATE)
        {
            if (k_msgq_get(&q_plot_ppg, &ppg_sensor_sample, K_NO_WAIT) == 0)
            {
                hpi_disp_draw_plotPPG((float)((ppg_sensor_sample.raw_red * 1.0000)));
                if (bpt_cal_done_flag == false)
                {
                    if (bpt_cal_last_status != ppg_sensor_sample.bpt_status)
                    {
                        bpt_cal_last_status = ppg_sensor_sample.bpt_status;
                        printk("BPT Status: %d", ppg_sensor_sample.bpt_status);
                    }
                    if (bpt_cal_last_progress != ppg_sensor_sample.bpt_progress)
                    {
                        bpt_cal_last_progress = ppg_sensor_sample.bpt_progress;
                        hpi_disp_bpt_update_progress(ppg_sensor_sample.bpt_progress);
                    }
                    if (ppg_sensor_sample.bpt_progress == 100)
                    {
                        hw_bpt_stop();

                        if (ppg_sensor_sample.bpt_status == 2)
                        {
                            printk("Calibration done");
                        }
                        bpt_cal_done_flag = true;

                        hw_bpt_get_calib();

                        ppg_data_stop();

                        lv_obj_add_flag(chart1, LV_OBJ_FLAG_HIDDEN);
                        lv_obj_clear_flag(label_cal_done, LV_OBJ_FLAG_HIDDEN);

                        lv_obj_add_flag(btn_bpt_cal_start, LV_OBJ_FLAG_HIDDEN);
                        lv_obj_clear_flag(btn_bpt_cal_exit, LV_OBJ_FLAG_HIDDEN);
                    }
                    hpi_disp_bpt_update_progress(ppg_sensor_sample.bpt_progress);
                    lv_disp_trig_activity(NULL);
                }
            }
        }

        if (curr_screen == SCR_PLOT_ECG)
        {
            if (k_msgq_get(&q_plot_ecg_bioz, &ecg_bioz_sensor_sample, K_NO_WAIT) == 0)
            {

                hpi_disp_draw_plotECG((float)((ecg_bioz_sensor_sample.ecg_sample / 1000.0000)), ecg_bioz_sensor_sample.ecg_lead_off);
                hpi_disp_update_hr(ecg_bioz_sensor_sample.hr_sample);
            }
        }

        if (curr_screen == SCR_PLOT_EDA)
        {
            if (k_msgq_get(&q_plot_ecg_bioz, &ecg_bioz_sensor_sample, K_NO_WAIT) == 0)
            {
                hpi_disp_draw_plotEDA((float)((ecg_bioz_sensor_sample.bioz_sample / 1000.0000)));
            }
        }

        if (curr_screen == SCR_VITALS)
        {
            if (temp_disp_counter >= (1000 / DISP_THREAD_REFRESH_INT_MS)) // Once a second
            {
                temp_val = read_temp();
                hpi_disp_update_temp(temp_val);
                temp_disp_counter = 0;
            }
            else
            {
                temp_disp_counter++;
            }
            // lv_task_handler();
        }

        if (batt_refresh_counter >= (1000 / DISP_THREAD_REFRESH_INT_MS))
        {
            // Fetch and update battery level
            //batt_level = read_battery_level();
            //hpi_disp_update_batt_level(batt_level);
            batt_refresh_counter = 0;
        }
        else
        {
            batt_refresh_counter++;
        }

        if (m_disp_inact_refresh_counter >= (3000 / DISP_THREAD_REFRESH_INT_MS))
        {
            int inactivity_time = lv_disp_get_inactive_time(NULL);
            // printk("Inactivity time: %d", inactivity_time);
            if (inactivity_time > 20000)
            {
                if (m_disp_status_off == false)
                {
                    printk("Display off");

                    display_set_brightness(display_dev, 0);
                    //  display_blanking_on(display_dev);

                    m_disp_status_off = true;
                }
            }
            else
            {
                if (m_disp_status_off == true)
                {
                    printk("Display on");
                    display_set_brightness(display_dev, 20);
                    //  display_blanking_off(display_dev);
                    m_disp_status_off = false;
                }
            }
        }
        else
        {
            m_disp_inact_refresh_counter++;
        }

        lv_task_handler();

        k_sleep(K_MSEC(DISP_THREAD_REFRESH_INT_MS));
    }
}

#define DISPLAY_SCREENS_THREAD_STACKSIZE 8192
#define DISPLAY_SCREENS_THREAD_PRIORITY 5

K_THREAD_DEFINE(display_screens_thread_id, DISPLAY_SCREENS_THREAD_STACKSIZE, display_screens_thread, NULL, NULL, NULL, DISPLAY_SCREENS_THREAD_PRIORITY, 0, 0);
