/*
 * HealthyPi Move — Health Store: synthetic data generator (test only)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Trends, baselines and sync-at-scale are impossible to test by wearing the watch:
 * a 7-day skin-temp baseline takes seven days, and a full sync needs tens of
 * thousands of samples. This generates BACKDATED, physiologically plausible data
 * straight into the epoch aggregator — a week in a couple of seconds.
 *
 * It supplies its OWN timestamps rather than reading the RTC, which is what makes
 * it fast. It feeds hpi_hs_epoch_*(), i.e. the REAL ingest path, so it exercises
 * the epoch aggregation, the segment layout, retention, sync paging and SUMMARY —
 * everything downstream of the zbus listeners.
 *
 * Every sample it writes carries HPI_HS_Q_SYNTHETIC. On a health device fabricated
 * data must never be mistakable for a measurement.
 *
 * Gated by CONFIG_HPI_HS_SYNTH (default n; off in release).
 */

#ifndef HPI_HS_SYNTH_H
#define HPI_HS_SYNTH_H

#include <stdint.h>

/* Generate `days` of backdated data ending `end_utc` (0 = "now" per the RTC).
 * Writes through the real epoch aggregator, so what lands in the store is exactly
 * what a real week of wear would have produced.
 *
 * The model deliberately includes the cases that break naive implementations:
 *   - a circadian HR curve (~50 asleep, 65-80 awake)
 *   - EXERCISE BOUTS to ~150 bpm — the reason HR_MIN/HR_MAX exist. A mean-only
 *     epoch would report these as ~90 and silently lose the peak.
 *   - MIDNIGHT step resets — the reason the aggregator force-flushes on a decrease.
 *     Without it the day's final total is swallowed.
 *   - OFF-SKIN gaps (charging) — no samples at all for a stretch, so gap handling
 *     and the quality AND are exercised.
 *   - a nocturnal skin-temp rise, so the 7-day baseline and the deviation are real.
 *   - sparse spot checks (SpO2, BP, ECG-HR) so the event types are populated.
 *
 * Returns the number of samples handed to the aggregator (NOT the number of records
 * stored — the whole point is that those differ by ~8x). */
int hpi_hs_synth_generate(uint32_t days, int64_t end_utc);

/* Ask the background generator to run. Returns immediately -- generation takes ~100 s
 * for a week and must NOT block the caller (the MCUmgr handler runs on the BLE/SMP
 * thread; blocking it would stall the connection and trip the watchdog).
 *
 * `wipe` discards the existing durable log first, so a re-run does not append a second
 * dataset on top of the first. seq is never rewound by the wipe.
 *
 * Returns 0 if accepted, -EBUSY if a generation is already running. */
int hpi_hs_synth_request(uint32_t days, bool wipe);

#endif /* HPI_HS_SYNTH_H */
