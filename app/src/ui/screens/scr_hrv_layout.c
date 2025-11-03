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


LOG_MODULE_REGISTER(hpi_disp_scr_hrv_layout, LOG_LEVEL_ERR);

// GUI Objects
lv_obj_t *scr_hrv_layout;
static lv_obj_t *btn_hrv_measure;
static lv_obj_t *arc_hrv;


// HRV Measurement Control
static bool hrv_measurement_active = false;

#define HRV_MEASUREMENT_DURATION_MS 30000 


// Timer callback function
void hrv_stop_timer_cb(struct k_timer *timer_id)
{
    hrv_measurement_active = false;

    LOG_INF("Transitioning to HRV frequency screen");
    hpi_load_scr_spl(SCR_SPL_HRV_LAYOUT, SCROLL_UP, (uint8_t)SCR_HRV_SUMMARY, 0, 0, 0);
}


K_TIMER_DEFINE(hrv_stop_timer, hrv_stop_timer_cb, NULL);             


void scr_hrv_measure_btn_event_handler(lv_event_t *e)
{
    if (!hrv_measurement_active) 
    {
        lv_event_code_t code = lv_event_get_code(e);

        if (code == LV_EVENT_CLICKED)
        {
            hrv_measurement_active = true;

            
            //hpi_load_scr_spl(SCR_SPL_RAW_PPG, SCROLL_UP, (uint8_t)SCR_HR, 0, 0, 0);
            hpi_load_scr_spl(SCR_SPL_HRV_PLOT, SCROLL_UP, (uint8_t)SCR_HRV_PPG_PLOT, 0, 0, 0);
         
             
            extern void hrv_reset(void);
            hrv_reset();

          
            k_timer_start(&hrv_stop_timer, K_MSEC(HRV_MEASUREMENT_DURATION_MS), K_NO_WAIT);

          
            extern void start_hrv_collection(void);
            start_hrv_collection();
        }
    }
}



// Draw HRV screen
void draw_scr_hrv_layout(enum scroll_dir m_scroll_dir)
{
    scr_hrv_layout = lv_obj_create(NULL);
    // AMOLED optimization: pure black background
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

    // --- SHOW SCREEN ---
    hpi_disp_set_curr_screen(SCR_HRV_SUMMARY);
    hpi_show_screen(scr_hrv_layout, m_scroll_dir);
}
