/*
 * HealthyPi Move — v2 AOD (always-on) face (the v2 design system)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Dim always-on watch face: pure-black AMOLED background with dim time, date,
 * heart+HR, and a "TAP TO WAKE" hint. Reuses the hero/small-numeral/label bins
 * in dim colors (no extra fonts). Tap anywhere wakes to Home.
 *
 * How it is used
 * --------------
 * Settings → Always-on (ROW_AOD) toggles the persisted flag via
 * hpi_v2_aod_get/set() (settings key hpiui/aod; default ON for new devices).
 * When the flag is ON, the display SMF sleep path paints this face then:
 *
 *  - CONFIG_HPI_SH8601_HW_AOD=y (default): sh8601_aod_enter() → AOD brightness
 *    0x4A + AODMON 0x49 (native panel AOD). On failure, soft-dims normal mode.
 *  - CONFIG_HPI_SH8601_HW_AOD=n: software dim of normal brightness only.
 *
 * Host loop is throttled while AOD is up. Touch or crown wakes the SMF;
 * sleep_exit leaves AOD (AODMOFF if HW) and lands on Home.
 *
 * Enable / wire-up map
 * --------------------
 *  - Toggle UI:     scr_settings.c ROW_AOD → hpi_v2_aod_set()
 *  - Persistence:   hpi_v2_widgets.c settings "hpiui/aod" (default 1)
 *  - Kconfig:       CONFIG_HPI_SH8601_HW_AOD (app/Kconfig + prj.conf)
 *  - Enter:         smf_display.c st_display_sleep_entry
 *  - Exit / wake:   st_display_sleep_exit
 *  - Driver API:    sh8601_aod_enter/exit (display_sh8601.c)
 *  - Face build:    this file (hpi_v2_aod_enter / _exit)
 */

#include <zephyr/kernel.h>
#include <lvgl.h>

#include "ui/move_ui.h"
#include "ui/hpi_r0_theme.h"
#include "ui/hpi_ui_subjects.h"
#include "hpi_sys.h"

/* dim AOD palette (v2) */
#define AOD_TIME   0x3B4144
#define AOD_DATE   0x2F3437
#define AOD_HR     0x5A4212
#define AOD_HINT   0x282D2F

static lv_obj_t *scr_aod;

static void aod_wake_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    /* Hand wake to the display SMF sleep path (touch/crown also signal this).
     * Do not navigate here — sleep_exit tears down AOD and shows Home. */
    hpi_display_signal_touch_wakeup();
}

/* Clear the static if LVGL auto-deletes us (e.g. hpi_show_screen auto_del). */
static void aod_deleted_cb(lv_event_t *e)
{
    if (lv_event_get_target(e) == scr_aod) {
        scr_aod = NULL;
    }
}

/* Build + show the AOD face. Called from display SMF sleep entry when AOD is ON. */
void hpi_v2_aod_enter(void)
{
    if (scr_aod) {
        /* Already showing — keep the existing face. */
        lv_scr_load(scr_aod);
        return;
    }

    /* Free the screen we are sleeping away from before building the face.
     * Without this the whole pre-sleep screen (often a fully-built carousel)
     * stayed allocated underneath the AOD face for the entire sleep, and was
     * then leaked when sleep_exit loaded Home over the top of it. */
    hpi_scr_release_current();

    scr_aod = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_aod, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr_aod, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(scr_aod, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(scr_aod, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr_aod, aod_wake_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(scr_aod, aod_deleted_cb, LV_EVENT_DELETE, NULL);

    /* dim hero time */
    lv_obj_t *time = lv_label_create(scr_aod);
    lv_label_set_text(time, "00:00");
    lv_obj_set_style_text_font(time, &HPI_FONT_HERO, 0);   /* Rubik 88 */
    lv_obj_set_style_text_color(time, lv_color_hex(AOD_TIME), 0);
    lv_obj_set_style_text_letter_space(time, -2, 0);
    lv_obj_align(time, LV_ALIGN_CENTER, 0, -34);
    hpi_ui_bind_label(time, &subj_time);

    /* dim date */
    lv_obj_t *date = lv_label_create(scr_aod);
    lv_label_set_text(date, "---");
    lv_obj_set_style_text_font(date, &HPI_FONT_LABEL, 0);  /* Manrope 22 */
    lv_obj_set_style_text_color(date, lv_color_hex(AOD_DATE), 0);
    lv_obj_set_style_text_letter_space(date, 2, 0);
    lv_obj_align(date, LV_ALIGN_CENTER, 0, 30);
    hpi_ui_bind_label(date, &subj_date);

    /* dim heart + HR */
    lv_obj_t *hrow = lv_obj_create(scr_aod);
    lv_obj_remove_style_all(hrow);
    lv_obj_set_size(hrow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(hrow, LV_ALIGN_CENTER, 0, 74);
    lv_obj_set_flex_flow(hrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hrow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hrow, 8, 0);

    lv_obj_t *hic = lv_label_create(hrow);
    lv_label_set_text(hic, SYM_HR);
    lv_obj_set_style_text_font(hic, &HPI_FONT_ICON, 0);
    lv_obj_set_style_text_color(hic, lv_color_hex(AOD_HR), 0);

    lv_obj_t *hr = lv_label_create(hrow);
    lv_label_set_text(hr, "--");
    lv_obj_set_style_text_font(hr, &HPI_FONT_NUM_SM, 0);   /* Rubik 22 */
    lv_obj_set_style_text_color(hr, lv_color_hex(AOD_HR), 0);
    hpi_ui_bind_label(hr, &subj_hr);

    /* tap-to-wake hint */
    lv_obj_t *hint = lv_label_create(scr_aod);
    lv_label_set_text(hint, "TAP TO WAKE");
    lv_obj_set_style_text_font(hint, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(AOD_HINT), 0);
    lv_obj_set_style_text_letter_space(hint, 3, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -40);

    lv_scr_load(scr_aod);
}

void hpi_v2_aod_exit(void)
{
    if (!scr_aod) {
        return;
    }
    lv_obj_t *s = scr_aod;
    scr_aod = NULL;
    /* Safe only when not the active screen — callers load the next screen first
     * (sleep_exit shows Home), or LVGL auto_del already freed us (DELETE cb). */
    if (lv_scr_act() != s) {
        lv_obj_del(s);
    }
}
