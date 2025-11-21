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
#include "hpi_sys.h"

LOG_MODULE_REGISTER(Ecg_scr_hrv);

// Mutex for thread-safe timer state access
K_MUTEX_DEFINE(timer_state_mutex_hrv);

static lv_obj_t *scr_ecg_hrv;
// static lv_obj_t *btn_ecg_cancel;  // Commented out - not used
static lv_obj_t *chart_ecg_hrv;
static lv_chart_series_t *ser_ecg_hrv;
static lv_obj_t *label_ecg_hr_hrv;
static lv_obj_t *label_timer_hrv;
static lv_obj_t *label_ecg_lead_off_hrv;
static lv_obj_t *label_info_hrv;
static lv_obj_t *arc_ecg_zone_hrv;  // Progress arc for measurement duration

static bool chart_ecg_hrv_update_hrv = true;
static float y_max_ecg_hrv = -10000;
static float y_min_ecg_hrv = 10000;

// static bool ecg_plot_hidden = false;

static float gx_hrv = 0;

// Timer control variables for lead-based automatic start/stop
static bool timer_running = false;
static bool timer_paused = true;  // Start paused, wait for lead ON
static bool lead_on_detected = false;

// Performance optimization variables - LVGL 9.2 optimized
static uint32_t sample_counter = 0;
static const uint32_t RANGE_UPDATE_INTERVAL = 128; // Update range every 64 samples - Less frequent for better performance

// High-performance batch processing buffer - aligned for LVGL 9.2
static int32_t batch_data[32] __attribute__((aligned(4))); // Batch buffer for efficiency
static uint32_t batch_count = 0;

// LVGL 9.2 Chart performance configuration flags
static bool chart_auto_refresh_enabled = true;

// Function declarations for LVGL 9.2 optimized chart management
static void ecg_chart_enable_performance_mode_hrv(bool enable);
static void ecg_chart_reset_performance_counters_hrv(void);

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_large_numeric;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_tiny;

extern lv_style_t style_bg_blue;
extern lv_style_t style_bg_red;

extern struct k_sem sem_ecg_cancel;

extern bool collecting;
extern bool check_gesture;
extern bool hrv_active;

// Commented out - unused function
                  
/*
static void btn_ecg_cancel_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        hpi_load_screen(SCR_ECG, SCROLL_DOWN);
    }
}
*/

