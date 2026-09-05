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
#define HRV_WINDOW_S    300     /* Task-Force short-term HRV window */
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
#define HRV_MIN_CONF 50
#define HRV_SDNN60_CHUNK_MS 60000 /* 60sec */

/* Physiological plausibility: 40..200 bpm. Same clamp the ECG HRV path uses. */
#define RR_MIN_MS   300
#define RR_MAX_MS   2000

#define ADJ_TOL_MS 200   /* floor above the relative band — covers the 160ms poll quantisation */

#if defined(CONFIG_HPI_HS_HRV_RR_RECORD)

#define RR_RUN_MAX_BEATS 512
struct rr_run_header {
    uint32_t t_start_ms;
    uint16_t n_beats;
    uint32_t t_stop_ms;
    uint32_t total_sum;
};

static int s_rr_rid = 0;
static uint16_t s_run_rr[RR_RUN_MAX_BEATS];
static uint8_t  s_run_conf[RR_RUN_MAX_BEATS];
static uint16_t s_run_n;
static int64_t  s_run_start_ms;
static int64_t  s_run_stop_ms;
static uint32_t s_total;

static void rr_run_flush(void)
{
    if (s_run_n == 0 || s_rr_rid <= 0) {
        s_run_n = 0;
        s_total = 0;
        return;
    }
    static uint8_t block[sizeof(struct rr_run_header) + RR_RUN_MAX_BEATS * (sizeof(uint16_t) + sizeof(uint8_t))];
    struct rr_run_header hdr = { .t_start_ms = (uint32_t)s_run_start_ms, .n_beats = s_run_n, .t_stop_ms = (uint32_t)s_run_stop_ms, .total_sum = s_total};
    size_t off = 0;
    memcpy(&block[off], &hdr, sizeof(hdr));                        off += sizeof(hdr);
    memcpy(&block[off], s_run_rr, s_run_n * sizeof(uint16_t));     off += s_run_n * sizeof(uint16_t);
    memcpy(&block[off], s_run_conf, s_run_n * sizeof(uint8_t));    off += s_run_n * sizeof(uint8_t);
    hpi_hs_rec_append((uint32_t)s_rr_rid, block, off);
    s_run_n = 0; s_total = 0;
}
void hpi_hs_hrv_rr_record_start(void)
{
    s_run_n = 0; s_total = 0;
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
    }
}
#endif

struct hs_sdnn60_acc {
    /* the in-progress chunk, within the CURRENT unbroken run */
    uint32_t chunk_n;
    int64_t  chunk_sum_rr;
    int64_t  chunk_sum_rr2;
    int64_t  chunk_beat_ms;

    /* completed 60s chunks, pooled as RMS of their SDNNs */
    double   sum_sdnn2;
    uint32_t n_chunks;
};

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

    struct hs_sdnn60_acc sdnn60; 

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

