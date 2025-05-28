#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>
#include <stdio.h>
#include <app_version.h>
#include <time.h>
#include <zephyr/posix/time.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/pm/device.h>
#include <zephyr/zbus/zbus.h>

#include "hw_module.h"
#include "power_ctrl.h"
#include "hpi_common_types.h"
#include "ui/move_ui.h"

#include <display_sh8601.h>

LOG_MODULE_REGISTER(display_common, LOG_LEVEL_DBG);

// LVGL Styles
static lv_style_t style_btn;

// Global LVGL Styles
lv_style_t style_tiny;
lv_style_t style_scr_black;
lv_style_t style_red_medium;
lv_style_t style_lbl_red_small;

lv_style_t style_white_small;
lv_style_t style_white_medium;

lv_style_t style_scr_container;

lv_style_t style_lbl_white_14;
lv_style_t style_lbl_white_medium;
lv_style_t style_white_large;

lv_style_t style_bg_blue;
lv_style_t style_bg_red;
lv_style_t style_bg_green;
lv_style_t style_bg_purple;

static volatile uint8_t hpi_disp_curr_brightness = DISPLAY_DEFAULT_BRIGHTNESS;

static int curr_screen = SCR_HOME;
K_MUTEX_DEFINE(mutex_curr_screen);

static lv_obj_t *label_batt_level;
static lv_obj_t *label_batt_level_val;

static lv_obj_t *lbl_hdr_hour;
static lv_obj_t *lbl_hdr_min;
static lv_obj_t *ui_label_date;

lv_obj_t *cui_battery_percent;
int tmp_scr_parent = 0;

// Externs
extern const struct device *display_dev;

extern struct k_sem sem_ecg_cancel;
extern struct k_sem sem_stop_one_shot_spo2;

/*Will be called when the styles of the base theme are already added to add new styles*/
static void new_theme_apply_cb(lv_theme_t *th, lv_obj_t *obj)
{
    LV_UNUSED(th);

    if (lv_obj_check_type(obj, &lv_btn_class))
    {
        lv_obj_add_style(obj, &style_btn, 0);
    }
    lv_style_set_bg_color(&style_scr_black, lv_color_black());
}

