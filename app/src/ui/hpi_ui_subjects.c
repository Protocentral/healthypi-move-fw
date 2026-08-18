/*
 * HealthyPi Move
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * P6 step A: LVGL Observer data-binding layer (see hpi_ui_subjects.h).
 */

#include <zephyr/kernel.h>
#include <stdio.h>
#include <string.h>

#include "hpi_ui_subjects.h"
#include "ui/move_ui.h"
#include "ui/hpi_r0_theme.h"
#include "hpi_user_settings_api.h"
#include "hpi_user_profile.h"
#include "hpi_sys.h"

lv_subject_t subj_hr, subj_spo2, subj_ecg, subj_temp, subj_bp,
             subj_hrv, subj_gsr, subj_steps, subj_time, subj_ampm, subj_date, subj_batt,
             subj_stress, subj_act, subj_sec;
lv_subject_t subj_hr_resting, subj_hr_min, subj_hr_max, subj_temp_dev;
lv_subject_t subj_batt_charging;

/* current + previous string storage per string subject */
#define SUBJ_STR_LEN 16
#define DEF_STR_SUBJ(n) static char n##_cur[SUBJ_STR_LEN]; static char n##_prv[SUBJ_STR_LEN]
DEF_STR_SUBJ(hr); DEF_STR_SUBJ(spo2); DEF_STR_SUBJ(ecg); DEF_STR_SUBJ(temp);
DEF_STR_SUBJ(bp); DEF_STR_SUBJ(hrv); DEF_STR_SUBJ(gsr); DEF_STR_SUBJ(steps);
DEF_STR_SUBJ(time); DEF_STR_SUBJ(ampm); DEF_STR_SUBJ(date);
DEF_STR_SUBJ(hr_min); DEF_STR_SUBJ(hr_max);
static char act_cur[28]; static char act_prv[28];   /* "3.1 MI . 412 KCAL" */
static char temp_dev_cur[24]; static char temp_dev_prv[24];   /* "+0.3\xB0 baseline" */
static char hr_resting_cur[20]; static char hr_resting_prv[20];   /* "RESTING 62 BPM" */

void hpi_ui_subjects_init(void)
{
    lv_subject_init_string(&subj_hr, hr_cur, hr_prv, SUBJ_STR_LEN, "--");
    lv_subject_init_string(&subj_spo2, spo2_cur, spo2_prv, SUBJ_STR_LEN, "--");
    lv_subject_init_string(&subj_ecg, ecg_cur, ecg_prv, SUBJ_STR_LEN, "--");
    lv_subject_init_string(&subj_temp, temp_cur, temp_prv, SUBJ_STR_LEN, "--");
    lv_subject_init_string(&subj_bp, bp_cur, bp_prv, SUBJ_STR_LEN, "--");
    lv_subject_init_string(&subj_hrv, hrv_cur, hrv_prv, SUBJ_STR_LEN, "--");
    lv_subject_init_string(&subj_gsr, gsr_cur, gsr_prv, SUBJ_STR_LEN, "--");
    lv_subject_init_string(&subj_steps, steps_cur, steps_prv, SUBJ_STR_LEN, "--");
    lv_subject_init_string(&subj_time, time_cur, time_prv, SUBJ_STR_LEN, "00:00");
    lv_subject_init_string(&subj_ampm, ampm_cur, ampm_prv, SUBJ_STR_LEN, "");
    lv_subject_init_string(&subj_date, date_cur, date_prv, SUBJ_STR_LEN, "---  --- --");
    lv_subject_init_int(&subj_batt, 0);
    lv_subject_init_int(&subj_batt_charging, 0);
    lv_subject_init_int(&subj_stress, -1);
    lv_subject_init_int(&subj_sec, 0);   /* current second 0..59 (minimal-face seconds bar) */
    lv_subject_init_string(&subj_act, act_cur, act_prv, sizeof(act_cur), "--");
    lv_subject_init_string(&subj_hr_resting, hr_resting_cur, hr_resting_prv,
                           sizeof(hr_resting_cur), "RESTING");
    lv_subject_init_string(&subj_hr_min, hr_min_cur, hr_min_prv, SUBJ_STR_LEN, "--");
    lv_subject_init_string(&subj_hr_max, hr_max_cur, hr_max_prv, SUBJ_STR_LEN, "--");
    lv_subject_init_string(&subj_temp_dev, temp_dev_cur, temp_dev_prv, sizeof(temp_dev_cur),
                           "baseline forming");
}

void hpi_ui_subj_set_stress(int level)
{
    lv_subject_set_int(&subj_stress, level);
}

