#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>
#include <stdio.h>
#include <zephyr/smf.h>
#include <app_version.h>
#include <time.h>
#include <zephyr/posix/time.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/pm/device.h>
#include <zephyr/zbus/zbus.h>

#include "hw_module.h"
#include "power_ctrl.h"
#include "sampling_module.h"
#include "ui/move_ui.h"

LOG_MODULE_REGISTER(display_module, LOG_LEVEL_DBG);

// LVGL Common Objects
// static lv_indev_drv_t m_keypad_drv;
// static lv_indev_t *m_keypad_indev = NULL;

extern const struct device *display_dev;

lv_indev_t *touch_indev;

static lv_obj_t *label_temp;

// LVGL Screens
lv_obj_t *scr_splash;
lv_obj_t *scr_menu;

lv_obj_t *ui_hour_group;
lv_obj_t *ui_label_hour;
lv_obj_t *ui_label_min;
lv_obj_t *ui_label_date;

lv_obj_t *ui_step_group;

lv_obj_t *ui_dailymission_group;

static lv_obj_t *lbl_time;

// LVGL Styles
static lv_style_t style_sub;
static lv_style_t style_hr;
static lv_style_t style_spo2;
static lv_style_t style_rr;
static lv_style_t style_temp;
static lv_style_t style_scr_back;
static lv_style_t style_batt_sym;
static lv_style_t style_batt_percent;

static lv_style_t style_info;
static lv_style_t style_icon;
lv_style_t style_scr_black;

lv_style_t style_lbl_red;
lv_style_t style_lbl_red_small;
lv_style_t style_lbl_white;
lv_style_t style_lbl_white_small;
lv_style_t style_lbl_orange;
lv_style_t style_lbl_white_tiny;
lv_style_t style_lbl_white_14;
lv_style_t style_lbl_black_small;

static lv_style_t style_btn;

static lv_obj_t *roller_session_select;
static lv_style_t style_scr_back;

lv_obj_t *btn_bpt_start_stop;

static lv_obj_t *label_batt_level;
static lv_obj_t *label_batt_level_val;
lv_obj_t *cui_battery_percent;

bool display_inited = false;

static volatile uint8_t hpi_disp_curr_brightness = DISPLAY_DEFAULT_BRIGHTNESS;

int curr_screen = SCR_VITALS;

K_SEM_DEFINE(sem_disp_inited, 0, 1);
K_MSGQ_DEFINE(q_plot_ecg_bioz, sizeof(struct hpi_ecg_bioz_sensor_data_t), 64, 1);
K_MSGQ_DEFINE(q_plot_ppg, sizeof(struct hpi_ppg_sensor_data_t), 64, 1);
K_MSGQ_DEFINE(q_plot_hrv, sizeof(struct hpi_computed_hrv_t), 64, 1);

bool bpt_cal_started = false;
int global_bp_sys = 0;
int global_bp_dia = 0;
int global_hr = 0;

// LV_IMG_DECLARE(pc_logo_bg3);
LV_IMG_DECLARE(pc_move_bg_200);
// LV_IMG_DECLARE(pc_logo_bg3);
LV_IMG_DECLARE(logo_round_white);

static bool m_display_active = true;

uint16_t disp_thread_refresh_int_ms = HPI_DEFAULT_DISP_THREAD_REFRESH_INT_MS;

// Externs
// extern struct k_sem sem_hw_inited;
extern struct k_sem sem_display_on;
extern struct k_sem sem_sampling_start;
extern struct k_sem sem_disp_boot_complete;
extern struct k_sem sem_crown_key_pressed;

// extern uint8_t global_batt_level;
// extern bool global_batt_charging;

extern lv_obj_t *scr_clock;
extern lv_obj_t *scr_ppg;
extern lv_obj_t *scr_vitals;

extern struct rtc_time global_system_time;

