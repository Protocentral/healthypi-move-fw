/*
 * HealthyPi Move — R0 design-handoff theme (P6 redesign spike)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Signal-Amber color tokens + Saira/Jost/JetBrains font roles from the
 * design handoff (v2 design system), plus the two
 * reusable R0 widgets (background motif, waveform monitor).
 */
#ifndef HPI_R0_THEME_H
#define HPI_R0_THEME_H

#include <lvgl.h>

/* v2 type system (the v2 design system): Rubik for all numerals,
 * Manrope for all labels — 4 text bins + 2 icon sizes, heavily subsetted. */
LV_FONT_DECLARE(rubik_500_88);       /* Hero numerals (time, HR, SpO2, temp) */
LV_FONT_DECLARE(rubik_500_32);       /* Secondary numerals (chips, min/max…) */
LV_FONT_DECLARE(rubik_500_22);       /* Small numerals (ECG countdown, AOD HR) */
LV_FONT_DECLARE(manrope_700_22);     /* All labels / units / captions        */
LV_FONT_DECLARE(matsym_24);          /* Icons (status/pill/shade brightness) */
LV_FONT_DECLARE(matsym_26);          /* Settings-row icons                   */
LV_FONT_DECLARE(matsym_28);          /* Large icons (chips + shade tiles)    */
LV_FONT_DECLARE(matsym_fp_72);       /* fingerprint (BPT sensor-wear hero)   */

/* Material Symbols glyphs (UTF-8) */
#define SYM_HR     "\xEE\xA1\xBE"   /* favorite          U+E87E */
#define SYM_STEPS  "\xEE\x94\xB6"   /* directions_walk   U+E536 */
#define SYM_RUN    "\xEE\x95\xA6"   /* directions_run    U+E566 */
#define SYM_THERMO "\xEE\x87\xBF"   /* device_thermostat U+E1FF */
#define SYM_BATT   "\xEE\x86\xA5"   /* battery_full           U+E1A5 */
#define SYM_BATT_CHG "\xEE\x86\xA3" /* battery_charging_full  U+E1A3 */
#define SYM_TREND  "\xEE\xA3\xA5"   /* trending_up       U+E8E5 */

/* v2 shade / settings / status icon glyphs (see matsym_24/26/28) */
#define SYM_SETTINGS     "\xEE\xA2\xB8"   /* U+E8B8 settings          */
#define SYM_BACK         "\xEE\x97\x84"   /* U+E5C4 arrow_back        */
#define SYM_ARR_UP       "\xEE\x8C\x96"   /* U+E316 keyboard_arrow_up */
#define SYM_ARR_DOWN     "\xEE\x8C\x93"   /* U+E313 keyboard_arrow_down*/
#define SYM_BRIGHT_LO    "\xEE\x86\xAD"   /* U+E1AD brightness_low    */
#define SYM_BRIGHT_HI    "\xEE\x86\xAC"   /* U+E1AC brightness_high   */
#define SYM_BRIGHT_6     "\xEE\x8E\xAB"   /* U+E3AB brightness_6      */
#define SYM_AOD          "\xEF\x9A\xAC"   /* U+F6AC aod_watch         */
#define SYM_WATCH        "\xEE\x8C\xB4"   /* U+E334 watch             */
/* No SYM_VIBRATION: this hardware has no haptic motor, so the design's Haptics
 * row does not exist. Its glyph (U+F467) was dropped from matsym_26 in the
 * 2026-07-16 regen that added brightness_high + bedtime. */
#define SYM_BLUETOOTH    "\xEE\x86\xA7"   /* U+E1A7 bluetooth         */
#define SYM_BATT_SAVER   "\xEE\xBF\x9E"   /* U+EFDE battery_saver     */
#define SYM_INFO         "\xEE\xA2\x8E"   /* U+E88E info              */
/* No SYM_DND (U+F08F) / SYM_RING_VOL (U+F0DD): nav-audit P1-5 removed those shade
 * toggles (nothing in this firmware backs them), and their glyphs were dropped
 * from matsym_28 in the 2026-07-16 regen that added device_thermostat. */
#define SYM_FLASHLIGHT   "\xEF\x80\x8B"   /* U+F00B flashlight_on  (matsym_28) */
#define SYM_BEDTIME      "\xEF\x85\x99"   /* U+F159 bedtime        (matsym_26) */
#define SYM_TREND_DN     "\xEE\xA3\xA3"   /* U+E8E3 trending_down     */
#define SYM_TREND_FLAT   "\xEE\xA3\xA4"   /* U+E8E4 trending_flat     */
#define SYM_DIAL         "\xEF\x81\x83"   /* U+F043 pattern (dial motif) */
#define SYM_SCHEDULE     "\xEE\xBF\x96"   /* U+EFD6 schedule (time format) */
#define SYM_HEIGHT       "\xEE\xA8\x96"   /* U+EA16 height                */
#define SYM_WEIGHT       "\xEF\x80\xB9"   /* U+F039 monitor_weight        */
#define SYM_HAND         "\xEE\x9D\xA4"   /* U+E764 back_hand (hand worn)  */

