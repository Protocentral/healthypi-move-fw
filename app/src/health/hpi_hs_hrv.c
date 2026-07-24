/*
 * HealthyPi Move — Health Store: continuous PPG-derived HRV (HS-2 P3)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Rationale: see hpi_hs_hrv.h and docs/HS_SYNC_REDESIGN_PLAN.md §3.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <math.h>
#include <string.h>

#include "health/hpi_hs_types.h"
#include "health/hpi_health_store.h"
#include "health/hpi_hs_hrv.h"

LOG_MODULE_REGISTER(hpi_hs_hrv, LOG_LEVEL_INF);

/* Defaulted so the module compiles standalone in the unit test (no app Kconfig). */
#if defined(CONFIG_HPI_HS_HRV_WINDOW_S)
#define HRV_WINDOW_S    CONFIG_HPI_HS_HRV_WINDOW_S
#else
#define HRV_WINDOW_S    300     /* Task-Force short-term HRV window */
#endif
#if defined(CONFIG_HPI_HS_HRV_MIN_CONF)
#define HRV_MIN_CONF    CONFIG_HPI_HS_HRV_MIN_CONF
#else
#define HRV_MIN_CONF    80
#endif
#if defined(CONFIG_HPI_HS_HRV_MIN_BEATS)
#define HRV_MIN_BEATS   CONFIG_HPI_HS_HRV_MIN_BEATS
#else
#define HRV_MIN_BEATS   30
#endif
#if defined(CONFIG_HPI_HS_HRV_MIN_COVERAGE)
#define HRV_MIN_COVERAGE CONFIG_HPI_HS_HRV_MIN_COVERAGE
#else
#define HRV_MIN_COVERAGE 50     /* % of the window backed by valid beats */
#endif

/* Physiological plausibility: 40..200 bpm. Same clamp the ECG HRV path uses. */
#define RR_MIN_MS   300
#define RR_MAX_MS   1500

struct hrv_win {
    bool     open;
    int64_t  win;          /* wall-clock window index = ts / HRV_WINDOW_S */

    uint32_t n;            /* accepted intervals                          */
    int64_t  sum_rr;       /* Σ rr        (ms)                            */
    int64_t  sum_rr2;      /* Σ rr²       (ms²)  -> SDNN                  */
    int64_t  sum_dd2;      /* Σ (rr-prev)² (ms²) -> RMSSD                 */
    uint32_t n_dd;         /* successive-difference count                 */

    uint16_t prev_rr;      /* previous ACCEPTED interval                  */
    bool     prev_valid;   /* false after a reject: see the note below    */

    uint16_t last_seen;    /* de-dup: the hub repeats its last R-R        */
};

static struct hrv_win s_w;
static K_MUTEX_DEFINE(s_hrv_lock);

/* Emit the window. Caller holds the lock. */
static void hrv_emit(void)
{
    if (!s_w.open) {
        return;
    }

    /* Coverage = how much of the wall-clock window is actually accounted for by
     * accepted beats. This is the quality signal: 300 clean beats in a 5-minute
     * window is ~100%; 40 beats scattered through a noisy one is ~13%, and the
     * resulting RMSSD means nothing. The client MUST reject low-coverage windows. */
    int32_t coverage = (int32_t)((s_w.sum_rr * 100) / ((int64_t)HRV_WINDOW_S * 1000));
    if (coverage > 100) {
        coverage = 100;   /* rounding / overlapping beats */
    }

    if (s_w.n < HRV_MIN_BEATS || coverage < HRV_MIN_COVERAGE || s_w.n_dd == 0) {
        LOG_DBG("hrv: window discarded (n=%u coverage=%d%%) - too sparse to mean anything",
                s_w.n, coverage);
        s_w.open = false;
        return;
    }

    int64_t ts_end = (s_w.win + 1) * (int64_t)HRV_WINDOW_S;

    double mean = (double)s_w.sum_rr / (double)s_w.n;
    double var  = ((double)s_w.sum_rr2 / (double)s_w.n) - (mean * mean);
    if (var < 0.0) {
        var = 0.0;   /* float noise */
    }
    double sdnn  = sqrt(var);
    double rmssd = sqrt((double)s_w.sum_dd2 / (double)s_w.n_dd);

    /* The window is by construction still + on-skin + high-confidence: say so, so
     * downstream baselines can filter on it. (HPI_HS_Q_LOW_MOTION had no producer at
     * all before this -- the bit existed but nothing ever set it.) */
    uint8_t q = HPI_HS_Q_VALID | HPI_HS_Q_ON_SKIN | HPI_HS_Q_LOW_MOTION | HPI_HS_Q_HIGH_CONF;

    hpi_hs_record(HPI_HS_T_HRV_RMSSD,    (int32_t)(rmssd * 10.0), q, ts_end);  /* ms x10 */
    hpi_hs_record(HPI_HS_T_HRV_SDNN,     (int32_t)(sdnn  * 10.0), q, ts_end);  /* ms x10 */
    hpi_hs_record(HPI_HS_T_HRV_MEAN_RR,  (int32_t)mean,           q, ts_end);  /* ms     */
    hpi_hs_record(HPI_HS_T_HRV_COVERAGE, coverage,                q, ts_end);  /* %      */

    LOG_INF("hrv: n=%u coverage=%d%% rmssd=%.1f sdnn=%.1f meanRR=%.0fms (~%.0f bpm)",
            s_w.n, coverage, rmssd, sdnn, mean, 60000.0 / mean);

    s_w.open = false;
}

