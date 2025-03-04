#include <zephyr/kernel.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <stdio.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/rtc.h>

#include "ui/move_ui.h"
#include "hw_module.h"

lv_obj_t *scr_today;

lv_obj_t *today_arcs;

lv_obj_t *today_arc_steps;
lv_obj_t *today_arc_cals;
lv_obj_t *today_arc_active_time;

lv_obj_t *label_today_steps;
lv_obj_t *label_today_cals;
lv_obj_t *label_today_active_time;

extern lv_style_t style_white_medium;

uint16_t m_steps_today_target = 10000;
uint16_t m_kcals_today_target = 500;
uint16_t m_active_time_today_target = 30;

void draw_scr_today(enum scroll_dir m_scroll_dir)
{
    scr_today = lv_obj_create(NULL);

    lv_obj_clear_flag(scr_today, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    //draw_bg(scr_today);
    draw_header_minimal(scr_today, 320);

    //today_arcs = ui_dailymissiongroup_create(scr_today);

    lv_obj_t *today_group;
    today_group = lv_obj_create(scr_today);
    lv_obj_set_width(today_group, 360);
    lv_obj_set_height(today_group, 360);
    lv_obj_set_align(today_group, LV_ALIGN_CENTER);
    lv_obj_clear_flag(today_group, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE); /// Flags
    lv_obj_set_style_bg_color(today_group, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(today_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(today_group, 0, LV_PART_MAIN);

    today_arc_steps = lv_arc_create(today_group);
    lv_obj_set_size(today_arc_steps, 360, 360);
    lv_obj_set_align(today_arc_steps, LV_ALIGN_CENTER);
    lv_obj_add_flag(today_arc_steps, LV_OBJ_FLAG_ADV_HITTEST); /// Flags
    lv_arc_set_value(today_arc_steps, 25);
    lv_arc_set_bg_angles(today_arc_steps, 90, 315);
    lv_obj_set_style_arc_color(today_arc_steps, lv_palette_darken(LV_PALETTE_GREY,3), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(today_arc_steps, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(today_arc_steps, true, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_arc_color(today_arc_steps, lv_palette_darken(LV_PALETTE_RED, 4), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(today_arc_steps, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(today_arc_steps, true, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    lv_obj_set_style_arc_width(today_arc_steps, 15, LV_PART_MAIN);      // Changes background arc width
    lv_obj_set_style_arc_width(today_arc_steps, 15, LV_PART_INDICATOR); // Changes set part width

    lv_obj_set_style_bg_color(today_arc_steps, lv_color_hex(0xFFFFFF), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(today_arc_steps, 0, LV_PART_KNOB | LV_STATE_DEFAULT);

    today_arc_cals = lv_arc_create(today_group);
    lv_obj_set_size(today_arc_cals, 310, 310);
    lv_obj_set_align(today_arc_cals, LV_ALIGN_CENTER);
    lv_obj_add_flag(today_arc_cals, LV_OBJ_FLAG_ADV_HITTEST); /// Flags
    lv_arc_set_value(today_arc_cals, 75);
    lv_arc_set_bg_angles(today_arc_cals, 90, 315);
    lv_obj_set_style_arc_color(today_arc_cals, lv_palette_darken(LV_PALETTE_GREY,3), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(today_arc_cals, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(today_arc_cals, true, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_arc_color(today_arc_cals, lv_palette_darken(LV_PALETTE_YELLOW, 3) , LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(today_arc_cals, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(today_arc_cals, true, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    lv_obj_set_style_arc_width(today_arc_cals, 15, LV_PART_MAIN);      // Changes background arc width
    lv_obj_set_style_arc_width(today_arc_cals, 15, LV_PART_INDICATOR); // Changes set part width

    lv_obj_set_style_bg_color(today_arc_cals, lv_color_hex(0xFFFFFF), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(today_arc_cals, 0, LV_PART_KNOB | LV_STATE_DEFAULT);

    today_arc_active_time = lv_arc_create(today_group);
    lv_obj_set_size(today_arc_active_time, 260, 260);
    lv_obj_set_align(today_arc_active_time, LV_ALIGN_CENTER);
    lv_obj_add_flag(today_arc_active_time, LV_OBJ_FLAG_ADV_HITTEST); /// Flags
    lv_arc_set_value(today_arc_active_time, 50);
    lv_arc_set_bg_angles(today_arc_active_time, 90, 315);
    lv_obj_set_style_arc_color(today_arc_active_time,lv_palette_darken(LV_PALETTE_GREY,3), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(today_arc_active_time, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(today_arc_active_time, true, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_arc_color(today_arc_active_time, lv_palette_darken(LV_PALETTE_GREEN, 1), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(today_arc_active_time, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(today_arc_active_time, true, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    lv_obj_set_style_arc_width(today_arc_active_time, 15, LV_PART_MAIN);      // Changes background arc width
    lv_obj_set_style_arc_width(today_arc_active_time, 15, LV_PART_INDICATOR); // Changes set part width

    lv_obj_set_style_bg_color(today_arc_active_time, lv_color_hex(0xFFFFFF), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(today_arc_active_time, 0, LV_PART_KNOB | LV_STATE_DEFAULT);

    /*lv_obj_t *label_title = lv_label_create(today_group);
    lv_label_set_text(label_title, "TODAY");
    lv_obj_align_to(label_title, NULL, LV_ALIGN_TOP_MID, 0, 65);
    */
    //lv_obj_set_style_text_color(label_title, lv_color_hex(0x303030), LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *img_steps = lv_img_create(today_group);
    lv_img_set_src(img_steps, &img_steps_48);
    lv_obj_set_width(img_steps, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(img_steps, LV_SIZE_CONTENT); /// 1
    lv_obj_add_flag(img_steps, LV_OBJ_FLAG_ADV_HITTEST);  /// Flags
    lv_obj_clear_flag(img_steps, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    lv_obj_align_to(img_steps, NULL, LV_ALIGN_CENTER, -25, -40);

    label_today_steps = lv_label_create(today_group);  
    lv_label_set_text(label_today_steps, "888");
    lv_obj_align_to(label_today_steps, img_steps, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
    //lv_obj_set_style_text_color(label_today_steps, lv_palette_lighten(LV_PALETTE_BLUE, 1), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_today_steps, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label_today_steps, &lv_font_montserrat_34, LV_PART_MAIN | LV_STATE_DEFAULT);
    //lv_obj_add_style(label_today_steps, &style_lbl_white, 0); 
    
    lv_obj_t *lbl_title_steps = lv_label_create(today_group);
    lv_label_set_text_fmt(lbl_title_steps, "/%d", m_steps_today_target);
    lv_obj_set_style_text_font(lbl_title_steps, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align_to(lbl_title_steps, label_today_steps, LV_ALIGN_OUT_RIGHT_MID, 40, 0);

    lv_obj_t *img_cals = lv_img_create(today_group);
    lv_img_set_src(img_cals, &img_calories_48);
    lv_obj_set_width(img_cals, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(img_cals, LV_SIZE_CONTENT); /// 1
    lv_obj_set_align(img_cals, LV_ALIGN_CENTER);
    lv_obj_add_flag(img_cals, LV_OBJ_FLAG_ADV_HITTEST);  /// Flags
    lv_obj_clear_flag(img_cals, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    lv_obj_align_to(img_cals, img_steps, LV_ALIGN_OUT_BOTTOM_MID, 4, 5);

    label_today_cals = lv_label_create(today_group);
    lv_label_set_text(label_today_cals, "100");
    lv_obj_align_to(label_today_cals, img_cals, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
    lv_obj_set_style_text_color(label_today_cals, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label_today_cals, &lv_font_montserrat_34, LV_PART_MAIN | LV_STATE_DEFAULT);
    //lv_obj_add_style(label_today_cals, &style_lbl_white, 0);

    lv_obj_t *lbl_title_cals = lv_label_create(today_group);
    lv_label_set_text_fmt(lbl_title_cals, "/%d", m_kcals_today_target);
    lv_obj_set_style_text_font(lbl_title_cals, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align_to(lbl_title_cals, label_today_cals, LV_ALIGN_OUT_RIGHT_MID, 40, 0);

    lv_obj_t *img_time;
    img_time = lv_img_create(today_group);
    lv_img_set_src(img_time, &img_timer_48);
    lv_obj_set_width(img_time, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(img_time, LV_SIZE_CONTENT); /// 1
    lv_obj_set_align(img_time, LV_ALIGN_CENTER);
    lv_obj_add_flag(img_time, LV_OBJ_FLAG_ADV_HITTEST);  /// Flags
    lv_obj_clear_flag(img_time, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    lv_obj_align_to(img_time, img_cals, LV_ALIGN_OUT_BOTTOM_MID, -3, 5);

    label_today_active_time = lv_label_create(today_group);
    lv_label_set_text(label_today_active_time, "00:00");
    lv_obj_align_to(label_today_active_time, img_time, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
    lv_obj_set_style_text_color(label_today_active_time, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label_today_active_time, &lv_font_montserrat_34, LV_PART_MAIN | LV_STATE_DEFAULT);

    /*lv_obj_t *lbl_title_time = lv_label_create(today_group);
    lv_label_set_text(lbl_title_time, "Active Time");
    lv_obj_set_style_text_font(lbl_title_time, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align_to(lbl_title_time, label_today_active_time, LV_ALIGN_BOTTOM_MID, 0, 10);
    */

    hpi_disp_set_curr_screen(SCR_TODAY);
    hpi_show_screen(scr_today, m_scroll_dir);
}

void hpi_scr_today_update_all(uint16_t steps, uint16_t kcals, uint16_t active_time_s)
{
    if (label_today_steps == NULL || label_today_cals == NULL || label_today_active_time == NULL)
        return;

    char str_steps[10];
    char str_cals[10];
    char str_time[10];

    snprintf(str_steps, 10, "%d", steps);
    snprintf(str_cals, 10, "%d", kcals);

    uint8_t hours = active_time_s / 3600;
    uint8_t minutes = (active_time_s % 3600) / 60;

    snprintf(str_time, 10, "%02d:%02d", hours, minutes);

    lv_label_set_text(label_today_steps, str_steps);
    lv_label_set_text(label_today_cals, str_cals);
    lv_label_set_text(label_today_active_time, str_time);

    lv_arc_set_value(today_arc_steps, (steps * 100) / m_steps_today_target);
    lv_arc_set_value(today_arc_cals, (kcals * 100) / m_kcals_today_target);
    lv_arc_set_value(today_arc_active_time, (active_time_s * 100) / m_active_time_today_target);
}