static void hs_sdnn60_run_break(struct hs_sdnn60_acc *a);
static void hs_sdnn60_feed(struct hs_sdnn60_acc *a, uint16_t rtor_ms);
static double hs_sdnn60_result(const struct hs_sdnn60_acc *a);

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
    double rmssd = sqrt((double)s_w.sum_dd2 / (double)s_w.n_dd);

    double sdnn60 = hs_sdnn60_result(&s_w.sdnn60);
    bool   sdnn60_valid = (sdnn60 >= 0.0);

    /* The window is by construction still + on-skin + high-confidence: say so, so
     * downstream baselines can filter on it. (HPI_HS_Q_LOW_MOTION had no producer at
     * all before this -- the bit existed but nothing ever set it.) */
    uint8_t q = HPI_HS_Q_VALID | HPI_HS_Q_ON_SKIN | HPI_HS_Q_LOW_MOTION | HPI_HS_Q_HIGH_CONF;

    hpi_hs_record(HPI_HS_T_HRV_RMSSD,    (int32_t)(rmssd * 10.0), q, ts_end);  /* ms x10 */
    hpi_hs_record(HPI_HS_T_HRV_SDNN,     (int32_t)(sdnn60  * 10.0), q, ts_end);  /* ms x10 */
    hpi_hs_record(HPI_HS_T_HRV_MEAN_RR,  (int32_t)mean,           q, ts_end);  /* ms     */
    hpi_hs_record(HPI_HS_T_HRV_NPAIRS,   (int32_t)s_w.n_dd,        q, ts_end); /* count   */
    hpi_hs_record(HPI_HS_T_HRV_NBEATS,   (int32_t)s_w.n,           q, ts_end); /* count   */
    hpi_hs_record(HPI_HS_T_HRV_COVERAGE, coverage,                q, ts_end);  /* %      */

    #if defined(CONFIG_HPI_HS_HRV_LOSS_LOG)
    LOG_INF("hrv: n=%u pairs=%u rmssd=%.1f sdnn=%.1f meanRR=%.0fms "
    "(~%.0f bpm) covergae = %d reject: zero=%u conf=%u motion=%u skin=%u range=%u", s_w.n, s_w.n_dd, rmssd, sdnn60, mean, 60000.0 / mean, coverage, s_w.rej_zero,
        s_w.rej_conf, s_w.rej_motion, s_w.rej_skin, s_w.rej_range);
    #else
    LOG_INF("hrv: n=%u pairs=%u rmssd=%.1f sdnn=%.1f meanRR=%.0fms (~%.0f bpm) coverage = %d", 
        s_w.n, s_w.n_dd, rmssd, sdnn60, mean, 60000.0 / mean, coverage);
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
    bool accept = (rtor_ms >= RR_MIN_MS) && (rtor_ms <= RR_MAX_MS) && (on_skin) && (still) && (rtor_conf >= HRV_MIN_CONF);

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
        hs_sdnn60_run_break(&s_w.sdnn60);
    }
    hs_sdnn60_feed(&s_w.sdnn60, rtor_ms); 

    #if defined(CONFIG_HPI_HS_HRV_RR_RECORD)
    if (!adjacent) {
        if(s_run_n > 0)
        {
            s_run_stop_ms = s_w.last_update_ms;
            rr_run_flush();              /* previous run just ended */
        }
        s_run_start_ms = now_ms;     /* this beat starts a new run */
    }
    if (s_run_n < RR_RUN_MAX_BEATS) {
        s_run_rr[s_run_n]   = rtor_ms;
        s_run_conf[s_run_n] = rtor_conf;
        s_run_n++;
        s_total += rtor_ms;
        s_run_stop_ms = now_ms;
    } else {
        int64_t keep_start = s_run_start_ms;
        rr_run_flush();
        s_run_start_ms = keep_start;
        s_run_rr[0] = rtor_ms;  s_run_conf[0] = rtor_conf;  s_run_n = 1;
        s_total = rtor_ms;
        s_run_stop_ms = now_ms;
    }
    #endif
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

/* A run just broke (adjacent == false). Discard only the in-progress chunk —
 * it was cut short mid-run, so it gets no partial credit. Completed chunks
 * already pooled are untouched. */
static void hs_sdnn60_run_break(struct hs_sdnn60_acc *a)
{
    a->chunk_n = 0;
    a->chunk_sum_rr = 0;
    a->chunk_sum_rr2 = 0;
    a->chunk_beat_ms = 0;
}

/* Feed one ACCEPTED beat — call for every beat, whether it continues a run
 * or is the first beat of a fresh one (hs_sdnn60_run_break, if needed, must
 * be called BEFORE this for the same beat). */
static void hs_sdnn60_feed(struct hs_sdnn60_acc *a, uint16_t rtor_ms)
{
    a->chunk_n++;
    a->chunk_sum_rr  += rtor_ms;
    a->chunk_sum_rr2 += (int64_t)rtor_ms * rtor_ms;
    a->chunk_beat_ms += rtor_ms;

    if (a->chunk_beat_ms >= HRV_SDNN60_CHUNK_MS) {
        double mean = (double)a->chunk_sum_rr / (double)a->chunk_n;
        double var  = ((double)a->chunk_sum_rr2 / (double)a->chunk_n) - (mean * mean);
        if (var < 0.0) {
            var = 0.0;   /* float noise */
        }
        double sdnn = sqrt(var);

        a->sum_sdnn2 += sdnn * sdnn;
        a->n_chunks++;

        a->chunk_n = 0;
        a->chunk_sum_rr = 0;
        a->chunk_sum_rr2 = 0;
        a->chunk_beat_ms = 0;
    }
}

/* Returns the pooled SDNN60 in ms, or -1.0 if no chunk ever completed
 * (legitimate — e.g. the whole window was fragmented into runs under 60s). */
static double hs_sdnn60_result(const struct hs_sdnn60_acc *a)
{
    if (a->n_chunks == 0) {
        return -1.0;
    }
    return sqrt(a->sum_sdnn2 / (double)a->n_chunks);
}
