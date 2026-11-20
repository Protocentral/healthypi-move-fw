/*
 * The one and only app screen: current time + a Start/Stop button that controls
 * the accelerometer recorder. Runs on the UI thread; polls the recorder state
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <stdio.h>

#include "acc_recorder.h"
#include "boot.h"
#include "ui.h"
#include "activity.h"
#include "ml_engine.h"

LOG_MODULE_REGISTER(home, LOG_LEVEL_INF);

#define HOME_TICK_MS 250

#define COL_GREEN 0x00A650
#define COL_RED   0xD32F2F
#define COL_GREY  0x4A4A4A

static lv_obj_t *lbl_time;
static lv_obj_t *lbl_date;
static lv_obj_t *btn;
static lv_obj_t *btn_lbl;
static lv_obj_t *lbl_status;
static lv_obj_t *lbl_act;
static int last_act_idx = INT32_MIN;
static uint8_t last_act_conf = 255;
static uint32_t last_act_us = UINT32_MAX;

static enum acc_rec_state last_state = (enum acc_rec_state)-1;
static uint32_t last_elapsed = UINT32_MAX;
static int64_t last_rtc_ms;

static const char *const month_names[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
					    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static bool recording_available(void) { return g_boot.imu_ok && g_boot.fs_ok; }

static void set_button(const char *text, uint32_t color, bool enabled)
{
	lv_label_set_text(btn_lbl, text);
	lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
	if (enabled) {
		lv_obj_remove_state(btn, LV_STATE_DISABLED);
	} else {
		lv_obj_add_state(btn, LV_STATE_DISABLED);
	}
}

static void btn_cb(lv_event_t *e)
{
	ARG_UNUSED(e);

	if (!recording_available()) {
		return;
	}

	switch (acc_rec_get_state()) {
	case ACC_REC_IDLE:
	case ACC_REC_ERROR:
		acc_rec_request_start();
		break;
	case ACC_REC_RECORDING:
		acc_rec_request_stop();
		break;
	default: 
		break;
	}
}

static void update_clock(void)
{
	struct rtc_time t;

	if (g_boot.rtc_ok && rtc_get_time(g_rtc_dev, &t) == 0) {
		lv_label_set_text_fmt(lbl_time, "%02d:%02d", t.tm_hour, t.tm_min);
		lv_label_set_text_fmt(lbl_date, "%d %s %d", t.tm_mday,
				      month_names[(t.tm_mon >= 0 && t.tm_mon < 12) ? t.tm_mon : 0],
				      t.tm_year + 1900);
	} else {
		lv_label_set_text(lbl_time, "--:--");
		lv_label_set_text(lbl_date, "");
	}
}

static void update_recorder_ui(void)
{
	enum acc_rec_state s = acc_rec_get_state();
	uint32_t el = acc_rec_get_elapsed_s();

	if (s == last_state && el == last_elapsed) {
		return;
	}

	if (s != last_state) {
		switch (s) {
		case ACC_REC_RECORDING:
			set_button("STOP", COL_RED, true);
			break;
		case ACC_REC_OPENING:
			set_button("STARTING", COL_GREY, false);
			break;
		case ACC_REC_CLOSING:
			set_button("SAVING", COL_GREY, false);
			break;
		case ACC_REC_IDLE:
		case ACC_REC_ERROR:
		default:
			set_button("START", COL_GREEN, true);
			break;
		}
	}

	switch (s) {
	case ACC_REC_RECORDING:
		lv_label_set_text_fmt(lbl_status, "REC %02u:%02u\n%u samples",
				      (unsigned int)(el / 60), (unsigned int)(el % 60),
				      (unsigned int)acc_rec_get_samples());
		break;
	case ACC_REC_OPENING:
		lv_label_set_text(lbl_status, "Starting...");
		break;
	case ACC_REC_CLOSING:
		lv_label_set_text(lbl_status, "Saving...");
		break;
	case ACC_REC_ERROR:
		lv_label_set_text_fmt(lbl_status, "Error: %s", acc_rec_get_error());
		break;
	case ACC_REC_IDLE:
	default:
		if (acc_rec_get_filename()[0] != '\0') {
			lv_label_set_text_fmt(lbl_status, "Saved %s\n%u samples",
					      acc_rec_get_filename(),
					      (unsigned int)acc_rec_get_samples());
		} else {
			lv_label_set_text(lbl_status, "Ready");
		}
		break;
	}

	last_state = s;
	last_elapsed = el;
}
static void update_activity_ui(void)
{
	struct activity_result r;

	activity_get_result(&r);

    uint32_t ms = inf_time_ms;

	if (r.idx == last_act_idx && r.conf_pct == last_act_conf && ms == last_act_us) {
		return;
	}
	last_act_idx = r.idx;
	last_act_conf = r.conf_pct;
	last_act_us = ms;

	if (r.idx == ACT_RES_UNAVAILABLE) {
		lv_label_set_text(lbl_act, "Model N/A");
	} else if (r.idx < 0) {
		lv_label_set_text(lbl_act, "Detecting...");
	} else {
		lv_label_set_text_fmt(lbl_act, "%s %u%%\n%u ms", r.label,
				      (unsigned int)r.conf_pct,
				      (unsigned int)ms);
	}
}
static void home_tick_cb(lv_timer_t *timer)
{
	ARG_UNUSED(timer);

	int64_t now = k_uptime_get();

	if (now - last_rtc_ms >= 1000) {
		last_rtc_ms = now;
		update_clock();
	}
	if (recording_available()) {
		update_recorder_ui();
	}
    if (g_boot.imu_ok) {
		update_activity_ui();
	}
}

void home_screen_create(void)
{
	lv_obj_t *scr = lv_obj_create(NULL);

	lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
	lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

	/* Time */
	lbl_time = lv_label_create(scr);
	lv_label_set_text(lbl_time, "--:--");
	lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_48, 0);
	lv_obj_set_style_text_color(lbl_time, lv_color_white(), 0);
	lv_obj_align(lbl_time, LV_ALIGN_CENTER, 0, lv_pct(-24));

	lbl_date = lv_label_create(scr);
	lv_label_set_text(lbl_date, "");
	lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_24, 0);
	lv_obj_set_style_text_color(lbl_date, lv_color_hex(0x9E9E9E), 0);
	lv_obj_align(lbl_date, LV_ALIGN_CENTER, 0, lv_pct(-10));

	/* Start / Stop button */
	btn = lv_button_create(scr);
	lv_obj_set_size(btn, lv_pct(52), lv_pct(17));
	lv_obj_align(btn, LV_ALIGN_CENTER, 0, lv_pct(10));
	lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
	lv_obj_set_style_shadow_width(btn, 0, 0);
	lv_obj_add_event_cb(btn, btn_cb, LV_EVENT_CLICKED, NULL);

	btn_lbl = lv_label_create(btn);
	lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_24, 0);
	lv_obj_set_style_text_color(btn_lbl, lv_color_white(), 0);
	lv_obj_center(btn_lbl);

	/* Status line */
	lbl_status = lv_label_create(scr);
	lv_obj_set_width(lbl_status, lv_pct(70));
	lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_20, 0);
	lv_obj_set_style_text_color(lbl_status, lv_color_hex(0xBDBDBD), 0);
	lv_obj_set_style_text_align(lbl_status, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_align(lbl_status, LV_ALIGN_CENTER, 0, lv_pct(30));

	if (g_boot.imu_ok && g_boot.fs_ok) {
		set_button("START", COL_GREEN, true);
		lv_label_set_text(lbl_status, "Ready");
	} else {
		set_button(g_boot.imu_ok ? "NO STORAGE" : "NO IMU", COL_GREY, false);
		lv_label_set_text(lbl_status, "Recording unavailable");
	}

    /* Activity result + inference time */
	lbl_act = lv_label_create(scr);
	lv_obj_set_width(lbl_act, lv_pct(70));
	lv_label_set_text(lbl_act, g_boot.imu_ok ? "Detecting..." : "");
	lv_obj_set_style_text_font(lbl_act, &lv_font_montserrat_20, 0);
	lv_obj_set_style_text_color(lbl_act, lv_color_hex(0x4FC3F7), 0);
	lv_obj_set_style_text_align(lbl_act, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_align(lbl_act, LV_ALIGN_CENTER, 0, lv_pct(-42));

	update_clock();
	last_rtc_ms = k_uptime_get();
	lv_timer_create(home_tick_cb, HOME_TICK_MS, NULL);

	lv_screen_load(scr);
	LOG_INF("Home screen up");
}