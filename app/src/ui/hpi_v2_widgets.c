/*
 * HealthyPi Move — v2 reusable UI primitives (the v2 design system)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Small building blocks the v2 metric screens compose from: the soft rounded
 * "chip" and the full-radius "pill". Both are plain flex-row containers with no
 * scroll and no default padding surprises — callers add icon/value/label
 * children and set per-child fonts/colors.
 */

#include <lvgl.h>
#include <math.h>
#include <string.h>
#include <errno.h>
#include <zephyr/settings/settings.h>
#include "ui/move_ui.h"
#include "ui/hpi_r0_theme.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- runtime accent (v2: user-selectable amber/blue/green/indigo) ----
 * Single source the accent-following screens read at build time; changing it
 * and rebuilding the carousel re-themes the UI. Persistence + picker land with
 * the settings surface (later phase); default is Signal Amber. */
static const uint32_t s_accent_rgb[4] = {
    0xF59E0B,   /* 0 Signal Amber (default) */
    0x2C6E84,   /* 1 Brand Blue             */
    0x16A34A,   /* 2 Green                  */
    0x8B84F0,   /* 3 Indigo                 */
};
static int s_accent_idx = 0;

/* ---- v2 UI preferences (persisted under the "hpiui" settings subtree) ----
 * accent (0-3), watch face (0=digital/1=minimal), AOD, dial motif, battery
 * saver. All persist immediately on set via Zephyr settings. */
static int s_face_idx    = 0;
/* Default Always-on ON for new devices; persisted value overrides after load. */
static int s_aod_on      = 1;
static int s_bsaver_on   = 0;
/* s_motif_on defined below with the dial; declared here for the handler. */
extern int hpi_v2_motif_get(void);

uint32_t hpi_accent_rgb(void) { return s_accent_rgb[s_accent_idx]; }
int      hpi_accent_get(void) { return s_accent_idx; }
void     hpi_accent_set(int idx)
{
    if (idx >= 0 && idx < 4) {
        s_accent_idx = idx;
        settings_save_one("hpiui/accent", &s_accent_idx, sizeof(s_accent_idx));
    }
}

int  hpi_v2_face_get(void) { return s_face_idx; }
void hpi_v2_face_set(int f)
{
    s_face_idx = f ? 1 : 0;
    settings_save_one("hpiui/face", &s_face_idx, sizeof(s_face_idx));
}
int  hpi_v2_aod_get(void) { return s_aod_on; }
void hpi_v2_aod_set(int on)
{
    s_aod_on = on ? 1 : 0;
    settings_save_one("hpiui/aod", &s_aod_on, sizeof(s_aod_on));
}
int  hpi_v2_bsaver_get(void) { return s_bsaver_on; }
void hpi_v2_bsaver_set(int on)
{
    s_bsaver_on = on ? 1 : 0;
    settings_save_one("hpiui/bsaver", &s_bsaver_on, sizeof(s_bsaver_on));
}

/* ---- v2 "Dial" background motif (60 radial ticks + faint outer ring) ----
 * Drawn on the object's LV_EVENT_DRAW_MAIN (no persistent child objects, so
 * ~0 RAM); repaints only on invalidate. Accent-colored at low opacity. */
static int s_motif_on = 1;
void hpi_v2_motif_set(int on)
{
    s_motif_on = on ? 1 : 0;
    settings_save_one("hpiui/motif", &s_motif_on, sizeof(s_motif_on));
}
int  hpi_v2_motif_get(void)   { return s_motif_on; }

/* Zephyr settings loader for the v2 UI prefs (auto-applied at settings_load). */
static int hpi_v2_prefs_set(const char *name, size_t len,
                            settings_read_cb read_cb, void *cb_arg)
{
    int *dst = NULL;
    if (!strcmp(name, "accent")) dst = &s_accent_idx;
    else if (!strcmp(name, "face"))   dst = &s_face_idx;
    else if (!strcmp(name, "aod"))    dst = &s_aod_on;
    else if (!strcmp(name, "motif"))  dst = &s_motif_on;
    else if (!strcmp(name, "bsaver")) dst = &s_bsaver_on;
    else return -ENOENT;

    if (len == sizeof(int)) {
        read_cb(cb_arg, dst, sizeof(int));
    }
    return 0;
}
SETTINGS_STATIC_HANDLER_DEFINE(hpiui, "hpiui", NULL, hpi_v2_prefs_set, NULL, NULL);

