#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <stdio.h>
#include <app_version.h>
#include <zephyr/logging/log.h>

#include "../move_ui.h"

lv_obj_t *cui_step_label;

static void scr_home_steps_btn_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        hpi_load_screen(SCR_TODAY, SCROLL_LEFT);
    }
}

lv_obj_t *ui_steps_button_create(lv_obj_t *comp_parent)
{
    lv_obj_t *cui_buttonround;
    cui_buttonround = lv_btn_create(comp_parent);
    lv_obj_set_width(cui_buttonround, 110);
    lv_obj_set_height(cui_buttonround, 110);
    lv_obj_set_x(cui_buttonround, 0);
    lv_obj_set_y(cui_buttonround, 0);
    lv_obj_set_align(cui_buttonround, LV_ALIGN_CENTER);
    lv_obj_add_flag(cui_buttonround, LV_OBJ_FLAG_SCROLL_ON_FOCUS); /// Flags
    lv_obj_clear_flag(cui_buttonround, LV_OBJ_FLAG_SCROLLABLE);    /// Flags
    lv_obj_set_style_radius(cui_buttonround, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(cui_buttonround, lv_palette_main(LV_PALETTE_BLUE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(cui_buttonround, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    // lv_obj_set_style_bg_img_src(cui_buttonround, &ui_img_measure_png, LV_PART_MAIN | LV_STATE_DEFAULT);
    // lv_obj_set_style_bg_img_src(cui_buttonround, &heart, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_color(cui_buttonround, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_opa(cui_buttonround, 255, LV_PART_MAIN | LV_STATE_PRESSED | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(cui_buttonround, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_pad(cui_buttonround, 2, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(cui_buttonround, scr_home_steps_btn_handler, LV_EVENT_ALL, NULL);
    //lv_obj_set_style_shadow_color(cui_buttonround, lv_color_hex(0xEE1C18), LV_PART_MAIN | LV_STATE_PRESSED);
    //lv_obj_set_style_shadow_opa(cui_buttonround, 255, LV_PART_MAIN | LV_STATE_PRESSED);
    //lv_obj_set_style_shadow_width(cui_buttonround, 50, LV_PART_MAIN | LV_STATE_PRESSED);
    //lv_obj_set_style_shadow_spread(cui_buttonround, 2, LV_PART_MAIN | LV_STATE_PRESSED);

    lv_obj_t *cui_step;
    cui_step = lv_img_create(cui_buttonround);
    lv_img_set_src(cui_step, &img_steps_48);
    lv_obj_set_width(cui_step, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(cui_step, LV_SIZE_CONTENT); /// 1
    lv_obj_set_align(cui_step, LV_ALIGN_TOP_MID);
    lv_obj_add_flag(cui_step, LV_OBJ_FLAG_ADV_HITTEST);  /// Flags
    lv_obj_clear_flag(cui_step, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    cui_step_label = lv_label_create(cui_buttonround);
    lv_obj_set_width(cui_step_label, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(cui_step_label, LV_SIZE_CONTENT); /// 1
    // lv_obj_set_x(cui_step_label, 2);
    // lv_obj_set_y(cui_step_label, -4);
    lv_obj_set_align(cui_step_label, LV_ALIGN_BOTTOM_MID);
    lv_label_set_text(cui_step_label, "--");
    lv_obj_set_style_text_color(cui_step_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(cui_step_label, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(cui_step_label, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    // lv_obj_t **children = lv_mem_alloc(sizeof(lv_obj_t *) * _UI_COMP_STEPGROUP_NUM);

    // children[UI_COMP_STEPGROUP_STEPGROUP] = cui_stepgroup;
    // children[UI_COMP_STEPGROUP_STEP] = cui_step;
    // children[UI_COMP_STEPGROUP_STEP_LABEL] = cui_step_label;

    // lv_obj_add_event_cb(cui_stepgroup, get_component_child_event_cb, LV_EVENT_GET_COMP_CHILD, children);
    // lv_obj_add_event_cb(cui_stepgroup, del_component_child_event_cb, LV_EVENT_DELETE, children);
    // ui_comp_stepgroup_create_hook(cui_stepgroup);
    return cui_buttonround;
}

void ui_steps_button_update(uint16_t steps)
{
    if (cui_step_label == NULL)
        return;

    char steps_str[6];
    snprintf(steps_str, 6, "%d", steps);
    lv_label_set_text(cui_step_label, steps_str);
}