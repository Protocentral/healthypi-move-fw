#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <stdio.h>
#include <app_version.h>
#include <zephyr/logging/log.h>

#include "../move_ui.h"

lv_obj_t *btn_spo2_disp;
lv_obj_t *ui_spo2_number;

lv_obj_t *ui_spo2_button_create(lv_obj_t *comp_parent)
{
    btn_spo2_disp = lv_btn_create(comp_parent);
    lv_obj_set_width(btn_spo2_disp, 65);
    lv_obj_set_height(btn_spo2_disp, 65);
    lv_obj_set_x(btn_spo2_disp, 0);
    lv_obj_set_y(btn_spo2_disp, 0);
    lv_obj_set_align(btn_spo2_disp, LV_ALIGN_CENTER);
    lv_obj_add_flag(btn_spo2_disp, LV_OBJ_FLAG_SCROLL_ON_FOCUS); /// Flags
    lv_obj_clear_flag(btn_spo2_disp, LV_OBJ_FLAG_SCROLLABLE);    /// Flags
    lv_obj_set_style_radius(btn_spo2_disp, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn_spo2_disp, lv_palette_main(LV_PALETTE_DEEP_ORANGE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn_spo2_disp, 64, LV_PART_MAIN | LV_STATE_DEFAULT);
    // lv_obj_set_style_bg_img_src(cui_buttonround, &ui_img_measure_png, LV_PART_MAIN | LV_STATE_DEFAULT);
    // lv_obj_set_style_bg_img_src(cui_buttonround, &heart, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_color(btn_spo2_disp, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_opa(btn_spo2_disp, 255, LV_PART_MAIN | LV_STATE_PRESSED | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(btn_spo2_disp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_pad(btn_spo2_disp, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(btn_spo2_disp, lv_color_hex(0xEE1C18), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_opa(btn_spo2_disp, 255, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(btn_spo2_disp, 50, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_spread(btn_spo2_disp, 2, LV_PART_MAIN | LV_STATE_PRESSED);

    LV_IMG_DECLARE( o2);

    lv_obj_t *cui_o2;
    cui_o2 = lv_img_create(btn_spo2_disp);
    lv_img_set_src(cui_o2, &o2);
    lv_obj_set_width(cui_o2, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(cui_o2, LV_SIZE_CONTENT); /// 1
    lv_obj_set_align(cui_o2, LV_ALIGN_TOP_MID);
    lv_obj_add_flag(cui_o2, LV_OBJ_FLAG_ADV_HITTEST);  /// Flags
    lv_obj_clear_flag(cui_o2, LV_OBJ_FLAG_SCROLLABLE); /// Flags*/

    ui_spo2_number = lv_label_create(btn_spo2_disp);
    lv_obj_set_width(ui_spo2_number, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_spo2_number, LV_SIZE_CONTENT); /// 1
    lv_obj_set_align(ui_spo2_number, LV_ALIGN_BOTTOM_MID);
    lv_label_set_text(ui_spo2_number, "--");
    lv_obj_set_style_text_color(ui_spo2_number, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_spo2_number, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_spo2_number, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    /*lv_obj_t *cui_bpm;
    cui_bpm = lv_label_create(cui_pulsegroup);
    lv_obj_set_width( cui_bpm, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height( cui_bpm, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_align( cui_bpm, LV_ALIGN_CENTER );
    lv_label_set_text(cui_bpm,"bpm");
    lv_obj_set_style_text_color(cui_bpm, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT );
    lv_obj_set_style_text_opa(cui_bpm, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(cui_bpm, &ui_font_Title, LV_PART_MAIN| LV_STATE_DEFAULT);*/

    return btn_spo2_disp;
}

void ui_spo2_button_update(uint8_t spo2)
{
    if (ui_spo2_number == NULL)
        return;

    char buf[4];
    sprintf(buf, "%d", spo2);
    lv_label_set_text(ui_spo2_number, buf);
}