void hpi_display_sleep_on(void)
{
    if (m_display_active == true)
    {
        LOG_DBG("Display off");
        // display_blanking_on(display_dev);
        display_set_brightness(display_dev, 0);

        hpi_pwr_display_sleep();

        // Slow down the display thread
        disp_thread_refresh_int_ms = 1000;

        m_display_active = false;
    }
}

void hpi_display_sleep_off(void)
{
    if (m_display_active == false)
    {
        LOG_DBG("Display on");
        hpi_disp_set_brightness(hpi_disp_get_brightness());

        // display_blanking_on(display_dev);
        hpi_move_load_screen(curr_screen, SCROLL_NONE);
        // display_blanking_off(display_dev);
        hpi_pwr_display_wake();

        // Speed up the display thread
        disp_thread_refresh_int_ms = HPI_DEFAULT_DISP_THREAD_REFRESH_INT_MS;

        m_display_active = true;
    }
}

/*Will be called when the styles of the base theme are already added to add new styles*/
static void new_theme_apply_cb(lv_theme_t *th, lv_obj_t *obj)
{
    LV_UNUSED(th);

    if (lv_obj_check_type(obj, &lv_btn_class))
    {
        lv_obj_add_style(obj, &style_btn, 0);
    }
}

void display_init_styles()
{
    /*Initialize the styles*/
    lv_style_init(&style_btn);
    lv_style_set_bg_color(&style_btn, lv_palette_darken(LV_PALETTE_GREY, 4));
    lv_style_set_border_color(&style_btn, lv_palette_darken(LV_PALETTE_RED, 3));
    lv_style_set_border_width(&style_btn, 3);

    /*Initialize the new theme from the current theme*/
    lv_theme_t *th_act = lv_disp_get_theme(NULL);
    static lv_theme_t th_new;
    th_new = *th_act;

    /*Set the parent theme and the style apply callback for the new theme*/
    lv_theme_set_parent(&th_new, th_act);
    lv_theme_set_apply_cb(&th_new, new_theme_apply_cb);

    /*Assign the new theme to the current display*/
    lv_disp_set_theme(NULL, &th_new);

    // Subscript (Unit) label style
    lv_style_init(&style_sub);
    lv_style_set_text_color(&style_sub, lv_color_white());
    lv_style_set_text_font(&style_sub, &lv_font_montserrat_16);

    lv_style_init(&style_batt_sym);
    lv_style_set_text_color(&style_batt_sym, lv_palette_main(LV_PALETTE_GREY));
    lv_style_set_text_font(&style_batt_sym, &lv_font_montserrat_34);

    lv_style_init(&style_batt_percent);
    lv_style_set_text_color(&style_batt_percent, lv_color_white());
    lv_style_set_text_font(&style_batt_percent, &lv_font_montserrat_24);

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
    lv_style_set_text_font(&style_lbl_white, &lv_font_montserrat_42);

    // Label Orange
    lv_style_init(&style_lbl_orange);
    lv_style_set_text_color(&style_lbl_orange, lv_palette_main(LV_PALETTE_YELLOW));
    lv_style_set_text_font(&style_lbl_orange, &lv_font_montserrat_20);

    // Label White Small
    lv_style_init(&style_lbl_white_small);
    lv_style_set_text_color(&style_lbl_white_small, lv_color_white());
    lv_style_set_text_font(&style_lbl_white_small, &lv_font_montserrat_20);

    // Label Black Small
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
    lv_style_set_text_font(&style_lbl_white_tiny, &lv_font_montserrat_16);

    // Label White 14
    lv_style_init(&style_lbl_white_14);
    lv_style_set_text_color(&style_lbl_white_14, lv_color_white());
    lv_style_set_text_font(&style_lbl_white_14, &lv_font_montserrat_24);

    // Label Black
    lv_style_init(&style_lbl_black_small);
    lv_style_set_text_color(&style_lbl_black_small, lv_color_black());
    lv_style_set_text_font(&style_lbl_black_small, &lv_font_montserrat_34);

    // Screen background style
    lv_style_init(&style_scr_back);
    // lv_style_set_radius(&style, 5);
    lv_style_set_bg_opa(&style_scr_back, LV_OPA_COVER);
    lv_style_set_border_width(&style_scr_back, 0);

    static lv_grad_dsc_t grad;
    grad.dir = LV_GRAD_DIR_VER;
    grad.stops_count = 2;
    grad.stops[0].color = lv_color_hex(0x003a57); // lv_palette_lighten(LV_PALETTE_GREY, 1);
    grad.stops[1].color = lv_color_black();       // lv_palette_main(LV_PALETTE_BLUE);

    // Shift the gradient to the bottom
    grad.stops[0].frac = 128;
    grad.stops[1].frac = 192;

    // lv_style_set_bg_color(&style_scr_back, lv_color_black());
    // lv_style_set_bg_grad(&style_scr_back, &grad);
    lv_style_init(&style_scr_black);
    lv_style_set_radius(&style_scr_black, 1);

    lv_style_set_bg_opa(&style_scr_black, LV_OPA_COVER);
    lv_style_set_border_width(&style_scr_black, 0);
    lv_style_set_bg_color(&style_scr_black, lv_color_black());

    lv_disp_set_bg_color(NULL, lv_color_black());
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

void draw_header_minimal(lv_obj_t *parent, int top_offset)
{
    lv_obj_add_style(parent, &style_scr_black, 0);

    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /*lv_obj_t *img_logo = lv_img_create(parent);
    lv_img_set_src(img_logo, &logo_round_white);
    lv_obj_set_size(img_logo, 25, 25);
    lv_obj_align_to(img_logo, NULL, LV_ALIGN_TOP_MID, -35, (top_offset+5));
    */

    // Battery Level
    label_batt_level = lv_label_create(parent);
    lv_label_set_text(label_batt_level, LV_SYMBOL_BATTERY_FULL);
    lv_obj_add_style(label_batt_level, &style_batt_sym, LV_STATE_DEFAULT);
    lv_obj_align(label_batt_level, LV_ALIGN_TOP_MID, -15, (top_offset + 2));

    label_batt_level_val = lv_label_create(parent);
    lv_label_set_text(label_batt_level_val, "--");
    lv_obj_add_style(label_batt_level_val, &style_batt_percent, LV_STATE_DEFAULT);
    lv_obj_align_to(label_batt_level_val, label_batt_level, LV_ALIGN_OUT_RIGHT_MID, 3, 0);
}

void draw_bg(lv_obj_t *parent)
{
    lv_obj_t *logo_bg = lv_img_create(parent);
    lv_img_set_src(logo_bg, &pc_move_bg_200);
    lv_obj_set_width(logo_bg, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(logo_bg, LV_SIZE_CONTENT); /// 1
    lv_obj_set_align(logo_bg, LV_ALIGN_CENTER);
    lv_obj_add_flag(logo_bg, LV_OBJ_FLAG_ADV_HITTEST);  /// Flags
    lv_obj_clear_flag(logo_bg, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    lv_obj_add_style(parent, &style_scr_black, 0);
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

void hpi_disp_update_batt_level(int batt_level, bool charging)
{
    if (label_batt_level == NULL || label_batt_level_val == NULL)
    {
        return;
    }

    if (batt_level <= 0)
    {
        batt_level = 0;
    }

    // printk("Updating battery level: %d\n", batt_level);

    char buf[8];
    sprintf(buf, "%2d % ", batt_level);
    lv_label_set_text(label_batt_level_val, buf);

    if (batt_level > 75)
    {
        if (charging)
            lv_label_set_text(label_batt_level, LV_SYMBOL_CHARGE " " LV_SYMBOL_BATTERY_FULL);
        else
            lv_label_set_text(label_batt_level, LV_SYMBOL_BATTERY_FULL);
    }
    else if (batt_level > 50)
    {
        if (charging)
            lv_label_set_text(label_batt_level, LV_SYMBOL_CHARGE " " LV_SYMBOL_BATTERY_3);
        else
            lv_label_set_text(label_batt_level, LV_SYMBOL_BATTERY_3);
    }
    else if (batt_level > 25)
    {
        if (charging)
            lv_label_set_text(label_batt_level, LV_SYMBOL_CHARGE " " LV_SYMBOL_BATTERY_2);
        else
            lv_label_set_text(label_batt_level, LV_SYMBOL_BATTERY_2);
    }
    else if (batt_level > 10)
    {
        if (charging)
            lv_label_set_text(label_batt_level, LV_SYMBOL_CHARGE " " LV_SYMBOL_BATTERY_1);
        else
            lv_label_set_text(label_batt_level, LV_SYMBOL_BATTERY_1);
    }
    else
    {
        if (charging)
            lv_label_set_text(label_batt_level, LV_SYMBOL_CHARGE " " LV_SYMBOL_BATTERY_EMPTY);
        else
            lv_label_set_text(label_batt_level, LV_SYMBOL_BATTERY_EMPTY);
    }
}

void disp_screen_event(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    // lv_obj_t *target = lv_event_get_target(e);

    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_LEFT)
    {
        lv_indev_wait_release(lv_indev_get_act());
        printf("Left at %d\n", curr_screen);

        if ((curr_screen + 1) == SCR_LIST_END)
        {
            printk("End of list\n");
            return;
        }
        else
        {
            printk("Loading screen %d\n", curr_screen + 1);
            hpi_move_load_screen(curr_screen + 1, SCROLL_LEFT);
        }
    }

    else if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT)
    {
        lv_indev_wait_release(lv_indev_get_act());
        printf("Right at %d\n", curr_screen);
        if ((curr_screen - 1) == SCR_LIST_START)
        {
            printk("Start of list\n");
            return;
        }
        else
        {

            printk("Loading screen %d\n", curr_screen - 1);
            hpi_move_load_screen(curr_screen - 1, SCROLL_RIGHT);
        }
    }
    else if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_BOTTOM)
    {
        lv_indev_wait_release(lv_indev_get_act());
        printk("Down at %d\n", curr_screen);

        hpi_move_load_scr_settings(SCROLL_DOWN);
    }
    else if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_TOP)
    {
        lv_indev_wait_release(lv_indev_get_act());
        printk("Up at %d\n", curr_screen);

        if (curr_screen == SCR_SPL_SETTINGS)
        {
            hpi_move_load_screen(SCR_HOME, SCROLL_NONE);
        }
    }
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

void hpi_show_screen(lv_obj_t *parent, enum scroll_dir m_scroll_dir)
{
    lv_obj_add_event_cb(parent, disp_screen_event, LV_EVENT_GESTURE, NULL);

    if (m_scroll_dir == SCROLL_LEFT)
    {
        lv_scr_load_anim(parent, LV_SCR_LOAD_ANIM_OVER_LEFT, SCREEN_TRANS_TIME, 0, true);
    }
    else if (m_scroll_dir == SCROLL_RIGHT)
    {
        lv_scr_load_anim(parent, LV_SCR_LOAD_ANIM_OVER_RIGHT, SCREEN_TRANS_TIME, 0, true);
    }
    else
    {
        lv_scr_load_anim(parent, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    }
}

void hpi_move_load_scr_settings(enum scroll_dir m_scroll_dir)
{
    draw_scr_settings(m_scroll_dir);
}

void hpi_move_load_screen(enum hpi_disp_screens m_screen, enum scroll_dir m_scroll_dir)
{
    switch (m_screen)
    {
    /*case SCR_CLOCK_SMALL:
        draw_scr_clock_small(m_scroll_dir);
        break;
        */
    case SCR_HOME:
        draw_scr_home(m_scroll_dir);
        break;
    case SCR_TODAY:
        draw_scr_today(m_scroll_dir);
        break;
    case SCR_PLOT_PPG:
        draw_scr_ppg(m_scroll_dir);
        break;
    case SCR_PLOT_ECG:
        draw_scr_ecg(m_scroll_dir);
        break;
    case SCR_PLOT_EDA:
        draw_scr_eda(m_scroll_dir);
        break;
    case SCR_PLOT_HRV:
        draw_scr_hrv(m_scroll_dir);
        break;
    case SCR_PLOT_HRV_SCATTER:
        draw_scr_hrv_scatter(m_scroll_dir);
        break;
    case SCR_BPT:
        draw_scr_bpt(m_scroll_dir);
        break;

    /*
    case SCR_CLOCK:
        draw_scr_clockface(m_scroll_dir);
        break;
    case SCR_VITALS:
        draw_scr_vitals_home(m_scroll_dir);
        break;
     case SCR_BPT_HOME:
         draw_scr_bpt_home(m_scroll_dir);
         break;*/
    default:
        printk("Invalid screen: %d", m_screen);
    }
}

void hpi_disp_set_brightness(uint8_t brightness_percent)
{
    uint8_t brightness = (uint8_t)((brightness_percent * 255) / 100);
    display_set_brightness(display_dev, brightness);
    hpi_disp_curr_brightness = brightness_percent;
}

uint8_t hpi_disp_get_brightness(void)
{
    return hpi_disp_curr_brightness;
}

static uint8_t m_disp_batt_level = 0;
static struct rtc_time m_disp_sys_time;
static uint8_t m_disp_hr = 0;

/* NOTE: All LVGL display updates should be called from the same display_screens_thread
 */

void display_screens_thread(void)
{
    struct hpi_ecg_bioz_sensor_data_t ecg_bioz_sensor_sample;
    struct hpi_ppg_sensor_data_t ppg_sensor_sample;

    struct hpi_computed_hrv_t hrv_sample;

    // int32_t temp_val;

    // int temp_disp_counter = 0;
    int batt_refresh_counter = 0;
    int hr_refresh_counter = 0;
    int time_refresh_counter = 0;
    int m_disp_inact_refresh_counter = 0;
    int scr_ppg_hr_spo2_refresh_counter = 0;

    static volatile uint16_t prev_rtor;

    k_sem_take(&sem_display_on, K_FOREVER);

    printk("Disp ON");

    if (!device_is_ready(display_dev))
    {
        LOG_ERR("Device not ready");
        // return;
    }

    LOG_DBG("Display device: %s", display_dev->name);

    // draw_scr_boot();
    //  Init all styles globally
    display_init_styles();

    display_blanking_off(display_dev);
    hpi_disp_set_brightness(50);

    k_sem_take(&sem_disp_boot_complete, K_FOREVER);

    //draw_scr_home(SCROLL_NONE);

     lv_task_handler();
    draw_scr_ppg(SCROLL_RIGHT);
    //  draw_scr_today(SCROLL_NONE);
    //  draw_scr_ecg(SCROLL_RIGHT);
    // draw_scr_bpt(SCROLL_RIGHT);
    //  draw_scr_settings(SCROLL_RIGHT);
    //  draw_scr_eda();
    //  draw_scr_hrv_scatter(SCROLL_RIGHT);

    LOG_INF("Display screens inited");

    while (1)
    {
        if (m_display_active == true)
        {
            if (k_msgq_get(&q_plot_ppg, &ppg_sensor_sample, K_NO_WAIT) == 0)
            {
                if (curr_screen == SCR_PLOT_PPG)
                {
                    hpi_disp_ppg_draw_plotPPG(ppg_sensor_sample);
                    hpi_ppg_disp_update_status(ppg_sensor_sample.scd_state);

                    if (scr_ppg_hr_spo2_refresh_counter >= (1000 / disp_thread_refresh_int_ms))
                    {
                        hpi_ppg_disp_update_hr(ppg_sensor_sample.hr);
                        hpi_ppg_disp_update_spo2(ppg_sensor_sample.spo2);

                        scr_ppg_hr_spo2_refresh_counter = 0;
                    }
                    else
                    {
                        scr_ppg_hr_spo2_refresh_counter++;
                    }
                }
                else if ((curr_screen == SCR_HOME)) // || (curr_screen == SCR_CLOCK_SMALL))
                {
                }
                else if (curr_screen == SCR_PLOT_HRV)
                {
                    if (ppg_sensor_sample.rtor != 0) // && ppg_sensor_sample.rtor != prev_rtor)
                    {
                        // printk("RTOR: %d | SCD: %d", ppg_sensor_sample.rtor, ppg_sensor_sample.scd_state);
                        hpi_disp_hrv_draw_plot_rtor((float)((ppg_sensor_sample.rtor)));
                        hpi_disp_hrv_update_rtor(ppg_sensor_sample.rtor);
                        prev_rtor = ppg_sensor_sample.rtor;
                    }
                }
                else if (curr_screen == SCR_PLOT_HRV_SCATTER)
                {
                    if (ppg_sensor_sample.rtor != 0) //&& ppg_sensor_sample.rtor != prev_rtor)
                    {
                        // printk("RTOR: %d | SCD: %d", ppg_sensor_sample.rtor, ppg_sensor_sample.scd_state);
                        hpi_disp_hrv_scatter_draw_plot_rtor((float)((ppg_sensor_sample.rtor)), (float)prev_rtor);
                        hpi_disp_hrv_scatter_update_rtor(ppg_sensor_sample.rtor);
                        prev_rtor = ppg_sensor_sample.rtor;
                    }
                }
                else if (curr_screen == SUBSCR_BPT_CALIBRATE)
                {

                    // hpi_disp_bpt_draw_plotPPG(ppg_sensor_sample.raw_red, ppg_sensor_sample.bpt_status, ppg_sensor_sample.bpt_progress);
                    //  hpi_disp_draw_plotPPG((float)(ppg_sensor_sample.raw_red * 1.0000));
                    /*if (bpt_cal_done_flag == false)
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

                            // ppg_data_stop();
                        }
                        hpi_disp_bpt_update_progress(ppg_sensor_sample.bpt_progress);
                        lv_disp_trig_activity(NULL);
                    }*/
                }

                /*
                if (curr_screen == SUBSCR_BPT_MEASURE)
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

                                if (curr_screen == SUBSCR_BPT_MEASURE)
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
                */
            }
            //}

            if (k_msgq_get(&q_plot_hrv, &hrv_sample, K_NO_WAIT) == 0)
            {
                if (curr_screen == SCR_PLOT_HRV)
                {
                    hpi_disp_hrv_update_sdnn(hrv_sample.rmssd);
                }
                else if (curr_screen == SCR_PLOT_HRV_SCATTER)
                {
                    hpi_disp_hrv_scatter_update_sdnn(hrv_sample.rmssd);
                }
            }

            if (k_msgq_get(&q_plot_ecg_bioz, &ecg_bioz_sensor_sample, K_NO_WAIT) == 0)
            {
                if (curr_screen == SCR_PLOT_ECG)
                {
                    //((float)((ecg_bioz_sensor_sample.ecg_sample / 1000.0000)), ecg_bioz_sensor_sample.ecg_lead_off);
                    hpi_ecg_disp_draw_plotECG(ecg_bioz_sensor_sample.ecg_samples, ecg_bioz_sensor_sample.ecg_num_samples, ecg_bioz_sensor_sample.ecg_lead_off);
                    hpi_ecg_disp_update_hr(ecg_bioz_sensor_sample.hr);
                }
                else if (curr_screen == SCR_PLOT_EDA)
                {
                    hpi_eda_disp_draw_plotEDA(ecg_bioz_sensor_sample.bioz_sample, ecg_bioz_sensor_sample.bioz_num_samples, ecg_bioz_sensor_sample.bioz_lead_off);
                }
            }

            if (curr_screen == SCR_HOME)
            {
                ui_hr_button_update(m_disp_hr);

                if (time_refresh_counter >= (1000 / disp_thread_refresh_int_ms))
                {
                    ui_home_time_display_update(m_disp_sys_time);
                    ui_hr_button_update(m_disp_hr);
                    time_refresh_counter = 0;
                }
                else
                {
                    time_refresh_counter++;
                }
            }

            if (batt_refresh_counter >= (1000 / disp_thread_refresh_int_ms))
            {
                if (m_display_active)
                {
                    hpi_disp_update_batt_level(m_disp_batt_level, 0);
                }
                batt_refresh_counter = 0;
            }
            else
            {
                batt_refresh_counter++;
            }
        }

        // Add button handlers
        if (k_sem_take(&sem_crown_key_pressed, K_NO_WAIT) == 0)
        {
            if (m_display_active == false)
            {
                lv_disp_trig_activity(NULL);
            }
            else
            {
                if (curr_screen == SCR_HOME)
                {
                    hpi_display_sleep_on();
                }
                else
                {
                    hpi_move_load_screen(SCR_HOME, SCROLL_NONE);
                }
            }
        }

        int inactivity_time = lv_disp_get_inactive_time(NULL);
        if (inactivity_time > DISP_SLEEP_TIME_MS)
        {
            hpi_display_sleep_on();
        }
        else
        {
            hpi_display_sleep_off();
        }

        //lv_task_handler();
        //k_msleep(disp_thread_refresh_int_ms);

       // k_msleep(30);
        k_msleep(lv_task_handler());
    }
}

K_MUTEX_DEFINE(mutex_curr_screen);

void hpi_disp_set_curr_screen(int screen)
{
    k_mutex_lock(&mutex_curr_screen, K_FOREVER);
    curr_screen = screen;
    k_mutex_unlock(&mutex_curr_screen);
}

static void disp_batt_status_listener(const struct zbus_channel *chan)
{
    const struct batt_status *batt_s = zbus_chan_const_msg(chan);

    // LOG_DBG("Ch Batt: %d, Charge: %d", batt_s->batt_level, batt_s->batt_charging);
    m_disp_batt_level = batt_s->batt_level;
}

ZBUS_LISTENER_DEFINE(disp_batt_lis, disp_batt_status_listener);

static void disp_sys_time_listener(const struct zbus_channel *chan)
{
    const struct rtc_time *sys_time = zbus_chan_const_msg(chan);
    m_disp_sys_time = *sys_time;
}
ZBUS_LISTENER_DEFINE(disp_sys_time_lis, disp_sys_time_listener);

static void disp_hr_listener(const struct zbus_channel *chan)
{
    const struct hpi_hr_t *hpi_hr = zbus_chan_const_msg(chan);
    m_disp_hr = hpi_hr->hr;
}
ZBUS_LISTENER_DEFINE(disp_hr_lis, disp_hr_listener);

static void disp_steps_listener(const struct zbus_channel *chan)
{
    const struct hpi_steps_t *hpi_steps = zbus_chan_const_msg(chan);
    if (curr_screen == SCR_HOME)
    {
        // ui_steps_button_update(hpi_steps->steps_walk);
    }
    // ui_steps_button_update(hpi_steps->steps_walk);
}
ZBUS_LISTENER_DEFINE(disp_steps_lis, disp_steps_listener);

static void disp_temp_listener(const struct zbus_channel *chan)
{
    const struct hpi_temp_t *hpi_temp = zbus_chan_const_msg(chan);
    // printk("ZB Temp: %.2f\n", hpi_temp->temp_f);
}
ZBUS_LISTENER_DEFINE(disp_temp_lis, disp_temp_listener);

#define DISPLAY_SCREENS_THREAD_STACKSIZE 32768
#define DISPLAY_SCREENS_THREAD_PRIORITY 7
// Power Cost - 80 uA

K_THREAD_DEFINE(display_screens_thread_id, DISPLAY_SCREENS_THREAD_STACKSIZE, display_screens_thread, NULL, NULL, NULL, DISPLAY_SCREENS_THREAD_PRIORITY, 0, 0);
