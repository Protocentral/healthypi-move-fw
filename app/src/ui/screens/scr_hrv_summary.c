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
#include "hrv_algos.h"

LOG_MODULE_REGISTER(hpi_disp_scr_hrv_summary, LOG_LEVEL_DBG);

lv_obj_t *scr_hrv_summary;

// GUI Charts
static lv_obj_t *chart_hrv_rr_trend;
static lv_chart_series_t *ser_hrv_rr;

// GUI Labels
static lv_obj_t *label_hrv_sdnn_val;
static lv_obj_t *label_hrv_rmssd_val;
static lv_obj_t *label_hrv_pnn50_val;
static lv_obj_t *label_hrv_mean_rr_val;
static lv_obj_t *label_hrv_stress_indicator;

// Externs
extern lv_style_t style_white_large;
extern lv_style_t style_white_medium;
extern lv_style_t style_white_small;
extern lv_style_t style_scr_black;
extern lv_style_t style_red_medium;

// Static variables for data tracking
static bool chart_hrv_update = true;
static float y_max_hrv = 0;
static float y_min_hrv = 10000;
static float gx = 0;

// HRV Stress Level Assessment
static const char* get_hrv_stress_level(float sdnn, float rmssd) {
    // Basic stress assessment based on HRV metrics
    if (sdnn > 50 && rmssd > 30) {
        return "Low Stress";
    } else if (sdnn > 30 && rmssd > 20) {
        return "Moderate";
    } else if (sdnn > 20 && rmssd > 15) {
        return "Elevated";
    } else {
        return "High Stress";
    }
}

static lv_color_t get_stress_color(const char* stress_level) {
    if (strcmp(stress_level, "Low Stress") == 0) {
        return lv_palette_main(LV_PALETTE_GREEN);
    } else if (strcmp(stress_level, "Moderate") == 0) {
        return lv_palette_main(LV_PALETTE_YELLOW);
    } else if (strcmp(stress_level, "Elevated") == 0) {
        return lv_palette_main(LV_PALETTE_ORANGE);
    } else {
        return lv_palette_main(LV_PALETTE_RED);
    }
}

