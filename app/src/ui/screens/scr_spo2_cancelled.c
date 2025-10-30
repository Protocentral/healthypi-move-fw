/*
 * HealthyPi Move - SpO2 Cancelled Screen
 *
 * Simple screen to indicate the SpO2 measurement was cancelled by the user.
 */

#include <zephyr/kernel.h>
#include <lvgl.h>
#include <zephyr/logging/log.h>

#include "ui/move_ui.h"
#include "hpi_common_types.h"

LOG_MODULE_REGISTER(hpi_disp_scr_spo2_cancelled, LOG_LEVEL_DBG);

static lv_obj_t *scr_spo2_cancelled;

// Extern styles/images/fonts declared in move_ui.h / common UI files
extern lv_style_t style_scr_black;

void draw_scr_spl_spo2_cancelled(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    ARG_UNUSED(arg4);

    scr_spo2_cancelled = lv_obj_create(NULL);
    lv_obj_add_style(scr_spo2_cancelled, &style_scr_black, 0);
    lv_obj_clear_flag(scr_spo2_cancelled, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cont = lv_obj_create(scr_spo2_cancelled);
    lv_obj_set_size(cont, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(cont, &style_scr_black, 0);

    lv_obj_t *img = lv_img_create(cont);
    lv_img_set_src(img, &img_failed_80);

    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, "SpO2 measurement cancelled");
    lv_obj_set_style_text_font(lbl, &ui_font_number_big, 0);

    hpi_disp_set_curr_screen(SCR_SPL_SPO2_CANCELLED);
    hpi_show_screen(scr_spo2_cancelled, m_scroll_dir);
}

void gesture_down_scr_spl_spo2_cancelled(void)
{
    hpi_load_screen(SCR_SPO2, SCROLL_DOWN);
}
