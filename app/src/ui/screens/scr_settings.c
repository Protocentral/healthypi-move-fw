#include <zephyr/kernel.h>
#include <lvgl.h>
#include <stdio.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(scr_settings, LOG_LEVEL_INF);

#include "ui/move_ui.h"
#include "hw_module.h"

lv_obj_t *scr_settings;

static lv_obj_t *lbl_brightness_slider;
static lv_obj_t *label_batt_level;
static lv_obj_t *label_batt_level_val;

static int scr_settings_prev_scr = SCR_HOME;

K_MUTEX_DEFINE(mutex_settings_prev_screen);

extern lv_style_t style_scr_black;
extern lv_style_t style_batt_sym;
extern lv_style_t style_batt_percent;

static void scr_settings_set_prev_scr(int prev_scr)
{
    k_mutex_lock(&mutex_settings_prev_screen, K_FOREVER);
    scr_settings_prev_scr = prev_scr;
    k_mutex_unlock(&mutex_settings_prev_screen);
}

static int scr_settings_get_prev_scr(void)
{
    int prev_scr;
    k_mutex_lock(&mutex_settings_prev_screen, K_FOREVER);
    prev_scr = scr_settings_prev_scr;
    k_mutex_unlock(&mutex_settings_prev_screen);
    return prev_scr;
}

static void slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);

    /*Refresh the text*/
    lv_label_set_text_fmt(lbl_brightness_slider, "Brightness: %d %", lv_slider_get_value(slider));
    lv_obj_align_to(lbl_brightness_slider, slider, LV_ALIGN_OUT_TOP_MID, 0, -15);
    hpi_disp_set_brightness(lv_slider_get_value(slider));
}

static lv_obj_t *msgbox_shutdown;

static void btn_shutdown_yes_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        LOG_INF("Shutdown confirmed");
        hpi_hw_pmic_off();
    }
}

static void btn_shutdown_no_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        LOG_INF("Shutdown cancelled");
        lv_msgbox_close(msgbox_shutdown);
    }
}

static void hpi_show_shutdown_mbox(void)
{
    msgbox_shutdown = lv_msgbox_create(NULL, "", "Turn device OFF?", NULL, false);
    // lv_obj_add_event_cb(msgbox_shutdown, mbox_off_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_center(msgbox_shutdown);

    /* setting's content*/
    lv_obj_t *content = lv_msgbox_get_content(msgbox_shutdown);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    //lv_obj_set_flex_align(content, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    //lv_obj_set_style_pad_right(content, -1, LV_PART_SCROLLBAR);
    lv_obj_set_size(content, 300, 300);

    lv_obj_t *btn_yes = lv_btn_create(content);
    //lv_obj_set_size(btn_yes, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_add_event_cb(btn_yes, btn_shutdown_yes_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *lbl_btn_yes = lv_label_create(btn_yes);
    lv_label_set_text(lbl_btn_yes, LV_SYMBOL_POWER "Yes");
    lv_obj_center(lbl_btn_yes);

    lv_obj_t *btn_no = lv_btn_create(content);
    //lv_obj_set_size(btn_no, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_add_event_cb(btn_no, btn_shutdown_no_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *lbl_btn_no = lv_label_create(btn_no);
    lv_label_set_text(lbl_btn_no, "No");
    lv_obj_center(lbl_btn_no);

    /*lv_obj_t *apply_button = lv_msgbox_add_footer_button(msgbox_shutdown, "Apply");
    lv_obj_set_flex_grow(apply_button, 1);

    lv_obj_t *cancel_button = lv_msgbox_add_footer_button(msgbox_shutdown, "Cancel");
    lv_obj_set_flex_grow(cancel_button, 1);
    */
}

/*static void mbox_off_event_cb(lv_event_t *e)
{

    lv_obj_t *obj = lv_event_get_current_target(e);
    //int btn_id = lv_msgbox_get_active_btn(obj);
    //LV_LOG_USER("Button %d clicked", btn_id);
    hpi_show_shutdown_mbox();

}*/

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

    draw_header_minimal(scr_settings, 10);

    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_flex_flow(&style, LV_FLEX_FLOW_ROW_WRAP);
    //lv_style_set_flex_main_place(&style, LV_FLEX_ALIGN_SPACE_EVENLY);

    /*Create a container with COLUMN flex direction*/
    lv_obj_t *cont_col = lv_obj_create(scr_settings);
    lv_obj_set_size(cont_col, 320, 300);
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 45);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_style(cont_col, &style_scr_black, 0);
    lv_obj_add_style(cont_col, &style, 0);
    //lv_style_set_flex_main_place(&style, LV_FLEX_ALIGN_SPACE_EVENLY);
    // lv_obj_set_style_pad_column(cont_col, 15, 0);

    label_batt_level = lv_label_create(cont_col);
    lv_label_set_text(label_batt_level, LV_SYMBOL_BATTERY_FULL "--" );
    lv_obj_add_style(label_batt_level, &style_batt_sym, LV_STATE_DEFAULT);
    lv_obj_align(label_batt_level, LV_ALIGN_TOP_MID, 0, 25);

    label_batt_level_val = lv_label_create(cont_col);
    lv_label_set_text(label_batt_level_val, LV_SYMBOL_BATTERY_FULL "--");
    lv_obj_add_style(label_batt_level_val, &style_batt_percent, LV_STATE_DEFAULT);
    lv_obj_align_to(label_batt_level_val, label_batt_level, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

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

    // lv_obj_align_to(lbl_brightness_slider, slider, LV_ALIGN_OUT_TOP_MID, 0, -20);

    hpi_disp_set_curr_screen(SCR_SPL_SETTINGS);
    hpi_show_screen(scr_settings, m_scroll_dir);
}