void draw_scr_hrv_summary(enum scroll_dir m_scroll_dir)
{
    scr_hrv_summary = lv_obj_create(NULL);
    lv_obj_clear_flag(scr_hrv_summary, LV_OBJ_FLAG_SCROLLABLE);
    draw_scr_common(scr_hrv_summary);

    // Create main container optimized for round display
    lv_obj_t *cont_main = lv_obj_create(scr_hrv_summary);
    lv_obj_set_size(cont_main, 360, 360); // Fit within round display with margin
    lv_obj_center(cont_main);
    lv_obj_set_flex_flow(cont_main, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_main, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(cont_main, &style_scr_black, 0);
    lv_obj_set_style_pad_all(cont_main, 5, LV_PART_MAIN);

    // Compact title
    lv_obj_t *label_title = lv_label_create(cont_main);
    lv_label_set_text(label_title, "HRV");
    lv_obj_add_style(label_title, &style_white_medium, 0);

    // Compact RR trend chart - reduced size for round display
    chart_hrv_rr_trend = lv_chart_create(cont_main);
    lv_obj_set_size(chart_hrv_rr_trend, 300, 80); // Reduced height
    lv_chart_set_type(chart_hrv_rr_trend, LV_CHART_TYPE_LINE);
    lv_chart_set_update_mode(chart_hrv_rr_trend, LV_CHART_UPDATE_MODE_CIRCULAR);
    lv_chart_set_range(chart_hrv_rr_trend, LV_CHART_AXIS_PRIMARY_Y, 500, 1200);
    lv_chart_set_point_count(chart_hrv_rr_trend, 40); // Fewer points for smaller display
    
    lv_obj_set_style_bg_color(chart_hrv_rr_trend, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(chart_hrv_rr_trend, 1, LV_PART_MAIN); // Thinner border
    lv_chart_set_div_line_count(chart_hrv_rr_trend, 3, 2); // Fewer grid lines
    
    ser_hrv_rr = lv_chart_add_series(chart_hrv_rr_trend, lv_palette_main(LV_PALETTE_CYAN), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_line_width(chart_hrv_rr_trend, 2, LV_PART_ITEMS); // Thinner line

    // Compact 2x2 metrics grid optimized for round display
    lv_obj_t *cont_metrics = lv_obj_create(cont_main);
    lv_obj_set_size(cont_metrics, 320, 140); // Compact size
    lv_obj_set_flex_flow(cont_metrics, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(cont_metrics, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(cont_metrics, &style_scr_black, 0);
    lv_obj_set_style_pad_all(cont_metrics, 2, LV_PART_MAIN);

    // SDNN Metric - compact design
    lv_obj_t *cont_sdnn = lv_obj_create(cont_metrics);
    lv_obj_set_size(cont_sdnn, 70, 65); // Much smaller
    lv_obj_set_flex_flow(cont_sdnn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_sdnn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(cont_sdnn, &style_scr_black, 0);
    lv_obj_set_style_border_width(cont_sdnn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(cont_sdnn, lv_palette_main(LV_PALETTE_BLUE), LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont_sdnn, 2, LV_PART_MAIN);

    lv_obj_t *label_sdnn_title = lv_label_create(cont_sdnn);
    lv_label_set_text(label_sdnn_title, "SDNN");
    lv_obj_add_style(label_sdnn_title, &style_white_small, 0);

    label_hrv_sdnn_val = lv_label_create(cont_sdnn);
    lv_label_set_text(label_hrv_sdnn_val, "--");
    lv_obj_add_style(label_hrv_sdnn_val, &style_white_small, 0); // Smaller font

    // RMSSD Metric - compact design
    lv_obj_t *cont_rmssd = lv_obj_create(cont_metrics);
    lv_obj_set_size(cont_rmssd, 70, 65);
    lv_obj_set_flex_flow(cont_rmssd, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_rmssd, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(cont_rmssd, &style_scr_black, 0);
    lv_obj_set_style_border_width(cont_rmssd, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(cont_rmssd, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont_rmssd, 2, LV_PART_MAIN);

    lv_obj_t *label_rmssd_title = lv_label_create(cont_rmssd);
    lv_label_set_text(label_rmssd_title, "RMSSD");
    lv_obj_add_style(label_rmssd_title, &style_white_small, 0);

    label_hrv_rmssd_val = lv_label_create(cont_rmssd);
    lv_label_set_text(label_hrv_rmssd_val, "--");
    lv_obj_add_style(label_hrv_rmssd_val, &style_white_small, 0);

    // pNN50 Metric - compact design
    lv_obj_t *cont_pnn50 = lv_obj_create(cont_metrics);
    lv_obj_set_size(cont_pnn50, 70, 65);
    lv_obj_set_flex_flow(cont_pnn50, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_pnn50, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(cont_pnn50, &style_scr_black, 0);
    lv_obj_set_style_border_width(cont_pnn50, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(cont_pnn50, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont_pnn50, 2, LV_PART_MAIN);

    lv_obj_t *label_pnn50_title = lv_label_create(cont_pnn50);
    lv_label_set_text(label_pnn50_title, "pNN50");
    lv_obj_add_style(label_pnn50_title, &style_white_small, 0);

    label_hrv_pnn50_val = lv_label_create(cont_pnn50);
    lv_label_set_text(label_hrv_pnn50_val, "--");
    lv_obj_add_style(label_hrv_pnn50_val, &style_white_small, 0);

    // Mean RR - compact design
    lv_obj_t *cont_mean = lv_obj_create(cont_metrics);
    lv_obj_set_size(cont_mean, 70, 65);
    lv_obj_set_flex_flow(cont_mean, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_mean, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(cont_mean, &style_scr_black, 0);
    lv_obj_set_style_border_width(cont_mean, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(cont_mean, lv_palette_main(LV_PALETTE_PURPLE), LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont_mean, 2, LV_PART_MAIN);

    lv_obj_t *label_mean_title = lv_label_create(cont_mean);
    lv_label_set_text(label_mean_title, "RR");
    lv_obj_add_style(label_mean_title, &style_white_small, 0);

    label_hrv_mean_rr_val = lv_label_create(cont_mean);
    lv_label_set_text(label_hrv_mean_rr_val, "--");
    lv_obj_add_style(label_hrv_mean_rr_val, &style_white_small, 0);

    // Compact stress indicator at bottom
    lv_obj_t *cont_stress = lv_obj_create(cont_main);
    lv_obj_set_size(cont_stress, 280, 35); // Much smaller
    lv_obj_set_flex_flow(cont_stress, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_stress, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(cont_stress, &style_scr_black, 0);
    lv_obj_set_style_border_width(cont_stress, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont_stress, 3, LV_PART_MAIN);

    lv_obj_t *label_stress_title = lv_label_create(cont_stress);
    lv_label_set_text(label_stress_title, "Stress:");
    lv_obj_add_style(label_stress_title, &style_white_small, 0);

    label_hrv_stress_indicator = lv_label_create(cont_stress);
    lv_label_set_text(label_hrv_stress_indicator, "Calculating...");
    lv_obj_add_style(label_hrv_stress_indicator, &style_white_small, 0);

    hpi_disp_set_curr_screen(SCR_HRV_SUMMARY);
    hpi_show_screen(scr_hrv_summary, m_scroll_dir);
}

void hpi_hrv_summary_update_metrics(float sdnn, float rmssd, float pnn50, float mean_rr)
{
    if (label_hrv_sdnn_val != NULL) {
        lv_label_set_text_fmt(label_hrv_sdnn_val, "%.0f", sdnn);  // No decimal for space
    }
    
    if (label_hrv_rmssd_val != NULL) {
        lv_label_set_text_fmt(label_hrv_rmssd_val, "%.0f", rmssd);  // No decimal for space
    }
    
    if (label_hrv_pnn50_val != NULL) {
        lv_label_set_text_fmt(label_hrv_pnn50_val, "%.0f%%", pnn50 * 100); // Include % symbol, no decimal
    }
    
    if (label_hrv_mean_rr_val != NULL) {
        lv_label_set_text_fmt(label_hrv_mean_rr_val, "%.0f", mean_rr);
    }

    // Update stress level indicator with shorter text
    const char* stress_level = get_hrv_stress_level(sdnn, rmssd);
    if (label_hrv_stress_indicator != NULL) {
        // Use shorter stress level names for small display
        const char* short_stress;
        if (strcmp(stress_level, "Low Stress") == 0) {
            short_stress = "Low";
        } else if (strcmp(stress_level, "Moderate") == 0) {
            short_stress = "Mod";
        } else if (strcmp(stress_level, "Elevated") == 0) {
            short_stress = "High";
        } else {
            short_stress = "V.High";
        }
        
        lv_label_set_text(label_hrv_stress_indicator, short_stress);
        lv_obj_set_style_text_color(label_hrv_stress_indicator, get_stress_color(stress_level), 0);
    }
}

void hpi_hrv_summary_draw_rr_plot(float rr_interval)
{
    if (chart_hrv_update && chart_hrv_rr_trend != NULL && ser_hrv_rr != NULL) {
        
        // Track min/max for auto-scaling
        if (rr_interval < y_min_hrv) {
            y_min_hrv = rr_interval;
        }
        if (rr_interval > y_max_hrv) {
            y_max_hrv = rr_interval;
        }

        lv_chart_set_next_value(chart_hrv_rr_trend, ser_hrv_rr, (int32_t)rr_interval);

        gx += 1;
        if (gx >= 40) { // Update range every 40 samples (reduced for smaller display)
            int32_t range_margin = (int32_t)((y_max_hrv - y_min_hrv) / 10);
            lv_chart_set_range(chart_hrv_rr_trend, LV_CHART_AXIS_PRIMARY_Y, 
                             (int32_t)y_min_hrv - range_margin, 
                             (int32_t)y_max_hrv + range_margin);
            
            gx = 0;
            y_max_hrv = -10000;
            y_min_hrv = 10000;
        }
    }
}

void hpi_hrv_summary_set_update_enabled(bool enabled)
{
    chart_hrv_update = enabled;
}
