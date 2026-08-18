/*
 * HealthyPi Move — Health Store: HRV-derived stress score (HS-2 P3 follow-on)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Stress today is EDA-only (gsr_algos.c: tonic + SCR-rate + amplitude), computed only
 * during a MANUAL 30-second GSR spot check, and scored on ABSOLUTE skin-conductance
 * values. That last part is the real problem: absolute EDA varies enormously between
 * people and even between sessions on the same person (electrode contact, hydration,
 * ambient temperature), so an absolute score is not comparable to anything -- not to
 * another user, and not to the same user yesterday.
 *
 * P3 made HRV continuous, which fixes both halves:
 *
 *   - RMSSD is the standard parasympathetic marker. When you are stressed it falls.
 *   - And crucially, it is scored AGAINST THE USER'S OWN BASELINE. An RMSSD of 30 ms is
 *     low for one person and perfectly normal for another; only the deviation from
 *     their own rolling baseline means anything. This is exactly how Whoop, Oura and
 *     Garmin do it, and it is why a personal baseline is not a refinement -- it is the
 *     whole basis of the metric.
 *
 * The mapping lives in a header of its own so it can be unit-tested without dragging in
 * the filesystem, zbus and the rest of the store.
 */

#ifndef HPI_HS_STRESS_H
#define HPI_HS_STRESS_H

#include <stdint.h>
#include <stdbool.h>

/* A baseline built from fewer than this many 5-minute HRV windows is not a baseline,
 * it is a guess. ~20 windows is roughly 100 minutes of valid, still, on-skin HRV --
 * realistically one decent night. Below it, report nothing rather than a number the
 * user would reasonably believe. */
// #define HPI_HS_STRESS_MIN_BASELINE_WINDOWS  20
#define HPI_HS_STRESS_MIN_BASELINE_PAIRS    600

/* Map RMSSD against the user's own baseline onto 0..100.
 *
 *   ratio = rmssd / baseline
 *     1.0  -> 50   (you are at your own normal)
 *     0.5  -> 100  (HRV halved: strongly suppressed)
 *     1.5  -> 0    (HRV well above your normal: relaxed)
 *
 * Linear in the ratio and clamped. Deliberately simple: a more elaborate curve would
 * imply a precision this signal does not have.
 *
 * Both arguments are ms x10 (the store's HRV scale). Returns 0..100, or -1 when the
 * inputs cannot support a score -- the caller MUST treat -1 as "no stress value",
 * never as "zero stress". */
static inline int32_t hpi_hs_stress_from_hrv(int32_t rmssd_x10, int32_t baseline_x10,
                                             uint32_t baseline_pairs)
{
    if (rmssd_x10 <= 0 || baseline_x10 <= 0 ||
        baseline_pairs < HPI_HS_STRESS_MIN_BASELINE_PAIRS) {
        return -1;
    }

    /* stress = 50 + (1 - ratio) * 100, in fixed point to stay off the FPU.
     *        = 50 + 100 - (100 * rmssd / baseline)
     *        = 150 - (100 * rmssd / baseline)                                  */
    int64_t score = 150 - ((int64_t)rmssd_x10 * 100) / baseline_x10;

    if (score < 0)   { score = 0; }
    if (score > 100) { score = 100; }
    return (int32_t)score;
}

#endif /* HPI_HS_STRESS_H */
