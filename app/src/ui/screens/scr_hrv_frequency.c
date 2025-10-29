#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <lvgl.h>
#include <stdio.h>
#include <math.h>

#include "hpi_common_types.h"
#include "ui/move_ui.h"

LOG_MODULE_REGISTER(hpi_disp_scr_hrv_frequency, LOG_LEVEL_DBG);

lv_obj_t *scr_hrv_frequency;

// GUI Charts and objects
static lv_obj_t *chart_hrv_spectrum;
static lv_obj_t *chart_hrv_stress_gauge;
static lv_chart_series_t *ser_lf_power;
static lv_chart_series_t *ser_hf_power;

// GUI Labels
static lv_obj_t *label_lf_power_val;
static lv_obj_t *label_hf_power_val;
static lv_obj_t *label_lf_hf_ratio_val;
static lv_obj_t *label_stress_score_val;
static lv_obj_t *label_parasympathetic_val;
static lv_obj_t *label_sympathetic_val;

// Externs
extern lv_style_t style_white_large;
extern lv_style_t style_white_medium;
extern lv_style_t style_white_small;
extern lv_style_t style_scr_black;
extern lv_style_t style_red_medium;

// Frequency domain analysis constants
#define LF_FREQ_MIN 0.04f   // Low frequency minimum (Hz)
#define LF_FREQ_MAX 0.15f   // Low frequency maximum (Hz) 
#define HF_FREQ_MIN 0.15f   // High frequency minimum (Hz)
#define HF_FREQ_MAX 0.4f    // High frequency maximum (Hz)

// Static variables for HRV frequency analysis
static float lf_power = 0.0f;
static float hf_power = 0.0f;
static float lf_hf_ratio = 0.0f;
static float stress_score = 0.0f;

// Simple frequency domain analysis functions
static float calculate_power_in_band(float *psd, int psd_length, float fs, float freq_min, float freq_max)
{
    float power = 0.0f;
    float freq_resolution = fs / (2.0f * psd_length);
    
    int start_idx = (int)(freq_min / freq_resolution);
    int end_idx = (int)(freq_max / freq_resolution);
    
    if (end_idx >= psd_length) end_idx = psd_length - 1;
    if (start_idx < 0) start_idx = 0;
    
    for (int i = start_idx; i <= end_idx; i++) {
        power += psd[i];
    }
    
    return power * freq_resolution;
}

static void calculate_stress_metrics(float lf, float hf, float *stress, float *parasympathetic, float *sympathetic)
{
    float total_power = lf + hf;
    
    if (total_power > 0) {
        *parasympathetic = (hf / total_power) * 100.0f;
        *sympathetic = (lf / total_power) * 100.0f;
        
        // Stress score: higher LF/HF ratio indicates more stress
        float ratio = lf / hf;
        *stress = (ratio > 1.0f) ? ((ratio - 1.0f) / 4.0f) * 100.0f : 0.0f;
        if (*stress > 100.0f) *stress = 100.0f;
    } else {
        *parasympathetic = 0.0f;
        *sympathetic = 0.0f;
        *stress = 0.0f;
    }
}