/* v2 BPT / EDA measurement-flow icon glyphs */
#define SYM_SENSORS        "\xEE\x94\x9E"   /* U+E51E sensors (finger-sensor req) */
#define SYM_PLAY           "\xEE\x80\xB7"   /* U+E037 play_arrow (MEASURE)        */
#define SYM_CHECK_CIRCLE   "\xEF\x82\xBE"   /* U+F0BE check_circle (result)       */
#define SYM_REFRESH        "\xEE\x97\x95"   /* U+E5D5 refresh (AGAIN)             */
#define SYM_DO_NOT_TOUCH   "\xEF\x86\xB0"   /* U+F1B0 do_not_touch (HOLD STILL)   */
#define SYM_WARNING        "\xEF\x82\x83"   /* U+F083 warning (out-of-band result)*/
#define SYM_WATER_DROP     "\xEE\x9E\x98"   /* U+E798 water_drop (EDA interp)      */
#define SYM_TOUCH_APP      "\xEE\xA4\x93"   /* U+E913 touch_app (make contact)    */
#define SYM_FINGERPRINT    "\xEE\xA4\x8D"   /* U+E90D fingerprint (sensor-wear)   */
#define SYM_BLOOD_PRESSURE "\xEE\x82\x97"   /* U+E097 blood_pressure              */

/* ---- color tokens (Signal Amber system) ---- */
#define R0_ACCENT     0xF59E0B  /* Signal Amber       */
#define R0_ON_ACCENT  0x1F1300
#define R0_SPO2       0x6FB3CC
#define R0_INDIGO     0x8B84F0
#define R0_GREEN      0x16A34A
#define R0_WARNING    0xEA580C
#define R0_ERROR      0xDC2626
#define R0_TEXT       0xFFFFFF
#define R0_TEXT_0     0xEEF1F2  /* brightest near-white (status battery %) */
#define R0_STATUS     0xDFE4E6  /* home status-row text (bright)          */
#define R0_TEXT_1     0xF2F4F5
#define R0_TEXT_2     0xC7CED1
#define R0_TEXT_3     0x8B9498
#define R0_TEXT_4     0x7F888C
#define R0_MUTED      0x6B7478

/* ---- v2 exact color tokens (the v2 design system) ---- */
#define V2_VALUE      0xF4F6F7  /* bright values / hero text            */
#define V2_LABEL      0x98A1A5  /* primary label gray                   */
#define V2_MUTED      0x6B7478  /* muted label gray                     */
#define V2_MUTED2     0x7F888C  /* secondary muted                      */
#define V2_ACCENT     0xF59E0B  /* Signal Amber (default; Phase T = runtime) */
#define V2_SPO2       0x6FB3CC  /* SpO2 blue / battery / calories / stand */
#define V2_GREEN      0x16A34A  /* steps / move ring                    */
#define V2_INDIGO     0x8B84F0  /* stress / HRV                         */
#define V2_HRV_BAR    0x5B5599  /* HRV history bars                     */
#define V2_CHIP_BG    0xFFFFFF  /* soft container chip: white @ ~5% opa */
#define V2_CHIP_OPA   13        /* ~5% of 255                           */
#define V2_TINT_OPA   33        /* ~13% of 255 (amber/blue tint pills)  */
#define V2_BP         0x6FB3CC  /* blood pressure blue (shared w/ SpO2)  */
#define V2_BP_SLASH   0x3D4448  /* dim slash between sys/dia             */
#define V2_ON_BP      0x0C1A20  /* text on filled blue PROCEED button    */
#define V2_EDA        0x2FBDA8  /* EDA / GSR teal                        */
#define V2_EDA_TINT_OPA 33      /* ~13% teal tint pill                   */

/* v2 measurement-flow icon font (BPT sensor-wear). BP/EDA heroes now share the
 * standard HPI_FONT_HERO (rubik_500_88, which carries '-' '/' ':' for --, sys/dia
 * and time) — no separate mid-size hero bin. */
#define HPI_FONT_ICON_XL matsym_fp_72   /* 72 - fingerprint sensor-wear    */

/* ---- v2 reusable widgets (hpi_v2_widgets.c) ----
 * chip: rounded soft container (white @5%), centered flex row, 22px radius.
 * pill: full-radius tinted container (tint_rgb @ ~13% if tinted, else soft). */
lv_obj_t *hpi_v2_chip(lv_obj_t *parent);
lv_obj_t *hpi_v2_pill(lv_obj_t *parent, uint32_t tint_rgb, uint8_t tint_opa);

/* Runtime user-selectable accent (0=amber 1=blue 2=green 3=indigo). Accent-
 * following screens read hpi_accent_rgb() at build time; rebuild to re-theme. */
uint32_t hpi_accent_rgb(void);
int  hpi_accent_get(void);
void hpi_accent_set(int idx);

/* v2 "Dial" background motif (60 ticks + faint outer ring, accent-colored).
 * Add behind a tile/screen's content; gated by the motif on/off toggle. */
void hpi_v2_dial_bg(lv_obj_t *parent);
void hpi_v2_motif_set(int on);
int  hpi_v2_motif_get(void);

