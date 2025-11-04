 #include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <lvgl.h>
#include <stdio.h>
#include <math.h>

#include "hpi_common_types.h"
#include "ui/move_ui.h"

LOG_MODULE_REGISTER(hpi_disp_scr_hrv_frequency_compact, LOG_LEVEL_DBG);

lv_obj_t *scr_hrv_frequency_compact;

// GUI Charts and objects - minimal for round display

static lv_obj_t *arc_stress_gauge;

// GUI Labels - minimal set
static lv_obj_t *label_lf_power_compact;
static lv_obj_t *label_hf_power_compact;
static lv_obj_t *label_lf_hf_ratio_compact;
static lv_obj_t *label_stress_level_compact;

// Externs
extern lv_style_t style_white_large;
extern lv_style_t style_white_medium;
extern lv_style_t style_white_small;
extern lv_style_t style_scr_black;

// Static variables for HRV frequency analysis
static float lf_power_compact = 0.0f;
static float hf_power_compact = 0.0f;
static float stress_score_compact = 0.0f;


// Simplified stress assessment for compact display
static int get_stress_percentage(float lf, float hf) {
    if (hf <= 0) return 100;
    float ratio = lf / hf;
    int stress_pct = (int)((ratio / 4.0f) * 100);
    return stress_pct > 100 ? 100 : stress_pct;
}

static lv_color_t get_stress_arc_color(int stress_percentage) {
    if (stress_percentage < 25) {
        return lv_palette_main(LV_PALETTE_GREEN);
    } else if (stress_percentage < 50) {
        return lv_palette_main(LV_PALETTE_YELLOW);
    } else if (stress_percentage < 75) {
        return lv_palette_main(LV_PALETTE_ORANGE);
    } else {
        return lv_palette_main(LV_PALETTE_RED);
    }
}

void gesture_handler(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_BOTTOM) {
        gesture_down_scr_spl_hrv();
    }
}

void gesture_down_scr_spl_hrv(void)
{
    printk("Exit HRV Frequency Compact\n");
    hpi_load_screen(SCR_HRV_SUMMARY, SCROLL_DOWN);
}

void hpi_hrv_frequency_compact_update_spectrum(double *rr_intervals, int num_intervals)
{
    LOG_INF("Updating HRV Frequency Compact Spectrum with %d intervals", num_intervals);

    // Calculate simplified power estimates
    double variance_total = 0.0;                              
    double mean_rr = 0.0;
    
    // Calculate mean
    for (int i = 0; i < num_intervals; i++) {
        LOG_INF("RR Interval[%d]: %.2f", i, rr_intervals[i]);
        k_msleep(10); 
        mean_rr += rr_intervals[i];
    }
    mean_rr /= num_intervals;
    LOG_INF("Mean RR: %lf", mean_rr);
    
    // Calculate variance
    for (int i = 0; i < num_intervals; i++) {
        double diff = rr_intervals[i] - mean_rr;
        variance_total += diff * diff;
       
    }
    variance_total /= (num_intervals - 1);
    LOG_INF("Total Variance: %lf", variance_total);

    // Simplified power distribution
    lf_power_compact = variance_total * 0.65;
    hf_power_compact = variance_total * 0.35;
    
    LOG_INF("LF Power (Compact): %lf", lf_power_compact);
    LOG_INF("HF Power (Compact): %lf", hf_power_compact);
    LOG_INF("LF/HF Ratio (Compact): %lf", lf_power_compact / hf_power_compact);
    stress_score_compact = get_stress_percentage(lf_power_compact, hf_power_compact);
    LOG_INF("Stress Score (Compact): %lf", stress_score_compact);
    
    hpi_hrv_frequency_compact_update_display();
}


