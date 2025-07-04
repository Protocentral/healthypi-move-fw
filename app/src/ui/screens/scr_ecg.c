#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <lvgl.h>
#include <stdio.h>

#include "hpi_common_types.h"

#include "ui/move_ui.h"
#include "hw_module.h"
#include "hpi_sys.h"

LOG_MODULE_REGISTER(hpi_disp_scr_ecg, LOG_LEVEL_DBG);

lv_obj_t *scr_ecg;

static lv_obj_t *btn_ecg_measure;
static lv_obj_t *label_ecg_hr;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_large;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_tiny;

extern lv_style_t style_bg_blue;
extern lv_style_t style_bg_red;
extern lv_style_t style_bg_green;

extern struct k_sem sem_ecg_start;

static void scr_ecg_start_btn_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        hpi_load_scr_spl(SCR_SPL_ECG_SCR2, SCROLL_UP, (uint8_t)SCR_ECG, 0, 0, 0);
        k_msleep(500);
        k_sem_give(&sem_ecg_start);
    }
}

void draw_scr_ecg(enum scroll_dir m_scroll_dir)
{
    scr_ecg = lv_obj_create(NULL);
    lv_obj_add_style(scr_ecg, &style_scr_black, 0);
    lv_obj_clear_flag(scr_ecg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cont_col = lv_obj_create(scr_ecg);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    // lv_obj_set_width(cont_col, lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_col, -1, LV_PART_SCROLLBAR);
    lv_obj_add_style(cont_col, &style_scr_black, 0);
    lv_obj_add_style(cont_col, &style_bg_blue, 0);

    lv_obj_t *label_signal = lv_label_create(cont_col);
    lv_label_set_text(label_signal, "ECG");

    lv_obj_t *cont_ecg = lv_obj_create(cont_col);
    lv_obj_set_size(cont_ecg, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_ecg, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_ecg, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(cont_ecg, 0, LV_PART_MAIN);
    lv_obj_add_style(cont_ecg, &style_scr_black, 0);
    lv_obj_clear_flag(cont_ecg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *img1 = lv_img_create(cont_ecg);
    //lv_img_set_src(img1, &ecg_70);

    uint8_t m_ecg_hr = 0;
    int64_t m_ecg_hr_last_update = 0;

    if (hpi_sys_get_last_ecg_update(&m_ecg_hr, &m_ecg_hr_last_update) != 0)
    {
        LOG_ERR("Error getting last ECG update");
        m_ecg_hr = 0;
        m_ecg_hr_last_update = 0;
    }

    label_ecg_hr = lv_label_create(cont_ecg);
    if (m_ecg_hr == 0)
    {
        lv_label_set_text(label_ecg_hr, "---");
    }
    else
    {
        lv_label_set_text_fmt(label_ecg_hr, "%d", m_ecg_hr);
    }
    lv_obj_add_style(label_ecg_hr, &style_white_large, 0);

    lv_obj_t *label_ecg_hr_unit = lv_label_create(cont_ecg);
    lv_label_set_text(label_ecg_hr_unit, "bpm");
    lv_obj_add_style(label_ecg_hr_unit, &style_white_medium, 0);

    lv_obj_t *label_ecg_last_update_time = lv_label_create(cont_col);

    char last_meas_str[25];
    hpi_helper_get_relative_time_str(m_ecg_hr_last_update, last_meas_str, sizeof(last_meas_str));
    lv_label_set_text(label_ecg_last_update_time, last_meas_str);
    lv_obj_set_style_text_align(label_ecg_last_update_time, LV_TEXT_ALIGN_CENTER, 0);

    btn_ecg_measure = lv_btn_create(cont_col);
    lv_obj_add_event_cb(btn_ecg_measure, scr_ecg_start_btn_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_set_height(btn_ecg_measure, 80);

    lv_obj_t *label_btn_bpt_measure = lv_label_create(btn_ecg_measure);
    lv_label_set_text(label_btn_bpt_measure, LV_SYMBOL_PLAY " Measure");
    lv_obj_center(label_btn_bpt_measure);

    hpi_disp_set_curr_screen(SCR_ECG);
    hpi_show_screen(scr_ecg, m_scroll_dir);
}