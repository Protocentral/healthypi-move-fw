/*
 * HealthyPi Move — Health Store: morning readiness / recovery score (H6)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * "Readiness" (Whoop recovery / Oura readiness / Garmin body-battery) answers one
 * question: how recovered is the body this morning, versus its own normal? Two
 * nightly signals carry it, both scored as DEVIATION FROM THE USER'S OWN BASELINE
 * because the absolute numbers are not comparable across people:
 *
 *   - HRV (RMSSD) UP vs baseline  => parasympathetic rebound => recovered.
 *   - resting HR DOWN vs baseline => recovered.
 *
 * Both inputs are measured DURING SLEEP (the store sleep-gates them) so the score
 * reflects overnight recovery, not daytime activity. The mapping lives in its own
 * header so it can be unit-tested without the filesystem / zbus / the rest of the
 * store, exactly like hpi_hs_stress.h.
 */

#ifndef HPI_HS_READINESS_H
#define HPI_HS_READINESS_H

#include <stdint.h>

/* Minimum nightly data behind each baseline before readiness means anything —
 * below these, report nothing (a premature recovery score is one the user would
 * act on). ~20 five-minute HRV windows and ~20 sleep-HR epochs is about one
 * decent night each. */
#define HPI_HS_READINESS_MIN_HRV_WINDOWS  20
#define HPI_HS_READINESS_MIN_RHR_N        20

/* Morning recovery score, 0..100 (higher = more recovered), from last night's
 * sleep vs the user's OWN rolling baseline.
 *
 *   rmssd_today_x10 / rmssd_base_x10 : last night's / 7-day-baseline sleep RMSSD (ms x10)
 *   rhr_today / rhr_base             : last night's / 7-day-baseline sleep resting HR (bpm)
 *   hrv_windows                      : 5-min HRV windows behind the RMSSD baseline
 *   rhr_n                            : sleep-HR epochs behind the RHR baseline
 *
 * All fixed-point (no FPU). Returns -1 when the baselines are too thin — the
 * caller MUST treat -1 as "no readiness value", never as "0 readiness".
 */
static inline int32_t hpi_hs_readiness(int32_t rmssd_today_x10, int32_t rmssd_base_x10,
                                       int32_t rhr_today, int32_t rhr_base,
                                       uint32_t hrv_windows, uint32_t rhr_n)
{
    if (rmssd_today_x10 <= 0 || rmssd_base_x10 <= 0 ||
        rhr_today <= 0 || rhr_base <= 0 ||
        hrv_windows < HPI_HS_READINESS_MIN_HRV_WINDOWS ||
        rhr_n < HPI_HS_READINESS_MIN_RHR_N) {
        return -1;
    }

    /* HRV sub-score: 100 * rmssd/base - 50, clamped. ratio 0.5 -> 0, 1.0 -> 50,
     * 1.5 -> 100 (HRV well above your normal reads as fully recovered). */
    int64_t hrv_score = ((int64_t)rmssd_today_x10 * 100) / rmssd_base_x10 - 50;
    if (hrv_score < 0)   { hrv_score = 0; }
    if (hrv_score > 100) { hrv_score = 100; }

    /* RHR sub-score: lower resting HR than baseline is better. 5 points per bpm
     * around a 50 midpoint (10 bpm below baseline -> 100, 10 above -> 0). */
    int64_t rhr_score = 50 + (int64_t)(rhr_base - rhr_today) * 5;
    if (rhr_score < 0)   { rhr_score = 0; }
    if (rhr_score > 100) { rhr_score = 100; }

    /* HRV is the stronger recovery signal, so weight it 60/40 over RHR. */
    int64_t score = (hrv_score * 60 + rhr_score * 40) / 100;
    if (score < 0)   { score = 0; }
    if (score > 100) { score = 100; }
    return (int32_t)score;
}

#endif /* HPI_HS_READINESS_H */
