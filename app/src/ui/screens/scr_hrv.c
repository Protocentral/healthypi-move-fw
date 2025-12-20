#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <lvgl.h>
#include <stdio.h>
#include <zephyr/smf.h>
#include <app_version.h>
#include <zephyr/logging/log.h>
#include "hpi_common_types.h"
#include "ui/move_ui.h"
#include "hpi_sys.h"
#include "stdlib.h"
#include "hrv_algos.h"
#include "hpi_common_types.h"
#include "ui/move_ui.h"
#include "hpi_sys.h"

LOG_MODULE_REGISTER(hpi_disp_scr_hrv, LOG_LEVEL_DBG);

int m_hrv_ratio_int = 0;
int m_hrv_ratio_dec = 0;
float ratio = 0.0f;
int64_t m_hrv_last_update = 0;

extern struct k_sem sem_ecg_start;

// GUI Objects
lv_obj_t *scr_hrv;
static lv_obj_t *btn_hrv_measure;
static lv_obj_t *arc_hrv;
static lv_obj_t *label_hrv_value;
static lv_obj_t *label_hrv_status;

// Externs - Modern style system
extern lv_style_t style_body_medium;
extern lv_style_t style_numeric_large;
extern lv_style_t style_caption;

extern float lf_power_compact;
extern float hf_power_compact ;

/**
 * @brief Button event handler for starting HRV evaluation
 */
static void scr_hrv_btn_start_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_CLICKED) {
        LOG_INF("HRV Evaluation started by user");
        
        // Display progress screen immediately
        hpi_load_scr_spl(SCR_SPL_HRV_EVAL_PROGRESS, SCROLL_UP, 0, 0, 0, 0);
        
        // Signal state machine to start HRV evaluation
        extern struct k_sem sem_hrv_eval_start;
        k_sem_give(&sem_hrv_eval_start);
    }
}

/**
 * @brief Draw HRV home screen with measurement controls
 * 
 * Main carousel screen showing HRV evaluation options:
 * - Start button to initiate new HRV evaluation
 */
