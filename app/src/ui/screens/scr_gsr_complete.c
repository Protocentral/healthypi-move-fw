/*
 * HealthyPi Move GSR Results Screen
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Displays GSR measurement results after completion
 * Shows: Tonic Level (SCL) and SCR Rate
 * Note: Stress level display temporarily hidden pending validation
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#include "hpi_common_types.h"
#include "ui/move_ui.h"
#include "hw_module.h"
#include "hpi_sys.h"

LOG_MODULE_REGISTER(hpi_disp_scr_gsr_complete, LOG_LEVEL_DBG);

lv_obj_t *scr_gsr_complete;

// GUI Objects for results display
static lv_obj_t *label_tonic_value;
static lv_obj_t *label_scr_count_value;
static lv_obj_t *label_peak_amp_value;

// Externs
extern lv_style_t style_body_medium;
extern lv_style_t style_numeric_large;
extern lv_style_t style_caption;

// Store results for display
static struct hpi_gsr_stress_index_t stored_results = {0};

// Color definitions
#define COLOR_GSR_TEAL      0x00897B

/**
 * @brief Get interpretation string for tonic level
 * Typical palmar GSR baseline: 2-20 uS
 */
static const char* get_tonic_context(uint16_t tonic_level_x100)
{
    if (tonic_level_x100 < 200) {         // < 2 uS
        return "Very Low";
    } else if (tonic_level_x100 < 500) {  // 2-5 uS
        return "Low";
    } else if (tonic_level_x100 < 1000) { // 5-10 uS
        return "Normal";
    } else if (tonic_level_x100 < 1500) { // 10-15 uS
        return "Elevated";
    } else {                              // > 15 uS
        return "High";
    }
}

/**
 * @brief Get interpretation string for SCR frequency
 * Typical SCR frequency at rest: 1-3 /min
 */
static const char* get_scr_context(uint8_t peaks_per_minute)
{
    if (peaks_per_minute == 0) {
        return "None";
    } else if (peaks_per_minute <= 2) {
        return "Low";
    } else if (peaks_per_minute <= 5) {
        return "Moderate";
    } else if (peaks_per_minute <= 8) {
        return "Active";
    } else {
        return "Very Active";
    }
}

static void scr_gsr_complete_gesture_cb(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    if (dir == LV_DIR_BOTTOM) {
        hpi_load_screen(SCR_GSR, SCROLL_DOWN);
    }
}

