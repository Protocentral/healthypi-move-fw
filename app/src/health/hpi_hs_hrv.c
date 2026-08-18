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
#include "health/hpi_hs_record.h"

LOG_MODULE_REGISTER(hpi_hs_hrv, LOG_LEVEL_INF);

/* Defaulted so the module compiles standalone in the unit test (no app Kconfig). */
#if defined(CONFIG_HPI_HS_HRV_WINDOW_S)
#define HRV_WINDOW_S    CONFIG_HPI_HS_HRV_WINDOW_S
#else
#define HRV_WINDOW_S    600     /* Task-Force short-term HRV window */
#endif
#if defined(CONFIG_HPI_HS_HRV_MIN_PAIRS)
#define HRV_MIN_PAIRS   CONFIG_HPI_HS_HRV_MIN_PAIRS
#else
#define HRV_MIN_PAIRS   10
#endif
#if defined(CONFIG_HPI_HS_HRV_MIN_COVERAGE)
#define HRV_MIN_COVERAGE CONFIG_HPI_HS_HRV_MIN_COVERAGE
#else
#define HRV_MIN_COVERAGE 50
#endif
#define HRV_MIN_CONF 70


/* Physiological plausibility: 40..200 bpm. Same clamp the ECG HRV path uses. */
#define RR_MIN_MS   300
#define RR_MAX_MS   2000

#define ADJ_TOL_MS 200   /* floor above the relative band — covers the 160ms poll quantisation */
// extern int s_hrv_rid;
// extern int rr_check;
#if defined(CONFIG_HPI_HS_HRV_RR_RECORD)

#define RR_RUN_MAX_BEATS 256
struct rr_run_header {
    uint32_t t_start_ms;
    uint16_t n_beats;
};

static int s_rr_rid = 0;
static uint16_t s_run_rr[RR_RUN_MAX_BEATS];
static uint8_t  s_run_conf[RR_RUN_MAX_BEATS];
static uint16_t s_run_n;
static int64_t  s_run_start_ms;
static void rr_record_dump(int rid)
{
    if (rid <= 0) {
        return;
    }

    /* static, not stack-local: this runs on whatever thread calls stop(), and
     * this codebase has already been bitten once by a large local buffer
     * overflowing a shallow thread stack (see the ppg_ctrl_thread note
     * elsewhere in this project). */
    static uint8_t stage[1024];
    size_t   have = 0;   /* bytes currently buffered but not yet parsed */
    uint32_t off  = 0;   /* read offset into the record on flash        */
    bool     eof  = false;
    int      run_idx = 0;
    int      total_beats = 0;

    LOG_INF("===== RR run-record dump (record %d) =====", rid);

    while (!eof) {
        int rd = hpi_hs_rec_get((uint32_t)rid, off, &stage[have], sizeof(stage) - have, &eof);
        if (rd < 0) {
            LOG_ERR("rr dump: rec_get failed: %d", rd);
            break;
        }
        have += rd;
        off  += rd;

        /* Parse every complete run block currently sitting in stage[] */
        size_t parsed = 0;
        while (have - parsed >= sizeof(struct rr_run_header)) {
            struct rr_run_header hdr;
            memcpy(&hdr, &stage[parsed], sizeof(hdr));

            size_t block_len = sizeof(hdr) +
                (size_t)hdr.n_beats * (sizeof(uint16_t) + sizeof(uint8_t));

            if (have - parsed < block_len) {
                break;   /* header's here, payload isn't fully buffered yet - wait for more */
            }

            const uint16_t *rr   = (const uint16_t *)&stage[parsed + sizeof(hdr)];
            const uint8_t  *conf = &stage[parsed + sizeof(hdr) + hdr.n_beats * sizeof(uint16_t)];

            LOG_INF("--- run %d: start=%u ms, n_beats=%u ---", run_idx, hdr.t_start_ms, hdr.n_beats);
            k_msleep(5);   /* don't flood the log */
            for (int i = 0; i < hdr.n_beats; i++) {
                LOG_INF("  [%d] rr=%u ms conf=%u", i, rr[i], conf[i]);
                k_msleep(10);   /* don't flood the log */
            }

            total_beats += hdr.n_beats;
            run_idx++;
            parsed += block_len;
        }

        if (parsed > 0) {
            memmove(stage, &stage[parsed], have - parsed);   /* keep leftover partial bytes */
            have -= parsed;
        }
        if (rd == 0) {
            break;   /* nothing more available - avoid spinning */
        }
    }

    LOG_INF("===== RR dump end: %d runs, %d total beats =====", run_idx, total_beats);
}

