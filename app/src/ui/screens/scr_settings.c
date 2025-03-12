#include <zephyr/kernel.h>
#include <lvgl.h>
#include <stdio.h>
#include <app_version.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(scr_settings, LOG_LEVEL_INF);

#include "ui/move_ui.h"
#include "hw_module.h"

lv_obj_t *scr_settings;

static lv_obj_t *label_batt_level;
static lv_obj_t *label_batt_level_val;

extern lv_style_t style_scr_black;
extern lv_style_t style_batt_sym;
extern lv_style_t style_batt_percent;

static void brightness_slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    hpi_disp_set_brightness(lv_slider_get_value(slider));
}

static lv_obj_t *msgbox_shutdown;

static void btn_shutdown_yes_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        LOG_DBG("Shutdown confirmed");
        hpi_hw_pmic_off();
    }
}

static void btn_shutdown_no_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        LOG_DBG("Shutdown cancelled");
        lv_msgbox_close(msgbox_shutdown);
    }
}

static void hpi_show_shutdown_mbox(void)
{
    msgbox_shutdown = lv_msgbox_create(NULL, "", "Shutdown?", NULL, false);
    lv_obj_center(msgbox_shutdown);

    lv_obj_t *content = lv_msgbox_get_content(msgbox_shutdown);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(content, -1, LV_PART_SCROLLBAR);

    lv_obj_set_height(content, 220);

    // lv_obj_add_style(content, &style, 0);

    lv_obj_t *btn_yes = lv_btn_create(content);
    lv_obj_set_size(btn_yes, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_add_event_cb(btn_yes, btn_shutdown_yes_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *lbl_btn_yes = lv_label_create(btn_yes);
    lv_label_set_text(lbl_btn_yes, LV_SYMBOL_OK " Yes");
    lv_obj_center(lbl_btn_yes);

    lv_obj_t *btn_no = lv_btn_create(content);
    lv_obj_set_size(btn_no, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_add_event_cb(btn_no, btn_shutdown_no_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *lbl_btn_no = lv_label_create(btn_no);
    lv_label_set_text(lbl_btn_no, LV_SYMBOL_CLOSE " No");
    lv_obj_center(lbl_btn_no);
}

static void btn_shutdown_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        LOG_INF("Shutdown button clicked");
        k_msleep(100);
        hpi_show_shutdown_mbox();
    }
}

void draw_scr_settings(enum scroll_dir m_scroll_dir)
{
    scr_settings = lv_obj_create(NULL);

    draw_scr_common(scr_settings);

    /*Create a container with COLUMN flex direction*/
    lv_obj_t *cont_col = lv_obj_create(scr_settings);
    lv_obj_set_size(cont_col, 300, LV_SIZE_CONTENT);
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 45);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_col, -1, LV_PART_SCROLLBAR);
    lv_obj_add_style(cont_col, &style_scr_black, 0);


    label_batt_level_val = lv_label_create(cont_col);
    lv_label_set_text(label_batt_level_val, LV_SYMBOL_BATTERY_FULL " --");
    lv_obj_add_style(label_batt_level_val, &style_batt_percent, LV_STATE_DEFAULT);
    lv_obj_align_to(label_batt_level_val, label_batt_level, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

    lv_obj_t *cont_brightness = lv_obj_create(cont_col);
    lv_obj_set_size(cont_brightness, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_brightness, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_brightness, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lb_brightness = lv_label_create(cont_brightness);
    lv_label_set_text(lb_brightness, "Brightness : ");

    lv_obj_t *slider_brightness = lv_slider_create(cont_brightness);
    lv_obj_set_size(slider_brightness, lv_pct(100), 30);
    lv_obj_add_event_cb(slider_brightness, brightness_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_slider_set_value(slider_brightness, hpi_disp_get_brightness(), LV_ANIM_OFF);

    lv_obj_t *btn_shutdown = lv_btn_create(cont_col);
    lv_obj_set_size(btn_shutdown, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_add_event_cb(btn_shutdown, btn_shutdown_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *lbl_btn_shutdown = lv_label_create(btn_shutdown);
    lv_label_set_text(lbl_btn_shutdown, LV_SYMBOL_POWER " Power Off");
    lv_obj_set_height(btn_shutdown, 80);
    lv_obj_center(lbl_btn_shutdown);

    lv_obj_t *lbl_ver = lv_label_create(cont_col);
    lv_label_set_text(lbl_ver, "v" APP_VERSION_STRING);

    hpi_disp_set_curr_screen(SCR_SPL_SETTINGS);
    hpi_show_screen(scr_settings, m_scroll_dir);
}

void hpi_disp_settings_update_batt_level(int batt_level, bool charging)
{
    if (label_batt_level_val == NULL)
    {
        return;
    }

    if (batt_level < 0)
    {
        batt_level = 0;
    }

    if (batt_level > 75)
    {
        if (charging)
            lv_label_set_text_fmt(label_batt_level_val, LV_SYMBOL_CHARGE " " LV_SYMBOL_BATTERY_FULL " %d %", batt_level);
        else
            lv_label_set_text_fmt(label_batt_level_val, LV_SYMBOL_BATTERY_FULL " %d %", batt_level);
    }

    else if (batt_level > 50)
    {
        if (charging)
            lv_label_set_text_fmt(label_batt_level_val, LV_SYMBOL_CHARGE " " LV_SYMBOL_BATTERY_3 " %d %", batt_level);
        else
            lv_label_set_text_fmt(label_batt_level_val, LV_SYMBOL_BATTERY_3 " %d %", batt_level);
    }
    else if (batt_level > 25)
    {
        if (charging)
            lv_label_set_text_fmt(label_batt_level_val, LV_SYMBOL_CHARGE " " LV_SYMBOL_BATTERY_2 " %d %", batt_level);
        else
            lv_label_set_text_fmt(label_batt_level_val, LV_SYMBOL_BATTERY_2 " %d %", batt_level);
    }
    else if (batt_level > 10)
    {
        if (charging)
            lv_label_set_text_fmt(label_batt_level_val, LV_SYMBOL_CHARGE " " LV_SYMBOL_BATTERY_1 " %d %", batt_level);
        else
            lv_label_set_text_fmt(label_batt_level_val, LV_SYMBOL_BATTERY_1 " %d %", batt_level);
    }
    else
    {
        if (charging)
            lv_label_set_text_fmt(label_batt_level_val, LV_SYMBOL_CHARGE " " LV_SYMBOL_BATTERY_EMPTY " %d %", batt_level);
        else
            lv_label_set_text_fmt(label_batt_level_val, LV_SYMBOL_BATTERY_EMPTY " %d %", batt_level);
    }
}