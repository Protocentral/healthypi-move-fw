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
extern lv_style_t style_lbl_orange;
extern lv_style_t style_lbl_white;
extern lv_style_t style_red_medium;
extern lv_style_t style_lbl_white_small;
extern lv_style_t style_white_medium;

extern lv_style_t style_scr_black;

extern struct k_sem sem_ecg_complete_ok;

static void scr_ecg_complete_btn_ok_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        k_sem_give(&sem_ecg_complete_ok);
        //hpi_move_load_scr_spl(SCR_SPL_PLOT_ECG, SCROLL_UP, (uint8_t)SCR_ECG);
        hpi_move_load_screen(SCR_ECG, SCROLL_UP);
    }
}

void draw_scr_ecg_complete(enum scroll_dir m_scroll_dir)
{
    scr_ecg_complete = lv_obj_create(NULL);
    draw_header_minimal(scr_ecg_complete, 10);
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

    lv_obj_t *label_signal = lv_label_create(cont_col);
    lv_label_set_text(label_signal, LV_SYMBOL_OK " ECG Complete");
    lv_obj_add_style(label_signal, &style_white_medium, 0);

    lv_obj_t *lbl_session_stats = lv_label_create(cont_col);
    lv_label_set_text_fmt(lbl_session_stats, "ECG recording saved\nStarted at %d ", 1234);

    lv_obj_t *btn_ok = lv_btn_create(cont_col);
    lv_obj_add_event_cb(btn_ok, scr_ecg_complete_btn_ok_event_handler, LV_EVENT_ALL, NULL);
    
    lv_obj_t *label_btn_ok = lv_label_create(btn_ok);
    lv_label_set_text(label_btn_ok, LV_SYMBOL_CLOSE "Close");
    lv_obj_center(label_btn_ok);

    hpi_disp_set_curr_screen(SCR_SPL_ECG_COMPLETE);
    hpi_show_screen_spl(scr_ecg_complete, m_scroll_dir, SCR_ECG);
}