static void dial_draw_cb(lv_event_t *e)
{
    if (!s_motif_on) {
        return;
    }
    lv_obj_t *obj = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    const int cx = (a.x1 + a.x2) / 2;
    const int cy = (a.y1 + a.y2) / 2;
    const lv_color_t col = lv_color_hex(hpi_accent_rgb());

    const int r_ring = 188;   /* faint outer ring radius        */
    const int r_out  = 190;   /* tick outer radius              */

    lv_draw_line_dsc_t ring;
    lv_draw_line_dsc_init(&ring);
    ring.color = col;
    ring.width = 1;
    ring.opa   = 18;          /* ~.07 */

    lv_draw_line_dsc_t tick;
    lv_draw_line_dsc_init(&tick);
    tick.color = col;
    tick.round_start = 1;
    tick.round_end   = 1;

    int first_x = 0, first_y = 0, prev_x = 0, prev_y = 0;
    for (int i = 0; i < 60; i++) {
        double ang = (double)i * 6.0 * M_PI / 180.0;   /* 60 ticks, 6deg apart */
        double ca = cos(ang), sa = sin(ang);

        /* outer ring as a 60-gon polyline */
        int rx = cx + (int)(r_ring * ca);
        int ry = cy + (int)(r_ring * sa);
        if (i == 0) {
            first_x = rx; first_y = ry;
        } else {
            ring.p1.x = prev_x; ring.p1.y = prev_y;
            ring.p2.x = rx;     ring.p2.y = ry;
            lv_draw_line(layer, &ring);
        }
        prev_x = rx; prev_y = ry;

        /* radial tick: majors every 5th, longer/brighter */
        bool major = (i % 5 == 0);
        int len = major ? 14 : 6;
        int r_in = r_out - len;
        tick.width = major ? 2 : 1;
        tick.opa   = major ? 46 : 22;   /* ~.18 / ~.085 */
        tick.p1.x = cx + (int)(r_in  * ca); tick.p1.y = cy + (int)(r_in  * sa);
        tick.p2.x = cx + (int)(r_out * ca); tick.p2.y = cy + (int)(r_out * sa);
        lv_draw_line(layer, &tick);
    }
    /* close the ring loop */
    ring.p1.x = prev_x;  ring.p1.y = prev_y;
    ring.p2.x = first_x; ring.p2.y = first_y;
    lv_draw_line(layer, &ring);
}

/* Add the dial motif behind a tile/screen's content. Create it FIRST (bottom of
 * the z-order) so content draws on top. Transparent, non-interactive. */
void hpi_v2_dial_bg(lv_obj_t *parent)
{
    lv_obj_t *bg = lv_obj_create(parent);
    lv_obj_remove_style_all(bg);
    lv_obj_set_size(bg, lv_pct(100), lv_pct(100));
    lv_obj_center(bg);
    lv_obj_remove_flag(bg, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bg, dial_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
}

/* Rounded soft chip: white @ ~5% opacity, 22px radius, centered row. */
lv_obj_t *hpi_v2_chip(lv_obj_t *parent)
{
    lv_obj_t *chip = lv_obj_create(parent);
    lv_obj_remove_style_all(chip);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(chip, 22, 0);
    lv_obj_set_style_bg_color(chip, lv_color_hex(V2_CHIP_BG), 0);
    lv_obj_set_style_bg_opa(chip, V2_CHIP_OPA, 0);
    lv_obj_set_style_pad_hor(chip, 16, 0);
    lv_obj_set_style_pad_ver(chip, 10, 0);
    lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(chip, 8, 0);
    return chip;
}

/* Full-radius pill. tint_opa 0 → soft white @5%; else tint_rgb @ tint_opa. */
lv_obj_t *hpi_v2_pill(lv_obj_t *parent, uint32_t tint_rgb, uint8_t tint_opa)
{
    lv_obj_t *pill = lv_obj_create(parent);
    lv_obj_remove_style_all(pill);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
    if (tint_opa == 0) {
        lv_obj_set_style_bg_color(pill, lv_color_hex(V2_CHIP_BG), 0);
        lv_obj_set_style_bg_opa(pill, V2_CHIP_OPA, 0);
    } else {
        lv_obj_set_style_bg_color(pill, lv_color_hex(tint_rgb), 0);
        lv_obj_set_style_bg_opa(pill, tint_opa, 0);
    }
    lv_obj_set_style_pad_hor(pill, 18, 0);
    lv_obj_set_style_pad_ver(pill, 9, 0);
    lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pill, 8, 0);
    return pill;
}
