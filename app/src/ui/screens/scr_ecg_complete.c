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

lv_obj_t *scr_ecg_complete;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_medium;

extern lv_style_t style_scr_black;

void draw_scr_ecg_complete(enum scroll_dir m_scroll_dir)
{
    scr_ecg_complete = lv_obj_create(NULL);
    draw_scr_common(scr_ecg_complete);
    // draw_bg(scr_ecg_complete);

    /*Create a container with COLUMN flex direction*/
    lv_obj_t *cont_col = lv_obj_create(scr_ecg_complete);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    // lv_obj_set_width(cont_col, lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_col, -1, LV_PART_SCROLLBAR);
    lv_obj_add_style(cont_col, &style_scr_black, 0);

    lv_obj_t *img1 = lv_img_create(cont_col);
    lv_img_set_src(img1, &img_complete_100);

    lv_obj_t *label_signal = lv_label_create(cont_col);
    lv_label_set_text(label_signal, "Record Complete");
    lv_obj_add_style(label_signal, &style_white_medium, 0);

    struct tm record_time = hw_get_sys_time();

    lv_obj_t *lbl_session_stats = lv_label_create(cont_col);
    lv_label_set_text_fmt(lbl_session_stats, "at %2d:%2d on %4d-%2d-%d", record_time.tm_hour, record_time.tm_min, record_time.tm_year + 1900, record_time.tm_mon + 1, record_time.tm_mday);
    lv_obj_set_style_text_align(lbl_session_stats, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *lbl_log_info = lv_label_create(cont_col);
    lv_label_set_text_fmt(lbl_log_info, "You can now download the recording from the app");
    lv_obj_set_style_text_align(lbl_log_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lbl_log_info, 300);

    /*lv_obj_t *btn_close = lv_btn_create(cont_col);
    lv_obj_add_event_cb(btn_close, scr_ecg_complete_btn_close_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_set_height(btn_close, 80);

    lv_obj_t *label_btn_ok = lv_label_create(btn_close);
    lv_label_set_text(label_btn_ok, LV_SYMBOL_CLOSE " Close");
    lv_obj_center(label_btn_ok);*/

    lv_obj_t *lbl_info_scroll = lv_label_create(cont_col);
    lv_label_set_text(lbl_info_scroll, LV_SYMBOL_DOWN);

    hpi_disp_set_curr_screen(SCR_SPL_ECG_COMPLETE);
    hpi_show_screen_spl(scr_ecg_complete, m_scroll_dir, SCR_ECG);
}