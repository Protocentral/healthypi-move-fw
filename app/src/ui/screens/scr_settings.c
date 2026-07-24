/*
 * HealthyPi Move — v2 Settings screen (the v2 design system)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Scrollable list of setting rows (icon + label + right-aligned value). The
 * value is accent-colored when the setting is on/active. Changing watch face /
 * accent / units drops the cached carousel so the new look takes effect when
 * the user swipes back. The Bluetooth row is a read-only status line (ON /
 * CONNECTED), sampled when the screen is built.
 */

#include <zephyr/kernel.h>
#include <lvgl.h>
#include <app_version.h>

#include "ui/move_ui.h"
#include "ui/hpi_r0_theme.h"
#include "hpi_user_settings_api.h"
#include "ble_module.h"

static lv_obj_t *scr_settings;

/* A7: ROW_BSAVER is gone. The toggle persisted a flag nothing acted on — no
 * scan-rate, sensor-duty or display policy ever read hpi_v2_bsaver_get(), so it
 * read as a working power control that did nothing. The setting itself is kept
 * in hpi_v2_widgets.c (still loaded/saved) so re-adding the row is one line once
 * a real battery-saver policy exists. */
enum row_id { ROW_BRIGHT, ROW_AOD, ROW_FACE, ROW_UNITS, ROW_TIMEFMT, ROW_ACCENT, ROW_MOTIF,
              ROW_SLEEP, ROW_HEIGHT, ROW_WEIGHT, ROW_HAND, ROW_BT, ROW_ABOUT };

static const char *const accent_name[4] = {"AMBER", "BLUE", "GREEN", "INDIGO"};

/* P1-2: sleep-timeout choices, in seconds. Cycled in place like the other
 * toggles rather than pushing a picker screen - the old
 * SCR_SPL_SLEEP_TIMEOUT_SELECT roller lived in the orphaned device-user-settings
 * menu, which P2 deleted. Must stay inside hpi_settings_validate()'s 10-120 s
 * range, or the store will heal the value out from under this list on next boot. */
static const uint8_t sleep_choice[] = {10, 15, 30, 60, 120};

/* set a row's value text + accent/muted color */
static void row_val(lv_obj_t *val, const char *text, bool active)
{
    lv_label_set_text(val, text);
    lv_obj_set_style_text_color(val, lv_color_hex(active ? hpi_accent_rgb() : V2_MUTED2), 0);
}

/* refresh a value label from live state */
static void row_refresh(lv_obj_t *val, enum row_id id)
{
    char b[16];
    switch (id) {
    case ROW_BRIGHT:
        snprintf(b, sizeof(b), "%d%%", hpi_disp_get_brightness());
        row_val(val, b, true);
        break;
    case ROW_AOD:    row_val(val, hpi_v2_aod_get() ? "ON" : "OFF", hpi_v2_aod_get()); break;
    case ROW_FACE:   row_val(val, hpi_v2_face_get() ? "MINIMAL" : "DIGITAL", true); break;
    case ROW_UNITS:  row_val(val, hpi_user_settings_get_temp_unit() ? "\xC2\xB0" "C" : "\xC2\xB0" "F", true); break;
    case ROW_TIMEFMT:row_val(val, hpi_user_settings_get_time_format() ? "12H" : "24H", true); break;
    case ROW_ACCENT: row_val(val, accent_name[hpi_accent_get() & 3], true); break;
    case ROW_MOTIF:  row_val(val, hpi_v2_motif_get() ? "ON" : "OFF", hpi_v2_motif_get()); break;
    case ROW_SLEEP:
        snprintf(b, sizeof(b), "%d s", hpi_user_settings_get_sleep_timeout());
        row_val(val, b, true);
        break;
    case ROW_HEIGHT: snprintf(b, sizeof(b), "%d cm", hpi_user_settings_get_height()); row_val(val, b, false); break;
    case ROW_WEIGHT: snprintf(b, sizeof(b), "%d kg", hpi_user_settings_get_weight()); row_val(val, b, false); break;
    case ROW_HAND:   row_val(val, hpi_user_settings_get_hand_worn() ? "RIGHT" : "LEFT", true); break;
    /* A4: live link state, not a toggle. An advertising on/off control is out of
     * scope on purpose — turning the radio off would strand the mobile app and,
     * with it, the only OTA path back. */
    case ROW_BT:     row_val(val, hpi_ble_is_connected() ? "CONNECTED" : "ON",
                             hpi_ble_is_connected()); break;
    case ROW_ABOUT:  row_val(val, "FW " APP_VERSION_STRING, false); break;
    default: break;
    }
}

