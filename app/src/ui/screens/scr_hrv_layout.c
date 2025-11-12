 /*
 * HealthyPi Move HRV Screen
 * 
 * SPDX-License-Identifier: MIT
 *
 * HRV (Heart Rate Variability) display with start button
 */

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


LOG_MODULE_REGISTER(hpi_disp_scr_hrv_layout, LOG_LEVEL_DBG);

bool battery_monitor_enabled = true;
int m_hrv_ratio_int = 0;
int m_hrv_ratio_dec = 0;
float ratio = 0.0f;

// GUI Objects
lv_obj_t *scr_hrv_layout;
static lv_obj_t *btn_hrv_measure;
static lv_obj_t *arc_hrv;
static lv_obj_t *label_hrv_value;
static lv_obj_t *label_hrv_unit;
static lv_obj_t *label_hrv_status;

int64_t m_hrv_last_update = 0;

// HRV Measurement Control
static bool hrv_measurement_active = false;

bool hrv_active = false;
int past_value = 0;
void hrv_check_and_transition(void);
K_TIMER_DEFINE(hrv_check_timer, hrv_check_and_transition, NULL);


// static void hrv_show_summary_work_handler(struct k_work *work)
// {
    
//     LOG_INF("Transitioning to HRV frequency screen (safe LVGL context)");
    
//     stop_hrv_collection();
//     hpi_load_scr_spl(SCR_SPL_HRV_LAYOUT, SCROLL_UP, (uint8_t)SCR_HRV_SUMMARY, 0, 0, 0);
// }

// K_WORK_DEFINE(hrv_check_work, hrv_show_summary_work_handler);
//#define HRV_MEASUREMENT_DURATION_MS 30000 

// void hrv_stop_timer_cb(struct k_timer *timer_id);
 uint32_t hrv_elapsed_time_ms = 30000;

// K_TIMER_DEFINE(hrv_stop_timer, hrv_stop_timer_cb, NULL);      

// // Timer callback function
// void hrv_stop_timer_cb(struct k_timer *timer_id)
// {
//     // hrv_measurement_active = false;

//     // LOG_INF("Transitioning to HRV frequency screen");
//     // hpi_load_scr_spl(SCR_SPL_HRV_LAYOUT, SCROLL_UP, (uint8_t)SCR_HRV_SUMMARY, 0, 0, 0);

//      hrv_elapsed_time_ms -= 1000;  // add 1s

//     // extern void hpi_hrv_disp_update_timer(int time_left);
//     // hpi_hrv_disp_update_timer(hrv_elapsed_time_ms);

//     if ( (HRV_MEASUREMENT_DURATION_MS - hrv_elapsed_time_ms)  >= HRV_MEASUREMENT_DURATION_MS) {
//         hrv_measurement_active = false;
//         k_timer_stop(&hrv_stop_timer);
//         hrv_elapsed_time_ms = 30000;
//         LOG_INF("Transitioning to HRV frequency screen");
//         hpi_load_scr_spl(SCR_SPL_HRV_LAYOUT, SCROLL_UP, (uint8_t)SCR_HRV_SUMMARY, 0, 0, 0);
//     }
// }


//K_TIMER_DEFINE(hrv_stop_timer, hrv_stop_timer_cb, NULL);     

void scr_hrv_measure_btn_event_handler(lv_event_t *e)
{
    if (!hrv_measurement_active) 
    {
        lv_event_code_t code = lv_event_get_code(e);

        if (code == LV_EVENT_CLICKED)
        {
            hrv_measurement_active = true;

            hrv_active = true;

            battery_monitor_enabled = false;

            hpi_load_scr_spl(SCR_SPL_HRV_PLOT, SCROLL_UP, (uint8_t)SCR_HRV_PPG_PLOT, 0, 0, 0);
         
             
            extern void hrv_reset(void);
            hrv_reset();

          
            //k_timer_start(&hrv_stop_timer, K_MSEC(HRV_MEASUREMENT_DURATION_MS), K_NO_WAIT);
           // k_timer_start(&hrv_stop_timer, K_SECONDS(1), K_SECONDS(1));  // periodic every second
            // hrv_elapsed_time_ms = 0;
            

            k_timer_start(&hrv_check_timer, K_SECONDS(1), K_SECONDS(1));  // periodic check every 1 sec

          
            extern void start_hrv_collection(void);
            start_hrv_collection();

          
        }
    }
}

void hrv_check_and_transition(void)
{
    int current_count = hrv_get_sample_count();
  
    if (current_count > past_value)
    {
        past_value = current_count;
    }

    if (current_count >= 30)
    {
     
        // LOG_INF("30 RR intervals collected. Moving to HRV summary screen...");
        hrv_measurement_active = false;

        stop_hrv_collection();

        k_timer_stop(&hrv_check_timer);

        past_value = 0;
;
    }
}





