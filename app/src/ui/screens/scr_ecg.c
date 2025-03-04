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
#include "hw_module.h"

lv_obj_t *scr_ecg;

static lv_obj_t *btn_ecg_measure;

// Externs
extern lv_style_t style_red_medium;

extern lv_style_t style_scr_black;

extern struct k_sem sem_ecg_start;

static void scr_ecg_start_btn_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        hpi_move_load_scr_spl(SCR_SPL_PLOT_ECG, SCROLL_UP, (uint8_t)SCR_ECG);
        k_msleep(500);
        k_sem_give(&sem_ecg_start);
    }
}

void draw_scr_ecg(enum scroll_dir m_scroll_dir)
{
    scr_ecg = lv_obj_create(NULL);
    draw_header_minimal(scr_ecg, 10);
    lv_obj_clear_flag(scr_ecg, LV_OBJ_FLAG_SCROLLABLE);
    // draw_bg(scr_ecg);

    /*Create a container with COLUMN flex direction*/
    /*lv_obj_t *cont_col = lv_obj_create(scr_ecg);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    // lv_obj_set_width(cont_col, lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_col, -1, LV_PART_SCROLLBAR);
    lv_obj_add_style(cont_col, &style_scr_black, 0);
    lv_obj_set_scroll_dir(cont_col, LV_DIR_VER);*/

    lv_obj_t *label_signal = lv_label_create(scr_ecg);
    lv_label_set_text(label_signal, "ECG");
    lv_obj_align(label_signal, LV_ALIGN_TOP_MID, 0, 25);

    lv_obj_t *label_install = lv_label_create(scr_ecg);
    lv_label_set_recolor(label_install, true);     
    lv_label_set_text(label_install, "To install this feature\nplease update your device firmware.\n\nVisit #42A5F5 move.protocentral.com # \n for details.");
    lv_obj_set_style_text_font(label_install, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_width(label_install, 380);
    lv_obj_set_style_text_align(label_install, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(label_install, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *img1 = lv_img_create(scr_ecg);
    lv_img_set_src(img1, &ecg_70);
    lv_obj_align_to(img1, label_install, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    /*btn_ecg_measure = lv_btn_create(cont_col);
    lv_obj_add_event_cb(btn_ecg_measure, scr_ecg_start_btn_event_handler, LV_EVENT_ALL, NULL);
    //lv_obj_align(btn_hr_settings, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_height(btn_ecg_measure, 80);

    lv_obj_t *label_btn_bpt_measure = lv_label_create(btn_ecg_measure);
    lv_label_set_text(label_btn_bpt_measure, LV_SYMBOL_PLAY " Measure");
    lv_obj_center(label_btn_bpt_measure);*/

    hpi_disp_set_curr_screen(SCR_ECG);
    hpi_show_screen(scr_ecg, m_scroll_dir);
}