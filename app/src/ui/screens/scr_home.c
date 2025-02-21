#include <zephyr/kernel.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <stdio.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/rtc.h>

#include "ui/move_ui.h"
#include "hw_module.h"

lv_obj_t *scr_home;

static lv_obj_t *meter_clock;
static lv_obj_t *home_step_disp;
static lv_obj_t *home_hr_disp;

static lv_meter_indicator_t *indic_min;
static lv_meter_indicator_t *indic_hour;
static lv_meter_indicator_t *indic_sec;

static lv_obj_t *ui_home_label_hour;
static lv_obj_t *ui_home_label_min;
static lv_obj_t *ui_home_label_date;

extern struct rtc_time global_system_time;

void ui_home_time_display_update(struct rtc_time in_time)
{
    if (ui_home_label_hour == NULL || ui_home_label_min == NULL || ui_home_label_date == NULL)
        return;

    char buf[5];
    char date_buf[20];

    char mon_strs[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    sprintf(buf, "%02d:", in_time.tm_hour);
    lv_label_set_text(ui_home_label_hour, buf);

    sprintf(buf, "%02d", in_time.tm_min);
    lv_label_set_text(ui_home_label_min, buf);

    sprintf(date_buf, "%02d %s %04d", in_time.tm_mday, mon_strs[in_time.tm_mon], in_time.tm_year + 1900);
    lv_label_set_text(ui_home_label_date, date_buf);
}

void hpi_home_hr_update(int hr)
{
    printk("HR Update : %d\n", hr);
    if (home_hr_disp == NULL)
        return;

    char buf[5];
    sprintf(buf, "%d", hr);
    // lv_label_set_text(home_hr_disp, buf);
}

void draw_scr_home(enum scroll_dir m_scroll_dir)
{
    scr_home = lv_obj_create(NULL);

    lv_obj_set_style_bg_color(scr_home, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_home, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    draw_bg(scr_home);
    //draw_header_minimal(scr_home, 320);

    home_step_disp = ui_steps_button_create(scr_home);
    lv_obj_align_to(home_step_disp, NULL, LV_ALIGN_TOP_MID, -100, 210);
    lv_obj_set_style_border_opa(home_step_disp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    home_hr_disp = ui_hr_button_create(scr_home);
    lv_obj_align_to(home_hr_disp, NULL, LV_ALIGN_TOP_MID, 100, 210);
    lv_obj_set_style_border_opa(home_hr_disp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_home_label_hour = lv_label_create(scr_home);
    // lv_obj_set_width(ui_label_hour, LV_SIZE_CONTENT);  /// 1
    // lv_obj_set_height(ui_label_hour, LV_SIZE_CONTENT); /// 1
    lv_obj_align_to(ui_home_label_hour, NULL, LV_ALIGN_TOP_MID, -90, 70);
    lv_label_set_text(ui_home_label_hour, "00:");
    lv_obj_set_style_text_color(ui_home_label_hour, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_home_label_hour, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_home_label_hour, &ui_font_Number_big, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_home_label_min = lv_label_create(scr_home);
    // lv_obj_set_width(ui_label_min, LV_SIZE_CONTENT);  /// 1
    // lv_obj_set_height(ui_label_min, LV_SIZE_CONTENT); /// 1
    lv_obj_align_to(ui_home_label_min, ui_home_label_hour, LV_ALIGN_OUT_RIGHT_TOP, 0, 0);
    lv_label_set_text(ui_home_label_min, "00");
    lv_obj_set_style_text_color(ui_home_label_min, lv_color_hex(0xEE1E1E), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_home_label_min, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_home_label_min, &ui_font_Number_big, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_home_label_date = lv_label_create(scr_home);
    lv_obj_set_width(ui_home_label_date, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_home_label_date, LV_SIZE_CONTENT); /// 1
    lv_label_set_text(ui_home_label_date, "-- -- ----");
    lv_obj_align_to(ui_home_label_date, ui_home_label_hour, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_color(ui_home_label_date, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_home_label_date, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_home_label_date, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    //ui_home_time_display_update(hw_get_current_time());

    hpi_disp_set_curr_screen(SCR_HOME);
    hpi_show_screen(scr_home, m_scroll_dir);
    
}

void draw_scr_home_analog(enum scroll_dir m_scroll_dir)
{
    scr_home = lv_obj_create(NULL);

    lv_obj_set_style_bg_color(scr_home, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_home, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    draw_bg(scr_home);
    draw_header_minimal(scr_home, 75);

    home_step_disp = ui_steps_button_create(scr_home);
    lv_obj_align_to(home_step_disp, NULL, LV_ALIGN_TOP_MID, -50, 100);
    lv_obj_set_style_border_opa(home_step_disp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    home_hr_disp = ui_hr_button_create(scr_home);
    lv_obj_align_to(home_hr_disp, NULL, LV_ALIGN_TOP_MID, 50, 210);
    lv_obj_set_style_border_opa(home_hr_disp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    meter_clock = lv_meter_create(scr_home);
    lv_obj_set_size(meter_clock, 400, 400);
    // lv_obj_set_style_opa(meter_clock, 50, LV_PART_MAIN);
    // lv_obj_set_style_bg_color(meter_clock, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(meter_clock, 0, LV_PART_MAIN);

    lv_obj_center(meter_clock);

    lv_obj_t *ui_label_12 = lv_label_create(scr_home);
    lv_obj_align_to(ui_label_12, NULL, LV_ALIGN_TOP_MID, 0, 5);
    lv_label_set_text(ui_label_12, "12");
    lv_obj_set_style_text_color(ui_label_12, lv_palette_main(LV_PALETTE_TEAL), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_label_12, &ui_font_Number_big, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *ui_label_3 = lv_label_create(scr_home);
    lv_obj_align_to(ui_label_3, NULL, LV_ALIGN_RIGHT_MID, -5, -5);
    lv_label_set_text(ui_label_3, "3");
    lv_obj_set_style_text_color(ui_label_3, lv_palette_main(LV_PALETTE_TEAL), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_label_3, &ui_font_Number_big, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *ui_label_6 = lv_label_create(scr_home);
    lv_obj_align_to(ui_label_6, NULL, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_label_set_text(ui_label_6, "6");
    lv_obj_set_style_text_color(ui_label_6, lv_palette_main(LV_PALETTE_TEAL), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_label_6, &ui_font_Number_big, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *ui_label_9 = lv_label_create(scr_home);
    lv_obj_align_to(ui_label_9, NULL, LV_ALIGN_LEFT_MID, 5, -5);
    lv_label_set_text(ui_label_9, "9");
    lv_obj_set_style_text_color(ui_label_9, lv_palette_main(LV_PALETTE_TEAL), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_label_9, &ui_font_Number_big, LV_PART_MAIN | LV_STATE_DEFAULT);

    /*Create a scale for the minutes*/
    /*61 ticks in a 360 degrees range (the last and the first line overlaps)*/
    lv_meter_scale_t *scale_min = lv_meter_add_scale(meter_clock);
    // lv_meter_set_scale_ticks(meter_clock, scale_min, 61, 1, 10, lv_color_white()); //61 ticks
    lv_meter_set_scale_range(meter_clock, scale_min, 1, 60, 360, 270);

    /*Create another scale for the hours. It's only visual and contains only major ticks*/
    // lv_meter_scale_t *scale_hour = lv_meter_add_scale(meter_clock);
    // lv_meter_set_scale_ticks(meter_clock, scale_hour, 12, 0, 0, lv_color_white());           // 12 ticks
    // lv_meter_set_scale_major_ticks(meter_clock, scale_hour, 3, 2, 20, lv_color_white(), 20); // Every tick is major
    // lv_meter_set_scale_range(meter_clock, scale_hour, 1, 12, 330, 300);                      //[1..12] values in an almost full circle

    // LV_IMG_DECLARE(img_hand)
    // LV_IMG_DECLARE(clock_long_hand)

    indic_min = lv_meter_add_needle_line(meter_clock, scale_min, 4, lv_color_white(), -25);                  // lv_meter_add_needle_img(meter_clock, scale_min, &clock_long_hand, 5, 5);
    indic_hour = lv_meter_add_needle_line(meter_clock, scale_min, 4, lv_palette_main(LV_PALETTE_BLUE), -60); // lv_meter_add_needle_img(meter_clock, scale_min, &clock_long_hand, 5, 5);
    indic_sec = lv_meter_add_needle_line(meter_clock, scale_min, 2, lv_palette_main(LV_PALETTE_RED), -20);   // lv_meter_add_needle_img(meter_clock, scale_min, &img_hand, 5, 5);

    // Create an animation to set the value
    /*lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_exec_cb(&a, set_value);
    lv_anim_set_values(&a, 0, 60);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_time(&a, 60000); // 2 sec for 1 turn of the minute hand (1 hour)
    lv_anim_set_var(&a, indic_sec);
    lv_anim_start(&a);*/

    lv_meter_set_indicator_end_value(meter_clock, indic_hour, 2);
    lv_meter_set_indicator_end_value(meter_clock, indic_min, 50);

    /*lv_anim_set_var(&a, indic_hour);
    lv_anim_set_time(&a, 120000); //24 sec for 1 turn of the hour hand
    lv_anim_set_values(&a, 0, 60);
    lv_anim_start(&a);
    */

    // lv_obj_add_event_cb(btn_hr_disp, scr_clock_small_hr_event_handler, LV_EVENT_ALL, NULL);

    hpi_disp_set_curr_screen(SCR_HOME);
    hpi_show_screen(scr_home, m_scroll_dir);
    // scr_home_set_time(global_system_time);
}

/*void draw_scr_home(enum scroll_dir m_scroll_dir)
{
    draw_scr_home_digital(m_scroll_dir);
}*/