static void hrv_open(int64_t ts)
{
    memset(&s_w, 0, sizeof(s_w));
    s_w.open = true;
    s_w.win  = ts / (int64_t)HRV_WINDOW_S;
}

void hpi_hs_hrv_feed(uint16_t rtor_ms, uint8_t rtor_conf, bool on_skin, bool still,
                     int64_t ts_utc)
{
    k_mutex_lock(&s_hrv_lock, K_FOREVER);

    /* ---- the gate ----
     * Motion destroys PRV. A permissive gate does not yield "more HRV data", it yields
     * a plausible-looking trend built from artefacts -- worse than no trend. */
    bool accept = (rtor_ms >= RR_MIN_MS) && (rtor_ms <= RR_MAX_MS) &&
                  (rtor_conf >= HRV_MIN_CONF) && on_skin && still;

    if (!accept) {
        /* A rejected beat BREAKS THE CHAIN. RMSSD is the RMS of SUCCESSIVE
         * differences, so pairing the next accepted beat with the one before a gap
         * would measure a difference across a hole in time -- inflating RMSSD, in the
         * direction that reads as "better recovery". Drop the successor pairing. */
        s_w.prev_valid = false;
        k_mutex_unlock(&s_hrv_lock);
        return;
    }

    /* De-dup: the hub reports its CURRENT R-R on every FIFO sample and only changes it
     * when a new beat is detected, so we see each interval many times over.
     *
     * Known limitation: two consecutive beats with an identical R-R (to the ms) are
     * indistinguishable from a repeat, so one gets dropped. That loses a
     * zero-successive-difference and biases RMSSD very slightly HIGH. Real R-R jitters
     * by several ms, so exact repeats are rare; the hub gives us no beat counter to do
     * better. The pre-existing ECG HRV path has the same limitation. */
    if (rtor_ms == s_w.last_seen) {
        k_mutex_unlock(&s_hrv_lock);
        return;
    }
    s_w.last_seen = rtor_ms;

    int64_t win = ts_utc / (int64_t)HRV_WINDOW_S;
    if (!s_w.open) {
        hrv_open(ts_utc);
    } else if (win != s_w.win) {
        hrv_emit();
        hrv_open(ts_utc);
    }

    s_w.n++;
    s_w.sum_rr  += rtor_ms;
    s_w.sum_rr2 += (int64_t)rtor_ms * rtor_ms;

    if (s_w.prev_valid) {
        int32_t d = (int32_t)rtor_ms - (int32_t)s_w.prev_rr;
        s_w.sum_dd2 += (int64_t)d * d;
        s_w.n_dd++;
    }
    s_w.prev_rr    = rtor_ms;
    s_w.prev_valid = true;

    k_mutex_unlock(&s_hrv_lock);
}

void hpi_hs_hrv_tick(int64_t now_utc)
{
    k_mutex_lock(&s_hrv_lock, K_FOREVER);
    if (s_w.open && (now_utc / (int64_t)HRV_WINDOW_S) > s_w.win) {
        hrv_emit();   /* the wearer took the watch off mid-window; do not leak it */
    }
    k_mutex_unlock(&s_hrv_lock);
}
