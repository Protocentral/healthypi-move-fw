#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <lvgl.h>
#include <stdio.h>

#include "hpi_common_types.h"
#include "ui/move_ui.h"
#include "hw_module.h"

K_SEM_DEFINE(sem_spo2_complete, 0, 1);

lv_obj_t *scr_spo2_complete;

static lv_obj_t *label_spo2_percent;
static lv_obj_t *label_spo2_last_update_time;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_white_large;

void draw_scr_spl_spo2_complete(enum scroll_dir m_scroll_dir)
{
    scr_spo2_complete = lv_obj_create(NULL);
    lv_obj_add_style(scr_spo2_complete, &style_scr_black, 0);
    lv_obj_clear_flag(scr_spo2_complete, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    // draw_scr_common(scr_spo2_complete);

    /*Create a container with COLUMN flex direction*/
    lv_obj_t *cont_col = lv_obj_create(scr_spo2_complete);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(cont_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(cont_col, &style_scr_black, 0);

    lv_obj_t *img1 = lv_img_create(cont_col);
    lv_img_set_src(img1, &img_complete_100);

    lv_obj_t *label_signal = lv_label_create(cont_col);
    lv_label_set_text(label_signal, "Success");

    lv_obj_t *cont_spo2_val = lv_obj_create(cont_col);
    lv_obj_set_size(cont_spo2_val, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_spo2_val, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(cont_spo2_val, &style_scr_black, 0);
    lv_obj_set_flex_align(cont_spo2_val, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *img_spo2 = lv_img_create(cont_spo2_val);
    lv_img_set_src(img_spo2, &icon_spo2_100);

    label_spo2_percent = lv_label_create(cont_spo2_val);
    lv_label_set_text(label_spo2_percent, "--");
    lv_obj_set_style_text_align(label_spo2_percent, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_style(label_spo2_percent, &style_white_large, 0);

    lv_obj_t *label_spo2_percent_sign = lv_label_create(cont_spo2_val);
    lv_label_set_text(label_spo2_percent_sign, " %");

    label_spo2_last_update_time = lv_label_create(cont_col);
    lv_label_set_text(label_spo2_last_update_time, "Last updated: 00:00");
    lv_obj_set_style_text_align(label_spo2_last_update_time, LV_TEXT_ALIGN_CENTER, 0);

    hpi_disp_set_curr_screen(SCR_SPL_SPO2_COMPLETE);
    hpi_show_screen(scr_spo2_complete, m_scroll_dir);
    k_sem_give(&sem_spo2_complete);
}

void hpi_disp_update_spo2(uint8_t spo2, int64_t ts_last_update)
{
    struct tm tm_last_update = *gmtime(&ts_last_update);

    if (label_spo2_percent == NULL)
        return;

    if (spo2 == 0)
    {
        lv_label_set_text(label_spo2_percent, "--");
    }
    else
    {
        lv_label_set_text_fmt(label_spo2_percent, "%2d", spo2);
    }
    lv_label_set_text_fmt(label_spo2_last_update_time, "Last updated: %02d:%02d", tm_last_update.tm_hour, tm_last_update.tm_min);
}
