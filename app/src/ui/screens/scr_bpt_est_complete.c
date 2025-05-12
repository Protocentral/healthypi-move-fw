#include <lvgl.h>
#include <stdio.h>
#include <zephyr/logging/log.h>

#include "hpi_common_types.h"
#include "hw_module.h"
#include "ui/move_ui.h"

LOG_MODULE_REGISTER(scr_bpt_est_complete, LOG_LEVEL_DBG);

lv_obj_t *scr_bpt_est_complete;

static lv_obj_t *label_bpt_est_done;
static lv_obj_t *label_bpt_est_done_val;
static lv_obj_t *label_bpt_est_done_val2;

static lv_obj_t *lbl_val_sys;
static lv_obj_t *lbl_val_dia;
static lv_obj_t *lbl_val_hr;
static lv_obj_t *lbl_val_spo2;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_large;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_tiny;

void draw_scr_bpt_est_complete(enum scroll_dir m_scroll_dir)
{
    scr_bpt_est_complete = lv_obj_create(NULL);
    lv_obj_add_style(scr_bpt_est_complete, &style_scr_black, 0);
    //lv_obj_set_scrollbar_mode(scr_bpt_cal_complete, LV_SCROLLBAR_MODE_ON);

    /*Create a container with COLUMN flex direction*/
    lv_obj_t *cont_col = lv_obj_create(scr_bpt_est_complete);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 25);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_col, -1, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_top(cont_col, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(cont_col, 1, LV_PART_MAIN);
    lv_obj_add_style(cont_col, &style_scr_black, 0);

    lv_obj_t *cont_row_1 = lv_obj_create(cont_col);
    lv_obj_set_size(cont_row_1, lv_pct(100), lv_pct(100));
    lv_obj_align_to(cont_row_1, NULL, LV_ALIGN_TOP_MID, 0, 25);
    lv_obj_set_flex_flow(cont_row_1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_row_1, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_row_1, -1, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_top(cont_row_1, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(cont_row_1, 1, LV_PART_MAIN);
    lv_obj_add_style(cont_row_1, &style_scr_black, 0);

    lv_obj_t *lbl_bpt_sys = lv_label_create(cont_row_1);
    lv_label_set_text(lbl_bpt_sys, "SYS");
    lv_obj_add_style(lbl_bpt_sys, &style_white_medium, 0);

    lbl_val_sys = lv_label_create(cont_row_1);
    lv_obj_add_style(lbl_val_sys, &style_white_large, 0);
    lv_label_set_text(lbl_val_sys, "--");

    lv_obj_t *cont_row_2 = lv_obj_create(cont_col);
    lv_obj_set_size(cont_row_2, lv_pct(100), lv_pct(100));
    lv_obj_align_to(cont_row_2, NULL, LV_ALIGN_TOP_MID, 0, 25);
    lv_obj_set_flex_flow(cont_row_2, LV_FLEX_FLOW_ROW); 
    lv_obj_set_flex_align(cont_row_2, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_row_2, -1, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_top(cont_row_2, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(cont_row_2, 1, LV_PART_MAIN);
    lv_obj_add_style(cont_row_2, &style_scr_black, 0);

    lv_obj_t *lbl_bpt_dia = lv_label_create(cont_row_2);
    lv_label_set_text(lbl_bpt_dia, "DIA");
    lv_obj_add_style(lbl_bpt_dia, &style_white_medium, 0);

    lbl_val_dia = lv_label_create(cont_row_2);
    lv_obj_add_style(lbl_val_dia, &style_white_large, 0);
    lv_label_set_text(lbl_val_dia, "--");

    lv_obj_t *cont_row_3 = lv_obj_create(cont_col);
    lv_obj_set_size(cont_row_3, lv_pct(100), lv_pct(100));
    lv_obj_align_to(cont_row_3, NULL, LV_ALIGN_TOP_MID, 0, 25);
    lv_obj_set_flex_flow(cont_row_3, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_row_3, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_row_3, -1, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_top(cont_row_3, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(cont_row_3, 1, LV_PART_MAIN);
    lv_obj_add_style(cont_row_3, &style_scr_black, 0);

    lv_obj_t *lbl_bpt_hr = lv_label_create(cont_row_3);
    lv_label_set_text(lbl_bpt_hr, "HR");
    lv_obj_add_style(lbl_bpt_hr, &style_white_medium, 0);

    lbl_val_hr = lv_label_create(cont_row_3);
    lv_obj_add_style(lbl_val_hr, &style_white_large, 0);
    lv_label_set_text(lbl_val_hr, "--");

    hpi_disp_set_curr_screen(SCR_SPL_BPT_EST_COMPLETE);
    hpi_show_screen(scr_bpt_est_complete, m_scroll_dir);
}