static void row_click_cb(lv_event_t *e)
{
    lv_obj_t *val = lv_event_get_user_data(e);
    enum row_id id = (enum row_id)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    bool rebuild = false;

    switch (id) {
    case ROW_BRIGHT: hpi_load_scr_spl(SCR_SPL_PULLDOWN, SCROLL_DOWN, SCR_SPL_SETTINGS, 0, 0, 0); return;
    case ROW_AOD:    hpi_v2_aod_set(!hpi_v2_aod_get()); break;
    case ROW_FACE:   hpi_v2_face_set(!hpi_v2_face_get()); rebuild = true; break;
    case ROW_UNITS:  hpi_user_settings_set_temp_unit(hpi_user_settings_get_temp_unit() ? 0 : 1); rebuild = true; break;
    case ROW_TIMEFMT:hpi_user_settings_set_time_format(hpi_user_settings_get_time_format() ? 0 : 1); break;
    case ROW_ACCENT: hpi_accent_set((hpi_accent_get() + 1) & 3); rebuild = true; break;
    case ROW_MOTIF:  hpi_v2_motif_set(!hpi_v2_motif_get()); rebuild = true; break;
    case ROW_SLEEP: {
        /* advance to the next choice, wrapping; land on the first choice if the
         * stored value isn't one of ours (e.g. set over BLE) */
        uint8_t cur = hpi_user_settings_get_sleep_timeout();
        size_t i = 0;
        for (size_t n = 0; n < ARRAY_SIZE(sleep_choice); n++) {
            if (sleep_choice[n] == cur) { i = (n + 1) % ARRAY_SIZE(sleep_choice); break; }
        }
        hpi_user_settings_set_sleep_timeout(sleep_choice[i]);
        break;
    }
    case ROW_HAND:   hpi_user_settings_set_hand_worn(hpi_user_settings_get_hand_worn() ? 0 : 1); break;
    case ROW_HEIGHT: hpi_load_scr_spl(SCR_SPL_HEIGHT_SELECT, SCROLL_UP, SCR_SPL_SETTINGS, 0, 0, 0); return;
    case ROW_WEIGHT: hpi_load_scr_spl(SCR_SPL_WEIGHT_SELECT, SCROLL_UP, SCR_SPL_SETTINGS, 0, 0, 0); return;
    default: return;   /* bt/about not clickable here */
    }
    row_refresh(val, id);
    if (rebuild) {
        hpi_carousel_rebuild();   /* new face/accent/units on next home show */
    }
}

static void back_chip_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    gesture_down_scr_settings();   /* same exit as the swipe-down gesture */
}

static lv_obj_t *make_row(lv_obj_t *list, const char *icon, uint32_t icon_col,
                          const char *label, enum row_id id, bool clickable)
{
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(row, 20, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(V2_CHIP_BG), 0);
    lv_obj_set_style_bg_opa(row, V2_CHIP_OPA, 0);
    lv_obj_set_style_pad_hor(row, 18, 0);
    lv_obj_set_style_pad_ver(row, 13, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_user_data(row, (void *)(intptr_t)id);

    lv_obj_t *ic = lv_label_create(row);
    lv_label_set_text(ic, icon);
    lv_obj_set_style_text_font(ic, &HPI_FONT_ICON_MD, 0);   /* Material Symbols 26 */
    lv_obj_set_style_text_color(ic, lv_color_hex(icon_col), 0);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, &HPI_FONT_LABEL, 0);    /* Manrope 22 */
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xE6EAEC), 0);
    lv_obj_set_flex_grow(lbl, 1);

    lv_obj_t *val = lv_label_create(row);
    lv_obj_set_style_text_font(val, &HPI_FONT_LABEL, 0);
    row_refresh(val, id);

    if (clickable) {
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, row_click_cb, LV_EVENT_CLICKED, val);
    }
    return row;
}

