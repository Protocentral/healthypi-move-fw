/*
 * HealthyPi Move — Health Store: module public API (H0 spec freeze)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * The single owner of health-data ingest, storage, statistics, derived metrics,
 * and sync. Every sensor/SMF records through hpi_hs_record(); every consumer (UI,
 * BLE/MCUmgr) queries through the stats/summary/sync API. Nothing else touches
 * trend files or the measurement NVS keys once this lands (status:
 * docs/ARCHITECTURE_REWRITE_PLAN.md §0; wire contract: docs/HPI_HS_API.md).
 *
 * H0 = interface only; no implementation yet. H1 wires ingest (dual-writing the
 * legacy trend files behind it); H2 adds correct stats + derived; H3 the durable
 * log; H4 the MCUmgr sync group.
 */

#ifndef HPI_HEALTH_STORE_H
#define HPI_HEALTH_STORE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "health/hpi_hs_types.h"

/* ---- Ingest -------------------------------------------------------------
 * The ONE entry point. Called from the data/sensor threads. Cheap and
 * non-blocking: enqueues into the RAM ring; the store's own thread persists.
 *
 * The store gates on quality: a sample missing HPI_HS_Q_VALID is dropped;
 * discrete metrics used for baselines/resting-HR additionally require the
 * relevant context bits. Pass the best-known quality; the store decides.
 *
 * `value` is fixed-point per the type's `scale` (e.g. skin temp degC*100).
 */
void hpi_hs_record(uint8_t type, int32_t value, uint8_t quality, int64_t ts_utc);

/* ---- Query-time statistics (statistically correct) ----------------------
 * Aggregated from raw samples over [from,to] (UTC seconds). `mean` is the TRUE
 * mean (sum/count), never an average-of-averages. Percentiles are 0 for
 * CUMULATIVE types; `sum` is the meaningful field there.
 */
struct hpi_hs_stats {
    uint32_t count;     /* number of samples that passed the quality filter    */
    int64_t  sum;       /* Σ value (for CUMULATIVE, this is the total)         */
    int32_t  mean;      /* sum/count for DISCRETE (fixed-point per type)       */
    int32_t  min;
    int32_t  max;
    int32_t  p10, p50, p90;   /* percentiles for DISCRETE, else 0             */
    int64_t  first_ts, last_ts;
};

/* Optional quality mask: only samples with (quality & q_require)==q_require are
 * counted (0 = "any VALID sample"). Returns 0 on success, <0 on error. */
int hpi_hs_stats(uint8_t type, int64_t from, int64_t to,
                 uint8_t q_require, struct hpi_hs_stats *out);

/* Downsampled series for plotting: fills exactly `max_buckets` evenly-spaced
 * buckets over [from,to). Replaces the per-screen hourly/minutely loaders.
 * `struct hpi_hs_bucket` and the bucket-reduction rules (notably **count == 0
 * means NO DATA, not zero** — plot gaps, not dives to 0) live in
 * hpi_hs_series.h, which is where the pure bucketing lives and is tested.
 *
 * Cost: one windowed pass over the durable log. Segments whose whole ts range
 * falls outside [from,to] are skipped via the P6 per-segment ts cache, so a 24 h
 * query does not read a week of flash. Still does file I/O — call it from the
 * store thread and cache the result; never from the display thread.
 *
 * Returns 0 on success, -EINVAL (bad args / to <= from), -ENOENT (unknown type).
 */
#include "hpi_hs_series.h"
int hpi_hs_series(uint8_t type, int64_t from, int64_t to,
                  struct hpi_hs_bucket *buckets, uint16_t max_buckets,
                  uint16_t *out_n);

