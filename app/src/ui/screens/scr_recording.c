/*
 * Copyright (c) 2024 Protocentral Electronics
 * SPDX-License-Identifier: Apache-2.0
 *
 * HealthyPi Move - Recording Configuration Screen
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#include "ui/move_ui.h"
#include "recording_module.h"

LOG_MODULE_REGISTER(scr_recording, LOG_LEVEL_DBG);

static lv_obj_t *scr_recording;
static lv_obj_t *checkbox_ppg;
static lv_obj_t *checkbox_gsr;
static lv_obj_t *dropdown_duration;
static lv_obj_t *label_estimated_size;
static lv_obj_t *btn_start_recording;

// External styles
extern lv_style_t style_body_medium;
extern lv_style_t style_numeric_large;
extern lv_style_t style_caption;

static struct recording_config current_config = {
	.signal_mask = REC_SIGNAL_PPG_WRIST | REC_SIGNAL_GSR,
	.duration_min = 30,
	.ppg_wrist_rate_hz = 128,
	.imu_accel_rate_hz = 100,
	.gsr_rate_hz = 25
};

static void update_size_estimate(void)
{
	uint32_t estimated = recording_estimate_size(&current_config);
	char size_str[64];
	
	if (estimated < 1024*1024) {
		snprintf(size_str, sizeof(size_str), "Est. Size: %.1f KB", (double)estimated / 1024.0);
	} else {
		snprintf(size_str, sizeof(size_str), "Est. Size: %.1f MB", (double)estimated / (1024.0 * 1024.0));
	}
	
	lv_label_set_text(label_estimated_size, size_str);
}

static void checkbox_ppg_event_handler(lv_event_t *e)
{
	if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
	
	if (lv_obj_get_state(checkbox_ppg) & LV_STATE_CHECKED) {
		current_config.signal_mask |= REC_SIGNAL_PPG_WRIST;
	} else {
		current_config.signal_mask &= ~REC_SIGNAL_PPG_WRIST;
	}
	update_size_estimate();
}

static void checkbox_gsr_event_handler(lv_event_t *e)
{
	if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
	
	if (lv_obj_get_state(checkbox_gsr) & LV_STATE_CHECKED) {
		current_config.signal_mask |= REC_SIGNAL_GSR;
	} else {
		current_config.signal_mask &= ~REC_SIGNAL_GSR;
	}
	update_size_estimate();
}

static void dropdown_duration_event_handler(lv_event_t *e)
{
	if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
	
	uint16_t sel = lv_dropdown_get_selected(dropdown_duration);
	const uint16_t durations[] = {5, 15, 30, 60, 120};
	current_config.duration_min = durations[sel];
	update_size_estimate();
}

static void start_btn_event_handler(lv_event_t *e)
{
	if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
	
	// Validate configuration
	if (current_config.signal_mask == 0) {
		LOG_WRN("No signals selected");
		return;
	}
	
	// Check available storage
	uint32_t available = 0;
	if (recording_get_available_space(&available) == 0) {
		uint32_t estimated = recording_estimate_size(&current_config);
		if (estimated > available) {
			LOG_ERR("Insufficient storage: need %u, have %u", estimated, available);
			// TODO: Show error dialog
			return;
		}
	}
	
	// Start recording
	int ret = recording_start(&current_config);
	if (ret < 0) {
		LOG_ERR("Failed to start recording: %d", ret);
		// TODO: Show error dialog
		return;
	}
	
	LOG_INF("Recording started successfully");
	
	// Switch to active recording screen overlay
	hpi_load_scr_spl(SCR_SPL_RECORDING_ACTIVE, SCROLL_UP, 0, 0, 0, 0);
}

void draw_scr_recording(enum scroll_dir m_scroll_dir)
{
	scr_recording = lv_obj_create(NULL);
	lv_obj_set_style_bg_color(scr_recording, lv_color_hex(0x000000), LV_PART_MAIN);
	lv_obj_clear_flag(scr_recording, LV_OBJ_FLAG_SCROLLABLE);

	// Title
	lv_obj_t *label_title = lv_label_create(scr_recording);
	lv_label_set_text(label_title, "Recording");
	lv_obj_add_style(label_title, &style_numeric_large, 0);
	lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 30);

	// Signal selection section
	lv_obj_t *label_signals = lv_label_create(scr_recording);
	lv_label_set_text(label_signals, "Signals");
	lv_obj_add_style(label_signals, &style_body_medium, 0);
	lv_obj_align(label_signals, LV_ALIGN_TOP_LEFT, 40, 80);

	// PPG checkbox
	checkbox_ppg = lv_checkbox_create(scr_recording);
	lv_checkbox_set_text(checkbox_ppg, "PPG Wrist");
	lv_obj_align(checkbox_ppg, LV_ALIGN_TOP_LEFT, 40, 110);
	if (current_config.signal_mask & REC_SIGNAL_PPG_WRIST) {
		lv_obj_add_state(checkbox_ppg, LV_STATE_CHECKED);
	}
	lv_obj_add_event_cb(checkbox_ppg, checkbox_ppg_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

	// GSR checkbox
	checkbox_gsr = lv_checkbox_create(scr_recording);
	lv_checkbox_set_text(checkbox_gsr, "GSR");
	lv_obj_align(checkbox_gsr, LV_ALIGN_TOP_LEFT, 40, 150);
	if (current_config.signal_mask & REC_SIGNAL_GSR) {
		lv_obj_add_state(checkbox_gsr, LV_STATE_CHECKED);
	}
	lv_obj_add_event_cb(checkbox_gsr, checkbox_gsr_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

	// Duration section
	lv_obj_t *label_duration = lv_label_create(scr_recording);
	lv_label_set_text(label_duration, "Duration");
	lv_obj_add_style(label_duration, &style_body_medium, 0);
	lv_obj_align(label_duration, LV_ALIGN_TOP_LEFT, 40, 200);

	dropdown_duration = lv_dropdown_create(scr_recording);
	lv_dropdown_set_options(dropdown_duration, "5 min\n15 min\n30 min\n60 min\n120 min");
	lv_dropdown_set_selected(dropdown_duration, 2, LV_ANIM_OFF); // Default 30 min
	lv_obj_set_width(dropdown_duration, 150);
	lv_obj_align(dropdown_duration, LV_ALIGN_TOP_LEFT, 40, 230);
	lv_obj_add_event_cb(dropdown_duration, dropdown_duration_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

	// Size estimate
	label_estimated_size = lv_label_create(scr_recording);
	lv_obj_add_style(label_estimated_size, &style_caption, 0);
	lv_obj_align(label_estimated_size, LV_ALIGN_TOP_LEFT, 40, 280);
	update_size_estimate();

	// Available space
	lv_obj_t *label_available = lv_label_create(scr_recording);
	uint32_t available = 0;
	char avail_str[64];
	if (recording_get_available_space(&available) == 0) {
		snprintf(avail_str, sizeof(avail_str), "Available: %.1f MB", 
		         available / (1024.0f * 1024.0f));
	} else {
		snprintf(avail_str, sizeof(avail_str), "Available: Unknown");
	}
	lv_label_set_text(label_available, avail_str);
	lv_obj_add_style(label_available, &style_caption, 0);
	lv_obj_align(label_available, LV_ALIGN_TOP_LEFT, 40, 305);

	// Start button
	btn_start_recording = lv_btn_create(scr_recording);
	lv_obj_set_size(btn_start_recording, 280, 50);
	lv_obj_align(btn_start_recording, LV_ALIGN_BOTTOM_MID, 0, -30);
	lv_obj_set_style_bg_color(btn_start_recording, lv_color_hex(0x00AA00), LV_PART_MAIN);
	lv_obj_add_event_cb(btn_start_recording, start_btn_event_handler, LV_EVENT_CLICKED, NULL);

	lv_obj_t *label_btn = lv_label_create(btn_start_recording);
	lv_label_set_text(label_btn, LV_SYMBOL_PLAY " START");
	lv_obj_add_style(label_btn, &style_body_medium, 0);
	lv_obj_center(label_btn);

	// Load the screen
	hpi_show_screen(scr_recording, m_scroll_dir);
}

void scr_recording_update(void)
{
	// Update if recording status changes
	// This would be called from ZBus listener
}

void scr_recording_unload(void)
{
	if (scr_recording != NULL) {
		lv_obj_del(scr_recording);
		scr_recording = NULL;
	}
}
