#include <lvgl.h>
#include <stdio.h>
#include <zephyr/logging/log.h>

#include "hpi_common_types.h"
#include "hw_module.h"
#include "ui/move_ui.h"

LOG_MODULE_REGISTER(scr_bpt_est_complete, LOG_LEVEL_DBG);

lv_obj_t *scr_bpt_est_complete;

static lv_obj_t *btn_close;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_large;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_tiny;

static void scr_btn_close_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        // k_sem_give(&sem_bpt_check_sensor);
        hpi_load_screen(SCR_BPT, SCROLL_UP);
    }
}

void draw_scr_bpt_est_complete(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_bpt_est_complete = lv_obj_create(NULL);
    lv_obj_add_style(scr_bpt_est_complete, &style_scr_black, 0);
    lv_obj_clear_flag(scr_bpt_est_complete, LV_OBJ_FLAG_SCROLLABLE);

    /*lv_obj_t *obj_circle = lv_obj_create(scr_bpt_est_complete);
    lv_obj_set_size(obj_circle, 390, 390);
    lv_obj_set_pos(obj_circle, 195, 195);
    lv_style_set_border_width(obj_circle, 10); // Or your desired border width
    lv_style_set_border_color(obj_circle, lv_palette_main(LV_PALETTE_GREEN));
    */

    /*lv_obj_t * arc = lv_arc_create(scr_bpt_est_complete);
    lv_obj_set_size(arc, 370, 370);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    //lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_value(arc, 360);
    lv_obj_center(arc);*/
    

    lv_obj_t *cont_col = lv_obj_create(scr_bpt_est_complete);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(cont_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(cont_col, &style_scr_black, 0);

    lv_obj_t *lbl_info_scroll = lv_label_create(cont_col);
    lv_label_set_text(lbl_info_scroll, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_color(lbl_info_scroll, lv_palette_darken(LV_PALETTE_RED, 2), LV_PART_MAIN);

    lv_obj_t *cont_sys = lv_obj_create(cont_col);
    lv_obj_set_size(cont_sys, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_sys, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(cont_sys, &style_scr_black, 0);
    lv_obj_set_flex_align(cont_sys, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl_bpt_sys = lv_label_create(cont_sys);
    lv_label_set_text(lbl_bpt_sys, "SYS");
    lv_obj_add_style(lbl_bpt_sys, &style_red_medium, 0);

    lv_obj_t *lbl_val_sys = lv_label_create(cont_sys);
    lv_obj_add_style(lbl_val_sys, &style_white_large, 0);
    lv_label_set_text_fmt(lbl_val_sys, "%03d", arg1);

    lv_obj_t *cont_dia = lv_obj_create(cont_col);
    lv_obj_set_size(cont_dia, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_dia, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(cont_dia, &style_scr_black, 0);
    lv_obj_set_flex_align(cont_dia, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl_bpt_dia = lv_label_create(cont_dia);
    lv_label_set_text(lbl_bpt_dia, "DIA");
    lv_obj_add_style(lbl_bpt_dia, &style_red_medium, 0);

    lv_obj_t *lbl_val_dia = lv_label_create(cont_dia);
    lv_obj_add_style(lbl_val_dia, &style_white_large, 0);
    lv_label_set_text_fmt(lbl_val_dia, "%03d", arg2);

    lv_obj_t *cont_hr = lv_obj_create(cont_col);
    lv_obj_set_size(cont_hr, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_hr, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(cont_hr, &style_scr_black, 0);
    lv_obj_set_flex_align(cont_hr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl_bpt_hr = lv_label_create(cont_hr);
    lv_label_set_text(lbl_bpt_hr, "HR");
    lv_obj_add_style(lbl_bpt_hr, &style_red_medium, 0);

    lv_obj_t *lbl_val_hr = lv_label_create(cont_hr);
    lv_obj_add_style(lbl_val_hr, &style_white_medium, 0);
    lv_label_set_text_fmt(lbl_val_hr, "%d", arg3);

    /*lv_obj_t *cont_spo2 = lv_obj_create(cont_col);
    lv_obj_set_size(cont_spo2, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_spo2, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(cont_spo2, &style_scr_black, 0);
    lv_obj_set_flex_align(cont_spo2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl_bpt_spo2 = lv_label_create(cont_spo2);
    lv_label_set_text(lbl_bpt_spo2, "SpO2");
    lv_obj_add_style(lbl_bpt_spo2, &style_red_medium, 0);

    lv_obj_t *lbl_val_spo2 = lv_label_create(cont_spo2);
    lv_obj_add_style(lbl_val_spo2, &style_white_medium, 0);
    lv_label_set_text_fmt(lbl_val_spo2, "%d", arg4);*/

    hpi_disp_set_curr_screen(SCR_SPL_BPT_EST_COMPLETE);
    hpi_show_screen(scr_bpt_est_complete, m_scroll_dir);
}