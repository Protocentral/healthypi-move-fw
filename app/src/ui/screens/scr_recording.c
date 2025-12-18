/*
 * HealthyPi Move - Recording Control Screen
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <stdio.h>

#include "ui/move_ui.h"
#include "recording_module.h"

LOG_MODULE_REGISTER(scr_recording, LOG_LEVEL_DBG);

/* Screen objects */
static lv_obj_t *scr_recording = NULL;
static lv_obj_t *label_title = NULL;
static lv_obj_t *label_status = NULL;
static lv_obj_t *label_duration = NULL;
static lv_obj_t *label_signals = NULL;
static lv_obj_t *btn_action = NULL;
static lv_obj_t *label_btn_action = NULL;
static lv_obj_t *btn_duration_1m = NULL;
static lv_obj_t *btn_duration_5m = NULL;
static lv_obj_t *btn_duration_15m = NULL;

/* Recording state tracking */
static bool is_recording = false;
static uint16_t selected_duration_s = 300;  /* Default 5 minutes */

/* Default signal mask: PPG Wrist + GSR (most common use case) */
#define DEFAULT_SIGNAL_MASK (REC_SIGNAL_PPG_WRIST | REC_SIGNAL_GSR)

/* External styles */
extern lv_style_t style_body_medium;
extern lv_style_t style_numeric_large;
extern lv_style_t style_caption;

/* Forward declarations */
static void btn_action_event_handler(lv_event_t *e);
static void btn_duration_event_handler(lv_event_t *e);
static void update_ui_for_state(bool recording);
static void update_duration_buttons(uint16_t duration_s);

/* LVGL delete event callback - clears static pointers when screen is deleted */
static void scr_recording_delete_event_cb(lv_event_t *e)
{
    LOG_DBG("Recording screen delete event - clearing pointers");
    scr_recording = NULL;
    label_title = NULL;
    label_status = NULL;
    label_duration = NULL;
    label_signals = NULL;
    btn_action = NULL;
    label_btn_action = NULL;
    btn_duration_1m = NULL;
    btn_duration_5m = NULL;
    btn_duration_15m = NULL;
}

static void btn_action_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        if (!is_recording) {
            /* Configure and start recording */
            struct hpi_recording_config_t config = {
                .duration_s = selected_duration_s,
                .signal_mask = DEFAULT_SIGNAL_MASK,
                .sample_decimation = 1,  /* Full rate */
            };

            int ret = hpi_recording_configure(&config);
            if (ret == 0) {
                ret = hpi_recording_start();
                if (ret == 0) {
                    LOG_INF("Recording started: %d seconds", selected_duration_s);
                    is_recording = true;
                    update_ui_for_state(true);
                } else {
                    LOG_ERR("Failed to start recording: %d", ret);
                }
            } else {
                LOG_ERR("Failed to configure recording: %d", ret);
            }
        } else {
            /* Stop recording */
            int ret = hpi_recording_stop();
            if (ret == 0) {
                LOG_INF("Recording stopped");
                is_recording = false;
                update_ui_for_state(false);
            } else {
                LOG_ERR("Failed to stop recording: %d", ret);
            }
        }
    }
}

static void btn_duration_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED && !is_recording) {
        lv_obj_t *btn = lv_event_get_target(e);
        uint16_t duration = (uint16_t)(uintptr_t)lv_event_get_user_data(e);

        selected_duration_s = duration;
        update_duration_buttons(duration);

        /* Update duration display */
        if (label_duration != NULL) {
            uint16_t mins = duration / 60;
            lv_label_set_text_fmt(label_duration, "%d min", mins);
        }

        LOG_DBG("Selected duration: %d seconds", duration);
    }
}

static void update_duration_buttons(uint16_t duration_s)
{
    /* Highlight selected duration button */
    lv_color_t selected_color = lv_color_hex(COLOR_PRIMARY_BLUE);
    lv_color_t default_color = lv_color_hex(COLOR_SURFACE_MEDIUM);

    if (btn_duration_1m != NULL) {
        lv_obj_set_style_bg_color(btn_duration_1m,
            (duration_s == 60) ? selected_color : default_color, LV_PART_MAIN);
    }
    if (btn_duration_5m != NULL) {
        lv_obj_set_style_bg_color(btn_duration_5m,
            (duration_s == 300) ? selected_color : default_color, LV_PART_MAIN);
    }
    if (btn_duration_15m != NULL) {
        lv_obj_set_style_bg_color(btn_duration_15m,
            (duration_s == 900) ? selected_color : default_color, LV_PART_MAIN);
    }
}

