/*
 * HealthyPi Move — Health Store: epoch aggregation (HS-2 P1)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Sits between the zbus listeners and hpi_hs_record(). Continuous signals used to
 * be written to the durable log on EVERY publish — HR every ~3 s, skin temp every
 * 5 s — which is ~46,000 samples/day, capped retention at ~1.2 days (so a missed
 * sync was permanent data loss), and made every sync move ~830 KB. Nothing
 * consumed that resolution: the live UI reads zbus, not the store, and beat-level
 * fidelity lives in the Record tier.
 *
 * This module reduces each continuous signal to ONE epoch record per window —
 * but the right statistic depends on the KIND of signal. Do NOT apply mean/min/max
 * uniformly; see docs/HS_SYNC_REDESIGN_PLAN.md §2.2.
 *
 *   SPIKY   (HR)            -> mean + min + max
 *       HR can swing 70->150->70 inside one minute, so the mean alone would
 *       silently under-report peaks. The extremes are real physiology.
 *
 *   LEVEL   (skin temp)     -> mean + count
 *       Moves over minutes, so the mean faithfully represents the window and a
 *       nightly min / daily max is recoverable from the SERIES of means. A
 *       within-epoch max would capture artifacts (contact pressure, sun, watch
 *       removal), not physiology. `count` lets sparse windows be rejected.
 *
 *   COUNTER (steps, energy) -> last value in window, cumulative semantics kept
 *       Monotonic within a day, so max == last and min == first (both redundant),
 *       and the MEAN OF A MONOTONIC RAMP IS MEANINGLESS — it is the midpoint of
 *       the ramp, not "average steps".
 *
 *   Sparse events (BP, SpO2 spot, ECG-HR, EDA, stress) do not pass through here
 *   at all — they are already only tens per day and stay raw.
 *
 * The producer API is unchanged from the caller's point of view: the listeners
 * feed here instead of calling hpi_hs_record() directly.
 */

#ifndef HPI_HS_EPOCH_H
#define HPI_HS_EPOCH_H

#include <stdint.h>
#include <stdbool.h>

/* Feed one sample. `ts_utc` is the sample's own timestamp; epochs are aligned to
 * WALL CLOCK (ts / epoch_s), not uptime, so windows survive a reboot and the app
 * can bucket deterministically. Samples without HPI_HS_Q_VALID are dropped (same
 * gate hpi_hs_record() applies). */
void hpi_hs_epoch_hr(int32_t bpm, uint8_t quality, int64_t ts_utc);
void hpi_hs_epoch_temp(int32_t temp_c_x100, uint8_t quality, int64_t ts_utc);

/* CUMULATIVE feeds take the CUMULATIVE total (not a delta) — the same value the
 * producer publishes today. A DECREASE (the local-midnight reset, e.g. 8000 -> 0)
 * force-closes the open epoch first, emitting the pre-reset peak; without that the
 * day's final total would be swallowed whenever the reset landed mid-window. */
void hpi_hs_epoch_steps(int32_t cumulative, uint8_t quality, int64_t ts_utc);
void hpi_hs_epoch_energy(int32_t cumulative, uint8_t quality, int64_t ts_utc);

/* Called from the store thread: closes any epoch whose window has elapsed. Without
 * this an epoch would stay open forever once its signal stops (watch taken off). */
void hpi_hs_epoch_tick(int64_t now_utc);

/* Close every open epoch immediately (shutdown, going off-skin). */
void hpi_hs_epoch_flush_all(int64_t now_utc);

#endif /* HPI_HS_EPOCH_H */
