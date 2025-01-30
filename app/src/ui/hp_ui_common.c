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
#include "hpi_common_types.h"
#include "ui/move_ui.h"

#include <display_sh8601.h>

LOG_MODULE_REGISTER(display_common, LOG_LEVEL_DBG);

// LV_IMG_DECLARE(pc_logo_bg3);
LV_IMG_DECLARE(pc_move_bg_200);
// LV_IMG_DECLARE(pc_logo_bg3);
LV_IMG_DECLARE(logo_round_white);

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
static lv_style_t style_btn;
static lv_style_t style_scr_back;

// Global LVGL Styles
lv_style_t style_scr_black;
lv_style_t style_lbl_red;
lv_style_t style_lbl_red_small;
lv_style_t style_lbl_white;
lv_style_t style_lbl_white_small;
lv_style_t style_lbl_orange;
lv_style_t style_lbl_white_tiny;
lv_style_t style_lbl_white_14;
lv_style_t style_lbl_black_small;

static volatile uint8_t hpi_disp_curr_brightness = DISPLAY_DEFAULT_BRIGHTNESS;

int curr_screen = SCR_VITALS;

static lv_obj_t *label_batt_level;
static lv_obj_t *label_batt_level_val;
lv_obj_t *cui_battery_percent;

// Externs
extern const struct device *display_dev;

void hpi_display_sleep_on(void)
{
    LOG_DBG("Display off");
    // display_blanking_on(display_dev);
    display_set_brightness(display_dev, 0);

    hpi_pwr_display_sleep();
}

void hpi_display_sleep_off(void)
{
    LOG_DBG("Display on");
    hpi_disp_set_brightness(hpi_disp_get_brightness());

    // display_blanking_on(display_dev);
    hpi_move_load_screen(curr_screen, SCROLL_NONE);
    // display_blanking_off(display_dev);
    hpi_pwr_display_wake();
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
    sprintf(buf, " %2d % ", batt_level);
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

void hpi_move_load_scr_settings(enum scroll_dir m_scroll_dir)
{
    draw_scr_settings(m_scroll_dir);
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

/*Will be called when the styles of the base theme are already added to add new styles*/
static void new_theme_apply_cb(lv_theme_t *th, lv_obj_t *obj)
{
    LV_UNUSED(th);

    if (lv_obj_check_type(obj, &lv_btn_class))
    {
        lv_obj_add_style(obj, &style_btn, 0);
    }
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
    lv_obj_align(label_batt_level, LV_ALIGN_TOP_MID, -30, (top_offset + 2));

    label_batt_level_val = lv_label_create(parent);
    lv_label_set_text(label_batt_level_val, "--");
    lv_obj_add_style(label_batt_level_val, &style_batt_percent, LV_STATE_DEFAULT);
    lv_obj_align_to(label_batt_level_val, label_batt_level, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
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

K_MUTEX_DEFINE(mutex_curr_screen);

void hpi_disp_set_curr_screen(int screen)
{
    k_mutex_lock(&mutex_curr_screen, K_FOREVER);
    curr_screen = screen;
    k_mutex_unlock(&mutex_curr_screen);
}