static void update_ui_for_state(bool recording)
{
    if (scr_recording == NULL) return;

    if (recording) {
        /* Recording state - solid red button with white text */
        if (label_status != NULL) {
            lv_label_set_text(label_status, LV_SYMBOL_STOP " RECORDING");
            lv_obj_set_style_text_color(label_status, lv_color_hex(COLOR_CRITICAL_RED), LV_PART_MAIN);
        }
        if (btn_action != NULL) {
            lv_obj_set_style_bg_color(btn_action, lv_color_hex(COLOR_BTN_RED), LV_PART_MAIN);
        }
        if (label_btn_action != NULL) {
            lv_label_set_text(label_btn_action, LV_SYMBOL_STOP " Stop");
            lv_obj_set_style_text_color(label_btn_action, lv_color_white(), LV_PART_MAIN);
        }
        /* Disable duration buttons during recording */
        if (btn_duration_1m != NULL) lv_obj_add_state(btn_duration_1m, LV_STATE_DISABLED);
        if (btn_duration_5m != NULL) lv_obj_add_state(btn_duration_5m, LV_STATE_DISABLED);
        if (btn_duration_15m != NULL) lv_obj_add_state(btn_duration_15m, LV_STATE_DISABLED);
    } else {
        /* Ready state - solid green button with white text */
        if (label_status != NULL) {
            lv_label_set_text(label_status, "Ready to Record");
            lv_obj_set_style_text_color(label_status, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);
        }
        if (btn_action != NULL) {
            lv_obj_set_style_bg_color(btn_action, lv_color_hex(COLOR_BTN_GREEN), LV_PART_MAIN);
        }
        if (label_btn_action != NULL) {
            lv_label_set_text(label_btn_action, LV_SYMBOL_PLAY " Start");
            lv_obj_set_style_text_color(label_btn_action, lv_color_white(), LV_PART_MAIN);
        }
        /* Enable duration buttons */
        if (btn_duration_1m != NULL) lv_obj_clear_state(btn_duration_1m, LV_STATE_DISABLED);
        if (btn_duration_5m != NULL) lv_obj_clear_state(btn_duration_5m, LV_STATE_DISABLED);
        if (btn_duration_15m != NULL) lv_obj_clear_state(btn_duration_15m, LV_STATE_DISABLED);

        /* Reset duration display */
        if (label_duration != NULL) {
            uint16_t mins = selected_duration_s / 60;
            lv_label_set_text_fmt(label_duration, "%d min", mins);
        }
    }
}