void draw_scr_settings(enum scroll_dir m_scroll_dir, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    ARG_UNUSED(a1); ARG_UNUSED(a2); ARG_UNUSED(a3); ARG_UNUSED(a4);

    scr_settings = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_settings, lv_color_hex(0x0E1114), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr_settings, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(scr_settings, LV_OBJ_FLAG_SCROLLABLE);

    /* header: back chip + SETTINGS title */
    lv_obj_t *hdr = lv_label_create(scr_settings);
    lv_label_set_text(hdr, "SETTINGS");
    lv_obj_set_style_text_font(hdr, &HPI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(hdr, lv_color_hex(V2_LABEL), 0);
    lv_obj_set_style_text_letter_space(hdr, 4, 0);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 20, 40);

    /* Back chip = the reliable exit. Swipe-down (gesture_down_scr_settings) is
     * ALSO wired, but the scrollable list below captures every vertical drag as a
     * scroll (LVGL claims any ver-scrollable under the finger), so the swipe never
     * reaches the screen -- the chip and the crown button are the exits that work.
     * Give it a visible circular surface + hairline border so it reads as a
     * button rather than a bare glyph, plus a >=44px hit area. */
    lv_obj_t *back = lv_obj_create(scr_settings);
    lv_obj_remove_style_all(back);
    lv_obj_set_size(back, 48, 48);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 18, 24);
    lv_obj_clear_flag(back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(back, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(back, 24, 0);            /* ~9% soft surface */
    lv_obj_set_style_border_width(back, 1, 0);
    lv_obj_set_style_border_color(back, lv_color_hex(0x3A4247), 0);
    lv_obj_set_style_border_opa(back, LV_OPA_COVER, 0);
    lv_obj_set_ext_click_area(back, 8);              /* forgiving touch target */
    lv_obj_add_event_cb(back, back_chip_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_ic = lv_label_create(back);
    lv_label_set_text(back_ic, SYM_BACK);
    lv_obj_set_style_text_font(back_ic, &HPI_FONT_ICON, 0);
    lv_obj_set_style_text_color(back_ic, lv_color_hex(0xEEF1F2), 0);
    lv_obj_center(back_ic);

    /* scrollable list */
    lv_obj_t *list = lv_obj_create(scr_settings);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 300, 300);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 78);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list, 10, 0);
    lv_obj_set_style_pad_bottom(list, 70, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    /* The SH8601 is SPI-bound (~74 ms per full frame), so momentum throw and
     * elastic rubber-band -- both multi-frame animations -- feel slow and draggy.
     * Track the finger 1:1 and stop on release instead, exactly as the carousel
     * tileview does for the same panel. */
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLL_ELASTIC);

    make_row(list, SYM_BRIGHT_6,  0x8B9498, "Brightness",   ROW_BRIGHT, true);
    make_row(list, SYM_AOD,       0x8B9498, "Always-on",    ROW_AOD,    true);
    make_row(list, SYM_WATCH,     0x8B9498, "Watch face",   ROW_FACE,   true);
    make_row(list, SYM_THERMO,    0x8B9498, "Units",        ROW_UNITS,  true);
    make_row(list, SYM_SCHEDULE,  0x8B9498, "Time format",  ROW_TIMEFMT,true);
    make_row(list, SYM_BRIGHT_HI, 0x8B9498, "Accent",       ROW_ACCENT, true);
    make_row(list, SYM_DIAL,      0x8B9498, "Dial",         ROW_MOTIF,  true);
    make_row(list, SYM_BEDTIME,   0x8B9498, "Sleep after",  ROW_SLEEP,  true);
    make_row(list, SYM_HEIGHT,    0x8B9498, "Height",       ROW_HEIGHT, true);
    make_row(list, SYM_WEIGHT,    0x8B9498, "Weight",       ROW_WEIGHT, true);
    make_row(list, SYM_HAND,      0x8B9498, "Hand worn",    ROW_HAND,   true);
    make_row(list, SYM_BLUETOOTH, 0x6FB3CC, "Bluetooth",    ROW_BT,     false);
    make_row(list, SYM_INFO,      0x8B9498, "About",        ROW_ABOUT,  false);

    hpi_disp_set_curr_screen(SCR_SPL_SETTINGS);
    hpi_show_screen(scr_settings, m_scroll_dir);
}

void gesture_down_scr_settings(void)
{
    /* Defer, not hpi_carousel_show(): this is reached from the back chip's
     * LV_EVENT_CLICKED (and, where it fires, the swipe gesture), i.e. from inside
     * LVGL input dispatch. Rebuilding the carousel synchronously there tears down
     * this screen -- including the back chip whose click is still being walked --
     * from within its own event. Queue it and let the display loop draw it on a
     * clean stack (same fix as the SpO2/BP measure screens). */
    hpi_load_scr_spl(SCR_HOME, SCROLL_DOWN, 0, 0, 0, 0);
}
