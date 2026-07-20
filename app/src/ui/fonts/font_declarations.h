/* HealthyPi Move — LVGL font declarations (v2 type system) */
/* Rubik = all numerals, Manrope = all labels. 4 text bins + 2 icon sizes.
 * Canonical role macros live in ui/hpi_r0_theme.h (HPI_FONT_*). */

LV_FONT_DECLARE(rubik_500_88);      // Hero numerals: time, HR, SpO2, temp
LV_FONT_DECLARE(rubik_500_32);      // Secondary numerals: chips, min/max, steps, …
LV_FONT_DECLARE(rubik_500_22);      // Small numerals: ECG countdown, AOD HR
LV_FONT_DECLARE(manrope_700_22);    // All labels / units / captions
LV_FONT_DECLARE(matsym_24);         // Material Symbols icons
LV_FONT_DECLARE(matsym_28);         // Material Symbols icons (large)