/* ---- P3: cached trend series for the metric tiles -----------------------
 * hpi_hs_series() does file I/O, so the UI must not call it: the store thread
 * refreshes these on the summary cadence (~5 min) and the display reads a
 * snapshot, the same contract as hpi_hs_summary().
 *
 * Deliberately compact (~52 B per registered type) - app-core RAM is tight, and
 * the tiles only need one number per point:
 *   mean[i]  - the bucket's mean, in the type's own fixed-point units
 *   valid    - bit i set = bucket i has data. **A clear bit is a GAP, not a
 *              zero** (watch off-skin / charging). Plot a break, or a charging
 *              window charts as a dive to 0 bpm.
 *   from/to  - the window the buckets span (bucket i starts at
 *              from + (to-from)*i/n)
 *
 * Registered types (hs_recompute_trends registry):
 *   HPI_HS_T_HR          last 24 h,  hourly   (n = 24)
 *   HPI_HS_T_SKIN_TEMP   last 7 days, daily   (n = 7)
 *   HPI_HS_T_SPO2        last 7 days, daily   (n = 7)
 *   HPI_HS_T_HRV_RMSSD   last 50 min, 5-min   (n = 10) — stress history bars
 *
 * Returns 0, -EINVAL (null out), or -ENOENT (no trend kept for `type`).
 * A never-yet-computed trend returns valid == 0 - render "no data", not zeros.
 */
#define HPI_HS_TREND_N 24
struct hpi_hs_trend {
    int16_t  mean[HPI_HS_TREND_N];
    uint32_t valid;          /* bit i = mean[i] holds real data */
    uint8_t  n;              /* buckets actually used (<= HPI_HS_TREND_N) */
    int64_t  from, to;
};
int hpi_hs_trend_get(uint8_t type, struct hpi_hs_trend *out);

/* ---- Derived metrics & today-summary ------------------------------------
 * Cheap cached read for at-a-glance UI and the SUMMARY sync command. Values are
 * fixed-point per their type; *_valid flags say whether enough data exists yet
 * (e.g. baselines need several nights). */
struct hpi_hs_summary {
    int64_t  day_start_ts;         /* local-midnight UTC seconds of "today"    */

    int32_t  hr_resting;   bool hr_resting_valid;    /* bpm                     */
    int32_t  hr_min, hr_avg, hr_max;                 /* today, bpm             */

    int32_t  spo2_avg, spo2_min;   bool spo2_valid;  /* today / last night, %  */

    int32_t  temp_dev_x100; bool temp_dev_valid;     /* Δ vs baseline, degC*100 */
    uint16_t temp_baseline_nights;                   /* nights in the baseline  */

    int32_t  hrv_sdnn_x10;  int32_t hrv_sdnn_base_x10; bool hrv_valid; /* today vs baseline */
    /* P3: RMSSD is the headline HRV metric and the basis of the stress score. */
    int32_t  hrv_rmssd_x10; int32_t hrv_rmssd_base_x10;
    uint16_t hrv_baseline_windows;   /* 5-min windows behind the baseline        */
    uint32_t hrv_rmssd_pairs;
    uint16_t hrv_wins;

    /* HRV-derived stress, 0..100, scored against the user's OWN baseline (an absolute
     * HRV number means nothing across people). `stress_hrv_valid` is false until the
     * baseline has enough windows -- report nothing rather than a number the user
     * would believe. Distinct from `stress_last`, which is the EDA spot check. */
    int32_t  stress_hrv;   bool stress_hrv_valid;

    /* H6: morning readiness / recovery, 0..100, from last night's sleep HRV +
     * resting HR vs the user's own baseline. Invalid until both baselines exist. */
    int32_t  readiness;    bool readiness_valid;

    /* Warm-up progress toward a valid readiness score, 0..100: fraction of the
     * required baseline data (sleep HRV windows + sleep-RHR epochs) accrued so
     * far. 100 does not by itself imply readiness_valid (a sleep-gated "today"
     * sample is also needed) — it exists so the UI can show "learning baseline"
     * progress instead of a bare "--" during the first nights of wear. */
    uint8_t  readiness_warmup_pct;

    uint32_t steps_today;
    uint32_t energy_today_kcal;

    int32_t  stress_last;   bool stress_valid;       /* 0..100                 */

    int64_t stress_last_ts;  /* UTC seconds of the last stress sample (EDA or HRV) */
};

int hpi_hs_summary(struct hpi_hs_summary *out);

