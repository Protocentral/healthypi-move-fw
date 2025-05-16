#include <lvgl.h>
#include <stdio.h>
#include <zephyr/logging/log.h>

#include "hpi_common_types.h"
#include "hw_module.h"
#include "ui/move_ui.h"

LOG_MODULE_REGISTER(scr_bpt_cal_progress, LOG_LEVEL_DBG);

lv_obj_t *scr_bpt_cal_progress;

static lv_obj_t *label_info2;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_large;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_tiny;

void draw_scr_bpt_cal_progress(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_bpt_cal_progress = lv_obj_create(NULL);
    lv_obj_add_style(scr_bpt_cal_progress, &style_scr_black, 0);
    lv_obj_clear_flag(scr_bpt_cal_progress, LV_OBJ_FLAG_SCROLLABLE); /// Flags
 
    lv_obj_t *cont_col = lv_obj_create(scr_bpt_cal_progress);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_col, -1, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_top(cont_col, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(cont_col, 1, LV_PART_MAIN);
    lv_obj_add_style(cont_col, &style_scr_black, 0);

    lv_obj_t *lbl_info_scroll = lv_label_create(cont_col);
    lv_label_set_text(lbl_info_scroll, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_color(lbl_info_scroll, lv_palette_darken(LV_PALETTE_RED, 2), LV_PART_MAIN);

    lv_obj_t *label_info = lv_label_create(cont_col);
    lv_label_set_long_mode(label_info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_info, 300);
    lv_label_set_text(label_info, "Calibration");
    lv_obj_add_style(label_info, &style_white_medium, 0);
    lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, 0);

    label_info2 = lv_label_create(cont_col);
    lv_label_set_long_mode(label_info2, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_info2, 300);
    lv_label_set_text(label_info2, "Waiting for app...");
    lv_obj_set_style_text_align(label_info2, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *spinner = lv_spinner_create(cont_col, 1000, 60);
    lv_obj_set_size(spinner, 100, 100);
    lv_obj_center(spinner);

    hpi_disp_set_curr_screen(SCR_SPL_BPT_CAL_PROGRESS);
    hpi_show_screen(scr_bpt_cal_progress, m_scroll_dir);
}

void scr_bpt_cal_progress_update_text(char *text)
{
    if (label_info2 == NULL)
    {
        return;
    }

    lv_label_set_text(label_info2, text);
}
