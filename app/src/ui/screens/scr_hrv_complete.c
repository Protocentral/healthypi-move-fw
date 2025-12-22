/*
 * HealthyPi Move HRV Results Screen
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Displays HRV analysis results with autonomic balance visualization
 * Shows: Sympathetic/Parasympathetic balance indicator, SDNN, RMSSD, LF/HF ratio
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <lvgl.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "hpi_common_types.h"
#include "ui/move_ui.h"
#include "arm_math.h"
#include "arm_const_structs.h"
#include <string.h>
#include <zephyr/sys/util.h>
#include "hrv_algos.h"

LOG_MODULE_REGISTER(hpi_disp_scr_hrv_frequency_compact, LOG_LEVEL_DBG);

lv_obj_t *scr_hrv_frequency_compact;

// Balance indicator components
static lv_obj_t *label_title;
static lv_obj_t *label_balance_state;      // "Relaxed", "Balanced", "Alert", etc.
static lv_obj_t *bar_balance_bg;           // Background gradient bar
static lv_obj_t *indicator_balance;        // Triangle/diamond indicator
static lv_obj_t *label_para;               // "Parasympathetic" label (left)
static lv_obj_t *label_symp;               // "Sympathetic" label (right)

// Metric labels
static lv_obj_t *label_sdnn;
static lv_obj_t *label_rmssd;
static lv_obj_t *label_lf_hf_ratio_compact;

// Externs
extern lv_style_t style_white_large;
extern lv_style_t style_white_medium;
extern lv_style_t style_white_small;
extern lv_style_t style_scr_black;
extern lv_style_t style_body_medium;
extern lv_style_t style_caption;

extern float lf_power_compact;
extern float hf_power_compact;
extern float stress_score_compact;
extern float sdnn_val;
extern float rmssd_val;

// Balance bar dimensions
#define BALANCE_BAR_WIDTH  280
#define BALANCE_BAR_HEIGHT 16
#define INDICATOR_SIZE     20

// Color definitions for balance gradient
#define COLOR_PARA_DOMINANT  0x4CAF50  // Green - parasympathetic (recovery)
#define COLOR_BALANCED       0xFFEB3B  // Yellow - balanced
#define COLOR_SYMP_DOMINANT  0xF44336  // Red - sympathetic (stress/alert)

static void lvgl_update_cb(void *user_data)
{
    hpi_hrv_frequency_compact_update_display();
}

/**
 * @brief Convert LF/HF ratio to balance position (0-100)
 *
 * Maps LF/HF ratio to a position on the balance bar:
 * - 0 = Far left (parasympathetic dominant, LF/HF < 0.5)
 * - 50 = Center (balanced, LF/HF ≈ 1.0)
 * - 100 = Far right (sympathetic dominant, LF/HF > 4.0)
 *
 * Uses logarithmic scaling for better visual distribution
 */
static int get_balance_position(float lf, float hf)
{
    if (hf <= 0.0f) return 75;  // Invalid data - show slightly sympathetic

    float lf_hf = lf / hf;

    // Logarithmic mapping: center at LF/HF = 1.0
    // log2(0.25) = -2, log2(1.0) = 0, log2(4.0) = 2
    // Map range [-2, 2] to [0, 100]

    // Clamp ratio to reasonable range
    if (lf_hf < 0.25f) lf_hf = 0.25f;
    if (lf_hf > 4.0f) lf_hf = 4.0f;

    // log2 scaling centered at 1.0
    float log_ratio = log2f(lf_hf);  // Range: -2 to +2

    // Map to 0-100 (center at 50)
    int position = (int)(50.0f + (log_ratio * 25.0f));

    // Clamp to valid range
    if (position < 0) position = 0;
    if (position > 100) position = 100;

    return position;
}

/**
 * @brief Get balance state description based on LF/HF ratio
 */
static const char* get_balance_state_text(float lf, float hf)
{
    if (hf <= 0.0f) return "Measuring...";

    float lf_hf = lf / hf;

    if (lf_hf < 0.5f)       return "Deep Recovery";
    else if (lf_hf < 0.8f)  return "Relaxed";
    else if (lf_hf < 1.2f)  return "Balanced";
    else if (lf_hf < 1.8f)  return "Mildly Active";
    else if (lf_hf < 2.5f)  return "Alert";
    else if (lf_hf < 3.5f)  return "Stressed";
    else                    return "High Stress";
}