void draw_scr_recording(enum scroll_dir m_scroll_dir)
{
    /* Check current recording state */
    is_recording = hpi_recording_is_active();

    scr_recording = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_recording, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_clear_flag(scr_recording, LV_OBJ_FLAG_SCROLLABLE);

    /*
     * LAYOUT FOR 390x390 ROUND AMOLED (1.2" screen)
     * ==============================================
     * Optimized for touch targets (min 44px recommended)
     * Title: y=50
     * Status: y=85
     * Duration display: y=125
     * Duration buttons: y=175
     * Signals info: y=235
     * Action button: y=275
     */

    /* Title */
    label_title = lv_label_create(scr_recording);
    lv_label_set_text(label_title, "Recording");
    lv_obj_set_pos(label_title, 0, 50);
    lv_obj_set_width(label_title, 390);
    lv_obj_add_style(label_title, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);

    /* Status indicator */
    label_status = lv_label_create(scr_recording);
    lv_obj_set_pos(label_status, 0, 85);
    lv_obj_set_width(label_status, 390);
    lv_obj_add_style(label_status, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    /* Large duration display - use body style (not numeric-only font) for "X min" text */
    label_duration = lv_label_create(scr_recording);
    lv_obj_set_pos(label_duration, 0, 125);
    lv_obj_set_width(label_duration, 390);
    lv_obj_add_style(label_duration, &style_body_medium, LV_PART_MAIN);
    lv_obj_set_style_text_font(label_duration, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_duration, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_duration, lv_color_white(), LV_PART_MAIN);

    /* Duration selection buttons container */
    lv_obj_t *cont_duration = lv_obj_create(scr_recording);
    lv_obj_remove_style_all(cont_duration);
    lv_obj_set_size(cont_duration, 300, 56);
    lv_obj_set_pos(cont_duration, (390 - 300) / 2, 175);
    lv_obj_set_flex_flow(cont_duration, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_duration, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(cont_duration, LV_OBJ_FLAG_SCROLLABLE);

    /* Duration buttons - larger for better touch targets */
    const int btn_w = 85;
    const int btn_h = 48;

    btn_duration_1m = lv_btn_create(cont_duration);
    lv_obj_set_size(btn_duration_1m, btn_w, btn_h);
    lv_obj_set_style_radius(btn_duration_1m, 24, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_duration_1m, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_duration_1m, btn_duration_event_handler, LV_EVENT_CLICKED, (void *)(uintptr_t)60);
    lv_obj_t *lbl_1m = lv_label_create(btn_duration_1m);
    lv_label_set_text(lbl_1m, "1m");
    lv_obj_center(lbl_1m);
    lv_obj_set_style_text_color(lbl_1m, lv_color_white(), LV_PART_MAIN);

    btn_duration_5m = lv_btn_create(cont_duration);
    lv_obj_set_size(btn_duration_5m, btn_w, btn_h);
    lv_obj_set_style_radius(btn_duration_5m, 24, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_duration_5m, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_duration_5m, btn_duration_event_handler, LV_EVENT_CLICKED, (void *)(uintptr_t)300);
    lv_obj_t *lbl_5m = lv_label_create(btn_duration_5m);
    lv_label_set_text(lbl_5m, "5m");
    lv_obj_center(lbl_5m);
    lv_obj_set_style_text_color(lbl_5m, lv_color_white(), LV_PART_MAIN);

    btn_duration_15m = lv_btn_create(cont_duration);
    lv_obj_set_size(btn_duration_15m, btn_w, btn_h);
    lv_obj_set_style_radius(btn_duration_15m, 24, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_duration_15m, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_duration_15m, btn_duration_event_handler, LV_EVENT_CLICKED, (void *)(uintptr_t)900);
    lv_obj_t *lbl_15m = lv_label_create(btn_duration_15m);
    lv_label_set_text(lbl_15m, "15m");
    lv_obj_center(lbl_15m);
    lv_obj_set_style_text_color(lbl_15m, lv_color_white(), LV_PART_MAIN);

    /* Highlight default duration */
    update_duration_buttons(selected_duration_s);

    /* Signals info */
    label_signals = lv_label_create(scr_recording);
    lv_label_set_text(label_signals, "PPG + GSR");
    lv_obj_set_pos(label_signals, 0, 235);
    lv_obj_set_width(label_signals, 390);
    lv_obj_add_style(label_signals, &style_caption, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_signals, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_signals, lv_color_hex(COLOR_TEXT_SECONDARY), LV_PART_MAIN);

    /* Main action button - large solid button with inverted colors */
    const int action_btn_w = 200;
    const int action_btn_h = 60;
    const int action_btn_y = 275;

    btn_action = lv_btn_create(scr_recording);
    lv_obj_set_size(btn_action, action_btn_w, action_btn_h);
    lv_obj_set_pos(btn_action, (390 - action_btn_w) / 2, action_btn_y);
    lv_obj_set_style_radius(btn_action, 30, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_action, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_action, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn_action, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_action, btn_action_event_handler, LV_EVENT_CLICKED, NULL);

    label_btn_action = lv_label_create(btn_action);
    //lv_obj_set_style_text_font(label_btn_action, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(label_btn_action);

    /* Set initial UI state */
    update_ui_for_state(is_recording);

    /* If recording, show current elapsed time */
    if (is_recording) {
        struct hpi_recording_status_t status;
        hpi_recording_get_status(&status);
        if (label_duration != NULL) {
            uint16_t mins = status.elapsed_s / 60;
            uint16_t secs = status.elapsed_s % 60;
            lv_label_set_text_fmt(label_duration, "%02d:%02d", mins, secs);
        }
    } else {
        /* Show selected duration */
        if (label_duration != NULL) {
            uint16_t mins = selected_duration_s / 60;
            lv_label_set_text_fmt(label_duration, "%d min", mins);
        }
    }

    /* Register delete callback to cleanup pointers when LVGL auto-deletes this screen */
    lv_obj_add_event_cb(scr_recording, scr_recording_delete_event_cb, LV_EVENT_DELETE, NULL);

    hpi_disp_set_curr_screen(SCR_RECORDING);
    hpi_show_screen(scr_recording, m_scroll_dir);
}

void hpi_scr_recording_update_status(struct hpi_recording_status_t *status)
{
    if (scr_recording == NULL || status == NULL) {
        return;
    }

    /* Update recording state if changed */
    bool new_recording_state = status->active && (status->state == REC_STATE_RECORDING);
    if (new_recording_state != is_recording) {
        is_recording = new_recording_state;
        update_ui_for_state(is_recording);
    }

    /* Update duration display */
    if (label_duration != NULL) {
        if (is_recording) {
            uint16_t mins = status->elapsed_s / 60;
            uint16_t secs = status->elapsed_s % 60;
            lv_label_set_text_fmt(label_duration, "%02d:%02d", mins, secs);
        }
    }

    /* Check if recording completed (auto-stopped) */
    if (!status->active && is_recording) {
        is_recording = false;
        update_ui_for_state(false);
        LOG_INF("Recording completed");
    }
}