void draw_scr_gsr_complete(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    LV_UNUSED(arg1); LV_UNUSED(arg2); LV_UNUSED(arg3); LV_UNUSED(arg4);

    scr_gsr_complete = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_gsr_complete, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_gsr_complete, LV_OBJ_FLAG_SCROLLABLE);

    /*
     * GSR RESULTS LAYOUT FOR 390x390 ROUND AMOLED
     * ============================================
     * 2-metric layout (stress level hidden pending validation):
     *   Top: y=60 (title)
     *   Primary Row: y=120 (SCL and SCR side by side - larger)
     *   Context: y=220 (interpretation text)
     *   Bottom: y=290 (swipe instruction)
     */

    // TOP: Title at y=60
    lv_obj_t *label_title = lv_label_create(scr_gsr_complete);
    lv_label_set_text(label_title, "GSR Results");
    lv_obj_set_pos(label_title, 0, 60);
    lv_obj_set_width(label_title, 390);
    lv_obj_add_style(label_title, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);

    // PRIMARY ROW: SCL and SCR side by side at y=120
    lv_obj_t *cont_primary = lv_obj_create(scr_gsr_complete);
    lv_obj_remove_style_all(cont_primary);
    lv_obj_set_size(cont_primary, 340, 80);
    lv_obj_set_pos(cont_primary, (390 - 340) / 2, 120);
    lv_obj_set_style_bg_opa(cont_primary, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont_primary, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_primary, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    // Left metric: SCL (Skin Conductance Level)
    lv_obj_t *cont_scl = lv_obj_create(cont_primary);
    lv_obj_remove_style_all(cont_scl);
    lv_obj_set_size(cont_scl, 160, 70);
    lv_obj_set_style_bg_opa(cont_scl, LV_OPA_TRANSP, LV_PART_MAIN);

    lv_obj_t *label_scl_title = lv_label_create(cont_scl);
    lv_label_set_text(label_scl_title, "Tonic Level");
    lv_obj_align(label_scl_title, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_style(label_scl_title, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_scl_title, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);

    label_tonic_value = lv_label_create(cont_scl);
    if (stored_results.stress_data_ready) {
        lv_label_set_text_fmt(label_tonic_value, "%u.%02u uS",
                               stored_results.tonic_level_x100 / 100,
                               stored_results.tonic_level_x100 % 100);
    } else {
        lv_label_set_text(label_tonic_value, "-- uS");
    }
    lv_obj_align(label_tonic_value, LV_ALIGN_TOP_MID, 0, 22);
    lv_obj_add_style(label_tonic_value, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_tonic_value, lv_color_hex(COLOR_GSR_TEAL), LV_PART_MAIN);

    // Right metric: SCR Rate
    lv_obj_t *cont_scr = lv_obj_create(cont_primary);
    lv_obj_remove_style_all(cont_scr);
    lv_obj_set_size(cont_scr, 160, 70);
    lv_obj_set_style_bg_opa(cont_scr, LV_OPA_TRANSP, LV_PART_MAIN);

    lv_obj_t *label_scr_title = lv_label_create(cont_scr);
    lv_label_set_text(label_scr_title, "SCR Rate");
    lv_obj_align(label_scr_title, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_style(label_scr_title, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_scr_title, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);

    label_scr_count_value = lv_label_create(cont_scr);
    if (stored_results.stress_data_ready) {
        lv_label_set_text_fmt(label_scr_count_value, "%u /min",
                               stored_results.peaks_per_minute);
    } else {
        lv_label_set_text(label_scr_count_value, "-- /min");
    }
    lv_obj_align(label_scr_count_value, LV_ALIGN_TOP_MID, 0, 22);
    lv_obj_add_style(label_scr_count_value, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_scr_count_value, lv_color_hex(COLOR_GSR_TEAL), LV_PART_MAIN);

    // CONTEXT: Interpretation text at y=220
    lv_obj_t *label_context = lv_label_create(scr_gsr_complete);
    if (stored_results.stress_data_ready) {
        lv_label_set_text_fmt(label_context, "%s  |  %s",
                              get_tonic_context(stored_results.tonic_level_x100),
                              get_scr_context(stored_results.peaks_per_minute));
    } else {
        lv_label_set_text(label_context, "");
    }
    lv_obj_set_pos(label_context, 0, 220);
    lv_obj_set_width(label_context, 390);
    lv_obj_add_style(label_context, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_context, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_context, lv_color_hex(COLOR_GSR_TEAL), LV_PART_MAIN);

    // BOTTOM: Swipe instruction at y=290
    lv_obj_t *label_info = lv_label_create(scr_gsr_complete);
    lv_label_set_text(label_info, "Swipe down to return");
    lv_obj_set_pos(label_info, 0, 290);
    lv_obj_set_width(label_info, 390);
    lv_obj_add_style(label_info, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_info, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);

    // Add gesture handler
    lv_obj_add_event_cb(scr_gsr_complete, scr_gsr_complete_gesture_cb, LV_EVENT_GESTURE, NULL);

    hpi_disp_set_curr_screen(SCR_SPL_GSR_COMPLETE);
    hpi_show_screen(scr_gsr_complete, m_scroll_dir);
}

/**
 * @brief Update GSR complete screen with stress index results
 * @param results Stress index data structure with all metrics
 *
 * NOTE: This function is called from ZBus listener context (non-LVGL thread).
 * It only stores data - the screen will read stored_results when drawn.
 * DO NOT call LVGL functions here as LVGL is not thread-safe.
 */
void hpi_gsr_complete_update_results(const struct hpi_gsr_stress_index_t *results)
{
    if (!results) {
        return;
    }

    // Store results for screen creation - this is thread-safe
    memcpy(&stored_results, results, sizeof(struct hpi_gsr_stress_index_t));

    LOG_DBG("GSR stress results stored: tonic=%u.%02u uS, SCR=%u/min, stress=%u",
            results->tonic_level_x100 / 100, results->tonic_level_x100 % 100,
            results->peaks_per_minute, results->stress_level);
}

void gesture_down_scr_gsr_complete(void)
{
    hpi_load_screen(SCR_GSR, SCROLL_DOWN);
}

void unload_scr_gsr_complete(void)
{
    if (scr_gsr_complete != NULL) {
        lv_obj_del(scr_gsr_complete);
        scr_gsr_complete = NULL;
        label_tonic_value = NULL;
        label_scr_count_value = NULL;
        label_peak_amp_value = NULL;
    }
}
