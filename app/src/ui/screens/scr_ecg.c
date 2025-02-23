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

lv_obj_t *scr_ecg;



static lv_obj_t *btn_hr_settings;

static bool chart_ecg_update = true;
static float y_max_ecg = 0;
static float y_min_ecg = 10000;

// static bool ecg_plot_hidden = false;

static float gx = 0;

// Externs
extern lv_style_t style_lbl_orange;
extern lv_style_t style_lbl_white;
extern lv_style_t style_red_medium;
extern lv_style_t style_lbl_white_small;

extern lv_style_t style_scr_black;

extern struct k_sem sem_ecg_start;

static void scr_ecg_settings_btn_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        hpi_move_load_scr_spl(SCR_SPL_PLOT_ECG, SCROLL_UP, (uint8_t)SCR_PLOT_ECG);
        k_sem_give(&sem_ecg_start);
    }
}

void draw_scr_ecg(enum scroll_dir m_scroll_dir)
{
    scr_ecg = lv_obj_create(NULL);
    draw_header_minimal(scr_ecg, 10);
    // draw_bg(scr_ecg);

    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_flex_flow(&style, LV_FLEX_FLOW_ROW_WRAP);
    lv_style_set_flex_main_place(&style, LV_FLEX_ALIGN_SPACE_EVENLY);
    lv_style_set_flex_cross_place(&style, LV_FLEX_ALIGN_CENTER);

    /*Create a container with COLUMN flex direction*/
    lv_obj_t *cont_col = lv_obj_create(scr_ecg);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    // lv_obj_set_width(cont_col, lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_col, -1, LV_PART_SCROLLBAR);
    lv_obj_add_style(cont_col, &style_scr_black, 0);
    lv_obj_add_style(cont_col, &style, 0);

   
    lv_obj_t *label_signal = lv_label_create(cont_col);
    lv_label_set_text(label_signal, "ECG");

    LV_IMG_DECLARE(ecg_120);
    lv_obj_t *img1 = lv_img_create(cont_col);
    lv_img_set_src(img1, &ecg_120);
    //lv_obj_align(img1, LV_ALIGN_CENTER, 0, 0);

    btn_hr_settings = lv_btn_create(cont_col);
    lv_obj_add_event_cb(btn_hr_settings, scr_ecg_settings_btn_event_handler, LV_EVENT_ALL, NULL);
    //lv_obj_align(btn_hr_settings, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_height(btn_hr_settings, 80);

    lv_obj_t *label_btn_bpt_measure = lv_label_create(btn_hr_settings);
    lv_label_set_text(label_btn_bpt_measure, LV_SYMBOL_PLAY " Measure");
    lv_obj_center(label_btn_bpt_measure);

    hpi_disp_set_curr_screen(SCR_PLOT_ECG);
    hpi_show_screen(scr_ecg, m_scroll_dir);
}