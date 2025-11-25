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
#include "hpi_sys.h"
#include "stdlib.h"
#include "hrv_algos.h"

LOG_MODULE_REGISTER(hpi_disp_scr_hrv, LOG_LEVEL_DBG);

bool battery_monitor_enabled = true;
int m_hrv_ratio_int = 0;
int m_hrv_ratio_dec = 0;
float ratio = 0.0f;
static bool hrv_measurement_active = false;
bool hrv_active = false;
int past_value = 0;
int64_t m_hrv_last_update = 0;

extern bool check_gesture;
extern int64_t last_measurement_time;
extern struct k_sem sem_ecg_start;


// GUI Objects
lv_obj_t *scr_hrv;
static lv_obj_t *btn_hrv_measure;
static lv_obj_t *arc_hrv;
static lv_obj_t *label_hrv_value;
static lv_obj_t *label_hrv_status;
static lv_obj_t *lfhf_arc;

// #define HRV_WORK_STACKSIZE 512

// K_WORK_DEFINE(hrv_check_work, hrv_check_and_transition_work);

// // Timer handler, now only schedules work—not logic directly
// static void hrv_check_timer_handler(struct k_timer *timer)
// {
//     k_work_submit(&hrv_check_work);
// }

// //K_TIMER_DEFINE(hrv_check_timer, hrv_check_and_transition, NULL);
// K_TIMER_DEFINE(hrv_check_timer, hrv_check_timer_handler, NULL);

// static void hrv_check_and_transition_work(struct k_work *work) {
//     hrv_check_and_transition();
// }

void scr_hrv_measure_btn_event_handler(lv_event_t *e)
{
    if (!hrv_measurement_active || check_gesture) 
    {
        check_gesture = false;

        lv_event_code_t code = lv_event_get_code(e);

        if (code == LV_EVENT_CLICKED)
        {
            hrv_measurement_active = true;

           // past_value = 0;

            hrv_active = true;

            battery_monitor_enabled = false;

            hpi_load_scr_spl(SCR_SPL_PLOT_HRV, SCROLL_UP, (uint8_t)SCR_SPL_PLOT_HRV, 0, 0, 0);
          
            k_msleep(500);
            
            hrv_reset();

           // k_timer_start(&hrv_check_timer, K_SECONDS(1), K_SECONDS(1));  

            start_hrv_collection();
            
            k_sem_give(&sem_ecg_start);
        }
    }
}

// void hrv_check_and_transition(void)
// {
//     int current_count = hrv_get_sample_count();
  
//     if (current_count > past_value)
//     {
//         past_value = current_count;
//     }

//     if (past_value >= HRV_LIMIT)
//     {
     
//         hrv_measurement_active = false;

//         stop_hrv_collection();

//         k_timer_stop(&hrv_check_timer);


//         past_value = 0;

//         float ratio = hpi_get_lf_hf_ratio();
//         if (ratio > 0.0f) 
//         {
//             int64_t now_ts = hw_get_sys_time_ts();
//             hpi_sys_set_last_lf_hf_update((uint16_t)(ratio * 100), now_ts);
//         }
//     }
// }

