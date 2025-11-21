#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <lvgl.h>
#include <stdio.h>
#include <stdint.h>
#include <zephyr/logging/log.h>
#include "hpi_common_types.h"
#include "ui/move_ui.h"
#include "hrv_algos.h"

LOG_MODULE_REGISTER(ppg_scr_hrv);

#define PPG_RAW_WINDOW_SIZE 128
#define PPG_SIGNAL_RED 0
#define PPG_SIGNAL_IR 1
#define PPG_SIGNAL_GREEN 2
#define PPG_SIGNAL_TIMEOUT_MS 10000  

K_MUTEX_DEFINE(timer_state_mutex_for_hrv);

// GUI Charts
static lv_obj_t *chart_ppg_hrv;
static lv_chart_series_t *ser_ppg_hrv;

// GUI Labels
static lv_obj_t *label_ppg_hr_hrv;
static lv_obj_t *label_rr_interval_hrv;
static lv_obj_t *label_ppg_no_signal_hrv;
static lv_obj_t *arc_hrv_zone;
lv_obj_t *scr_raw_ppg_hrv;
lv_obj_t *scr_ppg_outgauge_hrv;
lv_obj_t * label_hrv_timer;
lv_obj_t * label_hrv_intervals_collected;
lv_obj_t *label_hrv_bpm;
lv_obj_t * label_hrv_info;

extern lv_style_t style_red_medium;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;

static uint32_t batch_count = 0;
static int32_t batch_data[32] __attribute__((aligned(4))); // Batch buffer for efficiency
static bool lead_on_detected = false;


static float y_max_ppg_hrv = 0;
static float y_min_ppg_hrv = 10000;

float y2_max_hrv = 0;
float y2_min_hrv = 10000;

float y3_max_hrv = 0;
float y3_min_hrv = 10000;

static float gx_hrv = 0;

bool check_gesture = false;
extern bool collecting;
static bool timer_running = false;
static bool timer_paused = true;  // Start paused, wait for lead ON

static const uint32_t RANGE_UPDATE_INTERVAL = 128; // Update range every 64 samples - Less frequent for better performance
// Signal detection and timeout tracking
static uint32_t last_ppg_data_time = 0;
static enum hpi_ppg_status last_scd_state = HPI_PPG_SCD_STATUS_UNKNOWN;

// Performance optimization variables - LVGL 9.2 optimized
static uint32_t sample_counter = 0;
uint8_t ppg_disp_signal_type_hrv = PPG_SIGNAL_RED;
// static bool hrv_timer_running = false;
static bool hrv_timer_paused = true;
// static bool hrv_measurement_active = false;


