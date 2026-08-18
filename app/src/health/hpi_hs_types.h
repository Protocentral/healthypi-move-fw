/*
 * HealthyPi Move — Health Store: type registry & sample schema (H0 spec freeze)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * This header is the FROZEN CONTRACT for the on-device health-data store and the
 * HPI_HS MCUmgr sync group (see hpi_hs_sync.h, docs/HPI_HS_API.md). It is shared,
 * conceptually, with every client: the HealthyPi Move app, the reference Python
 * client, and any third-party/research client.
 *
 * STABILITY RULES (do not break clients):
 *   - Type ids are permanent. Never renumber or reuse an id; only append.
 *   - `key` strings are permanent (stable machine identifiers).
 *   - Changing units/scale of an existing type is a BREAKING change → new id +
 *     bump HPI_HS_SCHEMA_VERSION.
 *   - Adding a new type id is backward-compatible (old clients skip unknown ids).
 */

#ifndef HPI_HS_TYPES_H
#define HPI_HS_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Bump on any breaking change to a type's meaning/unit/scale or the wire schema.
 * Additive changes (new type ids) do NOT require a bump. Advertised in HELLO. */
#define HPI_HS_SCHEMA_VERSION   1

/* ---- Metric type registry ------------------------------------------------
 * Permanent ids. Grouped by subsystem with gaps reserved for growth. A metric
 * whose value is computed by the module (not ingested from a sensor) is marked
 * `derived` in its hpi_hs_type_info row.
 */
enum hpi_hs_type {
    /* cardiac (0x00..) */
    HPI_HS_T_HR             = 0x01,  /* heart rate, bpm                        */
    HPI_HS_T_RESTING_HR     = 0x02,  /* resting HR, bpm (derived)              */
    HPI_HS_T_ECG_HR         = 0x03,  /* HR from an ECG spot check, bpm (event) */
    /* P1 epoch statistics. HR is spiky (can swing 70->150->70 inside a minute),
     * so the epoch MEAN alone would destroy peaks — HR_MIN/HR_MAX carry them.
     * Derive daily peaks from these (or from SUMMARY), NEVER from the mean series. */
    HPI_HS_T_HR_MIN         = 0x04,  /* epoch min HR, bpm                      */
    HPI_HS_T_HR_MAX         = 0x05,  /* epoch max HR, bpm                      */

    /* oxygen (0x10..) */
    HPI_HS_T_SPO2           = 0x10,  /* SpO2, integer %                        */

    /* temperature (0x20..) */
    HPI_HS_T_SKIN_TEMP      = 0x20,  /* skin temperature, degC x100            */
    HPI_HS_T_SKIN_TEMP_DEV  = 0x21,  /* temp deviation vs baseline, degC x100 (signed, derived) */
    /* Skin temp is SLOW (moves over minutes), so the epoch mean faithfully
     * represents the window and no min/max is needed — a within-epoch max would
     * capture artifacts (contact pressure, sun, watch removal), not physiology.
     * The count says how many samples backed the mean, so sparse windows can be
     * rejected (same role HRV coverage plays). */
    HPI_HS_T_SKIN_TEMP_CNT  = 0x22,  /* samples backing the epoch mean, count  */

    /* blood pressure (0x30..) — two correlated samples share a timestamp */
    HPI_HS_T_BP_SYS         = 0x30,  /* systolic, mmHg (event)                 */
    HPI_HS_T_BP_DIA         = 0x31,  /* diastolic, mmHg (event)                */

    /* activity (0x40..) */
    HPI_HS_T_STEPS          = 0x40,  /* step count (cumulative)                */
    HPI_HS_T_ACTIVE_ENERGY  = 0x41,  /* active energy, kcal (cumulative)       */

