/*
 * HealthyPi Move
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * P6 step A: LVGL Observer data-binding layer.
 *
 * One lv_subject per displayed metric. Producers call the setters (from the
 * LVGL/display thread only - LVGL is not thread-safe) whenever a value changes;
 * the subject notifies its observers only when the value actually changed, and
 * each bound label updates itself. This replaces the per-screen hpi_*_update()
 * functions and the carousel's poll timer.
 */

#ifndef HPI_UI_SUBJECTS_H
#define HPI_UI_SUBJECTS_H

#include <lvgl.h>
#include <time.h>
#include <stdbool.h>

/* Metric subjects (string, pre-formatted incl. "--" for no-data) + battery (int
 * level, so its observer can colour-code). */
extern lv_subject_t subj_hr, subj_spo2, subj_ecg, subj_temp, subj_bp,
                    subj_hrv, subj_gsr, subj_steps, subj_time, subj_ampm, subj_date, subj_batt,
                    subj_stress, subj_act, subj_sec, subj_recovery;

/* Derived (H2) subjects: resting HR, today HR min/max, skin-temp deviation vs
 * baseline. Fed from the health-store summary cache via hpi_disp_push_subjects. */
extern lv_subject_t subj_hr_resting, subj_hr_min, subj_hr_max, subj_temp_dev;

/* Recovery tile caption: "learning baseline N%" while the readiness baselines
 * are still forming, empty once a valid score is shown (same idea as
 * subj_temp_dev's "baseline forming"). */
extern lv_subject_t subj_recovery_sub;

/* 0/1 — home/status battery icon tint (nPM1300 charging flag). */
extern lv_subject_t subj_batt_charging;

void hpi_ui_subjects_init(void);

/* "JUST NOW" / "15M AGO" / "2H AGO" / "3D AGO" / "—" from sample ts_utc. */
void hpi_ui_format_ago(int64_t ts_utc, char *buf, size_t buf_len);

/* Setters - LVGL/display-thread only. */
void hpi_ui_subj_set_hr(int hr);
void hpi_ui_subj_set_spo2(int spo2);
void hpi_ui_subj_set_ecg_hr(int hr);
void hpi_ui_subj_set_temp_x100(int temp_x100);
void hpi_ui_subj_set_bp(int sys, int dia);
void hpi_ui_subj_set_hrv_sdnn(int sdnn);
void hpi_ui_subj_set_gsr(int gsr);
void hpi_ui_subj_set_steps(int steps);
void hpi_ui_subj_set_stress(int level);
/* H6 readiness/recovery, 0..100; valid=false -> hero shows "--" and the caption
 * subject shows "learning baseline N%" from warmup_pct (0..100, how much of the
 * required baseline data has accrued — see hpi_hs_summary.readiness_warmup_pct). */
void hpi_ui_subj_set_recovery(int32_t score, bool valid, int warmup_pct);
void hpi_ui_subj_set_activity(int steps);   /* formats "X.X MI . N KCAL" */
void hpi_ui_subj_set_batt(int level, bool charging);
void hpi_ui_subj_set_time(struct tm t);

/* Derived (H2) setters. resting/min/max in bpm (<=0 -> "--"). temp-dev takes the
 * signed deviation in degC*100 + a validity flag; it renders in the user's temp
 * unit as e.g. "+0.3\xC2\xB0 baseline", or "baseline forming" until valid. */
void hpi_ui_subj_set_hr_resting(int bpm);
void hpi_ui_subj_set_hr_min(int bpm);
void hpi_ui_subj_set_hr_max(int bpm);
void hpi_ui_subj_set_temp_dev_x100(int dev_x100_c, bool valid);

/* Bind a label to a string subject (generic text observer), or to the battery
 * subject. The observer auto-frees when the label is deleted. */
void hpi_ui_bind_label(lv_obj_t *label, lv_subject_t *subject);
/* SoC text: "NN%" (icon carries charging state). */
void hpi_ui_bind_batt_pct(lv_obj_t *label);
/* Battery icon: battery_full (cyan) idle; battery_charging_full (green) when charging. */
void hpi_ui_bind_batt_icon(lv_obj_t *icon_label);

#endif /* HPI_UI_SUBJECTS_H */
