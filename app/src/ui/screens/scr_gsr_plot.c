/*
 * HealthyPi Move GSR Live Plot Screen
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include "ui/move_ui.h"
#include "hpi_sys.h"

LOG_MODULE_REGISTER(hpi_disp_scr_gsr_plot, LOG_LEVEL_DBG);

// Extern semaphore declarations for GSR control
extern struct k_sem sem_gsr_cancel;

// GUI
static lv_obj_t *scr_gsr_plot;
static lv_obj_t *chart_gsr_trend;
static lv_chart_series_t *ser_gsr_trend;
static lv_obj_t *btn_stop;
static lv_obj_t *label_timer; // shows remaining countdown
static lv_obj_t *arc_gsr_progress; // progress arc for measurement duration
static lv_obj_t *label_gsr_error;   // shows sensor disconnected / lead-off
static lv_obj_t *label_info_gsr; // reference for lead on/off handler
static lv_obj_t *label_gsr_value; // shows real-time GSR value

static bool gsr_contact_present = false; // track GSR contact state

// Styles extern
extern lv_style_t style_scr_black;

// Local state
static bool plot_ready = false;
// Performance optimization variables (mirror ECG plotting implementation)
static float y_max_gsr = -10000;
static float y_min_gsr = 10000;
static uint32_t gsr_sample_counter = 0;
static const uint32_t GSR_RANGE_UPDATE_INTERVAL = 128;
static int32_t gsr_batch_data[32] __attribute__((aligned(4)));
static uint32_t gsr_batch_count = 0;
static bool gsr_chart_auto_refresh_enabled = true;

// Function declarations for LVGL 9.2 optimized chart management
static void gsr_chart_enable_performance_mode(bool enable);
static void gsr_chart_reset_performance_counters(void);

// Simple timer update function (called from display thread, mirrors ECG pattern)
void hpi_gsr_disp_update_timer(uint16_t remaining_s)
{
    if (!plot_ready || !label_timer) return;
    
    // Optimize with caching to avoid unnecessary LVGL updates
    static uint16_t last_remaining = 0xFFFF;
    if (remaining_s != last_remaining) {
        last_remaining = remaining_s;
        lv_label_set_text_fmt(label_timer, "%02u", remaining_s);
        
        // Update the progress arc to show countdown progress
        if (arc_gsr_progress != NULL) {
            // Arc shows progress from full (60s) to empty (0s)
            // Value range: 0-60, display remaining time
            lv_arc_set_value(arc_gsr_progress, remaining_s);
        }
    }
}

void hpi_gsr_disp_update_us(float gsr_us)
{
    // Real-time uS display disabled - final tonic level shown on results screen
    // This function is intentionally left empty
    (void)gsr_us;
}



static void gsr_gesture_event_cb(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    if (dir == LV_DIR_BOTTOM) {
        gesture_down_scr_gsr_plot();
    }
}

static void gsr_touch_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        /* Immediately exit screen */
        gesture_down_scr_gsr_plot();
    }
}

