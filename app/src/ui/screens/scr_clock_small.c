#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>
#include <stdio.h>
#include <zephyr/smf.h>
#include <app_version.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/rtc.h>

#include "hpi_common_types.h"

#include "ui/move_ui.h"

lv_obj_t *scr_clock_small;

extern lv_obj_t *btn_hr_disp;
extern lv_obj_t *ui_hr_number;

extern lv_obj_t *ui_hour_group;
extern lv_obj_t *ui_label_hour;
extern lv_obj_t *ui_label_min;
extern lv_obj_t *ui_label_date;

extern lv_obj_t *ui_step_group;
extern lv_obj_t *ui_dailymission_group;

extern struct rtc_time global_system_time;

static void scr_clock_small_hr_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED)
    {
        printk("Clicked HR\n");
    }
}

void draw_scr_clock_small(enum scroll_dir m_scroll_dir)
{
    scr_clock_small = lv_obj_create(NULL);
    lv_obj_clear_flag(scr_clock_small, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    lv_obj_set_style_bg_color(scr_clock_small, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(scr_clock_small, 1, LV_STATE_DEFAULT);

    draw_bg(scr_clock_small);
    draw_header_minimal(scr_clock_small,0);

    ui_label_hour = lv_label_create(scr_clock_small);
    lv_obj_set_width(ui_label_hour, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_label_hour, LV_SIZE_CONTENT); /// 1
    lv_obj_align_to(ui_label_hour, NULL, LV_ALIGN_TOP_MID, -50, 35);
    lv_label_set_text(ui_label_hour, "-- : ");
    lv_obj_set_style_text_color(ui_label_hour, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_label_hour, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_label_hour, &lv_font_montserrat_42, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_label_min = lv_label_create(scr_clock_small);
    lv_obj_set_width(ui_label_min, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_label_min, LV_SIZE_CONTENT); /// 1
    lv_obj_align_to(ui_label_min, ui_label_hour, LV_ALIGN_OUT_RIGHT_TOP, 25, 0);
    lv_label_set_text(ui_label_min, "--");
    lv_obj_set_style_text_color(ui_label_min, lv_color_hex(0xEE1E1E), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_label_min, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_label_min, &lv_font_montserrat_42, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_label_date = lv_label_create(scr_clock_small);
    lv_obj_set_width(ui_label_date, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_label_date, LV_SIZE_CONTENT); /// 1
    lv_obj_align_to(ui_label_date, ui_label_hour, LV_ALIGN_OUT_BOTTOM_MID, 0, -7);
    lv_label_set_text(ui_label_date, "--");
    lv_obj_set_style_text_color(ui_label_date, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_label_date, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_label_date, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_time_display_update(global_system_time.tm_hour, global_system_time.tm_min, true );
    ui_date_display_update(global_system_time.tm_mday, global_system_time.tm_mon, (global_system_time.tm_year+2000));

    lv_obj_t *hr_display = ui_hr_button_create(scr_clock_small);
    lv_obj_align_to(hr_display, NULL, LV_ALIGN_TOP_MID, -90, 115);
    lv_obj_set_style_border_opa(hr_display, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *spo2_display = ui_spo2_button_create(scr_clock_small);
    lv_obj_align_to(spo2_display, NULL, LV_ALIGN_TOP_MID, 60, 115);
    lv_obj_set_style_border_opa(spo2_display, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_step_group = ui_steps_button_create(scr_clock_small);
    lv_obj_align_to(ui_step_group, NULL, LV_ALIGN_TOP_MID, 0,250);
    lv_obj_set_style_border_opa(ui_step_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    

    /*ui_dailymission_group = ui_dailymissiongroup_create(scr_clock_small);
    lv_obj_align_to(ui_dailymission_group, NULL, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_border_opa(ui_dailymission_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    */
    

    lv_obj_add_event_cb(btn_hr_disp, scr_clock_small_hr_event_handler, LV_EVENT_ALL, NULL);

    //hpi_disp_set_curr_screen(SCR_CLOCK_SMALL);
    //curr_screen = SCR_CLOCK_SMALL;
    hpi_show_screen(scr_clock_small, m_scroll_dir);
}