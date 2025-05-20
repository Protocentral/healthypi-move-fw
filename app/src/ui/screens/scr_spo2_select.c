#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <lvgl.h>
#include <stdio.h>

#include "ui/move_ui.h"

LOG_MODULE_REGISTER(scr_spo2_select, LOG_LEVEL_DBG);

lv_obj_t *scr_spo2_select;
lv_obj_t *btn_spo2_select_fi;
lv_obj_t *btn_spo2_select_wr;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_large;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_tiny;

extern lv_style_t style_bg_blue;
extern lv_style_t style_bg_red;

static void scr_spo2_sel_fi_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
    }
}

static void scr_spo2_sel_wr_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        hpi_load_scr_spl(SCR_SPL_SPO2_SCR2, SCROLL_UP, (uint8_t)SCR_SPO2, 0, 0, 0);
    }
}

void draw_scr_spo2_select(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_spo2_select = lv_obj_create(NULL);
    lv_obj_clear_flag(scr_spo2_select, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    lv_obj_t *cont_col = lv_obj_create(scr_spo2_select);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_col, -1, LV_PART_SCROLLBAR);
    lv_obj_add_style(cont_col, &style_scr_black, 0);
    //lv_obj_add_style(cont_col, &style_bg_red, 0);

    lv_obj_t *label_signal = lv_label_create(cont_col);
    lv_label_set_text(label_signal, "Select PPG\n Sensor");
    lv_obj_add_style(label_signal, &style_white_medium, 0);
    lv_obj_set_style_text_align(label_signal, LV_TEXT_ALIGN_CENTER, 0);

    // Button for PPG WR
    btn_spo2_select_wr = lv_btn_create(cont_col);
    lv_obj_t *label_ppg_wr = lv_label_create(btn_spo2_select_wr);
    lv_label_set_text(label_ppg_wr, "Wrist");
    lv_obj_set_height(btn_spo2_select_wr, 100);    
    lv_obj_center(label_ppg_wr);
    lv_obj_add_style(btn_spo2_select_wr, &style_white_medium, 0);
    lv_obj_add_event_cb(btn_spo2_select_wr, scr_spo2_sel_wr_handler, LV_EVENT_CLICKED, NULL);

    // Button for PPG FI
    btn_spo2_select_fi = lv_btn_create(cont_col);
    lv_obj_t *label_ppg_fi = lv_label_create(btn_spo2_select_fi);
    lv_label_set_text(label_ppg_fi, "Finger");
    lv_obj_set_height(btn_spo2_select_fi, 100);
    lv_obj_add_style(btn_spo2_select_fi, &style_white_medium, 0);

    lv_obj_center(label_ppg_fi);
    lv_obj_add_event_cb(btn_spo2_select_fi, scr_spo2_sel_fi_handler, LV_EVENT_CLICKED, NULL);

    hpi_disp_set_curr_screen(SCR_SPL_SPO2_SELECT);
    hpi_show_screen(scr_spo2_select, m_scroll_dir);    
}
