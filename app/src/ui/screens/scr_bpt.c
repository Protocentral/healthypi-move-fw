#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <stdio.h>
#include <app_version.h>
#include <zephyr/logging/log.h>

#include "ui/move_ui.h"

LOG_MODULE_REGISTER(scr_bpt, LOG_LEVEL_DBG);

lv_obj_t *scr_bpt;

extern lv_style_t style_scr_black;
extern lv_style_t style_scr_container;

static void scr_bpt_measure_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        LOG_DBG("Measure click");
        hpi_load_scr_spl(SCR_SPL_BPT_SCR2, SCROLL_UP, (uint8_t)SCR_BPT, 0, 0, 0);
    }
}

void draw_scr_bpt(enum scroll_dir m_scroll_dir)
{
    scr_bpt = lv_obj_create(NULL);
    lv_obj_add_style(scr_bpt, &style_scr_black, 0);
    lv_obj_set_scroll_dir(scr_bpt, LV_DIR_VER);

    lv_obj_t *cont_col = lv_obj_create(scr_bpt);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_col, -1, LV_PART_SCROLLBAR);
    lv_obj_add_style(cont_col, &style_scr_black, 0);
    lv_obj_add_style(cont_col, &style_scr_container, 0);
    lv_obj_set_scroll_dir(cont_col, LV_DIR_VER);

    lv_obj_t *label_signal = lv_label_create(cont_col);
    lv_label_set_text(label_signal, "Blood Pressure");

    lv_obj_t *img1 = lv_img_create(cont_col);
    lv_img_set_src(img1, &bp_70);

    lv_obj_t *btn_bpt_measure = lv_btn_create(cont_col);
    lv_obj_add_event_cb(btn_bpt_measure, scr_bpt_measure_handler, LV_EVENT_ALL, NULL);
    lv_obj_set_height(btn_bpt_measure, 80);

    lv_obj_t *label_btn_bpt_measure = lv_label_create(btn_bpt_measure);
    lv_label_set_text(label_btn_bpt_measure, LV_SYMBOL_PLAY " Measure");
    lv_obj_center(label_btn_bpt_measure);

    hpi_disp_set_curr_screen(SCR_BPT);
    hpi_show_screen(scr_bpt, m_scroll_dir);
}