void display_init_styles(void)
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

    /*lv_theme_t *th = lv_theme_default_init(NULL,
        lv_palette_main(LV_PALETTE_RED), lv_palette_main(LV_PALETTE_RED) ,
                                           true,
                                           &fredoka_28);

    lv_disp_set_theme(NULL, th);*/

    // Subscript (Unit) label style
    lv_style_init(&style_tiny);
    lv_style_set_text_color(&style_tiny, lv_color_white());
    lv_style_set_text_font(&style_tiny, &lv_font_montserrat_20);

    // Label White Small
    lv_style_init(&style_white_small);
    lv_style_set_text_color(&style_white_small, lv_color_white());
    lv_style_set_text_font(&style_white_small, &lv_font_montserrat_24);

    lv_style_init(&style_white_medium);
    lv_style_set_text_color(&style_white_medium, lv_color_white());
    lv_style_set_text_font(&style_white_medium, &lv_font_montserrat_34);

    lv_style_init(&style_white_large);
    lv_style_set_text_color(&style_white_large, lv_color_white());
    lv_style_set_text_font(&style_white_large, &oxanium_90); // &ui_font_Number_big); //&ui_font_Number_extra);

    // Label Red
    lv_style_init(&style_red_medium);
    lv_style_set_text_color(&style_red_medium, lv_palette_main(LV_PALETTE_RED));
    lv_style_set_text_font(&style_red_medium, &lv_font_montserrat_34);

    // Label White 14
    lv_style_init(&style_lbl_white_14);
    lv_style_set_text_color(&style_lbl_white_14, lv_color_white());
    lv_style_set_text_font(&style_lbl_white_14, &lv_font_montserrat_24);

    // Label Black
    lv_style_init(&style_lbl_white_medium);
    lv_style_set_text_color(&style_lbl_white_medium, lv_color_black());
    lv_style_set_text_font(&style_lbl_white_medium, &lv_font_montserrat_34);

    // Container for scrollable screen layout
    lv_style_init(&style_scr_container);
    lv_style_set_flex_flow(&style_scr_container, LV_FLEX_FLOW_ROW_WRAP);
    lv_style_set_flex_main_place(&style_scr_container, LV_FLEX_ALIGN_SPACE_EVENLY);
    lv_style_set_flex_cross_place(&style_scr_container, LV_FLEX_ALIGN_CENTER);

    // Black screen background
    lv_style_init(&style_scr_black);
    lv_style_set_bg_opa(&style_scr_black, LV_OPA_COVER);
    lv_style_set_border_width(&style_scr_black, 0);
    lv_style_set_bg_color(&style_scr_black, lv_color_black());

    lv_style_init(&style_bg_blue);
    lv_style_set_radius(&style_bg_blue, 15);
    lv_style_set_bg_opa(&style_bg_blue, LV_OPA_COVER);
    static lv_grad_dsc_t grad;
    grad.dir = LV_GRAD_DIR_VER;
    grad.stops_count = 2;
    grad.stops[0].color = lv_color_black();
    grad.stops[1].color = lv_palette_darken(LV_PALETTE_BLUE, 4);
    grad.stops[0].frac = 168;
    grad.stops[1].frac = 255;
    lv_style_set_bg_grad(&style_bg_blue, &grad);

    lv_style_init(&style_bg_red);
    lv_style_set_radius(&style_bg_red, 15);
    lv_style_set_bg_opa(&style_bg_red, LV_OPA_COVER);
    static lv_grad_dsc_t grad_red;
    grad_red.dir = LV_GRAD_DIR_VER;
    grad_red.stops_count = 2;
    grad_red.stops[0].color = lv_color_black();
    grad_red.stops[1].color = lv_palette_darken(LV_PALETTE_DEEP_ORANGE, 4);
    grad_red.stops[0].frac = 168;
    grad_red.stops[1].frac = 255;
    lv_style_set_bg_grad(&style_bg_red, &grad_red);

    lv_style_init(&style_bg_green);
    lv_style_set_radius(&style_bg_green, 15);
    lv_style_set_bg_opa(&style_bg_green, LV_OPA_COVER);
    static lv_grad_dsc_t grad_green;
    grad_green.dir = LV_GRAD_DIR_VER;
    grad_green.stops_count = 2;
    grad_green.stops[0].color = lv_color_black();
    grad_green.stops[1].color = lv_palette_darken(LV_PALETTE_CYAN, 2);
    grad_green.stops[0].frac = 168;
    grad_green.stops[1].frac = 255;
    lv_style_set_bg_grad(&style_bg_green, &grad_green);

    lv_style_init(&style_bg_purple);
    lv_style_set_radius(&style_bg_purple, 15);
    lv_style_set_bg_opa(&style_bg_purple, LV_OPA_COVER);
    static lv_grad_dsc_t grad_purple;
    grad_purple.dir = LV_GRAD_DIR_VER;
    grad_purple.stops_count = 2;
    grad_purple.stops[0].color = lv_color_black();
    grad_purple.stops[1].color = lv_palette_darken(LV_PALETTE_PURPLE, 4);
    grad_purple.stops[0].frac = 168;
    grad_purple.stops[1].frac = 255;
    lv_style_set_bg_grad(&style_bg_purple, &grad_purple);

    lv_disp_set_bg_color(NULL, lv_color_black());
}

void draw_scr_common(lv_obj_t *parent)
{
    lv_obj_add_style(parent, &style_scr_black, 0);
    lv_obj_set_scroll_dir(parent, LV_DIR_VER);
    // lv_obj_clear_flag(scr_bpt, LV_OBJ_FLAG_SCROLLABLE);
}

void hpi_display_sleep_on(void)
{
    LOG_DBG("Display off");
    // display_blanking_on(display_dev);
    display_set_brightness(display_dev, 0);

    // hpi_pwr_display_sleep();
}

void hpi_display_sleep_off(void)
{
    LOG_DBG("Display on");
    hpi_disp_set_brightness(hpi_disp_get_brightness());

    // display_blanking_on(display_dev);
    hpi_load_screen(curr_screen, SCROLL_NONE);
    display_blanking_off(display_dev);
    // hpi_pwr_display_wake();
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
    sprintf(buf, " %2d %% ", batt_level);
    lv_label_set_text(label_batt_level_val, buf);

    if (batt_level > 75)
    {
        if (charging)
            lv_label_set_text(label_batt_level, LV_SYMBOL_CHARGE " " LV_SYMBOL_BATTERY_FULL "");
        else
            lv_label_set_text(label_batt_level, LV_SYMBOL_BATTERY_FULL);
    }
    else if (batt_level > 50)
    {
        if (charging)
            lv_label_set_text(label_batt_level, LV_SYMBOL_CHARGE " " LV_SYMBOL_BATTERY_3 " ");
        else
            lv_label_set_text(label_batt_level, LV_SYMBOL_BATTERY_3);
    }
    else if (batt_level > 25)
    {
        if (charging)
            lv_label_set_text(label_batt_level, LV_SYMBOL_CHARGE " " LV_SYMBOL_BATTERY_2 " ");
        else
            lv_label_set_text(label_batt_level, LV_SYMBOL_BATTERY_2);
    }
    else if (batt_level > 10)
    {
        if (charging)
            lv_label_set_text(label_batt_level, LV_SYMBOL_CHARGE " " LV_SYMBOL_BATTERY_1 " ");
        else
            lv_label_set_text(label_batt_level, LV_SYMBOL_BATTERY_1);
    }
    else
    {
        if (charging)
            lv_label_set_text(label_batt_level, LV_SYMBOL_CHARGE " " LV_SYMBOL_BATTERY_EMPTY " ");
        else
            lv_label_set_text(label_batt_level, LV_SYMBOL_BATTERY_EMPTY);
    }
}