// Draw HRV screen
void draw_scr_hrv_layout(enum scroll_dir m_scroll_dir)
{
 
    scr_hrv_layout = lv_obj_create(NULL);

    lv_obj_set_style_bg_color(scr_hrv_layout, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_hrv_layout, LV_OBJ_FLAG_SCROLLABLE);


    // --- SINGLE BIG WHITE ARC ---
    arc_hrv = lv_arc_create(scr_hrv_layout);
    lv_obj_set_size(arc_hrv, 370, 370);      // Large radius like GSR
    lv_obj_center(arc_hrv);
    lv_arc_set_range(arc_hrv, 0, 100);       // HRV range
    lv_arc_set_bg_angles(arc_hrv, 135, 45);  // Background track full 270°
    lv_arc_set_angles(arc_hrv, 135, 135);    // Start position
    lv_obj_set_style_arc_color(arc_hrv, lv_color_hex(0x8000FF), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_hrv, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_hrv, lv_color_hex(0x8000FF), LV_PART_INDICATOR); // Progress purple
    lv_obj_set_style_arc_width(arc_hrv, 6, LV_PART_INDICATOR);
    lv_obj_remove_style(arc_hrv, NULL, LV_PART_KNOB); // Remove knob
    lv_obj_clear_flag(arc_hrv, LV_OBJ_FLAG_CLICKABLE);
     


    // --- TITLE ---
    lv_obj_t *label_title = lv_label_create(scr_hrv_layout);
    lv_label_set_text(label_title, "HRV");
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // // MID-UPPER RING: Flame icon (clean, no container)
    lv_obj_t *img_hrv = lv_img_create(scr_hrv_layout);
    lv_img_set_src(img_hrv, &img_calories_48);  // Using flame as HRV placeholder
    lv_obj_align(img_hrv, LV_ALIGN_TOP_MID, 0, 95);  // Positioned below title
    lv_obj_set_style_img_recolor(img_hrv, lv_color_hex(0x8000FF), LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(img_hrv, LV_OPA_COVER, LV_PART_MAIN);


    // CENTRAL ZONE: Main HRV Value/Status (properly spaced from icon)
    label_hrv_value = lv_label_create(scr_hrv_layout);
    lv_label_set_text(label_hrv_value, "--");
    lv_obj_align(label_hrv_value, LV_ALIGN_CENTER, 0, -10);  // Centered, slightly above middle
    lv_obj_set_style_text_color(label_hrv_value, lv_color_white(), LV_PART_MAIN);
    lv_obj_add_style(label_hrv_value, &style_numeric_large, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_hrv_value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // Unit/Status label directly below main value
    label_hrv_unit = lv_label_create(scr_hrv_layout);
    lv_label_set_text(label_hrv_unit, "--"); 
    lv_obj_align(label_hrv_unit, LV_ALIGN_CENTER, 0, 35);  // Below main value with gap
    lv_obj_set_style_text_color(label_hrv_unit, lv_color_hex(0x8000FF), LV_PART_MAIN);
    lv_obj_add_style(label_hrv_unit, &style_numeric_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_hrv_unit, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    

    label_hrv_status = lv_label_create(scr_hrv_layout);
    lv_label_set_text(label_hrv_status, "HeartRateVariability");
    lv_obj_align(label_hrv_status, LV_ALIGN_CENTER, 0, 80);  // Centered, below unit with gap
    lv_obj_set_style_text_color(label_hrv_status, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_add_style(label_hrv_status, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_hrv_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

   
    // --- MEASURE BUTTON ---
    btn_hrv_measure = hpi_btn_create_primary(scr_hrv_layout);
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
    // --- SHOW SCREEN ---
    hpi_disp_set_curr_screen(SCR_HRV_SUMMARY);
    hpi_show_screen(scr_hrv_layout, m_scroll_dir);
}

static void hrv_update_display(void)
{

    char last_meas_str[25];

    ratio = hpi_get_lf_hf_ratio();

    if(ratio > 0.0f){
        m_hrv_last_update = hw_get_sys_time_ts(); 
        hpi_sys_set_last_lf_hf_update(ratio, m_hrv_last_update);
    }

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

        if(ratio < 1.0){
            lv_label_set_text(label_hrv_unit, "Parasympathetic");
        }
        else if(ratio == 1.0){
            lv_label_set_text(label_hrv_unit, "Balanced");
        }
        else if(ratio > 1.0){
            lv_label_set_text(label_hrv_unit, "Sympathetic");
        }  
        lv_label_set_text_fmt(label_hrv_value, "%d.%02d", m_hrv_ratio_int, m_hrv_ratio_dec);
        lv_label_set_text(label_hrv_status, last_meas_str);
    }
    
}