void draw_scr_hrv(enum scroll_dir m_scroll_dir)
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
    lv_obj_set_style_arc_color(arc_hrv, lv_color_hex(0x8000FF), LV_PART_MAIN);
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


    // --- Gauge to represent sympathetic or parasympathetic ---
    lfhf_arc = lv_arc_create(scr_hrv);
    lv_obj_set_size(lfhf_arc, 115, 115);
    lv_arc_set_rotation(lfhf_arc, 135);
    lv_arc_set_bg_angles(lfhf_arc, 0, 270);
    lv_arc_set_value(lfhf_arc, 0);
    lv_obj_clear_flag(lfhf_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_style(lfhf_arc, NULL, LV_PART_KNOB);
    lv_obj_align(lfhf_arc, LV_ALIGN_CENTER, 0, 10);

    // --- LF/HF Ratio Value in the middle of the arc ---
    label_hrv_value = lv_label_create(scr_hrv);
    lv_label_set_text(label_hrv_value, "--"); 
    lv_obj_align(label_hrv_value, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_text_color(label_hrv_value, lv_color_hex(0xFFFFFF), LV_PART_MAIN);


    /* --- LEFT LABEL: PARASYM --- */
    lv_obj_t *label_para = lv_label_create(scr_hrv);
    lv_label_set_text(label_para, "Par ");
    lv_obj_set_style_text_color(label_para, lv_color_hex(COLOR_SUCCESS_GREEN), 0);
    lv_obj_align_to(label_para, lfhf_arc, LV_ALIGN_OUT_LEFT_MID, -5, 0);

    /* --- RIGHT LABEL: SYM --- */
    lv_obj_t *label_sym = lv_label_create(scr_hrv);
    lv_label_set_text(label_sym, " Sym");
    lv_obj_set_style_text_color(label_sym, lv_color_hex(0xFF4E4E), 0);
    lv_obj_align_to(label_sym, lfhf_arc, LV_ALIGN_OUT_RIGHT_MID, 5, 0);


    label_hrv_status = lv_label_create(scr_hrv);
    lv_label_set_text(label_hrv_status, "HeartRateVariability");
    lv_obj_align(label_hrv_status, LV_ALIGN_CENTER, 0, 80); 
    lv_obj_set_style_text_color(label_hrv_status, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_add_style(label_hrv_status, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_hrv_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

   
    // --- MEASURE BUTTON ---
    btn_hrv_measure = hpi_btn_create_primary(scr_hrv);
    lv_obj_set_size(btn_hrv_measure, 180, 50);
    lv_obj_align(btn_hrv_measure, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_set_style_radius(btn_hrv_measure, 25, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn_hrv_measure, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_hrv_measure, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_hrv_measure, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_hrv_measure, lv_color_hex(0x8000FF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn_hrv_measure, 0, LV_PART_MAIN);

    lv_obj_t *label_btn = lv_label_create(btn_hrv_measure);
    lv_label_set_text(label_btn, LV_SYMBOL_PLAY " Start HRV");
    lv_obj_center(label_btn);
    lv_obj_set_style_text_color(label_btn, lv_color_hex(0x8000FF), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_hrv_measure, scr_hrv_measure_btn_event_handler, LV_EVENT_CLICKED, NULL);

    hrv_update_display();

    hpi_disp_set_curr_screen(SCR_HRV);
    hpi_show_screen(scr_hrv, m_scroll_dir);
}

static void hrv_update_display(void)
{

    float ratio_norm;
    float max_ratio = 5.0f;
    char last_meas_str[25];
    ratio = hpi_get_lf_hf_ratio();
    int m_hrv_last_value = 0;

    hpi_sys_get_last_hrv_update(&m_hrv_last_value, &m_hrv_last_update);
    hpi_helper_get_relative_time_str(m_hrv_last_update, last_meas_str, sizeof(last_meas_str));

    if (ratio < 0.0f)
     {
        m_hrv_ratio_int = 0;
        m_hrv_ratio_dec = 0;
     }
     else 
     {
        m_hrv_ratio_int = (int)ratio;
        m_hrv_ratio_dec = abs((int)((ratio - m_hrv_ratio_int) * 100));

        lv_label_set_text_fmt(label_hrv_value, "%d.%02d", m_hrv_ratio_int, m_hrv_ratio_dec);
        lv_label_set_text(label_hrv_status, last_meas_str);
    }
    // Normalize ratio to 0-100 for arc
   
    if (ratio < 0) ratio = 0;
    if (ratio > max_ratio) ratio = max_ratio;

    int arc_val = (int)((ratio / max_ratio) * 100);
    lv_arc_set_value(lfhf_arc, arc_val);

    // Color based on ratio range
    if (ratio <= 1.0f) {
        lv_obj_set_style_arc_color(lfhf_arc, lv_color_hex(COLOR_SUCCESS_GREEN), LV_PART_INDICATOR);  // Green
    } else if (ratio <= 2.0f) {
        lv_obj_set_style_arc_color(lfhf_arc, lv_color_hex(0xFFCC00), LV_PART_INDICATOR);  // Yellow
    } else {
        lv_obj_set_style_arc_color(lfhf_arc, lv_color_hex(COLOR_CRITICAL_RED), LV_PART_INDICATOR);  // Red
    }


    
}