    /* HRV (0x50..) */
    HPI_HS_T_HRV_SDNN       = 0x50,  /* SDNN, ms x10                           */
    HPI_HS_T_HRV_RMSSD      = 0x51,  /* RMSSD, ms x10                          */
    HPI_HS_T_HRV_LFHF       = 0x52,  /* LF/HF ratio x100 (derived)             */
    /* P3 continuous HRV (PPG-derived). The MAX32664C already emits R-R intervals with
     * a confidence value and nothing read them, so HRV needed a MANUAL ECG spot check.
     * These come from 5-minute windows of gated wrist R-R. */
    HPI_HS_T_HRV_MEAN_RR    = 0x53,  /* mean R-R interval, ms                  */
    HPI_HS_T_HRV_COVERAGE   = 0x54,  /* % of the window backed by valid beats.
                                      * NOT optional: without it a client cannot tell a
                                      * clean 5 minutes from a noisy one, and will plot
                                      * motion artefacts as a physiological trend. */
    HPI_HS_T_HRV_NPAIRS = 0x55,
    HPI_HS_T_HRV_NBEATS = 0x56,

    /* electrodermal / stress (0x60..) — no standard HK/HC type */
    HPI_HS_T_EDA_SCL        = 0x60,  /* tonic level (SCL), uS x100             */
    HPI_HS_T_EDA_SCR_RATE   = 0x61,  /* SCR peaks per minute                   */
    HPI_HS_T_STRESS_EDA     = 0x62,  /* composite stress index 0..100 (derived)*/
    HPI_HS_T_STRESS_HRV     = 0x63,  /* HRV-derived stress index 0..100 (derived) */


    HPI_HS_T__COUNT_HINT    = 0x64,  /* not a type; keep ids below this compact */
};

/* Aggregation semantics for query-time statistics. */
enum hpi_hs_class {
    HPI_HS_CLASS_DISCRETE = 0,  /* avg/min/max/percentiles (HR, SpO2, temp…)   */
    HPI_HS_CLASS_CUMULATIVE,    /* sum over an interval (steps, energy)        */
    HPI_HS_CLASS_EVENT,         /* sparse, per-measurement (BP, ECG spot)      */
};

/* Per-sample quality / context flags. Ingest gates on these; aggregation and
 * derived metrics filter on them (e.g. resting HR = LOW_MOTION+ON_SKIN+HIGH_CONF,
 * temp baseline = DURING_SLEEP). */
#define HPI_HS_Q_VALID        (1u << 0)  /* timestamp valid (RTC synced) + in-range */
#define HPI_HS_Q_ON_SKIN      (1u << 1)  /* sensor reports skin contact             */
#define HPI_HS_Q_LOW_MOTION   (1u << 2)  /* IMU below the motion threshold          */
#define HPI_HS_Q_HIGH_CONF    (1u << 3)  /* sensor/algo confidence high             */
#define HPI_HS_Q_DURING_SLEEP (1u << 4)  /* captured in a detected sleep window     */
#define HPI_HS_Q_MANUAL       (1u << 5)  /* user-initiated spot check               */
/* SYNTHETIC data, generated on-device by hpi_hs_synth.c for testing trends without
 * wearing the watch for days. Additive bit: old clients ignore it. This exists so a
 * fabricated sample can NEVER be silently mistaken for a real physiological record
 * on a health device -- the app should filter it out of anything user-facing, and
 * it makes the test data purgeable. Only ever set when CONFIG_HPI_HS_SYNTH is on,
 * which is off in release builds. */
#define HPI_HS_Q_SYNTHETIC    (1u << 6)  /* fabricated test data - NOT a measurement */

/* ---- Wire sample --------------------------------------------------------
 * ONE value per sample. Multi-field metrics (BP sys/dia, EDA scl/scr) are
 * emitted as multiple typed samples sharing `ts_utc` (mirrors how HealthKit
 * correlates BP). Packed = 18 bytes; SYNC ships an array of these as a CBOR
 * byte-string for compactness (see hpi_hs_sync.h).
 */
struct __attribute__((packed)) hpi_hs_sample {
    uint32_t seq;       /* monotonic per-device sequence — the sync cursor key   */
    int64_t  ts_utc;    /* seconds since Unix epoch (UTC)                        */
    uint8_t  type;      /* enum hpi_hs_type                                     */
    uint8_t  quality;   /* HPI_HS_Q_* bitmask                                   */
    int32_t  value;     /* fixed-point; real = value / info.scale (signed ok)   */
};

