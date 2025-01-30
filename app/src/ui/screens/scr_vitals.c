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

#include "hpi_common_types.h"
#include "ui/move_ui.h"

// GUI Labels
static lv_obj_t *label_vitals_hr;
static lv_obj_t *label_spo2;

lv_obj_t *scr_vitals_home;
static lv_obj_t *label_vitals_temp;
static lv_obj_t *label_vitals_bp_val;

extern lv_style_t style_lbl_white;
extern lv_style_t style_lbl_red;
extern lv_style_t style_lbl_white_small;
extern lv_style_t style_lbl_red_small;

void draw_scr_vitals_home(enum scroll_dir m_scroll_dir)
{
    scr_vitals_home = lv_obj_create(NULL);

    draw_header_minimal(scr_vitals_home,10);

    // HR Number label
    label_vitals_hr = lv_label_create(scr_vitals_home);
    lv_label_set_text(label_vitals_hr, "120");
    lv_obj_align_to(label_vitals_hr, NULL, LV_ALIGN_CENTER, -60, -30);
    lv_obj_add_style(label_vitals_hr, &style_lbl_white, 0);

    // HR Sub bpm label
    lv_obj_t *label_hr_sub = lv_label_create(scr_vitals_home);
    lv_label_set_text(label_hr_sub, " bpm");
    lv_obj_align_to(label_hr_sub, label_vitals_hr, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);

    // HR caption label
    lv_obj_t *label_hr_cap = lv_label_create(scr_vitals_home);
    lv_label_set_text(label_hr_cap, "HR");
    lv_obj_align_to(label_hr_cap, label_vitals_hr, LV_ALIGN_OUT_TOP_MID, -5, -5);
    lv_obj_add_style(label_hr_cap, &style_lbl_red, 0);

    /*LV_IMG_DECLARE(heart);
    lv_obj_t *img1 = lv_img_create(scr_vitals_home);
    lv_img_set_src(img1, &heart);
    lv_obj_set_size(img1, 35, 33);
    lv_obj_align_to(img1, label_vitals_hr, LV_ALIGN_OUT_LEFT_MID, -5, 0);
    */

    // SpO2 Number label
    label_spo2 = lv_label_create(scr_vitals_home);
    lv_label_set_text(label_spo2, "98");
    lv_obj_align_to(label_spo2, NULL, LV_ALIGN_CENTER, 40, -30);
    lv_obj_add_style(label_spo2, &style_lbl_white, 0);
    // lv_obj_add_style(label_spo2, &style_spo2, 0);

    // SpO2 Sub % label
    lv_obj_t *label_spo2_sub = lv_label_create(scr_vitals_home);
    lv_label_set_text(label_spo2_sub, " %");
    lv_obj_align_to(label_spo2_sub, label_spo2, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);

    // SpO2 caption label
    lv_obj_t *label_spo2_cap = lv_label_create(scr_vitals_home);
    lv_label_set_text(label_spo2_cap, "SpO2");
    lv_obj_align_to(label_spo2_cap, label_spo2, LV_ALIGN_OUT_TOP_MID, -5, -5);
    lv_obj_add_style(label_spo2_cap, &style_lbl_red, 0);

    /*LV_IMG_DECLARE(o2);
    lv_obj_t *img2 = lv_img_create(scr_vitals_home);
    lv_img_set_src(img2, &o2);
    lv_obj_set_size(img2, 22, 35);
    lv_obj_align_to(img2, label_spo2, LV_ALIGN_OUT_LEFT_MID, -5, 0);
    */

    // BP Systolic Number label
    label_vitals_bp_val = lv_label_create(scr_vitals_home);
    lv_label_set_text(label_vitals_bp_val, "120 / 80");
    lv_obj_align_to(label_vitals_bp_val, NULL, LV_ALIGN_CENTER, -30, 45);
    lv_obj_add_style(label_vitals_bp_val, &style_lbl_white, 0);

    // BP Systolic Sub mmHg label
    lv_obj_t *label_bp_sys_sub = lv_label_create(scr_vitals_home);
    lv_label_set_text(label_bp_sys_sub, " mmHg");
    lv_obj_align_to(label_bp_sys_sub, label_vitals_bp_val, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);

    // BP Systolic caption label
    lv_obj_t *label_bp_sys_cap = lv_label_create(scr_vitals_home);
    lv_label_set_text(label_bp_sys_cap, "BP(Sys/Dia)");
    lv_obj_align_to(label_bp_sys_cap, label_vitals_bp_val, LV_ALIGN_OUT_TOP_MID, -5, -5);
    lv_obj_add_style(label_bp_sys_cap, &style_lbl_red, 0);

    // Temp Number label
    label_vitals_temp = lv_label_create(scr_vitals_home);
    lv_label_set_text(label_vitals_temp, "36.7");
    lv_obj_align_to(label_vitals_temp, NULL, LV_ALIGN_CENTER, 10, 95);
    lv_obj_add_style(label_vitals_temp, &style_lbl_white_small, 0);

    // Temp Sub deg C label
    lv_obj_t *label_temp_sub = lv_label_create(scr_vitals_home);
    lv_label_set_text(label_temp_sub, " °C");
    lv_obj_align_to(label_temp_sub, label_vitals_temp, LV_ALIGN_OUT_RIGHT_MID, 5, 0);

    // Temp caption label
    lv_obj_t *label_temp_cap = lv_label_create(scr_vitals_home);
    lv_label_set_text(label_temp_cap, "Temp");
    lv_obj_align_to(label_temp_cap, label_vitals_temp, LV_ALIGN_OUT_LEFT_MID, -10, 0);
    lv_obj_add_style(label_temp_cap, &style_lbl_red_small, 0);

    //curr_screen = SCR_VITALS;

    lv_obj_add_event_cb(scr_vitals_home, disp_screen_event, LV_EVENT_GESTURE, NULL);
    hpi_show_screen(scr_vitals_home, m_scroll_dir);
}