void draw_scr_hrv(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
 
    scr_hrv = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_hrv, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_hrv, LV_OBJ_FLAG_SCROLLABLE);

    // --- SINGLE BIG ARC ---
    arc_hrv = lv_arc_create(scr_hrv);
    lv_obj_set_size(arc_hrv, 370, 370);      
    lv_obj_center(arc_hrv);
    lv_arc_set_range(arc_hrv, 0, 100);      
    lv_arc_set_bg_angles(arc_hrv, 135, 45);  
    lv_arc_set_angles(arc_hrv, 135, 135);   
    lv_obj_set_style_arc_color(arc_hrv, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_hrv, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_hrv, lv_color_hex(0x8000FF), LV_PART_INDICATOR); 
    lv_obj_set_style_arc_width(arc_hrv, 6, LV_PART_INDICATOR);
    lv_obj_remove_style(arc_hrv, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc_hrv, LV_OBJ_FLAG_CLICKABLE);
     

    // --- TITLE ---
    lv_obj_t *label_title = lv_label_create(scr_hrv);
    lv_label_set_text(label_title, "HRV");
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // --- MID-UPPER RING: Flame icon (clean, no container) ---
    lv_obj_t *img_hrv = lv_img_create(scr_hrv);
    lv_img_set_src(img_hrv, &img_calories_48);  // Using flame as HRV placeholder
    lv_obj_align(img_hrv, LV_ALIGN_TOP_MID, 0, 80);  // Positioned below title
    lv_obj_set_style_img_recolor(img_hrv, lv_color_hex(0x8000FF), LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(img_hrv, LV_OPA_COVER, LV_PART_MAIN);

    // --- LF/HF Ratio Value in the middle of the arc ---
    // label_hrv_value = lv_label_create(scr_hrv);
    // lv_label_set_text(label_hrv_value, "--"); 
    // lv_obj_align(label_hrv_value, LV_ALIGN_CENTER, 0, 10);
    // lv_obj_add_style(label_hrv_value, &style_numeric_large, LV_PART_MAIN);
    // lv_obj_set_style_text_color(label_hrv_value, lv_color_hex(0xFFFFFF), LV_PART_MAIN);

    label_hrv_value = lv_label_create(scr_hrv);
    lv_label_set_text(label_hrv_value, "--");
    lv_obj_align(label_hrv_value, LV_ALIGN_CENTER, 0, -10);  // Centered, slightly above middle
    lv_obj_set_style_text_color(label_hrv_value, lv_color_white(), LV_PART_MAIN);
    lv_obj_add_style(label_hrv_value, &style_numeric_large, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_hrv_value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_t *label_hrv_unit = lv_label_create(scr_hrv);
    lv_label_set_text(label_hrv_unit, "LF/HF");
    lv_obj_align(label_hrv_unit, LV_ALIGN_CENTER, 0, 35);  // Below main value with gap
    lv_obj_set_style_text_color(label_hrv_unit, lv_color_hex(0x8000FF), LV_PART_MAIN);
    lv_obj_add_style(label_hrv_unit, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_hrv_unit, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);


    // /* --- LEFT LABEL: PARASYM --- */
    // lv_obj_t *label_para = lv_label_create(scr_hrv);
    // lv_label_set_text(label_para, "Par ");
    // lv_obj_set_style_text_color(label_para, lv_color_hex(COLOR_SUCCESS_GREEN), 0);
    // lv_obj_align_to(label_para, lfhf_arc, LV_ALIGN_OUT_LEFT_MID, -5, 0);

    // /* --- RIGHT LABEL: SYM --- */
    // lv_obj_t *label_sym = lv_label_create(scr_hrv);
    // lv_label_set_text(label_sym, " Sym");
    // lv_obj_set_style_text_color(label_sym, lv_color_hex(0xFF4E4E), 0);
    // lv_obj_align_to(label_sym, lfhf_arc, LV_ALIGN_OUT_RIGHT_MID, 5, 0);


    label_hrv_status = lv_label_create(scr_hrv);
    lv_label_set_text(label_hrv_status, "HeartRateVariability");
    lv_obj_align(label_hrv_status, LV_ALIGN_CENTER, 0, 80); 
    lv_obj_set_style_text_color(label_hrv_status, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_add_style(label_hrv_status, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_hrv_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

   
    // --- MEASURE BUTTON ---
    btn_hrv_measure = hpi_btn_create_primary(scr_hrv);
    lv_obj_set_size(btn_hrv_measure, 200, 60);
    lv_obj_align(btn_hrv_measure, LV_ALIGN_BOTTOM_MID, 0, -35);
    lv_obj_set_style_radius(btn_hrv_measure, 30, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn_hrv_measure, lv_color_hex(COLOR_BTN_PURPLE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_hrv_measure, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_hrv_measure, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn_hrv_measure, 0, LV_PART_MAIN);

    lv_obj_t *label_btn = lv_label_create(btn_hrv_measure);
    lv_label_set_text(label_btn, LV_SYMBOL_PLAY " Start HRV");
    lv_obj_center(label_btn);
    //lv_obj_set_style_text_font(label_btn, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_btn, lv_color_white(), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_hrv_measure, scr_hrv_btn_start_handler, LV_EVENT_CLICKED, NULL);
    hrv_update_display();
    hpi_disp_set_curr_screen(SCR_HRV);
    hpi_show_screen(scr_hrv, m_scroll_dir);
}

static void hrv_update_display(void)
{

    char last_meas_str[25];
    ratio = hpi_get_lf_hf_ratio();
    int m_hrv_last_value = 0;

    hpi_sys_get_last_hrv_update(&m_hrv_last_value, &m_hrv_last_update);
    hpi_helper_get_relative_time_str(m_hrv_last_update, last_meas_str, sizeof(last_meas_str));

    if (ratio < 0.0f || ratio == 0.0f)
     {
        m_hrv_ratio_int = 0;
        m_hrv_ratio_dec = 0;

        lv_label_set_text(label_hrv_value, "--");
        lv_label_set_text(label_hrv_status, "HeartRateVariability");
     }
     else 
     {
        m_hrv_ratio_int = (int)ratio;
        m_hrv_ratio_dec = abs((int)((ratio - m_hrv_ratio_int) * 100));

        lv_label_set_text_fmt(label_hrv_value, "%d.%02d", m_hrv_ratio_int, m_hrv_ratio_dec);
        lv_label_set_text(label_hrv_status, last_meas_str);
     }

    lv_arc_set_value(arc_hrv, get_stress_percentage(lf_power_compact, hf_power_compact));
    lv_obj_set_style_arc_color(arc_hrv, lv_color_hex(0x8000FF), LV_PART_INDICATOR);

}