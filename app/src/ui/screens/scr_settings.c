#include <zephyr/kernel.h>
#include <lvgl.h>
#include <stdio.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(scr_settings, LOG_LEVEL_INF);

#include "ui/move_ui.h"

lv_obj_t *scr_settings;

extern int curr_screen;

extern lv_style_t style_scr_black;


static void scroll_event_cb(lv_event_t *e)
{
    lv_obj_t *cont = lv_event_get_target(e);

    lv_area_t cont_a;
    lv_obj_get_coords(cont, &cont_a);
    lv_coord_t cont_y_center = cont_a.y1 + lv_area_get_height(&cont_a) / 2;

    lv_coord_t r = lv_obj_get_height(cont) * 7 / 10;
    uint32_t i;
    uint32_t child_cnt = lv_obj_get_child_cnt(cont);
    for (i = 0; i < child_cnt; i++)
    {
        lv_obj_t *child = lv_obj_get_child(cont, i);
        lv_area_t child_a;
        lv_obj_get_coords(child, &child_a);

        lv_coord_t child_y_center = child_a.y1 + lv_area_get_height(&child_a) / 2;

        lv_coord_t diff_y = child_y_center - cont_y_center;
        diff_y = LV_ABS(diff_y);

        /*Get the x of diff_y on a circle.*/
        lv_coord_t x;
        /*If diff_y is out of the circle use the last point of the circle (the radius)*/
        if (diff_y >= r)
        {
            x = r;
        }
        else
        {
            /*Use Pythagoras theorem to get x from radius and y*/
            uint32_t x_sqr = r * r - diff_y * diff_y;
            lv_sqrt_res_t res;
            lv_sqrt(x_sqr, &res, 0x8000); /*Use lvgl's built in sqrt root function*/
            x = r - res.i;
        }

        /*Translate the item by the calculated X coordinate*/
        lv_obj_set_style_translate_x(child, x, 0);

        /*Use some opacity with larger translations*/
        lv_opa_t opa = lv_map(x, 0, r, LV_OPA_TRANSP, LV_OPA_COVER);
        lv_obj_set_style_opa(child, LV_OPA_COVER - opa, 0);
    }
}

static lv_obj_t *lbl_brightness_slider;

static void slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);

    /*Refresh the text*/
    lv_label_set_text_fmt(lbl_brightness_slider, "Brightness: %d %", lv_slider_get_value(slider));
    lv_obj_align_to(lbl_brightness_slider, slider, LV_ALIGN_OUT_TOP_MID, 0, -15); /*Align top of the slider*/
}

static void btn_shutdown_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *btn = lv_event_get_target(e);
    if (code == LV_EVENT_CLICKED)
    {
        LOG_INF("Shutdown button clicked");
    }
}

void draw_scr_settings(enum scroll_dir m_scroll_dir)
{
    scr_settings = lv_obj_create(NULL);

    draw_header_minimal(scr_settings, 10);

    /*Create a container with COLUMN flex direction*/
    lv_obj_t *cont_col = lv_obj_create(scr_settings);
    lv_obj_set_size(cont_col, 350, 390);
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 45);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_style(cont_col, &style_scr_black, 0);

    // Brightness slider
    lbl_brightness_slider = lv_label_create(cont_col);
    lv_obj_set_size(lbl_brightness_slider, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_label_set_text(lbl_brightness_slider, "Brightness: 0 %");
    lv_obj_center(lbl_brightness_slider);

    lv_obj_t *slider = lv_slider_create(cont_col);
    lv_obj_set_size(slider, LV_PCT(100), 30);
    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *btn_shutdown = lv_btn_create(cont_col);
    lv_obj_set_size(btn_shutdown, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_add_event_cb(btn_shutdown, btn_shutdown_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *lbl_btn_shutdown = lv_label_create(btn_shutdown);
    lv_label_set_text(lbl_btn_shutdown, "Shutdown");
    lv_obj_center(lbl_btn_shutdown);

    lv_obj_t *btn_test = lv_btn_create(cont_col);
    lv_obj_add_event_cb(btn_test, btn_shutdown_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_set_size(btn_test, LV_PCT(100), LV_SIZE_CONTENT);

    lv_obj_t *lbl_btn_test = lv_label_create(btn_test); /*Add a label to the button*/
    lv_label_set_text(lbl_btn_test, "Test Sensors");    /*Set the labels text*/
    lv_obj_center(lbl_btn_test);

    // lv_obj_align_to(lbl_brightness_slider, slider, LV_ALIGN_OUT_TOP_MID, 0, -20);

    curr_screen = SCR_SETTINGS;
    hpi_show_screen(scr_settings, m_scroll_dir);
}