void hpi_ui_subj_set_activity(int steps)
{
    if (steps < 0) {
        steps = 0;
    }
    /* distance in 0.1-mile units: ~0.762 m/step / 1609.34 m/mi + step kcal */
    int dist_x10 = (int)(steps * 0.004735f);
    uint16_t kcal = hpi_get_kcals_from_steps((uint16_t)(steps > 65535 ? 65535 : steps));
    char b[28];
    snprintf(b, sizeof(b), "%d.%d MI · %u KCAL", dist_x10 / 10, dist_x10 % 10,
             (unsigned)kcal);
    lv_subject_copy_string(&subj_act, b);
}

/* --- setters --- */
static void set_int_str(lv_subject_t *s, int v)
{
    char b[SUBJ_STR_LEN];
    if (v > 0) {
        snprintf(b, sizeof(b), "%d", v);
    } else {
        strcpy(b, "--");
    }
    lv_subject_copy_string(s, b);
}

void hpi_ui_subj_set_hr(int hr)       { set_int_str(&subj_hr, hr); }
void hpi_ui_subj_set_spo2(int spo2)   { set_int_str(&subj_spo2, spo2); }
void hpi_ui_subj_set_ecg_hr(int hr)   { set_int_str(&subj_ecg, hr); }
void hpi_ui_subj_set_hrv_sdnn(int s)  { set_int_str(&subj_hrv, s); }
void hpi_ui_subj_set_gsr(int gsr)     { set_int_str(&subj_gsr, gsr); }

void hpi_ui_subj_set_hr_resting(int bpm)
{
    char b[20];
    if (bpm > 0) {
        /* No trailing "BPM" — the hero already owns that unit (handoff HR
         * footer is just "RESTING"; we keep the number for glance value). */
        snprintf(b, sizeof(b), "RESTING %d", bpm);
    } else {
        strcpy(b, "RESTING");   /* not enough data yet */
    }
    lv_subject_copy_string(&subj_hr_resting, b);
}
void hpi_ui_subj_set_hr_min(int bpm)     { set_int_str(&subj_hr_min, bpm); }
void hpi_ui_subj_set_hr_max(int bpm)     { set_int_str(&subj_hr_max, bpm); }

void hpi_ui_subj_set_temp_dev_x100(int dev_x100_c, bool valid)
{
    char b[24];
    if (!valid) {
        strcpy(b, "baseline forming");
    } else {
        /* Stored deviation is degC*100. Render in the user unit as a delta
         * (no +32 for °F — only scale *1.8). */
        int mag_x10;
        if (hpi_user_settings_get_temp_unit() == 0) {
            mag_x10 = dev_x100_c / 10;   /* degC*100 -> degC*10 */
        } else {
            mag_x10 = (dev_x100_c * 18) / 100;   /* degC*100 -> degF*10 */
        }
        char sign = (mag_x10 < 0) ? '-' : '+';
        int a = (mag_x10 < 0) ? -mag_x10 : mag_x10;
        snprintf(b, sizeof(b), "%c%d.%d\xC2\xB0 baseline", sign, a / 10, a % 10);
    }
    lv_subject_copy_string(&subj_temp_dev, b);
}

void hpi_ui_subj_set_temp_x100(int x)
{
    char b[SUBJ_STR_LEN];
    if (x > 0) {
        snprintf(b, sizeof(b), "%d.%d", x / 100, (x / 10) % 10);
    } else {
        strcpy(b, "--");
    }
    lv_subject_copy_string(&subj_temp, b);
}

void hpi_ui_subj_set_bp(int sys, int dia)
{
    char b[SUBJ_STR_LEN];
    if (sys > 0) {
        snprintf(b, sizeof(b), "%d/%d", sys, dia);
    } else {
        strcpy(b, "--");
    }
    lv_subject_copy_string(&subj_bp, b);
}

void hpi_ui_subj_set_steps(int steps)
{
    char b[SUBJ_STR_LEN];
    if (steps < 0) {
        steps = 0;
    }
    /* thousands-grouped bare number (e.g. "6,524"); caption supplies "STEPS" */
    char n[16];
    snprintf(n, sizeof(n), "%d", steps);
    int len = strlen(n), gi = 0;
    for (int i = 0; i < len; i++) {
        if (i > 0 && (len - i) % 3 == 0) {
            b[gi++] = ',';
        }
        b[gi++] = n[i];
    }
    b[gi] = '\0';
    lv_subject_copy_string(&subj_steps, b);
}

void hpi_ui_subj_set_batt(int level, bool charging)
{
    if (level < 0) {
        level = 0;
    }
    lv_subject_set_int(&subj_batt, level);
    lv_subject_set_int(&subj_batt_charging, charging ? 1 : 0);
}

