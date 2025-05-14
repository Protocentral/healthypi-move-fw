#include <lvgl.h>
#include <stdio.h>
#include <zephyr/logging/log.h>

#include "hpi_common_types.h"
#include "hw_module.h"
#include "ui/move_ui.h"

LOG_MODULE_REGISTER(scr_bpt_scr2, LOG_LEVEL_DBG);

lv_obj_t *scr_bpt_scr2;
static lv_obj_t *btn_spo2_proceed;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_large;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_tiny;

extern struct k_sem sem_bpt_est_start;

static void scr_bpt_btn_proceed_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        k_sem_give(&sem_bpt_est_start);
        hpi_load_scr_spl(SCR_SPL_BPT_SCR3, SCROLL_UP, (uint8_t)SCR_BPT, 0, 0, 0);
    }
}

void draw_scr_bpt_scr2(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_bpt_scr2 = lv_obj_create(NULL);
    lv_obj_add_style(scr_bpt_scr2, &style_scr_black, 0);
    lv_obj_clear_flag(scr_bpt_scr2, LV_OBJ_FLAG_SCROLLABLE);
    //  lv_obj_set_flag(scr_spo2, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    //  draw_scr_common(scr_spo2_scr2);

    // lv_obj_set_scrollbar_mode(scr_bpt_scr2, LV_SCROLLBAR_MODE_ON);

    /*Create a container with COLUMN flex direction*/
    lv_obj_t *cont_col = lv_obj_create(scr_bpt_scr2);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(cont_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_col, -1, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_top(cont_col, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(cont_col, 1, LV_PART_MAIN);
    lv_obj_add_style(cont_col, &style_scr_black, 0);

    lv_obj_t *lbl_info_scroll = lv_label_create(cont_col);
    lv_label_set_text(lbl_info_scroll, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_color(lbl_info_scroll, lv_palette_darken(LV_PALETTE_RED, 2), LV_PART_MAIN);

    lv_obj_t *img_bpt = lv_img_create(cont_col);
    lv_img_set_src(img_bpt, &img_bpt_finger_90);

    lv_obj_t *label_info = lv_label_create(cont_col);
    lv_label_set_long_mode(label_info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_info, 300);
    lv_label_set_text(label_info, "Wear finger sensor now");
    lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, 0);

    btn_spo2_proceed = lv_btn_create(cont_col);
    lv_obj_add_event_cb(btn_spo2_proceed, scr_bpt_btn_proceed_handler, LV_EVENT_ALL, NULL);
    lv_obj_set_height(btn_spo2_proceed, 85);

    lv_obj_t *label_btn = lv_label_create(btn_spo2_proceed);
    lv_label_set_text(label_btn, LV_SYMBOL_PLAY " Proceed");
    lv_obj_center(label_btn);

    hpi_disp_set_curr_screen(SCR_SPL_BPT_SCR2);
    hpi_show_screen(scr_bpt_scr2, m_scroll_dir);
}