void draw_scr_spl_raw_ppg_hrv(enum scroll_dir m_scroll_dir,uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_raw_ppg_hrv = lv_obj_create(NULL);

    // AMOLED optimization: pure black background
    lv_obj_set_style_bg_color(scr_raw_ppg_hrv, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_clear_flag(scr_raw_ppg_hrv, LV_OBJ_FLAG_SCROLLABLE);

    // ---------------- TIMER ARC ----------------
    arc_hrv_zone = lv_arc_create(scr_raw_ppg_hrv);
    lv_obj_set_size(arc_hrv_zone, 370, 370);  
    lv_obj_center(arc_hrv_zone);
    lv_arc_set_range(arc_hrv_zone, 0, 30);   

    // Configure background and progress arcs 
    lv_arc_set_bg_angles(arc_hrv_zone, 135, 405);
    lv_arc_set_value(arc_hrv_zone, 30);           
    lv_obj_set_style_arc_color(arc_hrv_zone, lv_color_hex(0x808080), LV_PART_MAIN);    
    lv_obj_set_style_arc_width(arc_hrv_zone, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_hrv_zone, lv_color_hex(0x666666), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc_hrv_zone, 6, LV_PART_INDICATOR);
    lv_obj_remove_style(arc_hrv_zone, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc_hrv_zone, LV_OBJ_FLAG_CLICKABLE);

    // ---------------- TIMER CONTAINER ----------------
    lv_obj_t *cont_timer = lv_obj_create(scr_raw_ppg_hrv);
    lv_obj_set_size(cont_timer, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(cont_timer, LV_ALIGN_TOP_MID, 0, 85);
    lv_obj_set_style_bg_opa(cont_timer, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_timer, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont_timer, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont_timer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_timer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Timer icon
    LV_IMG_DECLARE(timer_32);
    lv_obj_t *img_timer = lv_img_create(cont_timer);
    lv_img_set_src(img_timer, &timer_32);
    lv_obj_set_style_img_recolor(img_timer, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(img_timer, LV_OPA_COVER, LV_PART_MAIN);

    // Timer value
    label_hrv_timer = lv_label_create(cont_timer);
    lv_label_set_text(label_hrv_timer, "0");
    lv_obj_add_style(label_hrv_timer, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_hrv_timer, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_pad_left(label_hrv_timer, 8, LV_PART_MAIN);

    label_hrv_intervals_collected = lv_label_create(cont_timer);
    lv_label_set_text(label_hrv_intervals_collected, "/30 RR");
    lv_obj_add_style(label_hrv_intervals_collected, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_hrv_intervals_collected, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_pad_left(label_hrv_intervals_collected, 4, LV_PART_MAIN);

    // Timer unit
    lv_obj_t *label_timer_unit = lv_label_create(cont_timer);
    lv_label_set_text(label_timer_unit, "Collected");
    lv_obj_add_style(label_timer_unit, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_timer_unit, lv_color_hex(0xFFFFFF), LV_PART_MAIN);

    // // Initialize timer state
    // hrv_timer_running = false;
    // hrv_timer_paused  = true;
    // hrv_measurement_active = false;

    //  PPG CHART 
    chart_ppg_hrv = lv_chart_create(scr_raw_ppg_hrv);
    lv_obj_set_size(chart_ppg_hrv, 340, 100);
    lv_obj_align(chart_ppg_hrv, LV_ALIGN_CENTER, 0, -10);

    lv_chart_set_type(chart_ppg_hrv, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart_ppg_hrv, PPG_RAW_WINDOW_SIZE);
    lv_chart_set_update_mode(chart_ppg_hrv, LV_CHART_UPDATE_MODE_CIRCULAR);
    lv_chart_set_range(chart_ppg_hrv, LV_CHART_AXIS_PRIMARY_Y, -5000, 5000);
    lv_chart_set_div_line_count(chart_ppg_hrv, 0, 0);

    // Style chart (transparent for AMOLED)
    lv_obj_set_style_bg_opa(chart_ppg_hrv, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(chart_ppg_hrv, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(chart_ppg_hrv, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(chart_ppg_hrv, 5, LV_PART_MAIN);

    // Add HRV PPG data series
    ser_ppg_hrv = lv_chart_add_series(chart_ppg_hrv, lv_color_hex(0x8B0000), LV_CHART_AXIS_PRIMARY_Y);

    // Configure line style
    lv_obj_set_style_line_width(chart_ppg_hrv, 3, LV_PART_ITEMS);
    lv_obj_set_style_line_color(chart_ppg_hrv, lv_color_hex(0xFFFFFF), LV_PART_ITEMS);
    lv_obj_set_style_line_opa(chart_ppg_hrv, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_line_rounded(chart_ppg_hrv, false, LV_PART_ITEMS);

    // Disable points for smoother plot
    lv_obj_set_style_width(chart_ppg_hrv, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(chart_ppg_hrv, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(chart_ppg_hrv, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_border_opa(chart_ppg_hrv, LV_OPA_TRANSP, LV_PART_INDICATOR);

    lv_obj_add_flag(chart_ppg_hrv, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(chart_ppg_hrv, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(chart_ppg_hrv, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_chart_set_all_value(chart_ppg_hrv, ser_ppg_hrv, 0);

    // HR LABEL BELOW CHART 
    lv_obj_t *cont_hrv = lv_obj_create(scr_raw_ppg_hrv);
    lv_obj_set_size(cont_hrv, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(cont_hrv, LV_ALIGN_CENTER, 0, 75);
    lv_obj_set_style_bg_opa(cont_hrv, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_hrv, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont_hrv, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont_hrv, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_hrv, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Heart Icon (blue accent)
    lv_obj_t *img_heart = lv_img_create(cont_hrv);
    lv_img_set_src(img_heart, &img_heart_48px);
    lv_obj_set_style_img_recolor(img_heart, lv_color_hex(0x8B0000), LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(img_heart, LV_OPA_COVER, LV_PART_MAIN);

    // HRV Value
    
    label_ppg_hr_hrv = lv_label_create(cont_hrv);
    lv_label_set_text(label_ppg_hr_hrv, "--");
    lv_obj_add_style(label_ppg_hr_hrv, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_ppg_hr_hrv, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_pad_left(label_ppg_hr_hrv, 8, LV_PART_MAIN);

    // BPM unit
    lv_obj_t *label_bpm_unit = lv_label_create(cont_hrv);
    lv_label_set_text(label_bpm_unit, "BPM");
    lv_obj_add_style(label_bpm_unit, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_bpm_unit, lv_color_hex(0xFFFFFF), LV_PART_MAIN);



    // ---------------- RR INTERVAL LABEL BELOW CHART ----------------
    lv_obj_t *cont_hrv_rr = lv_obj_create(scr_raw_ppg_hrv);
    lv_obj_set_size(cont_hrv_rr, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(cont_hrv_rr, LV_ALIGN_CENTER, 0, 115);
    lv_obj_set_style_bg_opa(cont_hrv_rr, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_hrv_rr, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont_hrv_rr, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont_hrv_rr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_hrv_rr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // // RR Interval Value

    // label_rr_interval_hrv = lv_label_create(cont_hrv_rr);
    // lv_label_set_text(label_rr_interval_hrv, "--");
    // lv_obj_add_style(label_rr_interval_hrv, &style_body_medium, LV_PART_MAIN);
    // lv_obj_set_style_text_color(label_rr_interval_hrv, lv_color_white(), LV_PART_MAIN);
    // lv_obj_set_style_pad_left(label_rr_interval_hrv, 8, LV_PART_MAIN);

    // lv_obj_t * label_rr_unit = lv_label_create(cont_hrv_rr);
    // lv_label_set_text(label_rr_unit, "ms");
    // lv_obj_add_style(label_rr_unit, &style_caption, LV_PART_MAIN);
    // lv_obj_set_style_text_color(label_rr_unit, lv_color_hex(0xFFFFFF), LV_PART_MAIN);

     // Create "No Signal" overlay label (initially hidden)
    label_ppg_no_signal_hrv = lv_label_create(scr_raw_ppg_hrv);
    lv_label_set_text(label_ppg_no_signal_hrv, "No Signal");
    lv_obj_align(label_ppg_no_signal_hrv, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_style_text_color(label_ppg_no_signal_hrv, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_add_style(label_ppg_no_signal_hrv, &style_white_medium, 0);
    lv_obj_add_flag(label_ppg_no_signal_hrv, LV_OBJ_FLAG_HIDDEN); // Start hidden

    // Initialize timestamp and SCD state
    last_ppg_data_time = k_uptime_get_32();
    last_scd_state = HPI_PPG_SCD_STATUS_UNKNOWN;

    // Reset min/max tracking for fresh autoscaling
    y_min_ppg_hrv = 10000;
    y_max_ppg_hrv = 0;
    gx_hrv = 0;
    
    // Reset autoscale state to ensure first update happens
    hpi_ppg_autoscale_reset();


    // Gesture handler
    lv_obj_add_event_cb(scr_raw_ppg_hrv, gesture_handler_for_ppg, LV_EVENT_GESTURE, NULL);

   
    hpi_disp_set_curr_screen(SCR_SPL_HRV_PLOT);
    hpi_show_screen(scr_raw_ppg_hrv, m_scroll_dir);
}

void hpi_ppg_disp_update_rr_interval_hrv(int rr_interval)
{
    if (label_rr_interval_hrv == NULL)
        return;

    char buf[32];
    
    sprintf(buf, "%d", rr_interval);
    lv_label_set_text(label_rr_interval_hrv, buf);
}

void hpi_ppg_disp_update_hr_hrv(int hr)
{
    if (label_ppg_hr_hrv == NULL)
        return;

    char buf[32];
    if (hr == 0)
    {
        sprintf(buf, "--");
    }
    else
    {
        sprintf(buf, "%d", hr);
    }
   
    lv_label_set_text(label_ppg_hr_hrv, buf);
}

static void hpi_ppg_disp_add_samples_hrv(int num_samples)
{
    gx_hrv += num_samples;
}

/* Delegate autoscale to shared helper to keep behavior consistent across screens */
static void hpi_ppg_disp_do_set_scale_hrv(int disp_window_size)
{
    hpi_ppg_disp_do_set_scale_shared(chart_ppg_hrv, &y_min_ppg_hrv, &y_max_ppg_hrv, &gx_hrv, disp_window_size);
}

// /* Update "No Signal" label visibility based on data presence and SCD status */
static void hpi_ppg_update_signal_status_hrv(enum hpi_ppg_status scd_state)
{
    if (label_ppg_no_signal_hrv == NULL)
        return;

    uint32_t current_time = k_uptime_get_32();
    bool timeout = (current_time - last_ppg_data_time) > PPG_SIGNAL_TIMEOUT_MS;
    bool no_skin_contact = (scd_state == HPI_PPG_SCD_OFF_SKIN);

    // Show "No Signal" if timeout OR no skin contact (but ONLY check skin contact if we have valid state)
    if (timeout || no_skin_contact)
    {
        lv_obj_clear_flag(label_ppg_no_signal_hrv, LV_OBJ_FLAG_HIDDEN);
        
        // Update message based on reason - prioritize skin contact message when both conditions exist
        if (no_skin_contact)
        {
            lv_label_set_text(label_ppg_no_signal_hrv, "No Skin Contact");
        }
        else if (timeout)
        {
            lv_label_set_text(label_ppg_no_signal_hrv, "No Signal");
        }
    }
    else
    {
        lv_obj_add_flag(label_ppg_no_signal_hrv, LV_OBJ_FLAG_HIDDEN);
    }
}

void hpi_disp_ppg_draw_plotPPG_hrv(struct hpi_ppg_wr_data_t ppg_sensor_sample)
{
    // Update last data received timestamp
    last_ppg_data_time = k_uptime_get_32();

    // Store the SCD state for use in periodic timeout checks
    last_scd_state = ppg_sensor_sample.scd_state;

    // Update signal status based on SCD state
    hpi_ppg_update_signal_status_hrv(ppg_sensor_sample.scd_state);

    uint32_t *data_ppg = ppg_sensor_sample.raw_green;

    // Find min/max in current batch for accurate tracking
    uint32_t batch_min = UINT32_MAX;
    uint32_t batch_max = 0;
    
    for (int i = 0; i < ppg_sensor_sample.ppg_num_samples; i++)
    {
        if (data_ppg[i] < batch_min) batch_min = data_ppg[i];
        if (data_ppg[i] > batch_max) batch_max = data_ppg[i];
    }
    
    // Update global min/max with batch values
    if (y_min_ppg_hrv == 10000)
    {
        y_min_ppg_hrv = batch_min;
    }
    else
    {
        if (batch_min < y_min_ppg_hrv) y_min_ppg_hrv = batch_min;
    }
    
    if (y_max_ppg_hrv == 0)
    {
        y_max_ppg_hrv = batch_max;
    }
    else
    {
        if (batch_max > y_max_ppg_hrv) y_max_ppg_hrv = batch_max;
    }

    // Plot all samples
    for (int i = 0; i < ppg_sensor_sample.ppg_num_samples; i++)
    {
        lv_chart_set_next_value(chart_ppg_hrv, ser_ppg_hrv, data_ppg[i]);
        hpi_ppg_disp_add_samples_hrv(1);
        sample_counter++;
    }
    
    // Call autoscale once per batch, not per sample, for better performance
    hpi_ppg_disp_do_set_scale_hrv(PPG_RAW_WINDOW_SIZE);
}

/* Public function to check for signal timeout - called periodically by display SM */
void hpi_ppg_check_signal_timeout_hrv(void)
{
    // Use the last known SCD state for timeout checks
    // This way we only show timeout, not incorrectly assuming "no skin contact"
   hpi_ppg_update_signal_status_hrv(last_scd_state);
}
void hpi_hrv_disp_update_timer(int time_left)
{
    if (label_hrv_timer == NULL)
        return;

    // Optimize timer updates with caching
    static int last_time = -1;
    static char time_buf[8];
    
    if (time_left != last_time)
    { 
            
         // Use direct integer to string for better performance
            if (time_left < 10) 
            {
                time_buf[0] = '0' + time_left;
                time_buf[1] = '\0';
            } 
            else if (time_left < 100) 
            {
                time_buf[0] = '0' + (time_left / 10);
                time_buf[1] = '0' + (time_left % 10);
                time_buf[2] = '\0';
            } 
            else 
            {
                time_buf[0] = '0' + (time_left / 100);
                time_buf[1] = '0' + ((time_left / 10) % 10);
                time_buf[2] = '0' + (time_left % 10);
                time_buf[3] = '\0';
            }
            
            lv_label_set_text(label_hrv_timer, time_buf);
            
            // Update the progress arc to show progress towards completion
            if (arc_hrv_zone != NULL) {
                // Show progress: empty at start (30s), full at end (0s)
               
                lv_arc_set_range(arc_hrv_zone, 0, 30); 
                lv_arc_set_value(arc_hrv_zone, time_left);
                lv_arc_set_bg_angles(arc_hrv_zone, 135, 405);

               // lv_arc_set_value(arc_hrv_zone, arc_value);
                

                // Thread-safe access to timer_paused
                k_mutex_lock(&timer_state_mutex_for_hrv, K_FOREVER);
                bool is_paused = hrv_timer_paused;
                k_mutex_unlock(&timer_state_mutex_for_hrv);
                
                if (is_paused) {
                    lv_obj_set_style_arc_color(arc_hrv_zone, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR);  
                } else {
                    lv_obj_set_style_arc_color(arc_hrv_zone, lv_color_hex(0x830000), LV_PART_INDICATOR); 
                }
            
        }
        
        last_time = time_left;
    }
}

// void hpi_ecg_disp_draw_plotECG_for_hrv(int32_t *data_ecg, int num_samples, bool ecg_lead_off)
// {
//     // Early validation - LVGL 9.2 best practice
//     if ( chart_ppg_hrv == NULL || ser_ppg_hrv == NULL || data_ecg == NULL || num_samples <= 0) {
//         LOG_INF("Label null");
//         return;
//     }

//     // Performance optimization: Skip processing if chart is hidden
//     if (lv_obj_has_flag(chart_ppg_hrv, LV_OBJ_FLAG_HIDDEN)) {
//         LOG_INF("Chart is hidden");
//         return;
//     }

//     // Batch processing for efficiency
//     for (int i = 0; i < num_samples; i++)
//     {
//         batch_data[batch_count++] = data_ecg[i];
        
//         // Process batch when full or at end of samples
//         if (batch_count >= 32 || i == num_samples - 1) {
//             // Process batch
//             for (uint32_t j = 0; j < batch_count; j++) {
//                 lv_chart_set_next_value(chart_ppg_hrv, ser_ppg_hrv, batch_data[j]);
                
//                 // Track min/max for auto-scaling
//                 if (batch_data[j] < y_min_ppg_hrv) y_min_ppg_hrv = batch_data[j];
//                 if (batch_data[j] > y_max_ppg_hrv) y_max_ppg_hrv= batch_data[j];
//             }
            
//             sample_counter += batch_count; // Fix: Update sample counter correctly
//             batch_count = 0;
//         }
//     }
    
//     // Auto-scaling logic
//     if (sample_counter % RANGE_UPDATE_INTERVAL == 0) {
//         if (y_max_ppg_hrv > y_min_ppg_hrv) {
//             float range = y_max_ppg_hrv - y_min_ppg_hrv;
//             float margin = range * 0.1f; // 10% margin
            
//             int32_t new_min = (int32_t)(y_min_ppg_hrv - margin);
//             int32_t new_max = (int32_t)(y_max_ppg_hrv + margin);
            
//             // Ensure reasonable minimum range
//             if ((new_max - new_min) < 1000) {
//                 int32_t center = (new_min + new_max) / 2;
//                 new_min = center - 500;
//                 new_max = center + 500;
//             }
            
//             lv_chart_set_range(chart_ppg_hrv, LV_CHART_AXIS_PRIMARY_Y, new_min, new_max);
//         }
        
//         // Reset for next interval
//         y_min_ppg_hrv = 10000;
//         y_max_ppg_hrv = -10000;
//     }
// }

// void scr_ecg_lead_on_off_handler_for_hrv(bool lead_on_off)
// {
//     LOG_INF("Screen handler called with lead_on_off=%s", lead_on_off ? "OFF" : "ON");
    
//     if (label_ppg_no_signal_hrv == NULL) {
//         LOG_WRN("label_info is NULL, screen handler returning early");
//         return;
//     }

//     // Update lead state tracking (thread-safe)
//     k_mutex_lock(&timer_state_mutex_for_hrv, K_FOREVER);
//     lead_on_detected = !lead_on_off;  // ecg_lead_off == 0 means leads are ON
//     bool current_timer_running = timer_running;
//     bool current_timer_paused = timer_paused;
//     k_mutex_unlock(&timer_state_mutex_for_hrv);

//     if (lead_on_off == false)  // Lead ON condition (ecg_lead_off == false)
//     {
//         LOG_INF("Handling Lead ON: hiding info, showing chart, starting timer");
        
//         // Check if timer value indicates stabilization phase (>30s means stabilizing)
//         // During stabilization, show a message instead of hiding the label
//         // This will be updated by timer display function based on progress_timer value
        
//         lv_obj_clear_flag(chart_ppg_hrv, LV_OBJ_FLAG_HIDDEN);
//         // Don't hide label_info yet - will be handled by timer display based on countdown
//     }
//     else  // Lead OFF condition (ecg_lead_off == true)
//     {
//         LOG_INF("Handling Lead OFF: showing info, hiding chart, pausing timer");
//         lv_obj_clear_flag(label_ppg_no_signal_hrv, LV_OBJ_FLAG_HIDDEN);
//         lv_obj_add_flag(chart_ppg_hrv, LV_OBJ_FLAG_HIDDEN);
        
//         // Update message based on timer state
//         if (current_timer_running && !current_timer_paused) {
//             lv_label_set_text(label_ppg_no_signal_hrv, "Leads disconnected\nTimer paused - reconnect to continue");
//         } else {
//             lv_label_set_text(label_ppg_no_signal_hrv, "Place fingers on electrodes\nTimer will start automatically");
//         }
//     }
// }

void gesture_handler_for_ppg(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_BOTTOM) {
        gesture_down_scr_spl_ppg_for_hrv();
    }
}

void gesture_down_scr_spl_ppg_for_hrv(void)
{
    printk("Exit HRV Frequency Compact\n");
    check_gesture = true;
    collecting = false;
    hpi_load_screen(SCR_HRV, SCROLL_DOWN);
}

// void hpi_ecg_timer_start_for_hrv(void)
// {
//     k_mutex_lock(&timer_state_mutex_for_hrv, K_FOREVER);
//     timer_running = true;
//     timer_paused = false;
//     k_mutex_unlock(&timer_state_mutex_for_hrv);
    
//    /* LOG_INF("ECG timer STARTED - leads detected (running=%s, paused=%s)", 
//             timer_running ? "true" : "false", timer_paused ? "true" : "false");*/
// }

// void hpi_ecg_timer_pause_for_hrv(void)
// {
//     k_mutex_lock(&timer_state_mutex_for_hrv, K_FOREVER);
//     timer_paused = true;
//     k_mutex_unlock(&timer_state_mutex_for_hrv);
    
//    /* LOG_INF("ECG timer paused - leads off (running=%s, paused=%s)",
//             timer_running ? "true" : "false", timer_paused ? "true" : "false");*/
// }

// void hpi_ecg_timer_reset_for_hrv(void)
// {
//     k_mutex_lock(&timer_state_mutex_for_hrv, K_FOREVER);
//     timer_running = false;
//     timer_paused = true;
//     lead_on_detected = false;
//     k_mutex_unlock(&timer_state_mutex_for_hrv);
    
//    /* LOG_INF("ECG timer RESET - ready for fresh start (running=%s, paused=%s)",
//             timer_running ? "true" : "false", timer_paused ? "true" : "false");*/
// }

// bool hpi_ecg_timer_is_running_for_hrv(void)
// {
//     k_mutex_lock(&timer_state_mutex_for_hrv, K_FOREVER);
//     bool is_running = timer_running && !timer_paused;
//     k_mutex_unlock(&timer_state_mutex_for_hrv);
    
//    /* LOG_DBG("Timer status check: running=%s, paused=%s, is_running=%s",
//             timer_running ? "true" : "false", 
//             timer_paused ? "true" : "false",
//             is_running ? "true" : "false");*/
//     return is_running;
// }