/* v2 UI prefs (persisted): watch face (0=digital/1=minimal), AOD, battery saver. */
int  hpi_v2_face_get(void);
void hpi_v2_face_set(int f);
int  hpi_v2_aod_get(void);
void hpi_v2_aod_set(int on);
int  hpi_v2_bsaver_get(void);
void hpi_v2_bsaver_set(int on);

/* v2 AOD face — enter/exit from display SMF sleep when hpi_v2_aod_get() is set.
 * With CONFIG_HPI_SH8601_HW_AOD the SMF then drives SH8601 AODMON; otherwise
 * software dim of normal mode. */
void hpi_v2_aod_enter(void);
void hpi_v2_aod_exit(void);

/* ---- v2 type roles: 4 text bins + 2 icon sizes (22px floor) ----
 * Canonical names are HPI_FONT_*; the R0_FONT_* aliases are kept so existing
 * screens compile unchanged through the swap. */
#define HPI_FONT_HERO    rubik_500_88     /* 88 - hero numerals              */
#define HPI_FONT_VALUE   rubik_500_32     /* 32 - secondary numerals         */
#define HPI_FONT_NUM_SM  rubik_500_22     /* 22 - small numerals             */
#define HPI_FONT_LABEL   manrope_700_22   /* 22 - all labels/units/captions  */
#define HPI_FONT_ICON    matsym_24        /* 24 - icons                      */
#define HPI_FONT_ICON_MD matsym_26        /* 26 - settings-row icons         */
#define HPI_FONT_ICON_LG matsym_28        /* 28 - large icons                */

#define R0_FONT_HERO    HPI_FONT_HERO
#define R0_FONT_VALUE   HPI_FONT_VALUE
#define R0_FONT_LABEL   HPI_FONT_LABEL
#define R0_FONT_UNIT    HPI_FONT_LABEL
#define R0_FONT_STATUS  HPI_FONT_LABEL
#define R0_FONT_ICON    HPI_FONT_ICON_LG
#define R0_FONT_ICON_SM HPI_FONT_ICON

/* ---- standard layout anchors (offset from screen CENTER, px) ----
 * The metric template positions everything off these; a redesign re-spaces the
 * whole family by editing here. Keep content within R0_SAFE_R of center. */
#define R0_SAFE_R    165   /* round safe-area radius (~30px edge inset)      */
#define R0_Y_TIME   (-150) /* home Hero clock                               */
#define R0_Y_TITLE  (-128) /* section title/label (no top-time on metrics)  */
#define R0_Y_VALUE   (-60) /* hero value row                                */
#define R0_Y_PLOT     (20) /* waveform / ring / plot                        */
#define R0_Y_STATUS   (74) /* accent status word (RESTING/NORMAL/...)       */
#define R0_Y_STATS   (130) /* footer stat columns                           */

/* ---- reusable R0 widgets ---- */

/* Bedside-monitor waveform: fixed-width trace with a sweeping erase gap.
 * Push normalized samples (~0..1, midline at 0). Opaque black bg + own grid. */
lv_obj_t *hpi_wave_monitor_create(lv_obj_t *parent, int w, int h, lv_color_t color);
void hpi_wave_monitor_push(lv_obj_t *wm, float sample);
void hpi_wave_monitor_push_auto(lv_obj_t *wm, int32_t raw);  /* real samples, auto-scaled (PPG) */
void hpi_wave_monitor_push_linear(lv_obj_t *wm, int32_t raw);/* raw, linear min/max window scale */
void hpi_wave_monitor_push_ecg(lv_obj_t *wm, int32_t raw);   /* ECG: HP baseline + envelope AGC */
void hpi_wave_monitor_push_eda(lv_obj_t *wm, int32_t raw);   /* EDA/BioZ: window envelope + min span */
void hpi_wave_monitor_reset(lv_obj_t *wm);                  /* reset adaptive scaler + clear trace */
void hpi_wave_monitor_set_window(lv_obj_t *wm, int n_samples);  /* samples shown across width */
void hpi_wave_monitor_demo(lv_obj_t *wm, int bpm);     /* self-managing synthetic PPG feed */
void hpi_wave_monitor_demo_ecg(lv_obj_t *wm, int bpm); /* ...ECG morphology */

/* SpO2-style range bar: red/amber/green zones + marker at frac [0..1]. */
lv_obj_t *hpi_r0_range_bar_create(lv_obj_t *parent, int w, float frac);

/* Trend sparkline from normalized points [0..1]. `create` marks every point
 * real; `set` takes an explicit validity bitmap (bit i = pts[i] is data).
 * A clear bit draws a BREAK, not a zero — an off-skin/charging gap must never
 * chart as a dive to 0. Isolated real points are drawn as dots. */
#define HPI_R0_SPARK_MAX 24
lv_obj_t *hpi_r0_sparkline_create(lv_obj_t *parent, int w, int h, uint32_t color, const float *pts, int n);
void hpi_r0_sparkline_set(lv_obj_t *o, const float *pts, uint32_t valid, int n);

#endif /* HPI_R0_THEME_H */