/* ---- Sync engine hooks (used by hpi_hs_sync.c) --------------------------
 * Head sequence = the newest seq the store holds. `hpi_hs_read_since` copies up
 * to `max` packed samples with seq > since_seq into `buf` (must be
 * max*HPI_HS_SAMPLE_WIRE_SIZE bytes), oldest-first, setting *out_n / *next_seq /
 * *more.
 *
 * `hpi_hs_ack` records the client's durable cursor. It does **NOT** retire
 * anything today — retention is size-based (hs_retention()), which after HS-2
 * P1/P6 holds comfortably past a week. Dropping on a client's ack is an
 * optimisation and is unrecoverable if the client is wrong, so it stays unbuilt
 * until it can be validated on device. This matches the wire contract:
 * docs/HPI_HS_API.md makes ACK "Optional but recommended" and says the device
 * "may drop" — `rc:0` acknowledges receipt, not deletion, and clients learn what
 * is retrievable from HELLO's `oldest`. (The header previously claimed it
 * retired samples, in the present tense. It never did.) */
uint32_t hpi_hs_head_seq(void);
/* Oldest seq still retrievable (segment retention drops the tail). Returns
 * head+1 when the store holds nothing, so `oldest > head` == "empty" and a
 * client can tell an empty store from a stale cursor without probing. */
uint32_t hpi_hs_oldest_seq(void);
int  hpi_hs_read_since(uint32_t since_seq, uint8_t *buf, size_t buf_sz,
                       uint16_t max, uint16_t *out_n, uint32_t *next_seq, bool *more);
void hpi_hs_ack(uint32_t acked_seq);

/* Drain the RAM ring to the durable segments NOW, from the CALLER's context.
 * Normally the store thread does this every HS_FLUSH_MS. A producer that can
 * outrun the ring (HS_RING_N = 512) between flushes -- the synthetic generator
 * writes thousands of records in a burst -- must call this or the ring wraps and
 * the records are silently LOST. Safe only from the store thread or a thread that
 * cannot race it. */
void hpi_hs_flush_now(void);

/* Close every open epoch and push the ring + the latest-per-type snapshot to
 * flash, right now. Call from the deliberate power-off / reboot paths: the
 * store thread only persists /lfs/hs/lat on the ~5 minute summary cadence, and
 * a cumulative daily total (steps, active energy) sitting in an unemitted 60 s
 * epoch is otherwise lost on shutdown — the watch then resumes today's count
 * from a stale value. Safe from any thread (serialized against the store
 * thread's own file work). NOT for the fatal-error or watchdog paths: those
 * must not touch the filesystem. */
void hpi_hs_shutdown_flush(void);

/* Discard the durable sample log and restart it at a clean segment boundary.
 *
 * seq is NEVER rewound (it is rounded UP to the next segment), so a wipe cannot
 * collide with rows the app has already stored under the same (device, seq) key.
 * The app sees the jump through HELLO.oldest and resumes from there.
 *
 * This is the engine behind the user-facing erase (Settings > Erase Data on the
 * watch, HPI_HS_CMD_ERASE from the phone) — call hpi_storage_erase_health_data()
 * for that rather than this directly, so the bulk record tier goes with it. */
void hpi_hs_wipe_all(void);

#if defined(CONFIG_HPI_HS_SYNTH)
/* TEST ONLY alias of hpi_hs_wipe_all(), kept so the SYNTH path reads as the test
 * hook it is. */
void hpi_hs_test_wipe(void);
#endif

/* Early init: the ingest ring + lock, before any sensor can publish. Does NOT
 * touch the filesystem (not mounted yet at this point in boot). */
int hpi_hs_init(void);

/* Storage init: called AFTER the filesystem is mounted. Opens/creates the
 * durable log (/lfs/hs), restores the persisted seq cursor (kept monotonic
 * across reboots) and the latest-per-type snapshot. Starts durable flushing. */
int hpi_hs_storage_init(void);

/* Latest recorded sample of a type (RAM cache, restored from flash at boot).
 * Returns false if none exists yet. Used for boot-restore of on-screen values. */
bool hpi_hs_get_latest(uint8_t type, struct hpi_hs_sample *out);

#endif /* HPI_HEALTH_STORE_H */
