#include <zephyr/kernel.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <stdio.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/rtc.h>

#include "display_module.h"
#include "ui/move_ui.h"

lv_obj_t *scr_clock_analog;

static lv_obj_t *meter_clock;

extern int curr_screen;

static void set_value(void *indic, int32_t v)
{
    if(indic == NULL || meter_clock == NULL) return;
    
    lv_meter_set_indicator_end_value(meter_clock, indic, v);
}

void draw_scr_clock_analog(enum scroll_dir m_scroll_dir)
{
    scr_clock_analog = lv_obj_create(NULL);

    lv_obj_set_style_bg_color(scr_clock_analog, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_clock_analog, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    draw_bg(scr_clock_analog);
    draw_header_minimal(scr_clock_analog, 75);

    meter_clock = lv_meter_create(scr_clock_analog);
    lv_obj_set_size(meter_clock, 400, 400);
    // lv_obj_set_style_opa(meter_clock, 50, LV_PART_MAIN);
    // lv_obj_set_style_bg_color(meter_clock, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(meter_clock, 0, LV_PART_MAIN);

    lv_obj_center(meter_clock);

    /*Create a scale for the minutes*/
    /*61 ticks in a 360 degrees range (the last and the first line overlaps)*/
    lv_meter_scale_t *scale_min = lv_meter_add_scale(meter_clock);
    lv_meter_set_scale_ticks(meter_clock, scale_min, 61, 1, 10, lv_color_white()); /*61 ticks*/
    lv_meter_set_scale_range(meter_clock, scale_min, 0, 60, 360, 270);

    /*Create another scale for the hours. It's only visual and contains only major ticks*/
    lv_meter_scale_t *scale_hour = lv_meter_add_scale(meter_clock);
    lv_meter_set_scale_ticks(meter_clock, scale_hour, 12, 0, 0, lv_color_white());           /*12 ticks*/
    lv_meter_set_scale_major_ticks(meter_clock, scale_hour, 1, 2, 20, lv_color_white(), 14); /*Every tick is major*/
    lv_meter_set_scale_range(meter_clock, scale_hour, 1, 12, 330, 300);                      /*[1..12] values in an almost full circle*/

    // LV_IMG_DECLARE(img_hand)
    // LV_IMG_DECLARE(clock_long_hand)

    /*Add a the hands from images*/
    lv_meter_indicator_t *indic_min = lv_meter_add_needle_line(meter_clock, scale_min, 4, lv_color_white(), -25);                // lv_meter_add_needle_img(meter_clock, scale_min, &clock_long_hand, 5, 5);
    lv_meter_indicator_t *indic_hour = lv_meter_add_needle_line(meter_clock, scale_hour, 4, lv_color_white(), -60);              // lv_meter_add_needle_img(meter_clock, scale_min, &clock_long_hand, 5, 5);
    lv_meter_indicator_t *indic_sec = lv_meter_add_needle_line(meter_clock, scale_min, 2, lv_palette_main(LV_PALETTE_RED), -20); // lv_meter_add_needle_img(meter_clock, scale_min, &img_hand, 5, 5);

    /*Create an animation to set the value*/
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_exec_cb(&a, set_value);
    lv_anim_set_values(&a, 0, 60);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_time(&a, 60000); /*2 sec for 1 turn of the minute hand (1 hour)*/
    lv_anim_set_var(&a, indic_sec);
    lv_anim_start(&a);

    lv_meter_set_indicator_end_value(meter_clock, indic_hour, 2);
    lv_meter_set_indicator_end_value(meter_clock, indic_min, 50);

    /*lv_anim_set_var(&a, indic_hour);
    lv_anim_set_time(&a, 120000); //24 sec for 1 turn of the hour hand
    lv_anim_set_values(&a, 0, 60);
    lv_anim_start(&a);
    */

    lv_obj_t *hr_display = ui_hr_button_create(scr_clock_analog);
    lv_obj_align_to(hr_display, NULL, LV_ALIGN_TOP_MID, 0,240);
    lv_obj_set_style_border_opa(hr_display, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    // lv_obj_add_event_cb(btn_hr_disp, scr_clock_small_hr_event_handler, LV_EVENT_ALL, NULL);

    curr_screen = SCR_CLOCK_ANALOG;
    hpi_show_screen(scr_clock_analog, m_scroll_dir);
}