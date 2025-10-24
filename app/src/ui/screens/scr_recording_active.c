/*
 * Copyright (c) 2024 Protocentral Electronics
 * SPDX-License-Identifier: Apache-2.0
 *
 * HealthyPi Move - Active Recording Overlay Screen
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#include "ui/move_ui.h"
#include "recording_module.h"

LOG_MODULE_REGISTER(scr_recording_active, LOG_LEVEL_DBG);

static lv_obj_t *scr_recording_active;
static lv_obj_t *label_status;
static lv_obj_t *label_elapsed_time;
static lv_obj_t *label_samples;
static lv_obj_t *label_file_size;
static lv_obj_t *arc_progress;
static lv_obj_t *btn_pause;
static lv_obj_t *btn_stop;

// External styles
extern lv_style_t style_body_medium;
extern lv_style_t style_numeric_large;
extern lv_style_t style_caption;

static struct k_work_delayable update_work;

static void format_time(uint32_t seconds, char *buf, size_t buf_size)
{
	uint32_t hours = seconds / 3600;
	uint32_t mins = (seconds % 3600) / 60;
	uint32_t secs = seconds % 60;
	
	if (hours > 0) {
		snprintf(buf, buf_size, "%02u:%02u:%02u", hours, mins, secs);
	} else {
		snprintf(buf, buf_size, "%02u:%02u", mins, secs);
	}
}

static void update_display(void)
{
	struct recording_status status;
	
	if (recording_get_status(&status) != 0) {
		return;
	}
	
	// Update elapsed time
	char time_str[32];
	format_time(status.elapsed_ms / 1000, time_str, sizeof(time_str));
	lv_label_set_text(label_elapsed_time, time_str);
	
	// Update progress arc (0-360 degrees)
	if (status.total_duration_ms > 0) {
		uint16_t angle = (uint16_t)((status.elapsed_ms * 360ULL) / status.total_duration_ms);
		lv_arc_set_value(arc_progress, angle);
	}
	
	// Update sample counts
	char samples_str[128];
	snprintf(samples_str, sizeof(samples_str), 
	         "PPG: %u  GSR: %u",
	         status.ppg_wrist_samples,
	         status.gsr_samples);
	lv_label_set_text(label_samples, samples_str);
	
	// Update file size
	char size_str[64];
	if (status.file_size_bytes < 1024*1024) {
		snprintf(size_str, sizeof(size_str), "File: %.1f KB", 
		         status.file_size_bytes / 1024.0f);
	} else {
		snprintf(size_str, sizeof(size_str), "File: %.1f MB", 
		         status.file_size_bytes / (1024.0f * 1024.0f));
	}
	lv_label_set_text(label_file_size, size_str);
	
	// Update status
	const char *status_text = "RECORDING";
	if (status.state == REC_STATE_PAUSED) {
		status_text = "PAUSED";
	} else if (status.state == REC_STATE_ERROR) {
		status_text = "ERROR";
	}
	lv_label_set_text(label_status, status_text);
}

static void update_work_handler(struct k_work *work)
{
	if (scr_recording_active != NULL && recording_is_active()) {
		update_display();
		// Schedule next update in 1 second
		k_work_schedule(&update_work, K_MSEC(1000));
	}
}

static void btn_pause_event_handler(lv_event_t *e)
{
	if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
	
	struct recording_status status;
	if (recording_get_status(&status) == 0) {
		if (status.state == REC_STATE_PAUSED) {
			recording_resume();
			lv_label_set_text(lv_obj_get_child(btn_pause, 0), LV_SYMBOL_PAUSE " PAUSE");
		} else {
			recording_pause();
			lv_label_set_text(lv_obj_get_child(btn_pause, 0), LV_SYMBOL_PLAY " RESUME");
		}
	}
}

static void btn_stop_event_handler(lv_event_t *e)
{
	if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
	
	// Cancel update timer
	k_work_cancel_delayable(&update_work);
	
	// Stop recording
	recording_stop();
	
	LOG_INF("Recording stopped by user");
	
	// Load completion screen
	hpi_load_scr_spl(SCR_SPL_RECORDING_COMPLETE, SCROLL_DOWN, 0, 0, 0, 0);
}

void draw_scr_recording_active(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);
	ARG_UNUSED(arg4);
	
	scr_recording_active = lv_obj_create(NULL);
	lv_obj_set_style_bg_color(scr_recording_active, lv_color_hex(0x000000), LV_PART_MAIN);
	lv_obj_clear_flag(scr_recording_active, LV_OBJ_FLAG_SCROLLABLE);

	// Status indicator (animated dot + text)
	lv_obj_t *container_status = lv_obj_create(scr_recording_active);
	lv_obj_set_size(container_status, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
	lv_obj_set_style_bg_opa(container_status, LV_OPA_TRANSP, 0);
	lv_obj_clear_flag(container_status, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_align(container_status, LV_ALIGN_TOP_MID, 0, 20);

	// Red recording dot
	lv_obj_t *dot = lv_obj_create(container_status);
	lv_obj_set_size(dot, 12, 12);
	lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_bg_color(dot, lv_color_hex(0xFF0000), 0);
	lv_obj_align(dot, LV_ALIGN_LEFT_MID, 0, 0);

	label_status = lv_label_create(container_status);
	lv_label_set_text(label_status, "RECORDING");
	lv_obj_add_style(label_status, &style_body_medium, 0);
	lv_obj_align(label_status, LV_ALIGN_LEFT_MID, 20, 0);

	// Progress arc (center of screen)
	arc_progress = lv_arc_create(scr_recording_active);
	lv_obj_set_size(arc_progress, 200, 200);
	lv_arc_set_rotation(arc_progress, 270);
	lv_arc_set_bg_angles(arc_progress, 0, 360);
	lv_arc_set_value(arc_progress, 0);
	lv_obj_set_style_arc_width(arc_progress, 8, LV_PART_MAIN);
	lv_obj_set_style_arc_width(arc_progress, 8, LV_PART_INDICATOR);
	lv_obj_set_style_arc_color(arc_progress, lv_color_hex(0x00AA00), LV_PART_INDICATOR);
	lv_obj_align(arc_progress, LV_ALIGN_CENTER, 0, -20);
	lv_obj_remove_style(arc_progress, NULL, LV_PART_KNOB);
	lv_obj_clear_flag(arc_progress, LV_OBJ_FLAG_CLICKABLE);

	// Elapsed time (center of arc)
	label_elapsed_time = lv_label_create(scr_recording_active);
	lv_label_set_text(label_elapsed_time, "00:00");
	lv_obj_add_style(label_elapsed_time, &style_numeric_large, 0);
	lv_obj_align(label_elapsed_time, LV_ALIGN_CENTER, 0, -20);

	// Sample counters
	label_samples = lv_label_create(scr_recording_active);
	lv_label_set_text(label_samples, "PPG: 0  GSR: 0");
	lv_obj_add_style(label_samples, &style_caption, 0);
	lv_obj_align(label_samples, LV_ALIGN_CENTER, 0, 70);

	// File size
	label_file_size = lv_label_create(scr_recording_active);
	lv_label_set_text(label_file_size, "File: 0 KB");
	lv_obj_add_style(label_file_size, &style_caption, 0);
	lv_obj_align(label_file_size, LV_ALIGN_CENTER, 0, 95);

	// Control buttons
	btn_pause = lv_btn_create(scr_recording_active);
	lv_obj_set_size(btn_pause, 130, 45);
	lv_obj_align(btn_pause, LV_ALIGN_BOTTOM_LEFT, 20, -25);
	lv_obj_set_style_bg_color(btn_pause, lv_color_hex(0x555555), LV_PART_MAIN);
	lv_obj_add_event_cb(btn_pause, btn_pause_event_handler, LV_EVENT_CLICKED, NULL);

	lv_obj_t *label_pause = lv_label_create(btn_pause);
	lv_label_set_text(label_pause, LV_SYMBOL_PAUSE " PAUSE");
	lv_obj_center(label_pause);

	btn_stop = lv_btn_create(scr_recording_active);
	lv_obj_set_size(btn_stop, 130, 45);
	lv_obj_align(btn_stop, LV_ALIGN_BOTTOM_RIGHT, -20, -25);
	lv_obj_set_style_bg_color(btn_stop, lv_color_hex(0xAA0000), LV_PART_MAIN);
	lv_obj_add_event_cb(btn_stop, btn_stop_event_handler, LV_EVENT_CLICKED, NULL);

	lv_obj_t *label_stop = lv_label_create(btn_stop);
	lv_label_set_text(label_stop, LV_SYMBOL_STOP " STOP");
	lv_obj_center(label_stop);

	// Initialize update work
	k_work_init_delayable(&update_work, update_work_handler);
	
	// Initial update
	update_display();
	
	// Start periodic updates
	k_work_schedule(&update_work, K_MSEC(1000));

	// Load the screen
	hpi_show_screen(scr_recording_active, m_scroll_dir);
}

void scr_recording_active_update(void)
{
	update_display();
}

void unload_scr_recording_active(void)
{
	// Cancel update work
	k_work_cancel_delayable(&update_work);
	
	if (scr_recording_active != NULL) {
		lv_obj_del(scr_recording_active);
		scr_recording_active = NULL;
	}
}
