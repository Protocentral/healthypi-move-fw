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
static lv_obj_t *chart_hrv_spectrum_compact;
static lv_obj_t *arc_stress_gauge;
static lv_chart_series_t *ser_lf_power_compact;
static lv_chart_series_t *ser_hf_power_compact;

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

void draw_scr_hrv_frequency_compact(enum scroll_dir m_scroll_dir)
{
    scr_hrv_frequency_compact = lv_obj_create(NULL);
    lv_obj_clear_flag(scr_hrv_frequency_compact, LV_OBJ_FLAG_SCROLLABLE);
    draw_scr_common(scr_hrv_frequency_compact);

    // Create main container optimized for round display
    lv_obj_t *cont_main = lv_obj_create(scr_hrv_frequency_compact);
    lv_obj_set_size(cont_main, 360, 360);
    lv_obj_center(cont_main);
    lv_obj_set_flex_flow(cont_main, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_main, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(cont_main, &style_scr_black, 0);
    lv_obj_set_style_pad_all(cont_main, 5, LV_PART_MAIN);

    // Compact title
    lv_obj_t *label_title = lv_label_create(cont_main);
    lv_label_set_text(label_title, "HRV Frequency");
    lv_obj_add_style(label_title, &style_white_medium, 0);

    // Compact spectrum chart
    chart_hrv_spectrum_compact = lv_chart_create(cont_main);
    lv_obj_set_size(chart_hrv_spectrum_compact, 280, 60); // Very compact
    lv_chart_set_type(chart_hrv_spectrum_compact, LV_CHART_TYPE_BAR);
    lv_chart_set_range(chart_hrv_spectrum_compact, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_point_count(chart_hrv_spectrum_compact, 6); // Fewer bars
    
    lv_obj_set_style_bg_color(chart_hrv_spectrum_compact, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(chart_hrv_spectrum_compact, 1, LV_PART_MAIN);
    lv_chart_set_div_line_count(chart_hrv_spectrum_compact, 2, 0); // Minimal grid
    
    ser_lf_power_compact = lv_chart_add_series(chart_hrv_spectrum_compact, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
    ser_hf_power_compact = lv_chart_add_series(chart_hrv_spectrum_compact, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);

    // Initialize with zeros
    for (int i = 0; i < 6; i++) {
        lv_chart_set_value_by_id(chart_hrv_spectrum_compact, ser_lf_power_compact, i, 0);
        lv_chart_set_value_by_id(chart_hrv_spectrum_compact, ser_hf_power_compact, i, 0);
    }

    // Central stress gauge using arc
    arc_stress_gauge = lv_arc_create(cont_main);
    lv_obj_set_size(arc_stress_gauge, 120, 120);
    lv_arc_set_rotation(arc_stress_gauge, 135); // Start from bottom left
    lv_arc_set_bg_angles(arc_stress_gauge, 0, 270); // 3/4 circle
    lv_arc_set_value(arc_stress_gauge, 0);
    lv_obj_remove_style(arc_stress_gauge, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc_stress_gauge, LV_OBJ_FLAG_CLICKABLE);

    // Stress level label in center of arc
    label_stress_level_compact = lv_label_create(cont_main);
    lv_label_set_text(label_stress_level_compact, "Low");
    lv_obj_add_style(label_stress_level_compact, &style_white_medium, 0);
    lv_obj_align_to(label_stress_level_compact, arc_stress_gauge, LV_ALIGN_CENTER, 0, 0);

    // Compact metrics in 2x2 grid below arc
    lv_obj_t *cont_metrics = lv_obj_create(cont_main);
    lv_obj_set_size(cont_metrics, 280, 80);
    lv_obj_set_flex_flow(cont_metrics, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(cont_metrics, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(cont_metrics, &style_scr_black, 0);
    lv_obj_set_style_pad_all(cont_metrics, 3, LV_PART_MAIN);

    // LF Power - very compact
    lv_obj_t *cont_lf = lv_obj_create(cont_metrics);
    lv_obj_set_size(cont_lf, 65, 35);
    lv_obj_set_flex_flow(cont_lf, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_lf, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(cont_lf, &style_scr_black, 0);
    lv_obj_set_style_border_width(cont_lf, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(cont_lf, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont_lf, 1, LV_PART_MAIN);

    lv_obj_t *label_lf_title = lv_label_create(cont_lf);
    lv_label_set_text(label_lf_title, "LF");
    lv_obj_add_style(label_lf_title, &style_white_small, 0);

    label_lf_power_compact = lv_label_create(cont_lf);
    lv_label_set_text(label_lf_power_compact, "--");
    lv_obj_add_style(label_lf_power_compact, &style_white_small, 0);

    // HF Power - very compact
    lv_obj_t *cont_hf = lv_obj_create(cont_metrics);
    lv_obj_set_size(cont_hf, 65, 35);
    lv_obj_set_flex_flow(cont_hf, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_hf, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(cont_hf, &style_scr_black, 0);
    lv_obj_set_style_border_width(cont_hf, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(cont_hf, lv_palette_main(LV_PALETTE_BLUE), LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont_hf, 1, LV_PART_MAIN);

    lv_obj_t *label_hf_title = lv_label_create(cont_hf);
    lv_label_set_text(label_hf_title, "HF");
    lv_obj_add_style(label_hf_title, &style_white_small, 0);

    label_hf_power_compact = lv_label_create(cont_hf);
    lv_label_set_text(label_hf_power_compact, "--");
    lv_obj_add_style(label_hf_power_compact, &style_white_small, 0);

    // LF/HF Ratio - compact
    lv_obj_t *cont_ratio = lv_obj_create(cont_metrics);
    lv_obj_set_size(cont_ratio, 130, 35);
    lv_obj_set_flex_flow(cont_ratio, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_ratio, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(cont_ratio, &style_scr_black, 0);
    lv_obj_set_style_border_width(cont_ratio, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(cont_ratio, lv_palette_main(LV_PALETTE_PURPLE), LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont_ratio, 2, LV_PART_MAIN);

    lv_obj_t *label_ratio_title = lv_label_create(cont_ratio);
    lv_label_set_text(label_ratio_title, "LF/HF:");
    lv_obj_add_style(label_ratio_title, &style_white_small, 0);

    label_lf_hf_ratio_compact = lv_label_create(cont_ratio);
    lv_label_set_text(label_lf_hf_ratio_compact, "--");
    lv_obj_add_style(label_lf_hf_ratio_compact, &style_white_small, 0);

    hpi_disp_set_curr_screen(SCR_HRV_SUMMARY);
    hpi_show_screen(scr_hrv_frequency_compact, m_scroll_dir);
}

void hpi_hrv_frequency_compact_update_spectrum(float *rr_intervals, int num_intervals)
{
    // Simplified frequency analysis for compact display
    if (num_intervals < 10) return;
    
    // Calculate simplified power estimates
    float variance_total = 0.0f;
    float mean_rr = 0.0f;
    
    // Calculate mean
    for (int i = 0; i < num_intervals; i++) {
        mean_rr += rr_intervals[i];
    }
    mean_rr /= num_intervals;
    
    // Calculate variance
    for (int i = 0; i < num_intervals; i++) {
        float diff = rr_intervals[i] - mean_rr;
        variance_total += diff * diff;
    }
    variance_total /= (num_intervals - 1);
    
    // Simplified power distribution
    lf_power_compact = variance_total * 0.65f;
    hf_power_compact = variance_total * 0.35f;
    
    stress_score_compact = get_stress_percentage(lf_power_compact, hf_power_compact);
    
    // Update display
    hpi_hrv_frequency_compact_update_display();
}

void hpi_hrv_frequency_compact_update_display(void)
{
    // Update power values with compact formatting
    if (label_lf_power_compact != NULL) {
        lv_label_set_text_fmt(label_lf_power_compact, "%.0f", lf_power_compact);
    }
    
    if (label_hf_power_compact != NULL) {
        lv_label_set_text_fmt(label_hf_power_compact, "%.0f", hf_power_compact);
    }
    
    // Update LF/HF ratio
    if (label_lf_hf_ratio_compact != NULL && hf_power_compact > 0) {
        float ratio = lf_power_compact / hf_power_compact;
        lv_label_set_text_fmt(label_lf_hf_ratio_compact, "%.1f", ratio);
    }
    
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
    
    // Update spectrum chart (simplified)
    if (chart_hrv_spectrum_compact != NULL) {
        float total_power = lf_power_compact + hf_power_compact;
        if (total_power > 0) {
            int lf_pct = (int)((lf_power_compact / total_power) * 100);
            int hf_pct = (int)((hf_power_compact / total_power) * 100);
            
            // Show LF in first 3 bars, HF in last 3 bars
            for (int i = 0; i < 3; i++) {
                lv_chart_set_value_by_id(chart_hrv_spectrum_compact, ser_lf_power_compact, i, lf_pct);
                lv_chart_set_value_by_id(chart_hrv_spectrum_compact, ser_hf_power_compact, i, 0);
            }
            for (int i = 3; i < 6; i++) {
                lv_chart_set_value_by_id(chart_hrv_spectrum_compact, ser_hf_power_compact, i, hf_pct);
                lv_chart_set_value_by_id(chart_hrv_spectrum_compact, ser_lf_power_compact, i, 0);
            }
        }
    }
}
