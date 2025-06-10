#include <zephyr/kernel.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <stdio.h>

#include "ui/move_ui.h"
#include "hw_module.h"

lv_obj_t *scr_today;

static lv_obj_t *today_arc_steps;
static lv_obj_t *today_arc_cals;
static lv_obj_t *today_arc_active_time;

static lv_obj_t *label_today_steps;
static lv_obj_t *label_today_cals;
static lv_obj_t *label_today_active_time;

extern lv_style_t style_white_medium;

static uint16_t m_steps_today_target = 10000;
static uint16_t m_kcals_today_target = 500;
static uint16_t m_active_time_today_target = 30;

void draw_scr_today(enum scroll_dir m_scroll_dir)
{
    scr_today = lv_obj_create(NULL);
    lv_obj_clear_flag(scr_today, LV_OBJ_FLAG_SCROLLABLE);
    draw_scr_common(scr_today);

    // Steps Arc
    today_arc_steps = lv_arc_create(scr_today);
    lv_obj_set_size(today_arc_steps, 360, 360);
    lv_obj_set_align(today_arc_steps, LV_ALIGN_CENTER);
    lv_arc_set_value(today_arc_steps, 0);
    lv_arc_set_bg_angles(today_arc_steps, 90, 315);
    lv_obj_set_style_arc_color(today_arc_steps, lv_palette_darken(LV_PALETTE_GREY, 3), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(today_arc_steps, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(today_arc_steps, true, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(today_arc_steps, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(today_arc_steps, lv_palette_darken(LV_PALETTE_RED, 4), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(today_arc_steps, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(today_arc_steps, true, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(today_arc_steps, 15, LV_PART_MAIN);
    lv_obj_set_style_arc_width(today_arc_steps, 15, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(today_arc_steps, lv_color_hex(0xFFFFFF), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(today_arc_steps, 0, LV_PART_KNOB | LV_STATE_DEFAULT);

    // Calories Arc
    today_arc_cals = lv_arc_create(scr_today);
    lv_obj_set_size(today_arc_cals, 310, 310);
    lv_obj_align(today_arc_cals, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_value(today_arc_cals, 0);
    lv_arc_set_bg_angles(today_arc_cals, 90, 315);
    lv_obj_set_style_arc_color(today_arc_cals, lv_palette_darken(LV_PALETTE_GREY, 3), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(today_arc_cals, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(today_arc_cals, true, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(today_arc_cals, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(today_arc_cals, lv_palette_darken(LV_PALETTE_YELLOW, 3), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(today_arc_cals, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(today_arc_cals, true, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(today_arc_cals, 15, LV_PART_MAIN);
    lv_obj_set_style_arc_width(today_arc_cals, 15, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(today_arc_cals, lv_color_hex(0xFFFFFF), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(today_arc_cals, 0, LV_PART_KNOB | LV_STATE_DEFAULT);

    // Active Time Arc
    today_arc_active_time = lv_arc_create(scr_today);
    lv_obj_set_size(today_arc_active_time, 260, 260);
    lv_obj_align(today_arc_active_time, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_value(today_arc_active_time, 0);
    lv_arc_set_bg_angles(today_arc_active_time, 90, 315);
    lv_obj_set_style_arc_color(today_arc_active_time, lv_palette_darken(LV_PALETTE_GREY, 3), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(today_arc_active_time, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(today_arc_active_time, true, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(today_arc_active_time, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(today_arc_active_time, lv_palette_darken(LV_PALETTE_GREEN, 1), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(today_arc_active_time, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(today_arc_active_time, true, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(today_arc_active_time, 15, LV_PART_MAIN);
    lv_obj_set_style_arc_width(today_arc_active_time, 15, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(today_arc_active_time, lv_color_hex(0xFFFFFF), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(today_arc_active_time, 0, LV_PART_KNOB | LV_STATE_DEFAULT);

    // Create a vertical container for the labels
    lv_obj_t *labels_cont = lv_obj_create(scr_today);
    lv_obj_set_size(labels_cont, 250, 300);                                   // Adjust size as needed
    lv_obj_set_style_bg_opa(labels_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT); // Transparent background
    lv_obj_set_style_border_width(labels_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(labels_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(labels_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(labels_cont, LV_ALIGN_TOP_MID, 0, 100); // Adjust position as needed
    lv_obj_clear_flag(labels_cont, LV_OBJ_FLAG_SCROLLABLE); // Make non-scrollable

    // Create a horizontal (row) container for the steps labels
    lv_obj_t *steps_row_cont = lv_obj_create(labels_cont);
    lv_obj_set_size(steps_row_cont, 200, 48);                                     // Adjust size as needed
    lv_obj_set_style_bg_opa(steps_row_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT); // Transparent background
    lv_obj_set_style_border_width(steps_row_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(steps_row_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(steps_row_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(steps_row_cont, LV_OBJ_FLAG_SCROLLABLE); // Make non-scrollable

    lv_obj_t *img_steps = lv_img_create(steps_row_cont);
    lv_img_set_src(img_steps, &img_steps_48);

    // Steps label
    label_today_steps = lv_label_create(steps_row_cont);
    lv_label_set_text(label_today_steps, "0");
    lv_obj_set_style_text_color(label_today_steps, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label_today_steps, &lv_font_montserrat_34, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *lbl_title_steps = lv_label_create(steps_row_cont);
    lv_label_set_text_fmt(lbl_title_steps, "/%d", m_steps_today_target);
    lv_obj_set_style_text_font(lbl_title_steps, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Create a horizontal (row) container for the calories labels
    lv_obj_t *cals_row_cont = lv_obj_create(labels_cont);
    lv_obj_set_size(cals_row_cont, 200, 48); // Adjust size as needed
    
    lv_obj_set_style_bg_opa(cals_row_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT); // Transparent background
    lv_obj_set_style_border_width(cals_row_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(cals_row_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cals_row_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(cals_row_cont, LV_OBJ_FLAG_SCROLLABLE); // Make non-scrollable

    lv_obj_t *img_cals = lv_img_create(cals_row_cont);
    lv_img_set_src(img_cals, &img_calories_48);

    // Calories label
    label_today_cals = lv_label_create(cals_row_cont);
    lv_label_set_text(label_today_cals, "0");
    lv_obj_set_style_text_color(label_today_cals, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label_today_cals, &lv_font_montserrat_34, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *lbl_title_cals = lv_label_create(cals_row_cont);
    lv_label_set_text_fmt(lbl_title_cals, "/%d", m_kcals_today_target);
    lv_obj_set_style_text_font(lbl_title_cals, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Create a horizontal (row) container for the active time labels
    lv_obj_t *time_row_cont = lv_obj_create(labels_cont);
    lv_obj_set_size(time_row_cont, 200, 48); // Adjust size as needed
    lv_obj_set_style_bg_opa(time_row_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT); // Transparent background
    lv_obj_set_style_border_width(time_row_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(time_row_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time_row_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(time_row_cont, LV_OBJ_FLAG_SCROLLABLE); // Make non-scrollable

    lv_obj_t *img_time = lv_img_create(time_row_cont);
    lv_img_set_src(img_time, &img_timer_48);

    // Active time label
    label_today_active_time = lv_label_create(time_row_cont);
    lv_label_set_text(label_today_active_time, "00:00");
    lv_obj_set_style_text_color(label_today_active_time, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label_today_active_time, &lv_font_montserrat_34, LV_PART_MAIN | LV_STATE_DEFAULT);
    

    hpi_disp_set_curr_screen(SCR_TODAY);
    hpi_show_screen(scr_today, m_scroll_dir);
}

void hpi_scr_today_update_all(uint16_t steps, uint16_t kcals, uint16_t active_time_s)
{
    if (label_today_steps == NULL || label_today_cals == NULL || label_today_active_time == NULL)
        return;

    lv_label_set_text_fmt(label_today_steps, "%d", steps);
    lv_label_set_text_fmt(label_today_cals, "%d", kcals);

    uint8_t hours = active_time_s / 3600;
    uint8_t minutes = (active_time_s % 3600) / 60;
    lv_label_set_text_fmt(label_today_active_time, "%02d:%02d", hours, minutes);

    lv_arc_set_value(today_arc_steps, (steps * 100) / m_steps_today_target);
    lv_arc_set_value(today_arc_cals, (kcals * 100) / m_kcals_today_target);
    lv_arc_set_value(today_arc_active_time, (active_time_s * 100) / m_active_time_today_target);
}