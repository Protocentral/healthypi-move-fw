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

static void set_value(void *indic, int32_t v)
{
    lv_meter_set_indicator_end_value(meter_clock, indic, v);
}

void draw_scr_clock_analog(enum scroll_dir m_scroll_dir)
{
    scr_clock_analog = lv_obj_create(NULL);

    //lv_obj_set_style_bg_color(scr_clock_analog, lv_color_black(), LV_STATE_DEFAULT);
    //lv_obj_set_style_bg_opa(scr_clock_analog, 1, LV_STATE_DEFAULT);

    draw_bg(scr_clock_analog);

    meter_clock = lv_meter_create(scr_clock_analog);
    lv_obj_set_size(meter_clock, 390, 390);
    //lv_obj_set_style_opa(meter_clock, 50, LV_PART_MAIN);
    lv_obj_set_style_bg_color(meter_clock, lv_color_black(), LV_PART_MAIN);
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
    lv_meter_set_scale_major_ticks(meter_clock, scale_hour, 1, 2, 20, lv_color_white(), 10); /*Every tick is major*/
    lv_meter_set_scale_range(meter_clock, scale_hour, 1, 12, 330, 300);                      /*[1..12] values in an almost full circle*/

    //LV_IMG_DECLARE(img_hand)
    LV_IMG_DECLARE(clock_long_hand)

    /*Add a the hands from images*/
    lv_meter_indicator_t *indic_min = lv_meter_add_needle_img(meter_clock, scale_min, &clock_long_hand, 5, 5);
    lv_meter_indicator_t *indic_hour = lv_meter_add_needle_img(meter_clock, scale_min, &clock_long_hand, 5, 5);

    /*Create an animation to set the value*/
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_exec_cb(&a, set_value);
    lv_anim_set_values(&a, 0, 60);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_time(&a, 2000); /*2 sec for 1 turn of the minute hand (1 hour)*/
    lv_anim_set_var(&a, indic_min);
    lv_anim_start(&a);

    /*lv_anim_set_var(&a, indic_hour);
    lv_anim_set_time(&a, 24000); //24 sec for 1 turn of the hour hand
    lv_anim_set_values(&a, 0, 60);
    lv_anim_start(&a);
    */

    // lv_obj_add_event_cb(btn_hr_disp, scr_clock_small_hr_event_handler, LV_EVENT_ALL, NULL);

    // curr_screen = SCR_CLOCK_SMALL;
    hpi_show_screen(scr_clock_analog, m_scroll_dir);
}