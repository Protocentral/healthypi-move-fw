/*
 * UI thread: owns LVGL (same pattern as the base firmware's smf_display thread:
 * display + touch init, splash, boot screen, then the active screen, with
 * lv_timer_handler() every 20 ms). Written against the LVGL 9 API.
 *
 * Flow:  ui_start() -> display init -> splash (2 s) -> boot screen (lines arrive
 *        through ui_boot_add()) -> ui_boot_complete() -> hold 2 s -> home screen.
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>
#include <display_sh8601.h>
#include <string.h>

#include "touch.h"
#include "ui.h"

LOG_MODULE_REGISTER(ui, LOG_LEVEL_DBG);

#define UI_STACK_SIZE   12288
#define UI_PRIORITY     5
#define UI_TICK_MS      20
#define UI_BRIGHTNESS   200   /* 0..255 */
#define SPLASH_MS       2000
#define BOOT_HOLD_MS    2000
#define BOOT_TEXT_LEN   40

struct ui_boot_msg {
	bool ok;
	bool show_status;
	char text[BOOT_TEXT_LEN];
};

K_MSGQ_DEFINE(q_ui_boot, sizeof(struct ui_boot_msg), 24, 4);
K_SEM_DEFINE(sem_ui_start, 0, 1);
K_SEM_DEFINE(sem_ui_ready, 0, 1);
K_SEM_DEFINE(sem_boot_done, 0, 1);

static const struct device *display_dev = DEVICE_DT_GET(DT_NODELABEL(sh8601));

static lv_obj_t *scr_splash;
static lv_obj_t *scr_boot;
static lv_obj_t *boot_list;

/* ------------------------------------------------------------------ */
/* Boot-screen API (any thread)                                       */
/* ------------------------------------------------------------------ */
void ui_boot_add(const char *text, bool ok, bool show_status)
{
	struct ui_boot_msg m = {.ok = ok, .show_status = show_status};

	strncpy(m.text, text, sizeof(m.text) - 1);
	m.text[sizeof(m.text) - 1] = '\0';
	if (k_msgq_put(&q_ui_boot, &m, K_NO_WAIT) != 0) {
		LOG_WRN("Boot message queue full, dropped: %s", text);
	}
}

void ui_start(void) { k_sem_give(&sem_ui_start); }

bool ui_wait_boot_screen(k_timeout_t timeout) { return k_sem_take(&sem_ui_ready, timeout) == 0; }

void ui_boot_complete(void) { k_sem_give(&sem_boot_done); }

/* ------------------------------------------------------------------ */
/* Screens                                                            */
/* ------------------------------------------------------------------ */
static lv_obj_t *new_black_screen(void)
{
	lv_obj_t *scr = lv_obj_create(NULL);

	lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
	lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
	return scr;
}

static lv_obj_t *add_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
			   uint32_t color)
{
	lv_obj_t *l = lv_label_create(parent);

	lv_label_set_text(l, text);
	lv_obj_set_style_text_font(l, font, 0);
	lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
	return l;
}

static void screen_splash(void)
{
	scr_splash = new_black_screen();

	lv_obj_t *t = add_label(scr_splash, "ACC Recorder", &lv_font_montserrat_48, 0xFFFFFF);

	lv_obj_align(t, LV_ALIGN_CENTER, 0, -12);

	lv_obj_t *s = add_label(scr_splash, "HealthyPi Move", &lv_font_montserrat_20, 0x9E9E9E);

	lv_obj_align(s, LV_ALIGN_CENTER, 0, 28);

	lv_screen_load(scr_splash);
}