static lv_color_t get_stress_gauge_color(float stress_percentage)
{
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

void draw_scr_hrv_frequency(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_hrv_frequency = lv_obj_create(NULL);
    lv_obj_clear_flag(scr_hrv_frequency, LV_OBJ_FLAG_SCROLLABLE);
    draw_scr_common(scr_hrv_frequency);
    
    lv_obj_set_scrollbar_mode(scr_hrv_frequency, LV_SCROLLBAR_MODE_ON);

    // Create main container
    lv_obj_t *cont_col = lv_obj_create(scr_hrv_frequency);
    lv_obj_set_size(cont_col, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(cont_col, &style_scr_black, 0);

    // Title
    lv_obj_t *label_title = lv_label_create(cont_col);
    lv_label_set_text(label_title, "HRV Frequency Analysis");
    lv_obj_add_style(label_title, &style_white_medium, 0);

    // Power Spectrum Chart
    lv_obj_t *lbl_spectrum = lv_label_create(cont_col);
    lv_label_set_text(lbl_spectrum, "Power Spectral Density");
    lv_obj_add_style(lbl_spectrum, &style_white_small, 0);

    chart_hrv_spectrum = lv_chart_create(cont_col);
    lv_obj_set_size(chart_hrv_spectrum, 300, 130);
    lv_chart_set_type(chart_hrv_spectrum, LV_CHART_TYPE_BAR);
    lv_chart_set_range(chart_hrv_spectrum, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_point_count(chart_hrv_spectrum, 10);
    
    lv_obj_set_style_bg_color(chart_hrv_spectrum, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(chart_hrv_spectrum, 2, LV_PART_MAIN);
    lv_chart_set_div_line_count(chart_hrv_spectrum, 5, 0);
    
    ser_lf_power = lv_chart_add_series(chart_hrv_spectrum, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
    ser_hf_power = lv_chart_add_series(chart_hrv_spectrum, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);

    // Initialize spectrum chart with sample data
    for (int i = 0; i < 10; i++) {
        lv_chart_set_value_by_id(chart_hrv_spectrum, ser_lf_power, i, 0);
        lv_chart_set_value_by_id(chart_hrv_spectrum, ser_hf_power, i, 0);
    }

    // Frequency Band Power Metrics
    lv_obj_t *cont_freq_metrics = lv_obj_create(cont_col);
    lv_obj_set_size(cont_freq_metrics, lv_pct(95), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_freq_metrics, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(cont_freq_metrics, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(cont_freq_metrics, &style_scr_black, 0);

    // LF Power
    lv_obj_t *cont_lf = lv_obj_create(cont_freq_metrics);
    lv_obj_set_size(cont_lf, 140, 90);
    lv_obj_set_flex_flow(cont_lf, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_lf, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(cont_lf, &style_scr_black, 0);
    lv_obj_set_style_border_width(cont_lf, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(cont_lf, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);

    lv_obj_t *label_lf_title = lv_label_create(cont_lf);
    lv_label_set_text(label_lf_title, "LF Power");
    lv_obj_add_style(label_lf_title, &style_white_small, 0);

    lv_obj_t *label_lf_freq = lv_label_create(cont_lf);
    lv_label_set_text(label_lf_freq, "(0.04-0.15 Hz)");
    lv_obj_add_style(label_lf_freq, &style_white_small, 0);

    label_lf_power_val = lv_label_create(cont_lf);
    lv_label_set_text(label_lf_power_val, "-- ms²");
    lv_obj_add_style(label_lf_power_val, &style_white_medium, 0);

    // HF Power
    lv_obj_t *cont_hf = lv_obj_create(cont_freq_metrics);
    lv_obj_set_size(cont_hf, 140, 90);
    lv_obj_set_flex_flow(cont_hf, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_hf, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(cont_hf, &style_scr_black, 0);
    lv_obj_set_style_border_width(cont_hf, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(cont_hf, lv_palette_main(LV_PALETTE_BLUE), LV_PART_MAIN);

    lv_obj_t *label_hf_title = lv_label_create(cont_hf);
    lv_label_set_text(label_hf_title, "HF Power");
    lv_obj_add_style(label_hf_title, &style_white_small, 0);

    lv_obj_t *label_hf_freq = lv_label_create(cont_hf);
    lv_label_set_text(label_hf_freq, "(0.15-0.4 Hz)");
    lv_obj_add_style(label_hf_freq, &style_white_small, 0);

    label_hf_power_val = lv_label_create(cont_hf);
    lv_label_set_text(label_hf_power_val, "-- ms²");
    lv_obj_add_style(label_hf_power_val, &style_white_medium, 0);

    // LF/HF Ratio
    lv_obj_t *cont_ratio = lv_obj_create(cont_col);
    lv_obj_set_size(cont_ratio, lv_pct(90), 70);
    lv_obj_set_flex_flow(cont_ratio, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_ratio, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(cont_ratio, &style_scr_black, 0);
    lv_obj_set_style_border_width(cont_ratio, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(cont_ratio, lv_palette_main(LV_PALETTE_PURPLE), LV_PART_MAIN);

    lv_obj_t *label_ratio_title = lv_label_create(cont_ratio);
    lv_label_set_text(label_ratio_title, "LF/HF Ratio (Sympathetic/Parasympathetic)");
    lv_obj_add_style(label_ratio_title, &style_white_small, 0);

    label_lf_hf_ratio_val = lv_label_create(cont_ratio);
    lv_label_set_text(label_lf_hf_ratio_val, "--");
    lv_obj_add_style(label_lf_hf_ratio_val, &style_white_medium, 0);

    // Autonomic Balance Indicators
    lv_obj_t *cont_balance = lv_obj_create(cont_col);
    lv_obj_set_size(cont_balance, lv_pct(95), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_balance, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_balance, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(cont_balance, &style_scr_black, 0);

    // Sympathetic Activity
    lv_obj_t *cont_sympathetic = lv_obj_create(cont_balance);
    lv_obj_set_size(cont_sympathetic, 130, 80);
    lv_obj_set_flex_flow(cont_sympathetic, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_sympathetic, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(cont_sympathetic, &style_scr_black, 0);
    lv_obj_set_style_border_width(cont_sympathetic, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(cont_sympathetic, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN);

    lv_obj_t *label_sympathetic_title = lv_label_create(cont_sympathetic);
    lv_label_set_text(label_sympathetic_title, "Sympathetic");
    lv_obj_add_style(label_sympathetic_title, &style_white_small, 0);

    label_sympathetic_val = lv_label_create(cont_sympathetic);
    lv_label_set_text(label_sympathetic_val, "-- %");
    lv_obj_add_style(label_sympathetic_val, &style_white_medium, 0);

    // Parasympathetic Activity
    lv_obj_t *cont_parasympathetic = lv_obj_create(cont_balance);
    lv_obj_set_size(cont_parasympathetic, 130, 80);
    lv_obj_set_flex_flow(cont_parasympathetic, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_parasympathetic, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(cont_parasympathetic, &style_scr_black, 0);
    lv_obj_set_style_border_width(cont_parasympathetic, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(cont_parasympathetic, lv_palette_main(LV_PALETTE_CYAN), LV_PART_MAIN);

    lv_obj_t *label_parasympathetic_title = lv_label_create(cont_parasympathetic);
    lv_label_set_text(label_parasympathetic_title, "Parasympathetic");
    lv_obj_add_style(label_parasympathetic_title, &style_white_small, 0);

    label_parasympathetic_val = lv_label_create(cont_parasympathetic);
    lv_label_set_text(label_parasympathetic_val, "-- %");
    lv_obj_add_style(label_parasympathetic_val, &style_white_medium, 0);

    // Stress Score Gauge
    lv_obj_t *cont_stress = lv_obj_create(cont_col);
    lv_obj_set_size(cont_stress, lv_pct(90), 100);
    lv_obj_set_flex_flow(cont_stress, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_stress, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(cont_stress, &style_scr_black, 0);
    lv_obj_set_style_border_width(cont_stress, 2, LV_PART_MAIN);

    lv_obj_t *label_stress_title = lv_label_create(cont_stress);
    lv_label_set_text(label_stress_title, "Stress Level");
    lv_obj_add_style(label_stress_title, &style_white_small, 0);

    // Create a simple stress gauge using a bar
    chart_hrv_stress_gauge = lv_chart_create(cont_stress);
    lv_obj_set_size(chart_hrv_stress_gauge, 200, 20);
    lv_chart_set_type(chart_hrv_stress_gauge, LV_CHART_TYPE_BAR);
    lv_chart_set_range(chart_hrv_stress_gauge, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_point_count(chart_hrv_stress_gauge, 1);
    lv_obj_set_style_bg_color(chart_hrv_stress_gauge, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(chart_hrv_stress_gauge, 1, LV_PART_MAIN);

    // Create stress gauge series
    lv_chart_series_t *ser_stress = lv_chart_add_series(chart_hrv_stress_gauge, lv_palette_main(LV_PALETTE_GREEN), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_value_by_id(chart_hrv_stress_gauge, ser_stress, 0, 0);

    label_stress_score_val = lv_label_create(cont_stress);
    lv_label_set_text(label_stress_score_val, "Low Stress");
    lv_obj_add_style(label_stress_score_val, &style_white_medium, 0);

    hpi_disp_set_curr_screen(SCR_SPL_HRV_FREQUENCY);
    hpi_show_screen(scr_hrv_frequency, m_scroll_dir);
}

void hpi_hrv_frequency_update_spectrum(float *rr_intervals, int num_intervals)
{
    // This is a simplified version - in a real implementation you would:
    // 1. Apply window function to RR intervals
    // 2. Perform FFT
    // 3. Calculate power spectral density
    // 4. Integrate power in LF and HF bands
    
    // For demonstration, we'll use simplified calculations
    if (num_intervals < 10) return;
    
    // Calculate variance in different frequency bands (simplified)
    float variance_total = 0.0f;
    float mean_rr = 0.0f;
    
    // Calculate mean
    for (int i = 0; i < num_intervals; i++) {
        mean_rr += rr_intervals[i];
    }
    mean_rr /= num_intervals;
    
    // Calculate total variance
    for (int i = 0; i < num_intervals; i++) {
        float diff = rr_intervals[i] - mean_rr;
        variance_total += diff * diff;
    }
    variance_total /= (num_intervals - 1);
    
    // Simulate LF and HF power (in real implementation, use FFT)
    // This is a simplified estimation
    lf_power = variance_total * 0.6f; // Assume 60% of power in LF band
    hf_power = variance_total * 0.4f; // Assume 40% of power in HF band
    
    if (hf_power > 0) {
        lf_hf_ratio = lf_power / hf_power;
    } else {
        lf_hf_ratio = 0.0f;
    }
    
    // Calculate autonomic balance metrics
    float parasympathetic_activity, sympathetic_activity;
    calculate_stress_metrics(lf_power, hf_power, &stress_score, &parasympathetic_activity, &sympathetic_activity);
    
    // Update UI
    hpi_hrv_frequency_update_display();
}

void hpi_hrv_frequency_update_display(void)
{
    // Update power values
    if (label_lf_power_val != NULL) {
        lv_label_set_text_fmt(label_lf_power_val, "%.1f ms²", lf_power);
    }
    
    if (label_hf_power_val != NULL) {
        lv_label_set_text_fmt(label_hf_power_val, "%.1f ms²", hf_power);
    }
    
    if (label_lf_hf_ratio_val != NULL) {
        lv_label_set_text_fmt(label_lf_hf_ratio_val, "%.2f", lf_hf_ratio);
    }
    
    // Update autonomic balance
    float total_power = lf_power + hf_power;
    if (total_power > 0 && label_sympathetic_val != NULL && label_parasympathetic_val != NULL) {
        float sympathetic_pct = (lf_power / total_power) * 100.0f;
        float parasympathetic_pct = (hf_power / total_power) * 100.0f;
        
        lv_label_set_text_fmt(label_sympathetic_val, "%.0f %%", sympathetic_pct);
        lv_label_set_text_fmt(label_parasympathetic_val, "%.0f %%", parasympathetic_pct);
    }
    
    // Update stress gauge
    if (chart_hrv_stress_gauge != NULL) {
        lv_chart_series_t *ser = lv_chart_get_series_next(chart_hrv_stress_gauge, NULL);
        if (ser != NULL) {
            lv_chart_set_value_by_id(chart_hrv_stress_gauge, ser, 0, (int)stress_score);
            lv_obj_set_style_bg_color(chart_hrv_stress_gauge, get_stress_gauge_color(stress_score), LV_PART_ITEMS);
        }
    }
    
    // Update stress label
    if (label_stress_score_val != NULL) {
        const char* stress_text;
        if (stress_score < 25) {
            stress_text = "Low Stress";
        } else if (stress_score < 50) {
            stress_text = "Moderate Stress";
        } else if (stress_score < 75) {
            stress_text = "High Stress";
        } else {
            stress_text = "Very High Stress";
        }
        
        lv_label_set_text_fmt(label_stress_score_val, "%s (%.0f%%)", stress_text, stress_score);
        lv_obj_set_style_text_color(label_stress_score_val, get_stress_gauge_color(stress_score), 0);
    }
    
    // Update spectrum chart (simplified visualization)
    if (chart_hrv_spectrum != NULL && ser_lf_power != NULL && ser_hf_power != NULL) {
        // Normalize powers for display (0-100 scale)
        int lf_normalized = (int)((lf_power / (lf_power + hf_power + 0.001f)) * 100);
        int hf_normalized = (int)((hf_power / (lf_power + hf_power + 0.001f)) * 100);
        
        // Update first few bars to represent LF band
        for (int i = 0; i < 4; i++) {
            lv_chart_set_value_by_id(chart_hrv_spectrum, ser_lf_power, i, lf_normalized);
            lv_chart_set_value_by_id(chart_hrv_spectrum, ser_hf_power, i, 0);
        }
        
        // Update last few bars to represent HF band
        for (int i = 6; i < 10; i++) {
            lv_chart_set_value_by_id(chart_hrv_spectrum, ser_hf_power, i, hf_normalized);
            lv_chart_set_value_by_id(chart_hrv_spectrum, ser_lf_power, i, 0);
        }
        
        // Clear middle bars
        for (int i = 4; i < 6; i++) {
            lv_chart_set_value_by_id(chart_hrv_spectrum, ser_lf_power, i, 0);
            lv_chart_set_value_by_id(chart_hrv_spectrum, ser_hf_power, i, 0);
        }
    }
}
