#include <lvgl.h>
#include <stdio.h>
#include <zephyr/logging/log.h>

#include "hpi_common_types.h"
#include "hw_module.h"
#include "ui/move_ui.h"

LOG_MODULE_REGISTER(scr_bpt_cal_failed, LOG_LEVEL_DBG);

lv_obj_t *scr_bpt_cal_failed;

static lv_obj_t *btn_bpt_measure;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_large;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_tiny;


static void scr_bpt_btn_measure_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        //k_sem_give(&sem_bpt_check_sensor); 
        hpi_load_screen(SCR_BPT, SCROLL_UP);
    }
}

void draw_scr_bpt_cal_failed(enum scroll_dir m_scroll_dir)
{
    scr_bpt_cal_failed = lv_obj_create(NULL);
    lv_obj_add_style(scr_bpt_cal_failed, &style_scr_black, 0);
    //lv_obj_set_scrollbar_mode(scr_bpt_cal_complete, LV_SCROLLBAR_MODE_ON);

    /*Create a container with COLUMN flex direction*/
    lv_obj_t *cont_col = lv_obj_create(scr_bpt_cal_failed);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 25);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_col, -1, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_top(cont_col, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(cont_col, 1, LV_PART_MAIN);
    lv_obj_add_style(cont_col, &style_scr_black, 0);

    lv_obj_t *label_info = lv_label_create(cont_col);
    lv_label_set_long_mode(label_info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_info, 300);
    lv_label_set_text(label_info, "Calibration Failed");
    lv_obj_add_style(label_info, &style_white_medium, 0);
    lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *label_info2 = lv_label_create(cont_col);
    lv_label_set_long_mode(label_info2, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_info2, 300);
    lv_label_set_text(label_info2, "Your calibration was not successful.\n Please try again.");
    lv_obj_set_style_text_align(label_info2, LV_TEXT_ALIGN_CENTER, 0);

    btn_bpt_measure = lv_btn_create(cont_col);
    lv_obj_add_event_cb(btn_bpt_measure, scr_bpt_btn_measure_handler, LV_EVENT_ALL, NULL);
    lv_obj_set_height(btn_bpt_measure, 85);

    lv_obj_t *label_btn = lv_label_create(btn_bpt_measure);
    lv_label_set_text(label_btn, LV_SYMBOL_CLOSE " Close");
    lv_obj_center(label_btn);

    hpi_disp_set_curr_screen(SCR_SPL_BPT_CAL_COMPLETE);
    hpi_show_screen(scr_bpt_cal_failed, m_scroll_dir);
}