void draw_scr_hrv_frequency_compact(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{

        scr_hrv_frequency_compact = lv_obj_create(NULL);
        lv_obj_clear_flag(scr_hrv_frequency_compact, LV_OBJ_FLAG_SCROLLABLE);
        draw_scr_common(scr_hrv_frequency_compact);

        lv_obj_t *cont_main = lv_obj_create(scr_hrv_frequency_compact);
        lv_obj_set_size(cont_main, 360, 360);
        lv_obj_center(cont_main);
        lv_obj_add_style(cont_main, &style_scr_black, 0);
        lv_obj_set_style_pad_all(cont_main, 10, LV_PART_MAIN);
        lv_obj_set_style_border_width(cont_main, 0, LV_PART_MAIN);

        // Title
        lv_obj_t *label_title = lv_label_create(cont_main);
        lv_label_set_text(label_title, "HRV Frequency");
        lv_obj_add_style(label_title, &style_white_medium, 0);
        lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 5);

        // --- Row container for LF & HF ---
        lv_obj_t *cont_top = lv_obj_create(cont_main);
        lv_obj_set_size(cont_top, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_add_style(cont_top, &style_scr_black, 0);
        lv_obj_set_style_bg_opa(cont_top, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(cont_top, 0, LV_PART_MAIN);
        lv_obj_set_flex_flow(cont_top, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(cont_top, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_align(cont_top, LV_ALIGN_TOP_MID, 0, 40);

        // LF Power label
        label_lf_power_compact = lv_label_create(cont_top);
        lv_label_set_text(label_lf_power_compact, "LF: 0.00");
        lv_obj_set_style_text_color(label_lf_power_compact, lv_color_hex(0xFF7070), 0); 
        lv_obj_add_style(label_lf_power_compact, &style_white_small, 0);

        // HF Power label
        label_hf_power_compact = lv_label_create(cont_top);
        lv_label_set_text(label_hf_power_compact, "HF: 0.00");
        lv_obj_set_style_text_color(label_hf_power_compact, lv_color_hex(0x70A0FF), 0);  
        lv_obj_add_style(label_hf_power_compact, &style_white_small, 0);

        // --- Stress gauge ---
        arc_stress_gauge = lv_arc_create(cont_main);
        lv_obj_set_size(arc_stress_gauge, 170, 170);
        lv_arc_set_rotation(arc_stress_gauge, 135);
        lv_arc_set_bg_angles(arc_stress_gauge, 0, 270);
        lv_arc_set_value(arc_stress_gauge, 0);
        lv_obj_clear_flag(arc_stress_gauge, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_style(arc_stress_gauge, NULL, LV_PART_KNOB);
        lv_obj_align(arc_stress_gauge, LV_ALIGN_CENTER, 0, 10);

        // Stress label inside arc
        label_stress_level_compact = lv_label_create(cont_main);
        lv_label_set_text(label_stress_level_compact, "Low");
        lv_obj_add_style(label_stress_level_compact, &style_white_medium, 0);
        lv_obj_align_to(label_stress_level_compact, arc_stress_gauge, LV_ALIGN_CENTER, 0, 0);

        // --- Bottom metrics (LF/HF + Stress %) ---
        lv_obj_t *cont_bottom = lv_obj_create(cont_main);
        lv_obj_set_size(cont_bottom, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_add_style(cont_bottom, &style_scr_black, 0);
        lv_obj_set_style_bg_opa(cont_bottom, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(cont_bottom, 0, LV_PART_MAIN);
        lv_obj_set_flex_flow(cont_bottom, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(cont_bottom, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_align(cont_bottom, LV_ALIGN_BOTTOM_MID, 0, -10);

        label_lf_hf_ratio_compact = lv_label_create(cont_bottom);
        lv_label_set_text(label_lf_hf_ratio_compact, "LF/HF: 0.00");
        lv_obj_set_style_text_color(label_lf_hf_ratio_compact, lv_color_hex(0xC080FF), 0);  
        lv_obj_add_style(label_lf_hf_ratio_compact, &style_white_small, 0);

        // Gesture handler
        lv_obj_add_event_cb(scr_hrv_frequency_compact, gesture_handler, LV_EVENT_GESTURE, NULL);

           
        hpi_disp_set_curr_screen(SCR_HRV_SUMMARY);
        hpi_show_screen(scr_hrv_frequency_compact, m_scroll_dir);
}


void hpi_hrv_frequency_compact_update_display(void)
{

    float ratio = 0.0f;
   
    ratio = lf_power_compact / hf_power_compact;

    int lf_int = (int)lf_power_compact;
    int lf_dec = (int)((lf_power_compact - lf_int) * 100);  // 2 decimal digits

    int hf_int = (int)hf_power_compact;
    int hf_dec = (int)((hf_power_compact - hf_int) * 100);

    int ratio_int = (int)ratio;
    int ratio_dec = (int)((ratio - ratio_int) * 100);

    if (label_lf_power_compact)
        lv_label_set_text_fmt(label_lf_power_compact, "LF: %d.%02d", lf_int, abs(lf_dec));

    if (label_hf_power_compact)
        lv_label_set_text_fmt(label_hf_power_compact, "HF: %d.%02d", hf_int, abs(hf_dec));

    if (label_lf_hf_ratio_compact)
        lv_label_set_text_fmt(label_lf_hf_ratio_compact, "LF/HF: %d.%02d", ratio_int, abs(ratio_dec));

    
    // Update stress arc gauge
    if (arc_stress_gauge != NULL) {
        lv_arc_set_value(arc_stress_gauge, (int)stress_score_compact);
        lv_obj_set_style_arc_color(arc_stress_gauge, get_stress_arc_color((int)stress_score_compact), LV_PART_INDICATOR);
    }
    

    // Update stress label with very short text
    if (label_stress_level_compact != NULL) {
        const char* stress_text;
        if (stress_score_compact < 25) {
            stress_text = "Low";
        } else if (stress_score_compact < 50) {
            stress_text = "Med";
        } else if (stress_score_compact < 75) {
            stress_text = "High";
        } else {
            stress_text = "Max";
        }
        
        lv_label_set_text(label_stress_level_compact, stress_text);
        lv_obj_set_style_text_color(label_stress_level_compact, get_stress_arc_color((int)stress_score_compact), 0);
    }
    
   
}
float hpi_get_lf_hf_ratio(void) {
    if (hf_power_compact <= 0.0f)
        return 0.0f;
    return lf_power_compact / hf_power_compact;
}