void hpi_show_screen(lv_obj_t *m_screen, enum scroll_dir m_scroll_dir)
{
    lv_obj_add_event_cb(m_screen, disp_screen_event, LV_EVENT_GESTURE, NULL);

    if (m_scroll_dir == SCROLL_LEFT)
    {
        lv_scr_load_anim(m_screen, LV_SCR_LOAD_ANIM_OVER_LEFT, SCREEN_TRANS_TIME, 0, true);
    }
    else if (m_scroll_dir == SCROLL_RIGHT)
    {
        lv_scr_load_anim(m_screen, LV_SCR_LOAD_ANIM_OVER_RIGHT, SCREEN_TRANS_TIME, 0, true);
    }
    else if (m_scroll_dir == SCROLL_UP)
    {
        lv_scr_load_anim(m_screen, LV_SCR_LOAD_ANIM_OVER_TOP, SCREEN_TRANS_TIME, 0, true);
    }
    else if (m_scroll_dir == SCROLL_DOWN)
    {
        lv_scr_load_anim(m_screen, LV_SCR_LOAD_ANIM_OVER_BOTTOM, SCREEN_TRANS_TIME, 0, true);
    }
    else
    {
        lv_scr_load_anim(m_screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    }
}

void hpi_load_screen(int m_screen, enum scroll_dir m_scroll_dir)
{
    switch (m_screen)
    {
    case SCR_HOME:
        draw_scr_home(m_scroll_dir);
        break;
    case SCR_TODAY:
        draw_scr_today(m_scroll_dir);
        break;
    case SCR_HR:
        draw_scr_hr(m_scroll_dir);
        break;
    case SCR_SPO2:
        draw_scr_spo2(m_scroll_dir);
        break;
    case SCR_BPT:
        draw_scr_bpt(m_scroll_dir);
        break;

    case SCR_TEMP:
        draw_scr_temp(m_scroll_dir);
        break;
    case SCR_ECG:
        draw_scr_ecg(m_scroll_dir);
        break;

    /*case SCR_PLOT_EDA:
        draw_scr_pre(m_scroll_dir);
        break;*/
    default:
        printk("Invalid screen: %d", m_screen);
    }
}

void hpi_move_load_scr_settings(enum scroll_dir m_scroll_dir)
{
    draw_scr_settings(m_scroll_dir);
}

void disp_spl_screen_event(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    // lv_obj_t *target = lv_event_get_target(e);

    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_BOTTOM)
    {
        lv_indev_wait_release(lv_indev_get_act());
        printk("Down at %d\n", curr_screen);

        if (curr_screen == SCR_SPL_SETTINGS)
        {
            hpi_load_screen(SCR_HOME, SCROLL_DOWN);
        }
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
            hpi_load_screen(curr_screen + 1, SCROLL_LEFT);
        }
    }

    else if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT)
    {
        lv_indev_wait_release(lv_indev_get_act());
        printf("Right at %d\n", curr_screen);

        if (hpi_disp_get_curr_screen() == SCR_SPL_HR_SCR2)
        {
            hpi_load_screen(SCR_HR, SCROLL_LEFT);
            return;
        }

        if (hpi_disp_get_curr_screen() == SCR_SPL_SPO2_MEASURE)
        {
            hpi_load_screen(SCR_SPO2, SCROLL_LEFT);
            return;
        }

        if ((curr_screen - 1) == SCR_LIST_START)
        {
            printk("Start of list\n");
            return;
        }
        else
        {
            printk("Loading screen %d\n", curr_screen - 1);
            hpi_load_screen(curr_screen - 1, SCROLL_RIGHT);
        }
    }
    else if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_BOTTOM)
    {
        lv_indev_wait_release(lv_indev_get_act());
        printk("Down at %d\n", curr_screen);

        if (hpi_disp_get_curr_screen() == SCR_HOME)
        {
            hpi_move_load_scr_settings(SCROLL_DOWN);
        }
        else if (hpi_disp_get_curr_screen() == SCR_SPL_ECG_SCR2)
        {
            printk("Cancel ECG\n");
            k_sem_give(&sem_ecg_cancel);
            hpi_load_screen(SCR_ECG, SCROLL_DOWN);
        }
        else if (hpi_disp_get_curr_screen() == SCR_SPL_SPO2_MEASURE || hpi_disp_get_curr_screen() == SCR_SPL_SPO2_SELECT ||
                 hpi_disp_get_curr_screen() == SCR_SPL_SPO2_SCR2)
        {
            k_sem_give(&sem_stop_one_shot_spo2);
            hpi_load_screen(SCR_SPO2, SCROLL_DOWN);
        }
        else if (hpi_disp_get_curr_screen() == SCR_SPL_ECG_COMPLETE)
        {
            hpi_load_screen(SCR_ECG, SCROLL_UP);
        }
        else if (hpi_disp_get_curr_screen() == SCR_SPL_SPO2_COMPLETE || (hpi_disp_get_curr_screen() == SCR_SPL_SPO2_SELECT))
        {
            hpi_load_screen(SCR_SPO2, SCROLL_DOWN);
        }
        else if ((hpi_disp_get_curr_screen() == SCR_SPL_BPT_SCR2) || (hpi_disp_get_curr_screen() == SCR_SPL_BPT_SCR3) || (hpi_disp_get_curr_screen() == SCR_SPL_BPT_SCR4))
        {
            hpi_load_screen(SCR_BPT, SCROLL_DOWN);
        }
        else if (hpi_disp_get_curr_screen() == SCR_SPL_BPT_CAL_COMPLETE || (hpi_disp_get_curr_screen() == SCR_SPL_BPT_CAL_PROGRESS) ||
                 (hpi_disp_get_curr_screen() == SCR_SPL_BPT_EST_COMPLETE))
        {
            hpi_load_screen(SCR_BPT, SCROLL_DOWN);
        }
        else if (hpi_disp_get_curr_screen() == SCR_SPL_BLE)
        {
            hpi_load_screen(SCR_BPT, SCROLL_DOWN);
        }
        else if (hpi_disp_get_curr_screen() == SCR_SPL_PLOT_HRV)
        {
            hpi_load_screen(SCR_BPT, SCROLL_DOWN);
        }
        else if (hpi_disp_get_curr_screen() == SCR_SPL_BPT_CAL_PROGRESS)
        {
            hpi_load_screen(SCR_BPT, SCROLL_DOWN);
        }
    }
    else if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_TOP)
    {
        lv_indev_wait_release(lv_indev_get_act());
        printk("Up at %d\n", curr_screen);

        if (curr_screen == SCR_SPL_SETTINGS)
        {
            hpi_load_screen(SCR_HOME, SCROLL_UP);
        }
        /*else if (hpi_disp_get_curr_screen() == SCR_HR)
        {
            hpi_load_scr_spl(SCR_SPL_HR_SCR2, SCROLL_DOWN, SCR_HR, 0, 0, 0);
        }*/
    }
}