static void screen_boot(void)
{
	scr_boot = new_black_screen();

	lv_obj_t *title = add_label(scr_boot, "BOOT", &lv_font_montserrat_20, 0x00B0FF);

	lv_obj_align(title, LV_ALIGN_TOP_MID, 0, lv_pct(9));

	boot_list = lv_obj_create(scr_boot);
	lv_obj_set_size(boot_list, lv_pct(70), lv_pct(62));
	lv_obj_align(boot_list, LV_ALIGN_CENTER, 0, lv_pct(5));
	lv_obj_set_style_bg_opa(boot_list, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(boot_list, 0, 0);
	lv_obj_set_style_pad_all(boot_list, 0, 0);
	lv_obj_set_style_pad_row(boot_list, 6, 0);
	lv_obj_set_scrollbar_mode(boot_list, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_flex_flow(boot_list, LV_FLEX_FLOW_COLUMN);

	lv_screen_load(scr_boot);

	/* the splash is no longer needed */
	if (scr_splash) {
		lv_obj_delete(scr_splash);
		scr_splash = NULL;
	}
}

static void boot_add_line(const struct ui_boot_msg *m)
{
	const char *text = m->text;
	bool indent = false;

	while (*text == '\t' || *text == ' ') {
		indent = true;
		text++;
	}

	lv_obj_t *row = lv_obj_create(boot_list);

	lv_obj_set_width(row, lv_pct(100));
	lv_obj_set_height(row, LV_SIZE_CONTENT);
	lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(row, 0, 0);
	lv_obj_set_style_pad_all(row, 0, 0);
	lv_obj_set_style_pad_left(row, indent ? 16 : 0, 0);
	lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);

	if (m->show_status) {
		add_label(row, text, &lv_font_montserrat_20, 0xFFFFFF);
		add_label(row, m->ok ? "OK" : "FAIL", &lv_font_montserrat_20,
			  m->ok ? 0x00E676 : 0xFF5252);
	} else {
		/* informational line (version, free space, final result) */
		add_label(row, text, &lv_font_montserrat_20, m->ok ? 0xBDBDBD : 0xFFB300);
	}

	lv_obj_scroll_to_view(row, LV_ANIM_OFF);
}

static void ui_drain_boot_msgs(void)
{
	struct ui_boot_msg m;

	while (k_msgq_get(&q_ui_boot, &m, K_NO_WAIT) == 0) {
		boot_add_line(&m);
	}
}

/* ------------------------------------------------------------------ */
/* Display bring-up (identical sequence to st_display_init_entry)     */
/* ------------------------------------------------------------------ */
static void ui_display_init(void)
{
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Display device not ready");
	}

	/* The touch controller identifies the display module revision, and the two
	 * revisions need different panel init sequences. Probe it first. */
	sh8601_set_panel_variant(display_dev, hpi_touch_detect() == HPI_TOUCH_CST816S
						      ? SH8601_PANEL_V2
						      : SH8601_PANEL_V1);
	sh8601_reinit(display_dev);
	k_msleep(500);

	const struct device *touch_dev = hpi_touch_get_dev();

	if (touch_dev != NULL) {
		device_init(touch_dev);
	}
	k_msleep(50);

	display_blanking_off(display_dev);
	display_set_brightness(display_dev, UI_BRIGHTNESS);
}

static void ui_run_for(uint32_t ms)
{
	int64_t end = k_uptime_get() + ms;

	while (k_uptime_get() < end) {
		lv_timer_handler();
		k_msleep(UI_TICK_MS);
	}
}

/* ------------------------------------------------------------------ */
/* Thread                                                             */
/* ------------------------------------------------------------------ */
static void ui_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	k_sem_take(&sem_ui_start, K_FOREVER);
	LOG_INF("UI thread started");

	ui_display_init();

	screen_splash();
	ui_run_for(SPLASH_MS);

	screen_boot();
	k_sem_give(&sem_ui_ready);

	/* Show boot lines as they arrive, until boot_run() reports completion */
	for (;;) {
		ui_drain_boot_msgs();

		if (k_sem_take(&sem_boot_done, K_NO_WAIT) == 0) {
			ui_drain_boot_msgs(); /* the last line may have raced the semaphore */
			break;
		}
		lv_timer_handler();
		k_msleep(UI_TICK_MS);
	}

	/* Let the user read the result, with LVGL still running */
	ui_run_for(BOOT_HOLD_MS);

	/* Home screen: no swipes, no other screens */
	home_screen_create();
	if (scr_boot) {
		lv_obj_delete(scr_boot);
		scr_boot = NULL;
		boot_list = NULL;
	}

	for (;;) {
		lv_timer_handler();
		k_msleep(UI_TICK_MS);
	}
}

K_THREAD_DEFINE(ui_thread_id, UI_STACK_SIZE, ui_thread, NULL, NULL, NULL, UI_PRIORITY, 0, 0);