// static void scr_gsr_stop_btn_event_handler(lv_event_t *e)
// {
//     lv_event_code_t code = lv_event_get_code(e);
//     //if (code == LV_EVENT_CLICKED)
//     if (code == LV_EVENT_PRESSED) 
//     {
//         // Stop GSR measurement using semaphore control (same pattern as ECG)
//         hpi_load_screen(SCR_GSR, SCROLL_DOWN);
//         k_sem_give(&sem_gsr_cancel);
//     }
// }
// LVGL 9.2 optimized batch plot function (ECG-like flow adapted for GSR)
void hpi_gsr_disp_draw_plotGSR(int32_t *data_gsr, int num_samples, bool gsr_lead_off)
{

    // Early validation
    if (!plot_ready || chart_gsr_trend == NULL || ser_gsr_trend == NULL || data_gsr == NULL || num_samples <= 0) {
        return;
    }

    // Skip if chart hidden
    if (lv_obj_has_flag(chart_gsr_trend, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }


    // Batch processing for efficiency
    for (int i = 0; i < num_samples; i++) {
        gsr_batch_data[gsr_batch_count++] = data_gsr[i];

        if (gsr_batch_count >= 32 || i == num_samples - 1) {
            for (uint32_t j = 0; j < gsr_batch_count; j++) {
                lv_chart_set_next_value(chart_gsr_trend, ser_gsr_trend, gsr_batch_data[j]);

                if (gsr_batch_data[j] < y_min_gsr) y_min_gsr = gsr_batch_data[j];
                if (gsr_batch_data[j] > y_max_gsr) y_max_gsr = gsr_batch_data[j];
            }

            gsr_sample_counter += gsr_batch_count;
            gsr_batch_count = 0;
        }
    }

    // Auto-scaling logic (follow ECG pattern)
    if (gsr_sample_counter % GSR_RANGE_UPDATE_INTERVAL == 0) {
        if (y_max_gsr > y_min_gsr) {
            float range = y_max_gsr - y_min_gsr;
            float margin = range * 0.1f; // 10% margin

            int32_t new_min = (int32_t)(y_min_gsr - margin);
            int32_t new_max = (int32_t)(y_max_gsr + margin);

            // Ensure reasonable minimum range
            if ((new_max - new_min) < 200) {
                int32_t center = (new_min + new_max) / 2;
                new_min = center - 100;
                new_max = center + 100;
            }

            lv_chart_set_range(chart_gsr_trend, LV_CHART_AXIS_PRIMARY_Y, new_min, new_max);
        }

        // Reset extrema for next interval
        y_min_gsr = 10000;
        y_max_gsr = -10000;
    }

    // Refresh depending on auto-refresh flag (keep behavior similar to ECG)
    if (gsr_chart_auto_refresh_enabled) {
        lv_chart_refresh(chart_gsr_trend);
    }
}

void draw_scr_gsr_plot(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    LV_UNUSED(arg1); LV_UNUSED(arg2); LV_UNUSED(arg3); LV_UNUSED(arg4);

    scr_gsr_plot = lv_obj_create(NULL);
    // AMOLED: solid black background for power savings
    lv_obj_set_style_bg_color(scr_gsr_plot, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_gsr_plot, LV_OBJ_FLAG_SCROLLABLE);
    
    // Progress Arc - outer ring showing countdown from 60s to 0s (blue theme for GSR)
    arc_gsr_progress = lv_arc_create(scr_gsr_plot);
    lv_obj_set_size(arc_gsr_progress, 370, 370);  // 185px radius
    lv_obj_center(arc_gsr_progress);
    lv_arc_set_range(arc_gsr_progress, 0, 30);  // Timer range: 0-30 seconds
    
    // Background arc: Full 270° track (gray)
    lv_arc_set_bg_angles(arc_gsr_progress, 135, 45);  // Full background arc
    lv_arc_set_value(arc_gsr_progress, 30);  // Start at full (30 seconds), will countdown to 0
    
    // Style the progress arc - blue theme for GSR measurement
    lv_obj_set_style_arc_color(arc_gsr_progress, lv_color_hex(0x333333), LV_PART_MAIN);    // Background track
    lv_obj_set_style_arc_width(arc_gsr_progress, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_gsr_progress, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_INDICATOR);  // Blue progress
    lv_obj_set_style_arc_width(arc_gsr_progress, 6, LV_PART_INDICATOR);
    lv_obj_remove_style(arc_gsr_progress, NULL, LV_PART_KNOB);  // Remove knob
    lv_obj_clear_flag(arc_gsr_progress, LV_OBJ_FLAG_CLICKABLE);
   
    // Title - positioned at top center
    lv_obj_t *label_title = lv_label_create(scr_gsr_plot);
    lv_label_set_text(label_title, "GSR");
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

     // Screen title - properly positioned to avoid arc overlap
    // MID-UPPER RING: Timer container with icon (following design pattern)
    lv_obj_t *cont_timer = lv_obj_create(scr_gsr_plot);
    lv_obj_set_size(cont_timer, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(cont_timer, LV_ALIGN_TOP_MID, 0, 85);
    lv_obj_set_style_bg_opa(cont_timer, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_timer, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont_timer, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont_timer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_timer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Timer Icon
    LV_IMG_DECLARE(timer_32);
    lv_obj_t *img_timer = lv_img_create(cont_timer);
    lv_img_set_src(img_timer, &timer_32);
    lv_obj_set_style_img_recolor(img_timer, lv_color_hex(0xFFFFFF), LV_PART_MAIN);  // Orange theme
    lv_obj_set_style_img_recolor_opa(img_timer, LV_OPA_COVER, LV_PART_MAIN);

    // Timer value
    label_timer = lv_label_create(cont_timer);
    lv_label_set_text(label_timer, "30");
    lv_obj_add_style(label_timer, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_timer, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_pad_left(label_timer, 8, LV_PART_MAIN);

    // Timer unit
    lv_obj_t *label_timer_unit = lv_label_create(cont_timer);
    lv_label_set_text(label_timer_unit, "s");
    lv_obj_add_style(label_timer_unit, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_timer_unit, lv_color_hex(0xFFFFFF), LV_PART_MAIN);  // Orange accent

    // Chart - positioned in center area
    chart_gsr_trend = lv_chart_create(scr_gsr_plot);
    lv_obj_set_size(chart_gsr_trend, 340, 120);
    lv_obj_align(chart_gsr_trend, LV_ALIGN_CENTER, 0, -20);

    // Configure chart properties
    lv_chart_set_type(chart_gsr_trend, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart_gsr_trend, GSR_DISP_WINDOW_SIZE);
    lv_chart_set_update_mode(chart_gsr_trend, LV_CHART_UPDATE_MODE_CIRCULAR);
    lv_chart_set_div_line_count(chart_gsr_trend, 0, 0);
    
    // Configure main chart background (transparent for AMOLED)
    lv_obj_set_style_bg_opa(chart_gsr_trend, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(chart_gsr_trend, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(chart_gsr_trend, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(chart_gsr_trend, 5, LV_PART_MAIN);
    
    // Create series for GSR data
    ser_gsr_trend = lv_chart_add_series(chart_gsr_trend, lv_color_hex(COLOR_PRIMARY_BLUE), LV_CHART_AXIS_PRIMARY_Y);
    
    // Configure line series styling - blue theme for GSR
    lv_obj_set_style_line_width(chart_gsr_trend, 3, LV_PART_ITEMS);
    lv_obj_set_style_line_color(chart_gsr_trend, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_ITEMS);
    lv_obj_set_style_line_opa(chart_gsr_trend, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_line_rounded(chart_gsr_trend, false, LV_PART_ITEMS);
    
    // Disable points completely
    lv_obj_set_style_width(chart_gsr_trend, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(chart_gsr_trend, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(chart_gsr_trend, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_border_opa(chart_gsr_trend, LV_OPA_TRANSP, LV_PART_INDICATOR);
    
    // Performance optimizations for real-time GSR display
    lv_obj_add_flag(chart_gsr_trend, LV_OBJ_FLAG_IGNORE_LAYOUT);           // Skip layout calculations
    lv_obj_clear_flag(chart_gsr_trend, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(chart_gsr_trend, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    /* Allow gesture to bubble up to screen */
    lv_obj_add_flag(chart_gsr_trend, LV_OBJ_FLAG_GESTURE_BUBBLE);
    // Initialize chart with baseline values
    lv_chart_set_all_value(chart_gsr_trend, ser_gsr_trend, 0);

    // Note: Real-time uS display disabled - final tonic level shown on results screen
    // Set label_gsr_value to NULL since we're not creating it
    label_gsr_value = NULL;

    // Stop button - positioned at bottom center
//     btn_stop = hpi_btn_create_primary(scr_gsr_plot);
//     lv_obj_set_size(btn_stop, 140, 48);
//     lv_obj_align(btn_stop, LV_ALIGN_BOTTOM_MID, 0, -30);
// //    lv_obj_add_event_cb(btn_stop, scr_gsr_stop_btn_event_handler, LV_EVENT_CLICKED, NULL);
//     lv_obj_add_event_cb(btn_stop, scr_gsr_stop_btn_event_handler, LV_EVENT_PRESSED, NULL);

//     lv_obj_set_style_radius(btn_stop, 22, LV_PART_MAIN);
//     lv_obj_t *label_btn = lv_label_create(btn_stop);
//     lv_label_set_text(label_btn, "Stop");
//     lv_obj_center(label_btn);
//     lv_obj_set_style_text_color(label_btn, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN);

    // Lead off status label (positioned at bottom)
    label_gsr_error = lv_label_create(scr_gsr_plot);
    lv_label_set_long_mode(label_gsr_error, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_gsr_error, 300);
    lv_label_set_text(label_gsr_error, "Make skin contact\nwith electrodes\nTimer will start");
    lv_obj_align(label_gsr_error, LV_ALIGN_CENTER, 0, 0);  // Centered overlay on chart
    lv_obj_add_style(label_gsr_error, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_gsr_error, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_gsr_error, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    
    // Set reference for lead on/off handler
    label_info_gsr = label_gsr_error;

    // CRITICAL: Move STOP button to foreground AFTER creating all other elements
    // This ensures the button is always on top and can receive touch events
   // lv_obj_move_foreground(btn_stop);

    // Initialize performance optimization system
    gsr_chart_reset_performance_counters();
    gsr_chart_enable_performance_mode(true);  // Start in high-performance mode
    plot_ready = true;

    lv_obj_add_event_cb(scr_gsr_plot, gsr_gesture_event_cb, LV_EVENT_GESTURE, NULL);

    hpi_disp_set_curr_screen(SCR_SPL_PLOT_GSR);
    hpi_show_screen(scr_gsr_plot, m_scroll_dir);
}

// LVGL 9.2 optimized chart management functions
static void gsr_chart_enable_performance_mode(bool enable)
{
    if (chart_gsr_trend == NULL) return;
    
    if (enable) {
        // Enable performance optimizations
        lv_obj_add_flag(chart_gsr_trend, LV_OBJ_FLAG_IGNORE_LAYOUT);
        gsr_chart_auto_refresh_enabled = false;
    } else {
        // Restore normal operation
        lv_obj_clear_flag(chart_gsr_trend, LV_OBJ_FLAG_IGNORE_LAYOUT);
        gsr_chart_auto_refresh_enabled = true;
        lv_chart_refresh(chart_gsr_trend);
    }
}

static void gsr_chart_reset_performance_counters(void)
{
    gsr_sample_counter = 0;
    gsr_batch_count = 0;
    // Initialize for proper range detection
    y_max_gsr = -10000;
    y_min_gsr = 10000;
}

void scr_gsr_lead_on_off_handler(bool lead_off)
{
   // LOG_DBG("GSR screen handler: lead_off=%s", lead_off ? "OFF" : "ON");

    if (label_info_gsr == NULL) {
        LOG_WRN("label_info_gsr is NULL, screen handler returning early");
        return;
    }

    gsr_contact_present = !lead_off; // true if contact present

    if (gsr_contact_present) {
        // Contact OK - show chart, hide error message
        lv_obj_add_flag(label_info_gsr, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(chart_gsr_trend, LV_OBJ_FLAG_HIDDEN);
       
    } else {
        // No contact - show error message, hide chart
        lv_obj_clear_flag(label_info_gsr, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(chart_gsr_trend, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(label_info_gsr, "Make skin contact\nwith electrodes\nTimer will start");
    }

    // // CRITICAL: Always ensure STOP button remains clickable and on top
    // if (btn_stop != NULL) {
    //     lv_obj_move_foreground(btn_stop);
    //     lv_obj_add_flag(btn_stop, LV_OBJ_FLAG_CLICKABLE);
    // }
}

void gesture_down_scr_gsr_plot(void)
{
    LOG_DBG("Cancel GSR");
    k_sem_give(&sem_gsr_cancel);
    hpi_load_screen(SCR_GSR, SCROLL_DOWN);
}

void unload_scr_gsr_plot(void)
{
    plot_ready = false;
    if (scr_gsr_plot) {
        lv_obj_del(scr_gsr_plot);
        scr_gsr_plot = NULL;
    }
}