void draw_scr_ecg_hrv(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_ecg_hrv = lv_obj_create(NULL);
    // AMOLED OPTIMIZATION: Pure black background for power efficiency
    lv_obj_set_style_bg_color(scr_ecg_hrv, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_ecg_hrv, LV_OBJ_FLAG_SCROLLABLE);

    // CIRCULAR AMOLED-OPTIMIZED ECG MEASUREMENT SCREEN
    // Display center: (195, 195), Usable radius: ~185px
    // Orange/amber theme for ECG measurement consistency

    // Get ECG/HR data
    uint16_t hr = 0;
    int64_t hr_last_update = 0;
    if (hpi_sys_get_last_hr_update(&hr, &hr_last_update) != 0) {
        hr = 0;
        hr_last_update = 0;
    }

    // OUTER RING: ECG Timer Countdown Arc (Radius 170-185px) - Orange theme for measurement
    arc_ecg_zone_hrv= lv_arc_create(scr_ecg_hrv);
    lv_obj_set_size(arc_ecg_zone_hrv, 370, 370);  // 185px radius
    lv_obj_center(arc_ecg_zone_hrv);
    lv_arc_set_range(arc_ecg_zone_hrv, 0, 30);  // Timer range: 0-30 seconds
    
    // Background arc: Full 270° track (gray)
    lv_arc_set_bg_angles(arc_ecg_zone_hrv, 135, 45);  // Full background arc
    lv_arc_set_value(arc_ecg_zone_hrv, 30);  // Start at full (30 seconds), will countdown to 0
    
    // Style the progress arc - orange theme for ECG measurement
    lv_obj_set_style_arc_color(arc_ecg_zone_hrv, lv_color_hex(0x808080), LV_PART_MAIN);    // Background track
    lv_obj_set_style_arc_width(arc_ecg_zone_hrv, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_ecg_zone_hrv, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR);  // Orange progress
    lv_obj_set_style_arc_width(arc_ecg_zone_hrv, 6, LV_PART_INDICATOR);
    lv_obj_remove_style(arc_ecg_zone_hrv, NULL, LV_PART_KNOB);  // Remove knob
    lv_obj_clear_flag(arc_ecg_zone_hrv, LV_OBJ_FLAG_CLICKABLE);

    // Screen title - properly positioned to avoid arc overlap
    // MID-UPPER RING: Timer container with icon (following design pattern)
    lv_obj_t *cont_timer_hrv = lv_obj_create(scr_ecg_hrv);
    lv_obj_set_size(cont_timer_hrv, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(cont_timer_hrv, LV_ALIGN_TOP_MID, 0, 85);
    lv_obj_set_style_bg_opa(cont_timer_hrv, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_timer_hrv, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont_timer_hrv, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont_timer_hrv, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_timer_hrv, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Timer Icon
    LV_IMG_DECLARE(timer_32);
    lv_obj_t *img_timer_hrv = lv_img_create(cont_timer_hrv);
    lv_img_set_src(img_timer_hrv, &timer_32);
    lv_obj_set_style_img_recolor(img_timer_hrv, lv_color_hex(0xFFFFFF), LV_PART_MAIN);  // Orange theme
    lv_obj_set_style_img_recolor_opa(img_timer_hrv, LV_OPA_COVER, LV_PART_MAIN);

    // Timer value
    label_timer_hrv = lv_label_create(cont_timer_hrv);
    lv_label_set_text(label_timer_hrv, "0");
    lv_obj_add_style(label_timer_hrv, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_timer_hrv, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_pad_left(label_timer_hrv, 8, LV_PART_MAIN);

    // Timer unit
    lv_obj_t *label_timer_hrv_unit = lv_label_create(cont_timer_hrv);
    lv_label_set_text(label_timer_hrv_unit, "/30 RR collected ");
    lv_obj_add_style(label_timer_hrv_unit, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_timer_hrv_unit, lv_color_hex(0xFFFFFF), LV_PART_MAIN);  // Orange accent

    // Initialize timer state - start paused, waiting for lead ON detection
    timer_running = false;
    timer_paused = true;
    lead_on_detected = false;

    // CENTRAL ZONE: ECG Chart (positioned in center area)
    chart_ecg_hrv = lv_chart_create(scr_ecg_hrv);
    lv_obj_set_size(chart_ecg_hrv, 340, 100);  // Smaller chart for circular design
    lv_obj_align(chart_ecg_hrv, LV_ALIGN_CENTER, 0, -10);  // Centered position
    
    // Configure chart type and fundamental properties
    lv_chart_set_type(chart_ecg_hrv, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart_ecg_hrv, ECG_DISP_WINDOW_SIZE);
    lv_chart_set_update_mode(chart_ecg_hrv, LV_CHART_UPDATE_MODE_CIRCULAR);  // ECG-like behavior
    
    // Set Y-axis range for ECG data - start with reasonable defaults
    lv_chart_set_range(chart_ecg_hrv, LV_CHART_AXIS_PRIMARY_Y, -5000, 5000);
    
    // Disable division lines for clean ECG display
    lv_chart_set_div_line_count(chart_ecg_hrv, 0, 0);
    
    // Configure main chart background (transparent for AMOLED)
    lv_obj_set_style_bg_opa(chart_ecg_hrv, LV_OPA_TRANSP, LV_PART_MAIN);  // Transparent background
    lv_obj_set_style_border_width(chart_ecg_hrv, 0, LV_PART_MAIN);        // No border
    lv_obj_set_style_outline_width(chart_ecg_hrv, 0, LV_PART_MAIN);       // No outline
    lv_obj_set_style_pad_all(chart_ecg_hrv, 5, LV_PART_MAIN);             // Minimal padding
    
    // Create series for ECG data
    ser_ecg_hrv = lv_chart_add_series(chart_ecg_hrv, lv_color_hex(COLOR_CRITICAL_RED), LV_CHART_AXIS_PRIMARY_Y);
    
    // Configure line series styling - orange theme
    lv_obj_set_style_line_width(chart_ecg_hrv, 3, LV_PART_ITEMS);         // Increased line width for better visibility
    lv_obj_set_style_line_color(chart_ecg_hrv, lv_color_hex(0xFF8C00), LV_PART_ITEMS);
    lv_obj_set_style_line_opa(chart_ecg_hrv, LV_OPA_COVER, LV_PART_ITEMS); // Full opacity for medical clarity
    lv_obj_set_style_line_rounded(chart_ecg_hrv, false, LV_PART_ITEMS);   // Sharp lines for precision
    
    // Disable points completely
    lv_obj_set_style_width(chart_ecg_hrv, 0, LV_PART_INDICATOR);          // No point width
    lv_obj_set_style_height(chart_ecg_hrv, 0, LV_PART_INDICATOR);         // No point height
    lv_obj_set_style_bg_opa(chart_ecg_hrv, LV_OPA_TRANSP, LV_PART_INDICATOR); // Transparent points
    lv_obj_set_style_border_opa(chart_ecg_hrv, LV_OPA_TRANSP, LV_PART_INDICATOR); // No point borders
    
    // Performance optimizations for real-time ECG display
    lv_obj_add_flag(chart_ecg_hrv, LV_OBJ_FLAG_IGNORE_LAYOUT);           // Skip layout calculations
    lv_obj_clear_flag(chart_ecg_hrv, LV_OBJ_FLAG_SCROLLABLE);            // Disable scrolling
    lv_obj_clear_flag(chart_ecg_hrv, LV_OBJ_FLAG_CLICK_FOCUSABLE);       // No focus events
    
    // Initialize chart with baseline values
    lv_chart_set_all_value(chart_ecg_hrv, ser_ecg_hrv, 0);
    
    // HR Container below chart (following design pattern)
    lv_obj_t *cont_hr_hrv = lv_obj_create(scr_ecg_hrv);
    lv_obj_set_size(cont_hr_hrv, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(cont_hr_hrv, LV_ALIGN_CENTER, 0, 75);  // Below chart
    lv_obj_set_style_bg_opa(cont_hr_hrv, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_hr_hrv, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont_hr_hrv, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont_hr_hrv, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_hr_hrv, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Heart Icon
    lv_obj_t *img_heart_hrv = lv_img_create(cont_hr_hrv);
    lv_img_set_src(img_heart_hrv, &img_heart_48px);
    lv_obj_set_style_img_recolor(img_heart_hrv, lv_color_hex(COLOR_CRITICAL_RED), LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(img_heart_hrv, LV_OPA_COVER, LV_PART_MAIN);

    // HR Value
    label_ecg_hr_hrv = lv_label_create(cont_hr_hrv);
    if (hr == 0) {
        lv_label_set_text(label_ecg_hr_hrv, "--");
    } else {
        lv_label_set_text_fmt(label_ecg_hr_hrv, "%d", hr);
    }
    lv_obj_add_style(label_ecg_hr_hrv, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_ecg_hr_hrv, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_pad_left(label_ecg_hr_hrv, 8, LV_PART_MAIN);

    // HR Unit
    lv_obj_t *label_hr_unit_hrv = lv_label_create(cont_hr_hrv);
    lv_label_set_text(label_hr_unit_hrv, "BPM");
    lv_obj_add_style(label_hr_unit_hrv, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_hr_unit_hrv, lv_color_hex(COLOR_CRITICAL_RED), LV_PART_MAIN);

    // Lead off status label (positioned at bottom)
    label_ecg_lead_off_hrv = lv_label_create(scr_ecg_hrv);
    lv_label_set_long_mode(label_ecg_lead_off_hrv, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_ecg_lead_off_hrv, 300);
    lv_label_set_text(label_ecg_lead_off_hrv, "Place fingers on electrodes\nTimer will start automatically");
    lv_obj_align(label_ecg_lead_off_hrv, LV_ALIGN_CENTER, 0, 0);  // Centered overlay on chart
    lv_obj_add_style(label_ecg_lead_off_hrv, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_ecg_lead_off_hrv, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_ecg_lead_off_hrv, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    
    // Set reference for lead on/off handler
    label_info_hrv = label_ecg_lead_off_hrv;
    
    // Initially HIDE the label - it will be shown by lead-off handler if needed
    lv_obj_add_flag(label_info_hrv, LV_OBJ_FLAG_HIDDEN);

    // Initialize performance optimization system
    ecg_chart_reset_performance_counters_hrv();
    ecg_chart_enable_performance_mode_hrv(true);  // Start in high-performance mode

    hpi_disp_set_curr_screen(SCR_SPL_SCREEN_HRV2);
     int curr_screen = hpi_disp_get_curr_screen();
LOG_INF("Current screen value = %d", curr_screen);
    hpi_show_screen(scr_ecg_hrv, m_scroll_dir);
}

// LVGL 9.2 optimized chart management functions
static void ecg_chart_enable_performance_mode_hrv(bool enable)
{
    if (chart_ecg_hrv == NULL) return;
    
    if (enable) {
        // Enable performance optimizations
        lv_obj_add_flag(chart_ecg_hrv, LV_OBJ_FLAG_IGNORE_LAYOUT);
        chart_auto_refresh_enabled = false;
    } else {
        // Restore normal operation
        lv_obj_clear_flag(chart_ecg_hrv, LV_OBJ_FLAG_IGNORE_LAYOUT);
        chart_auto_refresh_enabled = true;
        lv_chart_refresh(chart_ecg_hrv);
    }
}

static void ecg_chart_reset_performance_counters_hrv(void)
{
    sample_counter = 0;
    batch_count = 0;
    // Initialize for proper range detection
    y_max_ecg_hrv = -10000;
    y_min_ecg_hrv = 10000;
}

// Simplified scaling function - LVGL 9.2 optimized
void hpi_ecg_disp_do_set_scale_hrv(int disp_window_size)
{
    // This function is now simplified as range updating is handled 
    // more efficiently in the main plotting function
    if (gx_hrv >= disp_window_size)
    {
        gx_hrv = 0;
    }
}

// Simplified sample counter - now handled in main plot function  
void hpi_ecg_disp_add_samples_hrv(int num_samples)
{
    // Sample counting is now handled directly in the plot function
    // for better performance
}

void hpi_ecg_disp_update_hr_hrv(int hr)
{
    // Check if we're on the ECG measurement screen (scr2) before updating
    if (hpi_disp_get_curr_screen() != SCR_SPL_SCREEN_HRV2 || label_ecg_hr_hrv == NULL)
        return;

    LOG_INF("Inside hr update function");
    // Use standard sprintf for reliability - avoid custom conversion that can cause font issues
    static char hr_buf[8]; // Static buffer to avoid repeated allocations
    static int last_hr = -1; // Cache last value to avoid unnecessary updates
    
    if (hr != last_hr) { // Only update if value changed
        if (hr == 0)
        {
            // Use standard dashes - the inter_semibold_24 font used by style_white_medium has hyphens
            strcpy(hr_buf, "--");
        }
        else
        {
            // Use standard sprintf for proper character encoding
            snprintf(hr_buf, sizeof(hr_buf), "%d", hr);
        }
        
        lv_label_set_text(label_ecg_hr_hrv, hr_buf);
        last_hr = hr;
    }
}

void hpi_ecg_disp_update_timer_hrv(int time_left)
{
    if (label_timer_hrv == NULL)
        return;

    // Optimize timer updates with caching
    static int last_time = -1;
    static char time_buf[8];
    
    if (time_left != last_time) { // Only update if changed
        // Check if in stabilization phase (time > 30s means we're stabilizing)
        bool is_stabilizing = (time_left > 30);
        
        if (is_stabilizing) {
            // Show stabilization countdown (35s = 5s stabilizing, 30s = starting recording)
            int stabilization_time = time_left - 30;
            
            // Update timer label with stabilization time
            if (stabilization_time < 10) {
                time_buf[0] = '0' + stabilization_time;
                time_buf[1] = '\0';
            } else {
                time_buf[0] = '0' + (stabilization_time / 10);
                time_buf[1] = '0' + (stabilization_time % 10);
                time_buf[2] = '\0';
            }
            lv_label_set_text(label_timer_hrv, time_buf);
            
            // Show stabilization message
            if (label_info_hrv != NULL) {
                lv_label_set_text(label_info_hrv, "Signal stabilizing...\nPlease hold still");
                lv_obj_clear_flag(label_info_hrv, LV_OBJ_FLAG_HIDDEN);
            }
            
            // Arc stays at 0 during stabilization
            if (arc_ecg_zone_hrv != NULL) {
                lv_arc_set_value(arc_ecg_zone_hrv, 0);
                lv_obj_set_style_arc_color(arc_ecg_zone_hrv, lv_color_hex(0x4A90E2), LV_PART_INDICATOR);  // Blue during stabilization
            }
        } else {
            // Normal recording mode
            
            // Hide the info label when recording (leads are on)
            if (label_info_hrv != NULL && time_left > 0) {
                lv_obj_add_flag(label_info_hrv, LV_OBJ_FLAG_HIDDEN);
            }
            
            // Use direct integer to string for better performance
            if (time_left < 10) {
                time_buf[0] = '0' + time_left;
                time_buf[1] = '\0';
            } else if (time_left < 100) {
                time_buf[0] = '0' + (time_left / 10);
                time_buf[1] = '0' + (time_left % 10);
                time_buf[2] = '\0';
            } else {
                time_buf[0] = '0' + (time_left / 100);
                time_buf[1] = '0' + ((time_left / 10) % 10);
                time_buf[2] = '0' + (time_left % 10);
                time_buf[3] = '\0';
            }
            
            lv_label_set_text(label_timer_hrv, time_buf);
            
            // Update the progress arc to show progress towards completion
            if (arc_ecg_zone_hrv != NULL) {
                // Show progress: empty at start (30s), full at end (0s)
                int arc_value = (time_left < 0) ? 30 : ((time_left > 30) ? 0 : (30 - time_left));
                lv_arc_set_value(arc_ecg_zone_hrv, arc_value);
                
                // Change arc color based on timer state: Orange when running, gray when paused
                // Thread-safe access to timer_paused
                k_mutex_lock(&timer_state_mutex_hrv, K_FOREVER);
                bool is_paused = timer_paused;
                k_mutex_unlock(&timer_state_mutex_hrv);
                
                if (is_paused) {
                    lv_obj_set_style_arc_color(arc_ecg_zone_hrv, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR);  // Gray when paused
                } else {
                    lv_obj_set_style_arc_color(arc_ecg_zone_hrv, lv_color_hex(0xFF8C00), LV_PART_INDICATOR);  // Orange when running
                }
            }
        }
        
        last_time = time_left;
    }
}

void hpi_ecg_timer_start_hrv(void)
{
    k_mutex_lock(&timer_state_mutex_hrv, K_FOREVER);
    timer_running = true;
    timer_paused = false;
    k_mutex_unlock(&timer_state_mutex_hrv);
    
   /* LOG_INF("ECG timer STARTED - leads detected (running=%s, paused=%s)", 
            timer_running ? "true" : "false", timer_paused ? "true" : "false");*/
}

void hpi_ecg_timer_pause_hrv(void)
{
    k_mutex_lock(&timer_state_mutex_hrv, K_FOREVER);
    timer_paused = true;
    k_mutex_unlock(&timer_state_mutex_hrv);
    
   /* LOG_INF("ECG timer paused - leads off (running=%s, paused=%s)",
            timer_running ? "true" : "false", timer_paused ? "true" : "false");*/
}

void hpi_ecg_timer_reset_hrv(void)
{
    k_mutex_lock(&timer_state_mutex_hrv, K_FOREVER);
    timer_running = false;
    timer_paused = true;
    lead_on_detected = false;
    k_mutex_unlock(&timer_state_mutex_hrv);
    
   /* LOG_INF("ECG timer RESET - ready for fresh start (running=%s, paused=%s)",
            timer_running ? "true" : "false", timer_paused ? "true" : "false");*/
}

bool hpi_ecg_timer_is_running_hrv(void)
{
    k_mutex_lock(&timer_state_mutex_hrv, K_FOREVER);
    bool is_running = timer_running && !timer_paused;
    k_mutex_unlock(&timer_state_mutex_hrv);
    
   /* LOG_DBG("Timer status check: running=%s, paused=%s, is_running=%s",
            timer_running ? "true" : "false", 
            timer_paused ? "true" : "false",
            is_running ? "true" : "false");*/
    return is_running;
}

void hpi_ecg_disp_draw_plotECG_hrv(int32_t *data_ecg, int num_samples, bool ecg_lead_off)
{
    if (!chart_ecg_hrv || !ser_ecg_hrv || !data_ecg || num_samples <= 0) {
        LOG_WRN("Invalid pointers or data in HRV plot");
        return;
    }
    // Early validation - LVGL 9.2 best practice
    if (chart_ecg_hrv_update_hrv == false || chart_ecg_hrv == NULL || ser_ecg_hrv == NULL || data_ecg == NULL || num_samples <= 0) {
        return;
    }

    // Performance optimization: Skip processing if chart is hidden
    if (lv_obj_has_flag(chart_ecg_hrv, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    // Batch processing for efficiency
    for (int i = 0; i < num_samples; i++)
    {
        batch_data[batch_count++] = data_ecg[i];
        
        // Process batch when full or at end of samples
        if (batch_count >= 32 || i == num_samples - 1) {
            // Process batch
            for (uint32_t j = 0; j < batch_count; j++) {
                lv_chart_set_next_value(chart_ecg_hrv, ser_ecg_hrv, batch_data[j]);
                
                // Track min/max for auto-scaling
                if (batch_data[j] < y_min_ecg_hrv) y_min_ecg_hrv = batch_data[j];
                if (batch_data[j] > y_max_ecg_hrv) y_max_ecg_hrv = batch_data[j];
            }
            
            sample_counter += batch_count; // Fix: Update sample counter correctly
            batch_count = 0;
        }
    }
    
    // Auto-scaling logic
    if (sample_counter % RANGE_UPDATE_INTERVAL == 0) {
        if (y_max_ecg_hrv > y_min_ecg_hrv) {
            float range = y_max_ecg_hrv - y_min_ecg_hrv;
            float margin = range * 0.1f; // 10% margin
            
            int32_t new_min = (int32_t)(y_min_ecg_hrv - margin);
            int32_t new_max = (int32_t)(y_max_ecg_hrv + margin);
            
            // Ensure reasonable minimum range
            if ((new_max - new_min) < 1000) {
                int32_t center = (new_min + new_max) / 2;
                new_min = center - 500;
                new_max = center + 500;
            }
            
            lv_chart_set_range(chart_ecg_hrv, LV_CHART_AXIS_PRIMARY_Y, new_min, new_max);
        }
        
        // Reset for next interval
        y_min_ecg_hrv = 10000;
        y_max_ecg_hrv = -10000;
    }
}

void scr_ecg_lead_on_off_handler_hrv(bool lead_on_off)
{
    LOG_INF("Screen handler called with lead_on_off=%s", lead_on_off ? "OFF" : "ON");
    
    if (label_info_hrv == NULL) {
        LOG_WRN("label_info_hrv is NULL, screen handler returning early");
        return;
    }

    // Update lead state tracking (thread-safe)
    k_mutex_lock(&timer_state_mutex_hrv, K_FOREVER);
    lead_on_detected = !lead_on_off;  // ecg_lead_off == 0 means leads are ON
    bool current_timer_running = timer_running;
    bool current_timer_paused = timer_paused;
    k_mutex_unlock(&timer_state_mutex_hrv);

    if (lead_on_off == false)  // Lead ON condition (ecg_lead_off == false)
    {
        LOG_INF("Handling Lead ON: showing chart and hiding info label");
        
        // Show the chart immediately when leads are detected
        lv_obj_clear_flag(chart_ecg_hrv, LV_OBJ_FLAG_HIDDEN);
        
        // Hide the info label - timer update will manage its visibility during stabilization
        lv_obj_add_flag(label_info_hrv, LV_OBJ_FLAG_HIDDEN);
    }
    else  // Lead OFF condition (ecg_lead_off == true)
    {
        LOG_INF("Handling Lead OFF: showing info label and hiding chart");
        lv_obj_clear_flag(label_info_hrv, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(chart_ecg_hrv, LV_OBJ_FLAG_HIDDEN);
        
        // Update message based on timer state
        if (current_timer_running && !current_timer_paused) {
            lv_label_set_text(label_info_hrv, "Leads disconnected\nTimer paused - reconnect to continue");
        } else {
            lv_label_set_text(label_info_hrv, "Place fingers on electrodes\nTimer will start automatically");
        }
    }
}

void gesture_down_scr_ecg_hrv(void)
{
    printk("Cancel ECG\n");
    LOG_INF("ECG measurement cancelled by user");
    
    // Reset timer state when cancelling
    hpi_ecg_timer_reset_hrv();
    collecting = false;
    check_gesture = true;
    hrv_active = false;
    
    //k_sem_give(&sem_ecg_cancel);
    hpi_load_screen(SCR_HRV_SUMMARY, SCROLL_DOWN);
}
