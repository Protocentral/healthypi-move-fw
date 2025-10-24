/*
 * Copyright (c) 2024 Protocentral Electronics
 * SPDX-License-Identifier: Apache-2.0
 *
 * HealthyPi Move - Recording Complete Screen
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#include "ui/move_ui.h"
#include "recording_module.h"

LOG_MODULE_REGISTER(scr_recording_complete, LOG_LEVEL_DBG);

static lv_obj_t *scr_recording_complete;

// External styles
extern lv_style_t style_title;
extern lv_style_t style_body_medium;
extern lv_style_t style_numeric_large;
extern lv_style_t style_caption;

static void format_duration(uint32_t seconds, char *buf, size_t buf_size)
{
	uint32_t hours = seconds / 3600;
	uint32_t mins = (seconds % 3600) / 60;
	uint32_t secs = seconds % 60;
	
	if (hours > 0) {
		snprintf(buf, buf_size, "%uh %um %us", hours, mins, secs);
	} else if (mins > 0) {
		snprintf(buf, buf_size, "%um %us", mins, secs);
	} else {
		snprintf(buf, buf_size, "%us", secs);
	}
}

static void format_file_size(uint32_t bytes, char *buf, size_t buf_size)
{
	if (bytes >= 1024 * 1024) {
		snprintf(buf, buf_size, "%.2f MB", (double)bytes / (1024.0 * 1024.0));
	} else if (bytes >= 1024) {
		snprintf(buf, buf_size, "%.1f KB", (double)bytes / 1024.0);
	} else {
		snprintf(buf, buf_size, "%u B", bytes);
	}
}

static void btn_done_event_handler(lv_event_t *e)
{
	if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
	
	// Return to home screen
	hpi_load_screen(SCR_HOME, SCROLL_DOWN);
}

static void gesture_down_handler(lv_event_t *e)
{
	if (lv_event_get_code(e) != LV_EVENT_GESTURE) return;
	
	lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
	if (dir == LV_DIR_BOTTOM) {
		lv_indev_wait_release(lv_indev_get_act());
		hpi_load_screen(SCR_HOME, SCROLL_DOWN);
	}
}

void draw_scr_recording_complete(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);
	ARG_UNUSED(arg4);
	
	scr_recording_complete = lv_obj_create(NULL);
	lv_obj_set_style_bg_color(scr_recording_complete, lv_color_hex(COLOR_SURFACE_DARK), LV_PART_MAIN);
	lv_obj_clear_flag(scr_recording_complete, LV_OBJ_FLAG_SCROLLABLE);
	
	// Add gesture handler for swipe down
	lv_obj_add_event_cb(scr_recording_complete, gesture_down_handler, LV_EVENT_GESTURE, NULL);

	// Success icon (checkmark)
	lv_obj_t *icon_container = lv_obj_create(scr_recording_complete);
	lv_obj_set_size(icon_container, 80, 80);
	lv_obj_set_style_radius(icon_container, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_bg_color(icon_container, lv_color_hex(COLOR_SUCCESS_GREEN), 0);
	lv_obj_set_style_border_width(icon_container, 0, 0);
	lv_obj_clear_flag(icon_container, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_align(icon_container, LV_ALIGN_TOP_MID, 0, 30);

	lv_obj_t *label_icon = lv_label_create(icon_container);
	lv_label_set_text(label_icon, LV_SYMBOL_OK);
	lv_obj_set_style_text_font(label_icon, &lv_font_montserrat_24, 0);
	lv_obj_set_style_text_color(label_icon, lv_color_hex(0xFFFFFF), 0);
	lv_obj_center(label_icon);

	// Title
	lv_obj_t *label_title = lv_label_create(scr_recording_complete);
	lv_label_set_text(label_title, "Recording Complete");
	lv_obj_add_style(label_title, &style_headline, 0);
	lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 125);

	// Get recording status for summary
	struct recording_status status;
	char duration_str[32] = "0s";
	char size_str[32] = "0 KB";
	char signals_str[64] = "None";
	
	if (recording_get_status(&status) == 0) {
		// Duration (convert ms to seconds)
		format_duration(status.elapsed_ms / 1000, duration_str, sizeof(duration_str));
		
		// File size
		format_file_size(status.file_size_bytes, size_str, sizeof(size_str));
		
		// Signals recorded (determine from sample counts)
		int signal_count = 0;
		char temp_str[64] = "";
		
		if (status.ppg_wrist_samples > 0) {
			if (signal_count > 0) strcat(temp_str, ", ");
			strcat(temp_str, "PPG");
			signal_count++;
		}
		if (status.imu_accel_samples > 0) {
			if (signal_count > 0) strcat(temp_str, ", ");
			strcat(temp_str, "IMU");
			signal_count++;
		}
		if (status.gsr_samples > 0) {
			if (signal_count > 0) strcat(temp_str, ", ");
			strcat(temp_str, "GSR");
			signal_count++;
		}
		
		if (signal_count > 0) {
			snprintf(signals_str, sizeof(signals_str), "%s", temp_str);
		}
	}

	// Summary container
	lv_obj_t *container_summary = lv_obj_create(scr_recording_complete);
	lv_obj_set_size(container_summary, 300, 120);
	lv_obj_set_style_bg_color(container_summary, lv_color_hex(COLOR_SURFACE_MEDIUM), 0);
	lv_obj_set_style_border_width(container_summary, 0, 0);
	lv_obj_set_style_radius(container_summary, 12, 0);
	lv_obj_clear_flag(container_summary, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_align(container_summary, LV_ALIGN_CENTER, 0, 0);

	// Duration
	lv_obj_t *label_duration_title = lv_label_create(container_summary);
	lv_label_set_text(label_duration_title, "Duration");
	lv_obj_add_style(label_duration_title, &style_caption, 0);
	lv_obj_set_style_text_color(label_duration_title, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
	lv_obj_align(label_duration_title, LV_ALIGN_TOP_LEFT, 15, 10);

	lv_obj_t *label_duration = lv_label_create(container_summary);
	lv_label_set_text(label_duration, duration_str);
	lv_obj_add_style(label_duration, &style_body_medium, 0);
	lv_obj_align(label_duration, LV_ALIGN_TOP_LEFT, 15, 30);

	// File size
	lv_obj_t *label_size_title = lv_label_create(container_summary);
	lv_label_set_text(label_size_title, "File Size");
	lv_obj_add_style(label_size_title, &style_caption, 0);
	lv_obj_set_style_text_color(label_size_title, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
	lv_obj_align(label_size_title, LV_ALIGN_TOP_LEFT, 155, 10);

	lv_obj_t *label_size = lv_label_create(container_summary);
	lv_label_set_text(label_size, size_str);
	lv_obj_add_style(label_size, &style_body_medium, 0);
	lv_obj_align(label_size, LV_ALIGN_TOP_LEFT, 155, 30);

	// Signals
	lv_obj_t *label_signals_title = lv_label_create(container_summary);
	lv_label_set_text(label_signals_title, "Signals");
	lv_obj_add_style(label_signals_title, &style_caption, 0);
	lv_obj_set_style_text_color(label_signals_title, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
	lv_obj_align(label_signals_title, LV_ALIGN_TOP_LEFT, 15, 60);

	lv_obj_t *label_signals = lv_label_create(container_summary);
	lv_label_set_text(label_signals, signals_str);
	lv_obj_add_style(label_signals, &style_body_medium, 0);
	lv_obj_align(label_signals, LV_ALIGN_TOP_LEFT, 15, 80);

	// Info text
	lv_obj_t *label_info = lv_label_create(scr_recording_complete);
	lv_label_set_text(label_info, "File saved to /lfs/rec/");
	lv_obj_add_style(label_info, &style_caption, 0);
	lv_obj_set_style_text_color(label_info, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
	lv_obj_align(label_info, LV_ALIGN_BOTTOM_MID, 0, -95);

	// Done button
	lv_obj_t *btn_done = lv_btn_create(scr_recording_complete);
	lv_obj_set_size(btn_done, 250, 50);
	lv_obj_align(btn_done, LV_ALIGN_BOTTOM_MID, 0, -25);
	lv_obj_set_style_bg_color(btn_done, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN);
	lv_obj_add_event_cb(btn_done, btn_done_event_handler, LV_EVENT_CLICKED, NULL);

	lv_obj_t *label_done = lv_label_create(btn_done);
	lv_label_set_text(label_done, "Done");
	lv_obj_add_style(label_done, &style_body_medium, 0);
	lv_obj_center(label_done);

	// Load the screen with proper animation
	lv_scr_load_anim(scr_recording_complete, LV_SCR_LOAD_ANIM_OVER_TOP, 300, 0, false);
}

void unload_scr_recording_complete(void)
{
	if (scr_recording_complete != NULL) {
		lv_obj_del(scr_recording_complete);
		scr_recording_complete = NULL;
	}
}
