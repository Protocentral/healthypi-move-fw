#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <lvgl.h>
#include <stdio.h>

#include "hpi_common_types.h"
#include "ui/move_ui.h"
#include "trends.h"

LOG_MODULE_REGISTER(hpi_disp_scr_spo2_scr2, LOG_LEVEL_DBG);

static lv_obj_t *scr_spo2_scr2;
static lv_obj_t *btn_spo2_proceed;

static int spo2_source = 0;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_large;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_tiny;

extern lv_style_t style_bg_blue;
extern lv_style_t style_bg_red;

extern struct k_sem sem_start_one_shot_spo2;
extern struct k_sem sem_fi_spo2_est_start;

static void scr_spo2_btn_proceed_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        if (spo2_source == SPO2_SOURCE_PPG_WR)
        {
            LOG_DBG("Proceeding with wrist PPG sensor");
            k_sem_give(&sem_start_one_shot_spo2);
            hpi_load_screen(SCR_SPL_SPO2_MEASURE, SCROLL_UP);
        }
        else if (spo2_source == SPO2_SOURCE_PPG_FI)
        {
            LOG_DBG("Proceeding with finger PPG sensor");
            k_sem_give(&sem_fi_spo2_est_start);
        }
        else
        {
            LOG_ERR("Invalid SpO2 source selected: %d", spo2_source);
            return;
        }
    }
}

void draw_scr_spo2_scr2(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    spo2_source = arg2;

    scr_spo2_scr2 = lv_obj_create(NULL);
    lv_obj_add_style(scr_spo2_scr2, &style_scr_black, 0);
    lv_obj_clear_flag(scr_spo2_scr2, LV_OBJ_FLAG_SCROLLABLE);

    /*Create a container with COLUMN flex direction*/
    lv_obj_t *cont_col = lv_obj_create(scr_spo2_scr2);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(cont_col, &style_scr_black, 0);

    lv_obj_t *lbl_scroll_cancel = lv_label_create(cont_col);
    lv_label_set_text(lbl_scroll_cancel, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_color(lbl_scroll_cancel, lv_palette_darken(LV_PALETTE_RED, 2), LV_PART_MAIN);

    if (spo2_source == SPO2_SOURCE_PPG_WR)
    {
        lv_obj_t *img_spo2_wr = lv_img_create(cont_col);
        lv_img_set_src(img_spo2_wr, &img_spo2_hand);

        lv_obj_t *label_info = lv_label_create(cont_col);
        lv_label_set_long_mode(label_info, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(label_info, 300);
        lv_label_set_text(label_info, "Ensure that your Move is worn on the wrist snugly as shown.");
        lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, 0);
    }
    else if (spo2_source == SPO2_SOURCE_PPG_FI)
    {
        lv_obj_t *img_spo2_fi = lv_img_create(cont_col);
        lv_img_set_src(img_spo2_fi, &img_bpt_finger_90);

        lv_obj_t *label_info = lv_label_create(cont_col);
        lv_label_set_long_mode(label_info, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(label_info, 300);
        lv_label_set_text(label_info, "Wear finger sensor as per the instructions now");
        lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, 0);
    }

    btn_spo2_proceed = lv_btn_create(cont_col);
    lv_obj_add_event_cb(btn_spo2_proceed, scr_spo2_btn_proceed_handler, LV_EVENT_ALL, NULL);
    lv_obj_set_height(btn_spo2_proceed, 85);

    lv_obj_t *label_btn = lv_label_create(btn_spo2_proceed);
    lv_label_set_text(label_btn, LV_SYMBOL_PLAY " Proceed");
    lv_obj_center(label_btn);

    hpi_disp_set_curr_screen(SCR_SPL_SPO2_SCR2);
    hpi_show_screen(scr_spo2_scr2, m_scroll_dir);
}

void gesture_down_scr_spo2_scr2(void)
{
    hpi_load_screen(SCR_HR, SCROLL_DOWN);
}