static void rr_run_flush(void)
{
    if (s_run_n == 0 || s_rr_rid <= 0) {
        s_run_n = 0;
        return;
    }
    static uint8_t block[sizeof(struct rr_run_header) + RR_RUN_MAX_BEATS * (sizeof(uint16_t) + sizeof(uint8_t))];
    struct rr_run_header hdr = { .t_start_ms = (uint32_t)s_run_start_ms, .n_beats = s_run_n };
    size_t off = 0;
    memcpy(&block[off], &hdr, sizeof(hdr));                        off += sizeof(hdr);
    memcpy(&block[off], s_run_rr, s_run_n * sizeof(uint16_t));     off += s_run_n * sizeof(uint16_t);
    memcpy(&block[off], s_run_conf, s_run_n * sizeof(uint8_t));    off += s_run_n * sizeof(uint8_t);

    hpi_hs_rec_append((uint32_t)s_rr_rid, block, off);
    s_run_n = 0;
}
void hpi_hs_hrv_rr_record_start(void)
{
    s_run_n = 0;
    s_rr_rid = hpi_hs_rec_start(HPI_HS_SIG_HRV_RR, HPI_HS_SFMT_U16, 1, 1);
    if (s_rr_rid <= 0) {
        LOG_ERR("RR raw rec_start failed: %d", s_rr_rid);
        s_rr_rid = 0;
    }
}

void hpi_hs_hrv_rr_record_stop(void)
{
    rr_run_flush();
    if (s_rr_rid > 0) {
        int rid = s_rr_rid;          /* save it - s_rr_rid gets reset below */
        hpi_hs_rec_stop((uint32_t)s_rr_rid);
        s_rr_rid = 0;
         rr_record_dump(rid);
    }
}
#endif
struct hrv_win {
    bool     open;
    int64_t  win;          /* wall-clock window index = ts / HRV_WINDOW_S */
    int64_t  last_update_ms; /* monotonic ms of the last ACCEPTED beat */
    uint32_t n_runs;        /* count of contiguous runs seen this window */

    uint32_t n;            /* accepted intervals                          */
    int64_t  sum_rr;       /* Σ rr        (ms)                            */
    int64_t  sum_rr2;      /* Σ rr²       (ms²)  -> SDNN                  */
    int64_t  sum_dd2;      /* Σ (rr-prev)² (ms²) -> RMSSD                 */
    uint32_t n_dd;         /* successive-difference count                 */

    uint16_t prev_rr;      /* previous ACCEPTED interval                  */
    bool     prev_valid;   /* false after a reject: see the note below    */

    uint16_t last_seen;    /* de-dup: the hub repeats its last R-R        */
    uint16_t last_seen_valid; /* true if last_seen is a valid ACCEPTED beat   */

    #if defined(CONFIG_HPI_HS_HRV_LOSS_LOG)
    uint16_t rej_zero;
    uint16_t rej_conf;
    uint16_t rej_motion;
    uint16_t rej_skin;
    uint16_t rej_range;
    #endif
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

