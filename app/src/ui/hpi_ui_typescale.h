/*
 * HealthyPi Move — UI type scale (P6 Step C)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * One consistent Inter type scale for the carousel + measurement/result
 * templates. Replaces the ad-hoc montserrat_14/20/24 mix (14px was below the
 * readable floor on the 390px round AMOLED) and the earlier "everything is
 * inter_semibold_24" flattening. Reference by ROLE, not size, so screens stay
 * consistent as the scale is tuned. Use as &FONT_ROLE in
 * lv_obj_set_style_text_font().
 */
#ifndef HPI_UI_TYPESCALE_H
#define HPI_UI_TYPESCALE_H

#include <lvgl.h>

/* v2 swap: the Inter scale folds into the 4-bin Rubik/Manrope system. Numerals
 * -> Rubik 32 (rubik_500_32), all text -> Manrope 22 (manrope_700_22). */
LV_FONT_DECLARE(rubik_500_32);
LV_FONT_DECLARE(manrope_700_22);

#define FONT_HERO_DUAL rubik_500_32     /* compound numeric hero, e.g. BP "120/80" */
#define FONT_TITLE     manrope_700_22   /* screen title / metric name              */
#define FONT_UNIT      manrope_700_22   /* units, date, secondary labels           */
#define FONT_BODY      manrope_700_22   /* status text                             */
#define FONT_CAPTION   manrope_700_22   /* tile call-to-action hint                */

#endif /* HPI_UI_TYPESCALE_H */
