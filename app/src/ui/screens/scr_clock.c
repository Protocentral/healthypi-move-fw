#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/dac.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>
#include <stdio.h>
#include <zephyr/smf.h>
#include <app_version.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/rtc.h>

#include "display_module.h"
#include "sampling_module.h"

#include "ui/move_ui.h"

lv_obj_t *scr_clock;

extern lv_obj_t *btn_hr_disp;
extern lv_obj_t *ui_hr_number;

extern lv_obj_t *ui_hour_group;
extern lv_obj_t *ui_label_hour;
extern lv_obj_t *ui_label_min;
extern lv_obj_t *ui_label_date;

extern lv_obj_t *ui_step_group;

extern struct rtc_time global_system_time;
extern int curr_screen;

static void scr_clock_hr_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED)
    {
        printk("Clicked HR\n");
    }
}

void draw_scr_clockface(enum scroll_dir m_scroll_dir)
{
    scr_clock = lv_obj_create(NULL);
    lv_obj_clear_flag(scr_clock, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    lv_obj_set_style_bg_color(scr_clock, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(scr_clock, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    draw_header_minimal(scr_clock);
    draw_bg(scr_clock);

    ui_hour_group = lv_obj_create(scr_clock);
    lv_obj_set_width(ui_hour_group, 250);
    lv_obj_set_height(ui_hour_group, 250);
    lv_obj_clear_flag(ui_hour_group, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE); /// Flags
    lv_obj_set_style_bg_color(ui_hour_group, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_hour_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_hour_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_label_hour = lv_label_create(ui_hour_group);
    lv_obj_set_width(ui_label_hour, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_label_hour, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_label_hour, -5);
    lv_obj_set_y(ui_label_hour, 25);
    lv_label_set_text(ui_label_hour, "--");
    lv_obj_set_style_text_color(ui_label_hour, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_label_hour, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_label_hour, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_label_min = lv_label_create(scr_clock);
    lv_obj_set_width(ui_label_min, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_label_min, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_label_min, 105);
    lv_obj_set_y(ui_label_min, 120);
    lv_label_set_text(ui_label_min, "--");
    lv_obj_set_style_text_color(ui_label_min, lv_color_hex(0xEE1E1E), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_label_min, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_label_min, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_time_display_update(global_system_time.tm_hour, global_system_time.tm_min, false);

    ui_step_group = ui_steps_button_create(scr_clock);
    lv_obj_set_x(ui_step_group, -60);
    lv_obj_set_y(ui_step_group, 60);
    lv_obj_set_style_border_opa(ui_step_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *hr_display = ui_hr_button_create(scr_clock);
    lv_obj_set_x(hr_display, 66);
    lv_obj_set_y(hr_display, -140);
    lv_obj_set_style_border_opa(hr_display, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(btn_hr_disp, scr_clock_hr_event_handler, LV_EVENT_ALL, NULL);

    curr_screen = SCR_CLOCK;

    // lv_obj_add_event_cb(scr_clock, disp_screen_event, LV_EVENT_GESTURE, NULL);
    hpi_show_screen(scr_clock, m_scroll_dir);
}