    if (s_w.n_dd < HRV_MIN_PAIRS || coverage < HRV_MIN_COVERAGE) {
       LOG_DBG("hrv: window discarded (n=%u pairs=%u) - too few valid pairs",s_w.n, s_w.n_dd);
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
    hpi_hs_record(HPI_HS_T_HRV_NPAIRS,   (int32_t)s_w.n_dd,        q, ts_end); /* count   */
    hpi_hs_record(HPI_HS_T_HRV_NBEATS,   (int32_t)s_w.n,           q, ts_end); /* count   */
    hpi_hs_record(HPI_HS_T_HRV_COVERAGE, coverage,                q, ts_end);  /* %      */

    #if defined(CONFIG_HPI_HS_HRV_LOSS_LOG)
    LOG_INF("hrv: n=%u pairs=%u rmssd=%.1f sdnn=%.1f meanRR=%.0fms "
    "(~%.0f bpm) covergae = %d reject: zero=%u conf=%u motion=%u skin=%u range=%u", s_w.n, s_w.n_dd, rmssd, sdnn, mean, 60000.0 / mean, coverage, s_w.rej_zero,
        s_w.rej_conf, s_w.rej_motion, s_w.rej_skin, s_w.rej_range);
    #else
    LOG_INF("hrv: n=%u pairs=%u rmssd=%.1f sdnn=%.1f meanRR=%.0fms (~%.0f bpm) coverage = %d", 
        s_w.n, s_w.n_dd, rmssd, sdnn, mean, 60000.0 / mean, coverage);
    #endif

    s_w.open = false;
}

static void hrv_open(int64_t ts)
{
    memset(&s_w, 0, sizeof(s_w));
    s_w.open = true;
    s_w.win  = ts / (int64_t)HRV_WINDOW_S;
    s_w.n_runs = 1;
}

void hpi_hs_hrv_feed(uint16_t rtor_ms, uint8_t rtor_conf, bool on_skin, bool still, int64_t ts_utc, int64_t now_ms)
{
    k_mutex_lock(&s_hrv_lock, K_FOREVER);

    /* ---- the gate ----
     * Motion destroys PRV. A permissive gate does not yield "more HRV data", it yields
     * a plausible-looking trend built from artefacts -- worse than no trend. */
    bool accept = (rtor_ms >= RR_MIN_MS) && (rtor_ms <= RR_MAX_MS) && on_skin && still;

    if (!accept) {
        /* A rejected beat BREAKS THE CHAIN. RMSSD is the RMS of SUCCESSIVE
         * differences, so pairing the next accepted beat with the one before a gap
         * would measure a difference across a hole in time -- inflating RMSSD, in the
         * direction that reads as "better recovery". Drop the successor pairing. */
        s_w.prev_valid = false;

        #if defined(CONFIG_HPI_HS_HRV_LOSS_LOG)
        if (rtor_ms == 0) {
            s_w.rej_zero++;
        } else if (rtor_conf < HRV_MIN_CONF) {
            s_w.rej_conf++;
        } else if (!still) {
            s_w.rej_motion++;
        } else if (!on_skin) {
            s_w.rej_skin++;
        } else if (rtor_ms < RR_MIN_MS || rtor_ms > RR_MAX_MS) {
            s_w.rej_range++;
        }
        #endif
        k_mutex_unlock(&s_hrv_lock);
        return;
    }
     /*
     * Calculate elapsed BEFORE updating last_seen / last_update_ms.
     */
    int64_t elapsed = 0;

    if (s_w.prev_valid) {
        elapsed = now_ms - s_w.last_update_ms;
    }

    /*
     * Check whether the hub is repeating its current R-R value.
     *
     * Same R-R shortly after the previous accepted beat:
     *     -> hub repetition, ignore it.
     *
     * Same R-R approximately one R-R period later:
     *     -> genuine new beat, accept it.
     */
    bool same_rr = s_w.last_seen_valid && (rtor_ms == s_w.last_seen);

    if (same_rr && s_w.prev_valid) {
        bool new_same_rr_beat =
            (elapsed >= ((int64_t)rtor_ms) - ADJ_TOL_MS) &&
            (elapsed <= ((int64_t)rtor_ms) + ADJ_TOL_MS);

        if (!new_same_rr_beat) {
            k_mutex_unlock(&s_hrv_lock);
            return;
        }
    }

    s_w.last_seen = rtor_ms;
    s_w.last_seen_valid = true;

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

    bool adjacent = false;
    if (s_w.prev_valid) {

        adjacent = (elapsed >= ((int64_t)rtor_ms / 2) - ADJ_TOL_MS) &&
                (elapsed <= ((int64_t)rtor_ms * 3 / 2) + ADJ_TOL_MS);
    }

    if(adjacent)
    {
        int32_t d = (int32_t)rtor_ms - (int32_t)s_w.prev_rr;
        s_w.sum_dd2 += (int64_t)d * d;
        s_w.n_dd++;
    }
    else
    {
        s_w.n_runs++;
    }
    // #if defined(CONFIG_HPI_HS_HRV_RR_RECORD)
    // if (!adjacent) {
    //     rr_run_flush();              /* previous run just ended */
    //     s_run_start_ms = now_ms;     /* this beat starts a new run */
    // }
    // if (s_run_n < RR_RUN_MAX_BEATS) {
    //     s_run_rr[s_run_n]   = rtor_ms;
    //     s_run_conf[s_run_n] = rtor_conf;
    //     s_run_n++;
    // } else {
    //     rr_run_flush();
    //     s_run_start_ms = now_ms;
    //     s_run_rr[0] = rtor_ms;  s_run_conf[0] = rtor_conf;  s_run_n = 1;
    // }
    // #endif
    s_w.prev_rr    = rtor_ms;
    s_w.prev_valid = true;
    s_w.last_update_ms = now_ms;

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