/**
 * @brief Get color for balance state indicator
 */
static lv_color_t get_balance_indicator_color(int position)
{
    // Gradient from green (left) through yellow (center) to red (right)
    if (position < 35) {
        // Green zone (parasympathetic)
        return lv_color_hex(COLOR_PARA_DOMINANT);
    } else if (position < 65) {
        // Yellow zone (balanced)
        return lv_color_hex(COLOR_BALANCED);
    } else {
        // Red zone (sympathetic)
        return lv_color_hex(COLOR_SYMP_DOMINANT);
    }
}

// Keep for backwards compatibility with other modules
int get_stress_percentage(float lf, float hf) {
    if (hf <= 0.0f) return 100;
    float lf_hf = lf / hf;

    if (lf_hf < 0.5f)       return 15;
    else if (lf_hf < 1.0f)  return 30;
    else if (lf_hf < 1.5f)  return 45;
    else if (lf_hf < 2.0f)  return 55;
    else if (lf_hf < 3.0f)  return 70;
    else if (lf_hf < 5.0f)  return 80;
    else if (lf_hf < 8.0f)  return 90;
    else                    return 95;
}

void gesture_handler(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_BOTTOM) {
        gesture_down_scr_spl_hrv_complete();
    }
}

void gesture_down_scr_spl_hrv_complete(void)
{
    hpi_load_screen(SCR_HRV, SCROLL_DOWN);
}