void hpi_ui_format_ago(int64_t ts_utc, char *buf, size_t buf_len)
{
    if (buf == NULL || buf_len == 0) {
        return;
    }
    if (ts_utc <= 0 || !hpi_sys_is_time_valid()) {
        snprintf(buf, buf_len, "--");
        return;
    }
    int64_t now = hw_get_sys_time_ts();
    if (now <= 0) {
        snprintf(buf, buf_len, "--");
        return;
    }
    int64_t d = now - ts_utc;
    if (d < 0) {
        d = 0;
    }
    /* Prefer %u over %lld — safer with picolibc printf config on-device. */
    if (d < 60) {
        snprintf(buf, buf_len, "JUST NOW");
    } else if (d < 3600) {
        snprintf(buf, buf_len, "%uM AGO", (unsigned)(d / 60));
    } else if (d < 86400) {
        snprintf(buf, buf_len, "%uH AGO", (unsigned)(d / 3600));
    } else {
        snprintf(buf, buf_len, "%uD AGO", (unsigned)(d / 86400));
    }
}

void hpi_ui_subj_set_time(struct tm t)
{
    static const char dow[7][4] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char mon[12][4] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    char b[SUBJ_STR_LEN];

    /* subj_time stays letter-free (the Hero font has no A-Z); AM/PM is a
     * separate Label-size subject. */
    if (hpi_user_settings_get_time_format() == 0) {
        snprintf(b, sizeof(b), "%02d:%02d", t.tm_hour, t.tm_min);
        lv_subject_copy_string(&subj_time, b);
        lv_subject_copy_string(&subj_ampm, "");
    } else {
        int h = t.tm_hour % 12; if (h == 0) h = 12;
        snprintf(b, sizeof(b), "%d:%02d", h, t.tm_min);
        lv_subject_copy_string(&subj_time, b);
        lv_subject_copy_string(&subj_ampm, t.tm_hour >= 12 ? "PM" : "AM");
    }

    int wd = (t.tm_wday >= 0 && t.tm_wday < 7) ? t.tm_wday : 0;
    int mo = (t.tm_mon >= 0 && t.tm_mon < 12) ? t.tm_mon : 0;
    snprintf(b, sizeof(b), "%s, %s %d", dow[wd], mon[mo], t.tm_mday);   /* e.g. "FRI, FEB 2" */
    lv_subject_copy_string(&subj_date, b);

    if (t.tm_sec >= 0 && t.tm_sec < 60) {
        lv_subject_set_int(&subj_sec, t.tm_sec);   /* minimal-face seconds progress */
    }
}

/* --- observers --- */
static void str_observer_cb(lv_observer_t *observer, lv_subject_t *subject)
{
    lv_obj_t *label = lv_observer_get_target_obj(observer);
    if (label != NULL) {
        lv_label_set_text(label, lv_subject_get_string(subject));
    }
}

void hpi_ui_bind_label(lv_obj_t *label, lv_subject_t *subject)
{
    lv_subject_add_observer_obj(subject, str_observer_cb, label, NULL);
}

/* Percent is always "NN%" (caller owns the battery icon). Charging state is
 * shown by swapping the matsym icon to battery_charging_full + green tint. */
static void batt_pct_paint(lv_obj_t *label)
{
    if (label == NULL) {
        return;
    }
    int level = lv_subject_get_int(&subj_batt);
    if (level < 0) {
        level = 0;
    }
    lv_label_set_text_fmt(label, "%d%%", level);
}

static void batt_pct_observer_cb(lv_observer_t *observer, lv_subject_t *subject)
{
    ARG_UNUSED(subject);
    batt_pct_paint(lv_observer_get_target_obj(observer));
}

void hpi_ui_bind_batt_pct(lv_obj_t *label)
{
    if (label == NULL) {
        return;
    }
    lv_subject_add_observer_obj(&subj_batt, batt_pct_observer_cb, label, NULL);
    batt_pct_paint(label);
}

static void batt_icon_paint(lv_obj_t *icon)
{
    if (icon == NULL) {
        return;
    }
    bool chg = lv_subject_get_int(&subj_batt_charging) != 0;
    /* Idle: battery_full cyan. Charging: battery_charging_full (bolt-in-battery) green. */
    lv_label_set_text(icon, chg ? SYM_BATT_CHG : SYM_BATT);
    lv_obj_set_style_text_color(icon, lv_color_hex(chg ? V2_GREEN : V2_SPO2), 0);
    lv_obj_set_style_text_opa(icon, LV_OPA_COVER, 0);
}

static void batt_icon_observer_cb(lv_observer_t *observer, lv_subject_t *subject)
{
    ARG_UNUSED(subject);
    batt_icon_paint(lv_observer_get_target_obj(observer));
}

void hpi_ui_bind_batt_icon(lv_obj_t *icon_label)
{
    if (icon_label == NULL) {
        return;
    }
    lv_subject_add_observer_obj(&subj_batt_charging, batt_icon_observer_cb, icon_label, NULL);
    batt_icon_paint(icon_label);
}
