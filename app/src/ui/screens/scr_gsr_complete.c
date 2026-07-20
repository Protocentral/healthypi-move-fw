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
#include "ui/hpi_r0_theme.h"
#include "hw_module.h"
#include "hpi_sys.h"

LOG_MODULE_REGISTER(hpi_disp_scr_gsr_complete, LOG_LEVEL_DBG);

lv_obj_t *scr_gsr_complete;

// GUI Objects for results display
//static lv_obj_t *label_tonic_value;
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

static void scr_gsr_done_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    hpi_load_screen(SCR_GSR, SCROLL_DOWN);
}

/* v2 EDA result (the v2 design system, EDA screen 8 "Result"): the
 * SCR count in the teal hero, a teal TONIC pill (value + µS) with the SCR
 * interpretation, and a DONE button. Data from the stored stress index. */
void draw_scr_gsr_complete(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    LV_UNUSED(arg1); LV_UNUSED(arg2); LV_UNUSED(arg3); LV_UNUSED(arg4);
    bool ready = stored_results.stress_data_ready;

    scr_gsr_complete = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_gsr_complete, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_gsr_complete, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label_title = lv_label_create(scr_gsr_complete);
    lv_label_set_text(label_title, "SKIN RESPONSE");
    lv_obj_align(label_title, LV_ALIGN_CENTER, 0, -128);
    lv_obj_set_style_text_font(label_title, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(label_title, lv_color_hex(V2_MUTED2), 0);
    lv_obj_set_style_text_letter_space(label_title, 2, 0);

    /* SCR count hero (teal) */
    label_scr_count_value = lv_label_create(scr_gsr_complete);
    if (ready) {
        lv_label_set_text_fmt(label_scr_count_value, "%u", stored_results.peaks_per_minute);
    } else {
        lv_label_set_text(label_scr_count_value, "--");
    }
    lv_obj_align(label_scr_count_value, LV_ALIGN_CENTER, 0, -52);
    lv_obj_set_style_text_font(label_scr_count_value, &HPI_FONT_HERO, 0);
    lv_obj_set_style_text_color(label_scr_count_value, lv_color_hex(V2_EDA), 0);
    lv_obj_set_style_text_letter_space(label_scr_count_value, -1, 0);

    lv_obj_t *unit = lv_label_create(scr_gsr_complete);
    lv_label_set_text(unit, "SCR / 30S");
    lv_obj_align(unit, LV_ALIGN_CENTER, 0, 6);
    lv_obj_set_style_text_font(unit, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(unit, lv_color_hex(V2_MUTED), 0);
    lv_obj_set_style_text_letter_space(unit, 2, 0);

    /* teal pill: interpretation + tonic (SCL) value in µS */
    lv_obj_t *pill = hpi_v2_pill(scr_gsr_complete, V2_EDA, V2_EDA_TINT_OPA);
    lv_obj_align(pill, LV_ALIGN_CENTER, 0, 52);
    lv_obj_set_style_pad_hor(pill, 16, 0);

    lv_obj_t *pic = lv_label_create(pill);
    lv_label_set_text(pic, SYM_WATER_DROP);
    lv_obj_set_style_text_font(pic, &HPI_FONT_ICON, 0);
    lv_obj_set_style_text_color(pic, lv_color_hex(V2_EDA), 0);

    lv_obj_t *ptx = lv_label_create(pill);
    if (ready) {
        char buf[40];
        snprintf(buf, sizeof(buf), "%s \xC2\xB7 %u.%02u \xC2\xB5S",
                 get_scr_context(stored_results.peaks_per_minute),
                 stored_results.tonic_level_x100 / 100,
                 stored_results.tonic_level_x100 % 100);
        lv_label_set_text(ptx, buf);
    } else {
        lv_label_set_text(ptx, "NO DATA");
    }
    lv_obj_set_style_text_font(ptx, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(ptx, lv_color_hex(V2_LABEL), 0);
    lv_obj_set_style_text_letter_space(ptx, 1, 0);

    /* DONE button */
    lv_obj_t *btn = hpi_btn_create_secondary(scr_gsr_complete);
    lv_obj_set_size(btn, 160, 56);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 128);
    lv_obj_add_event_cb(btn, scr_gsr_done_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *blbl = lv_label_create(btn);
    lv_label_set_text(blbl, "DONE");
    lv_obj_center(blbl);
    lv_obj_set_style_text_font(blbl, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(blbl, lv_color_hex(V2_VALUE), 0);
    lv_obj_set_style_text_letter_space(blbl, 1, 0);

    /* keep the swipe-down-to-return gesture as well */
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

    LOG_DBG("GSR stress results stored: tonic=%u.%02u uS, SCR=%u/30s, stress=%u",
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
       // label_tonic_value = NULL;
        label_scr_count_value = NULL;
        label_peak_amp_value = NULL;
    }
}