int hpi_helper_get_date_time_str(int64_t in_ts, char *date_time_str)
{
    struct tm today_time_tm = *gmtime(&in_ts);
    if (in_ts == 0)
    {
        snprintf(date_time_str, 30, "Last Updated: --");
    }
    else
    {
        snprintf(date_time_str, 74, "Last Updated: %02d:%02d\n%02d-%02d-%04d", today_time_tm.tm_hour,
                 today_time_tm.tm_min, today_time_tm.tm_mday, today_time_tm.tm_mon, today_time_tm.tm_year + 1900);
    }
    return 0;
}

void hdr_time_display_update(struct rtc_time in_time)
{
    if (lbl_hdr_min == NULL || lbl_hdr_hour == NULL)
        return;

    char buf[6];
    // char date_buf[20];

    /*char mon_strs[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
                            */

    sprintf(buf, "%02d:", in_time.tm_hour);
    lv_label_set_text(lbl_hdr_hour, buf);

    sprintf(buf, "%02d", in_time.tm_min);
    lv_label_set_text(lbl_hdr_min, buf);
}

void draw_bg(lv_obj_t *parent)
{
    lv_obj_t *logo_bg = lv_img_create(parent);
    lv_img_set_src(logo_bg, &bck_heart_200);
    lv_obj_set_width(logo_bg, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(logo_bg, LV_SIZE_CONTENT); /// 1
    lv_obj_set_align(logo_bg, LV_ALIGN_CENTER);
    lv_obj_add_flag(logo_bg, LV_OBJ_FLAG_ADV_HITTEST);  /// Flags
    lv_obj_clear_flag(logo_bg, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    lv_obj_add_style(parent, &style_scr_black, 0);
}

void hpi_disp_set_curr_screen(int screen)
{
    k_mutex_lock(&mutex_curr_screen, K_FOREVER);
    curr_screen = screen;
    k_mutex_unlock(&mutex_curr_screen);
}

int hpi_disp_get_curr_screen(void)
{
    k_mutex_lock(&mutex_curr_screen, K_FOREVER);
    int screen = curr_screen;
    k_mutex_unlock(&mutex_curr_screen);
    return screen;
}