void draw_scr_spl_hrv_complete(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_hrv_frequency_compact = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_hrv_frequency_compact, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_clear_flag(scr_hrv_frequency_compact, LV_OBJ_FLAG_SCROLLABLE);

    /*
     * HRV RESULTS LAYOUT FOR 390x390 ROUND AMOLED
     * =============================================
     * Adjusted layout to prevent overlaps:
     *   y=45:  Title "HRV Analysis"
     *   y=80:  Balance state text ("Relaxed", "Balanced", etc.)
     *   y=115: Balance gradient bar with indicator
     *   y=150: Recovery / Stress labels (below bar)
     *   y=185: SDNN value
     *   y=215: RMSSD value
     *   y=245: LF/HF ratio
     *   y=290: Swipe instruction
     */

    // Title at y=45
    label_title = lv_label_create(scr_hrv_frequency_compact);
    lv_label_set_text(label_title, "HRV Analysis");
    lv_obj_set_pos(label_title, 0, 45);
    lv_obj_set_width(label_title, 390);
    lv_obj_add_style(label_title, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);

    // Balance state text at y=80
    label_balance_state = lv_label_create(scr_hrv_frequency_compact);
    lv_label_set_text(label_balance_state, "Balanced");
    lv_obj_set_pos(label_balance_state, 0, 80);
    lv_obj_set_width(label_balance_state, 390);
    lv_obj_add_style(label_balance_state, &style_white_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_balance_state, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_balance_state, lv_color_hex(COLOR_BALANCED), LV_PART_MAIN);

    // Balance gradient bar background at y=115
    bar_balance_bg = lv_obj_create(scr_hrv_frequency_compact);
    lv_obj_remove_style_all(bar_balance_bg);
    lv_obj_set_size(bar_balance_bg, BALANCE_BAR_WIDTH, BALANCE_BAR_HEIGHT);
    lv_obj_set_pos(bar_balance_bg, (390 - BALANCE_BAR_WIDTH) / 2, 115);
    lv_obj_set_style_radius(bar_balance_bg, BALANCE_BAR_HEIGHT / 2, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar_balance_bg, LV_OPA_COVER, LV_PART_MAIN);

    // Create gradient using multiple colored segments
    // Left segment (green - parasympathetic)
    lv_obj_t *seg_left = lv_obj_create(bar_balance_bg);
    lv_obj_remove_style_all(seg_left);
    lv_obj_set_size(seg_left, BALANCE_BAR_WIDTH / 3, BALANCE_BAR_HEIGHT);
    lv_obj_set_pos(seg_left, 0, 0);
    lv_obj_set_style_bg_color(seg_left, lv_color_hex(COLOR_PARA_DOMINANT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(seg_left, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(seg_left, BALANCE_BAR_HEIGHT / 2, LV_PART_MAIN);

    // Center segment (yellow - balanced)
    lv_obj_t *seg_center = lv_obj_create(bar_balance_bg);
    lv_obj_remove_style_all(seg_center);
    lv_obj_set_size(seg_center, BALANCE_BAR_WIDTH / 3 + 10, BALANCE_BAR_HEIGHT);
    lv_obj_set_pos(seg_center, BALANCE_BAR_WIDTH / 3 - 5, 0);
    lv_obj_set_style_bg_color(seg_center, lv_color_hex(COLOR_BALANCED), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(seg_center, LV_OPA_COVER, LV_PART_MAIN);

    // Right segment (red - sympathetic)
    lv_obj_t *seg_right = lv_obj_create(bar_balance_bg);
    lv_obj_remove_style_all(seg_right);
    lv_obj_set_size(seg_right, BALANCE_BAR_WIDTH / 3, BALANCE_BAR_HEIGHT);
    lv_obj_set_pos(seg_right, 2 * BALANCE_BAR_WIDTH / 3, 0);
    lv_obj_set_style_bg_color(seg_right, lv_color_hex(COLOR_SYMP_DOMINANT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(seg_right, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(seg_right, BALANCE_BAR_HEIGHT / 2, LV_PART_MAIN);

    // Center marker line (white vertical line at balance point)
    lv_obj_t *center_marker = lv_obj_create(bar_balance_bg);
    lv_obj_remove_style_all(center_marker);
    lv_obj_set_size(center_marker, 2, BALANCE_BAR_HEIGHT + 6);
    lv_obj_set_pos(center_marker, BALANCE_BAR_WIDTH / 2 - 1, -3);
    lv_obj_set_style_bg_color(center_marker, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(center_marker, LV_OPA_70, LV_PART_MAIN);

    // Balance indicator (circle) - positioned below bar
    indicator_balance = lv_obj_create(scr_hrv_frequency_compact);
    lv_obj_remove_style_all(indicator_balance);
    lv_obj_set_size(indicator_balance, INDICATOR_SIZE, INDICATOR_SIZE);
    // Initial position at center, below the bar
    int initial_x = (390 - BALANCE_BAR_WIDTH) / 2 + (BALANCE_BAR_WIDTH / 2) - (INDICATOR_SIZE / 2);
    lv_obj_set_pos(indicator_balance, initial_x, 115 + BALANCE_BAR_HEIGHT + 2);
    lv_obj_set_style_bg_color(indicator_balance, lv_color_hex(COLOR_BALANCED), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(indicator_balance, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(indicator_balance, INDICATOR_SIZE / 2, LV_PART_MAIN);
    lv_obj_set_style_border_width(indicator_balance, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(indicator_balance, lv_color_white(), LV_PART_MAIN);

    // Recovery / Stress labels at y=155 (below indicator)
    label_para = lv_label_create(scr_hrv_frequency_compact);
    lv_label_set_text(label_para, "Recovery");
    lv_obj_set_pos(label_para, (390 - BALANCE_BAR_WIDTH) / 2, 155);
    lv_obj_add_style(label_para, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_para, lv_color_hex(COLOR_PARA_DOMINANT), LV_PART_MAIN);

    label_symp = lv_label_create(scr_hrv_frequency_compact);
    lv_label_set_text(label_symp, "Stress");
    lv_obj_set_pos(label_symp, (390 + BALANCE_BAR_WIDTH) / 2 - 40, 155);
    lv_obj_add_style(label_symp, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_symp, lv_color_hex(COLOR_SYMP_DOMINANT), LV_PART_MAIN);

    // SDNN at y=185
    label_sdnn = lv_label_create(scr_hrv_frequency_compact);
    lv_label_set_text(label_sdnn, "SDNN: -- ms");
    lv_obj_set_pos(label_sdnn, 0, 185);
    lv_obj_set_width(label_sdnn, 390);
    lv_obj_add_style(label_sdnn, &style_white_small, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_sdnn, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_sdnn, lv_color_hex(0xFF7070), LV_PART_MAIN);

    // RMSSD at y=215
    label_rmssd = lv_label_create(scr_hrv_frequency_compact);
    lv_label_set_text(label_rmssd, "RMSSD: -- ms");
    lv_obj_set_pos(label_rmssd, 0, 215);
    lv_obj_set_width(label_rmssd, 390);
    lv_obj_add_style(label_rmssd, &style_white_small, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_rmssd, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_rmssd, lv_color_hex(0x70A0FF), LV_PART_MAIN);

    // LF/HF ratio at y=245
    label_lf_hf_ratio_compact = lv_label_create(scr_hrv_frequency_compact);
    lv_label_set_text(label_lf_hf_ratio_compact, "LF/HF: --");
    lv_obj_set_pos(label_lf_hf_ratio_compact, 0, 245);
    lv_obj_set_width(label_lf_hf_ratio_compact, 390);
    lv_obj_add_style(label_lf_hf_ratio_compact, &style_white_small, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_lf_hf_ratio_compact, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_lf_hf_ratio_compact, lv_color_hex(0xC080FF), LV_PART_MAIN);

    // Swipe instruction at y=290
    lv_obj_t *label_swipe = lv_label_create(scr_hrv_frequency_compact);
    lv_label_set_text(label_swipe, "Swipe down to return");
    lv_obj_set_pos(label_swipe, 0, 290);
    lv_obj_set_width(label_swipe, 390);
    lv_obj_add_style(label_swipe, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_swipe, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_swipe, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);

    // Gesture handler
    lv_obj_add_event_cb(scr_hrv_frequency_compact, gesture_handler, LV_EVENT_GESTURE, NULL);
    hpi_disp_set_curr_screen(SCR_SPL_HRV_FREQUENCY);
    hpi_show_screen(scr_hrv_frequency_compact, m_scroll_dir);
    lv_async_call(lvgl_update_cb, NULL);
}

void hpi_hrv_frequency_compact_update_display(void)
{
    float ratio = 0.0f;

    // Calculate balance position (0-100, center=50)
    int balance_pos = get_balance_position(lf_power_compact, hf_power_compact);
    ratio = (hf_power_compact > 0.0f) ? (lf_power_compact / hf_power_compact) : 0.0f;

    int ratio_int = (int)ratio;
    int ratio_dec = (int)((ratio - ratio_int) * 100);

    int sdnn_int = (int)sdnn_val;
    int sdnn_dec = (int)((sdnn_val - sdnn_int) * 10);

    int rmssd_int = (int)rmssd_val;
    int rmssd_dec = (int)((rmssd_val - rmssd_int) * 10);

    // Update balance state text and color
    if (label_balance_state) {
        const char *state_text = get_balance_state_text(lf_power_compact, hf_power_compact);
        lv_label_set_text(label_balance_state, state_text);
        lv_obj_set_style_text_color(label_balance_state,
                                     get_balance_indicator_color(balance_pos), LV_PART_MAIN);
    }

    // Update balance indicator position
    if (indicator_balance) {
        // Map 0-100 to pixel position on the bar
        int bar_start_x = (390 - BALANCE_BAR_WIDTH) / 2;
        int indicator_x = bar_start_x + (balance_pos * BALANCE_BAR_WIDTH / 100) - (INDICATOR_SIZE / 2);

        // Clamp to bar bounds
        if (indicator_x < bar_start_x - INDICATOR_SIZE / 2) {
            indicator_x = bar_start_x - INDICATOR_SIZE / 2;
        }
        if (indicator_x > bar_start_x + BALANCE_BAR_WIDTH - INDICATOR_SIZE / 2) {
            indicator_x = bar_start_x + BALANCE_BAR_WIDTH - INDICATOR_SIZE / 2;
        }

        // Update position (bar is at y=115, indicator below at y=115+16+2=133)
        lv_obj_set_pos(indicator_balance, indicator_x, 115 + BALANCE_BAR_HEIGHT + 2);
        lv_obj_set_style_bg_color(indicator_balance,
                                   get_balance_indicator_color(balance_pos), LV_PART_MAIN);
    }

    // SDNN and RMSSD
    if (label_sdnn)
        lv_label_set_text_fmt(label_sdnn, "SDNN: %d.%d ms", sdnn_int, abs(sdnn_dec));

    if (label_rmssd)
        lv_label_set_text_fmt(label_rmssd, "RMSSD: %d.%d ms", rmssd_int, abs(rmssd_dec));

    // LF/HF ratio
    if (label_lf_hf_ratio_compact) {
        if (hf_power_compact > 0.0f)
            lv_label_set_text_fmt(label_lf_hf_ratio_compact, "LF/HF: %d.%02d", ratio_int, abs(ratio_dec));
        else
            lv_label_set_text(label_lf_hf_ratio_compact, "LF/HF: --");
    }
}
