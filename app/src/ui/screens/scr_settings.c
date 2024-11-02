#include <zephyr/kernel.h>
#include <lvgl.h>
#include <stdio.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(scr_settings, LOG_LEVEL_INF);

#include "ui/move_ui.h"

lv_obj_t *scr_settings;

extern lv_style_t style_scr_black;

static lv_obj_t *lbl_brightness_slider;

static void slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);

    /*Refresh the text*/
    lv_label_set_text_fmt(lbl_brightness_slider, "Brightness: %d %", lv_slider_get_value(slider));
    lv_obj_align_to(lbl_brightness_slider, slider, LV_ALIGN_OUT_TOP_MID, 0, -15);
    hpi_disp_set_brightness(lv_slider_get_value(slider));
}

static lv_obj_t *mbox1;

static void mbox_off_event_cb(lv_event_t *e)
{
    
    lv_obj_t * obj = lv_event_get_current_target(e);
    int btn_id =lv_msgbox_get_active_btn(obj);
    LV_LOG_USER("Button %d clicked", btn_id );

    if (btn_id == 0)
    {
        LOG_INF("Shutdown confirmed");
        lv_msgbox_close(mbox1);
        //hpi_power_off();
    }
    else
    {
        LOG_INF("Shutdown cancelled");
        lv_msgbox_close(mbox1);
    }
}

static void btn_shutdown_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *btn = lv_event_get_target(e);
    if (code == LV_EVENT_CLICKED)
    {
        LOG_INF("Shutdown button clicked");
        static const char *btns[] = {"Yes", "No", ""};

        mbox1 = lv_msgbox_create(NULL, "", "Turn device OFF?", btns, false);
        lv_obj_add_event_cb(mbox1, mbox_off_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_center(mbox1);
    }
}

void draw_scr_settings(enum scroll_dir m_scroll_dir)
{
    scr_settings = lv_obj_create(NULL);

    draw_header_minimal(scr_settings, 10);

    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_flex_flow(&style, LV_FLEX_FLOW_ROW_WRAP);
    lv_style_set_flex_main_place(&style, LV_FLEX_ALIGN_SPACE_EVENLY);

    /*Create a container with COLUMN flex direction*/
    lv_obj_t *cont_col = lv_obj_create(scr_settings);
    lv_obj_set_size(cont_col, 320, 300);
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 45);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_style(cont_col, &style_scr_black, 0);
    lv_obj_add_style(cont_col, &style, 0);
    lv_style_set_flex_main_place(&style, LV_FLEX_ALIGN_SPACE_EVENLY);
    // lv_obj_set_style_pad_column(cont_col, 15, 0);

    // Brightness slider
    lbl_brightness_slider = lv_label_create(cont_col);
    lv_obj_set_size(lbl_brightness_slider, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_label_set_text_fmt(lbl_brightness_slider, "Brightness: %d %", hpi_disp_get_brightness());
    lv_obj_center(lbl_brightness_slider);

    lv_obj_t *slider = lv_slider_create(cont_col);
    lv_obj_set_size(slider, LV_PCT(100), 30);
    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_slider_set_value(slider, hpi_disp_get_brightness(), LV_ANIM_OFF);

    lv_obj_t *btn_shutdown = lv_btn_create(cont_col);
    lv_obj_set_size(btn_shutdown, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_add_event_cb(btn_shutdown, btn_shutdown_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *lbl_btn_shutdown = lv_label_create(btn_shutdown);
    lv_label_set_text(lbl_btn_shutdown, LV_SYMBOL_POWER " Power Off");
    lv_obj_center(lbl_btn_shutdown);

    lv_obj_t *btn_test = lv_btn_create(cont_col);
    lv_obj_add_event_cb(btn_test, btn_shutdown_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_set_size(btn_test, LV_PCT(100), LV_SIZE_CONTENT);

    lv_obj_t *lbl_btn_test = lv_label_create(btn_test); /*Add a label to the button*/
    lv_label_set_text(lbl_btn_test, "Test Sensors");    /*Set the labels text*/
    lv_obj_center(lbl_btn_test);

    // lv_obj_align_to(lbl_brightness_slider, slider, LV_ALIGN_OUT_TOP_MID, 0, -20);

    hpi_disp_set_curr_screen(SCR_SETTINGS);
    hpi_show_screen(scr_settings, m_scroll_dir);
}