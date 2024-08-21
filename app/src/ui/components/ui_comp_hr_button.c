#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <stdio.h>
#include <app_version.h>
#include <zephyr/logging/log.h>

#include "../move_ui.h"

lv_obj_t *btn_hr_disp;
lv_obj_t *ui_hr_number;

lv_obj_t *ui_hr_button_create(lv_obj_t *comp_parent)
{
    btn_hr_disp = lv_btn_create(comp_parent);
    lv_obj_set_width(btn_hr_disp, 80);
    lv_obj_set_height(btn_hr_disp, 80);
    lv_obj_set_x(btn_hr_disp, 0);
    lv_obj_set_y(btn_hr_disp, 0);
    lv_obj_set_align(btn_hr_disp, LV_ALIGN_CENTER);
    lv_obj_add_flag(btn_hr_disp, LV_OBJ_FLAG_SCROLL_ON_FOCUS); /// Flags
    lv_obj_clear_flag(btn_hr_disp, LV_OBJ_FLAG_SCROLLABLE);    /// Flags
    lv_obj_set_style_radius(btn_hr_disp, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn_hr_disp, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn_hr_disp, 2, LV_PART_MAIN | LV_STATE_DEFAULT);

    // lv_obj_set_style_bg_img_src(cui_buttonround, &ui_img_measure_png, LV_PART_MAIN | LV_STATE_DEFAULT);
    // lv_obj_set_style_bg_img_src(cui_buttonround, &heart, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_color(btn_hr_disp, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_opa(btn_hr_disp, 255, LV_PART_MAIN | LV_STATE_PRESSED | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(btn_hr_disp, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_pad(btn_hr_disp, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    //lv_obj_set_style_shadow_color(btn_hr_disp, lv_color_hex(0xEE1C18), LV_PART_MAIN | LV_STATE_PRESSED);
    //lv_obj_set_style_shadow_opa(btn_hr_disp, 255, LV_PART_MAIN | LV_STATE_PRESSED);
    //lv_obj_set_style_shadow_width(btn_hr_disp, 50, LV_PART_MAIN | LV_STATE_PRESSED);
    //lv_obj_set_style_shadow_spread(btn_hr_disp, 2, LV_PART_MAIN | LV_STATE_PRESSED);

    lv_obj_t *cui_heart;
    cui_heart = lv_img_create(btn_hr_disp);
    lv_img_set_src(cui_heart, &ui_img_heart_png);
    lv_obj_set_width(cui_heart, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(cui_heart, LV_SIZE_CONTENT); /// 1
    lv_obj_set_align(cui_heart, LV_ALIGN_TOP_MID);
    lv_obj_add_flag(cui_heart, LV_OBJ_FLAG_ADV_HITTEST);  /// Flags
    lv_obj_clear_flag(cui_heart, LV_OBJ_FLAG_SCROLLABLE); /// Flags*/

    ui_hr_number = lv_label_create(btn_hr_disp);
    lv_obj_set_width(ui_hr_number, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_hr_number, LV_SIZE_CONTENT); /// 1
    lv_obj_set_align(ui_hr_number, LV_ALIGN_BOTTOM_MID);
    lv_label_set_text(ui_hr_number, "90");
    lv_obj_set_style_text_color(ui_hr_number, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_hr_number, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_hr_number, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    /*lv_obj_t *cui_bpm;
    cui_bpm = lv_label_create(cui_pulsegroup);
    lv_obj_set_width( cui_bpm, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height( cui_bpm, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_align( cui_bpm, LV_ALIGN_CENTER );
    lv_label_set_text(cui_bpm,"bpm");
    lv_obj_set_style_text_color(cui_bpm, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT );
    lv_obj_set_style_text_opa(cui_bpm, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(cui_bpm, &ui_font_Title, LV_PART_MAIN| LV_STATE_DEFAULT);*/

    return btn_hr_disp;
}

void ui_hr_button_update(uint8_t hr_bpm)
{
    if (ui_hr_number == NULL)
        return;

    char buf[4];
    sprintf(buf, "%d", hr_bpm);
    lv_label_set_text(ui_hr_number, buf);
}
