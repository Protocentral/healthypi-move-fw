#include <lvgl.h>
#include <stdio.h>
#include <zephyr/logging/log.h>

#include "hpi_common_types.h"
#include "hw_module.h"
#include "ui/move_ui.h"

LOG_MODULE_REGISTER(scr_ble, LOG_LEVEL_DBG);

lv_obj_t *scr_ble;

static lv_obj_t *btn_cancel;
static lv_obj_t *lbl_pair_code;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_large;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_tiny;

static void scr_btn_cancel_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        // k_sem_give(&sem_bpt_check_sensor);
        hpi_load_screen(SCR_HOME, SCROLL_UP);
    }
}

void draw_scr_ble(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    uint32_t hpi_ble_event = arg1;
    uint32_t hpi_ble_passkey = arg2;

    scr_ble = lv_obj_create(NULL);
    lv_obj_add_style(scr_ble, &style_scr_black, 0);
    // lv_obj_set_scrollbar_mode(scr_ble, LV_SCROLLBAR_MODE_ON);

    /*Create a container with COLUMN flex direction*/
    lv_obj_t *cont_col = lv_obj_create(scr_ble);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(cont_col, &style_scr_black, 0);

    if (hpi_ble_event == HPI_BLE_EVENT_PAIR_REQUEST)
    {
        lv_obj_t *label_info = lv_label_create(cont_col);
        lv_label_set_long_mode(label_info, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(label_info, 300);
        lv_label_set_text(label_info, "BLE Pairing Requested");
        lv_obj_add_style(label_info, &style_white_medium, 0);
        lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, 0);

        lv_obj_t *label_info2 = lv_label_create(cont_col);
        lv_label_set_long_mode(label_info2, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(label_info2, 300);
        lv_label_set_text(label_info2, "Enter the below PIN to pair");
        lv_obj_set_style_text_align(label_info2, LV_TEXT_ALIGN_CENTER, 0);

        lbl_pair_code = lv_label_create(cont_col);
        lv_label_set_text_fmt(lbl_pair_code, "%06d", hpi_ble_passkey);
        lv_obj_set_style_text_align(lbl_pair_code, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_add_style(lbl_pair_code, &style_white_large, 0);

        btn_cancel = lv_btn_create(cont_col);
        lv_obj_add_event_cb(btn_cancel, scr_btn_cancel_handler, LV_EVENT_ALL, NULL);
        lv_obj_set_height(btn_cancel, 85);

        lv_obj_t *label_btn = lv_label_create(btn_cancel);
        lv_label_set_text(label_btn, LV_SYMBOL_CLOSE " Cancel");
        lv_obj_center(label_btn);
    }
    else if (hpi_ble_event == HPI_BLE_EVENT_PAIR_SUCCESS)
    {
        lv_obj_t *label_info = lv_label_create(cont_col);
        lv_label_set_long_mode(label_info, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(label_info, 300);
        lv_label_set_text(label_info, "BLE Pairing Complete");
        lv_obj_add_style(label_info, &style_white_medium, 0);
        lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, 0);

        lv_obj_t *img1 = lv_img_create(cont_col);
        lv_img_set_src(img1, &img_complete_85);

        btn_cancel = lv_btn_create(cont_col);
        lv_obj_add_event_cb(btn_cancel, scr_btn_cancel_handler, LV_EVENT_ALL, NULL);
        lv_obj_set_height(btn_cancel, 85);

        lv_obj_t *label_btn = lv_label_create(btn_cancel);
        lv_label_set_text(label_btn, LV_SYMBOL_CLOSE " Close");
        lv_obj_center(label_btn);
    }
    else if (hpi_ble_event == HPI_BLE_EVENT_PAIR_FAILED)
    {
        lv_obj_t *label_info = lv_label_create(cont_col);
        lv_label_set_long_mode(label_info, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(label_info, 300);
        lv_label_set_text(label_info, "BLE Pairing Failed");
        lv_obj_add_style(label_info, &style_white_medium, 0);
        lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, 0);

        lv_obj_t *img1 = lv_img_create(cont_col);
        lv_img_set_src(img1, &img_failed_80);

        btn_cancel = lv_btn_create(cont_col);
        lv_obj_add_event_cb(btn_cancel, scr_btn_cancel_handler, LV_EVENT_ALL, NULL);
        lv_obj_set_height(btn_cancel, 85);

        lv_obj_t *label_btn = lv_label_create(btn_cancel);
        lv_label_set_text(label_btn, LV_SYMBOL_CLOSE " Close");
        lv_obj_center(label_btn);
    }
    else
    {
        LOG_ERR("Unknown BLE event: %d", hpi_ble_event);
    }

    lv_disp_trig_activity(NULL);

    hpi_disp_set_curr_screen(SCR_SPL_BLE);
    hpi_show_screen(scr_ble, m_scroll_dir);
}