#define HPI_HS_SAMPLE_WIRE_SIZE  18   /* sizeof(struct hpi_hs_sample), packed    */

/* ---- Type metadata (self-describing; also serialized by the TYPES command) --
 * `scale` is the divisor to recover real units: real = value / scale.
 * `hk_type` / `hc_type` are the Apple HealthKit / Android Health Connect
 * identifiers a bridge maps to ("" when there is no standard type). */
struct hpi_hs_type_info {
    uint8_t     id;          /* enum hpi_hs_type                                */
    const char *key;         /* stable machine id, e.g. "hr", "skin_temp"       */
    const char *unit;        /* "bpm","%","degC","mmHg","count","kcal","ms","uS","ratio","index" */
    uint16_t    scale;       /* real = value / scale  (1,10,100)                */
    uint8_t     data_class;  /* enum hpi_hs_class                               */
    bool        derived;     /* true = computed by the module, not sensor-ingested */
    const char *hk_type;     /* HealthKit HKQuantityTypeIdentifier or ""        */
    const char *hc_type;     /* Health Connect record class or ""               */
};

/* Registry accessors (defined in hpi_hs_types.c alongside the table). */
const struct hpi_hs_type_info *hpi_hs_type_lookup(uint8_t type);   /* NULL if unknown */
size_t hpi_hs_type_count(void);
const struct hpi_hs_type_info *hpi_hs_type_at(size_t index);

/* ---- Record tier (episodic raw-signal sessions) -------------------------
 * The second storage tier: ECG/GSR/PPG/HRV/IMU capture sessions. Each session is
 * a self-describing header + a raw sample payload (stored in segments). Fetched
 * via the RECORDS sync command (list → chunked get), CRC-verified by the client.
 * See docs/HPI_HS_API.md (RECORDS).
 */
enum hpi_hs_signal {
    HPI_HS_SIG_ECG        = 0x01,  /* MAX30001 ECG, int32 raw            */
    HPI_HS_SIG_BIOZ       = 0x02,  /* MAX30001 BioZ / GSR, int32 raw     */
    HPI_HS_SIG_PPG_WRIST  = 0x03,  /* MAX32664C PPG (multi-LED)          */
    HPI_HS_SIG_PPG_FINGER = 0x04,  /* MAX32664D PPG (multi-LED)          */
    HPI_HS_SIG_HRV_RR     = 0x05,  /* R-R intervals, uint16 ms           */
    HPI_HS_SIG_ACC        = 0x06,  /* IMU accel, int16 x/y/z             */
};

/* Sample encoding of a record payload. */
enum hpi_hs_sfmt {
    HPI_HS_SFMT_I32 = 0,  /* int32 little-endian per sample/channel */
    HPI_HS_SFMT_I16 = 1,  /* int16 little-endian                    */
    HPI_HS_SFMT_U16 = 2,  /* uint16 little-endian (e.g. RR ms)      */
};

/* Record flags. */
#define HPI_HS_REC_F_COMPLETE   (1u << 0)  /* session closed cleanly              */
#define HPI_HS_REC_F_PARTIAL    (1u << 1)  /* interrupted (reset/battery) — usable */
#define HPI_HS_REC_F_COMPRESSED (1u << 2)  /* payload is compressed               */

/* Self-describing session header (precedes/indexes the payload). Packed. */
struct __attribute__((packed)) hpi_hs_record_hdr {
    uint32_t id;             /* monotonic record id (RECORDS cursor key)         */
    int64_t  start_ts;       /* UTC seconds at capture start                     */
    uint8_t  signal;         /* enum hpi_hs_signal                              */
    uint8_t  sfmt;           /* enum hpi_hs_sfmt                                */
    uint8_t  channels;       /* interleaved channels per sample (1 = mono)       */
    uint8_t  flags;          /* HPI_HS_REC_F_*                                  */
    uint16_t sample_rate_hz; /* nominal sample rate                             */
    uint32_t n_samples;      /* samples per channel captured                     */
    uint32_t byte_len;       /* payload length in bytes                          */
    uint32_t crc32;          /* CRC-32 of the payload (integrity)                */
};

#endif /* HPI_HS_TYPES_H */
