/*
 * HealthyPi Move — Health Store: continuous PPG-derived HRV (HS-2 P3)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * The MAX32664C wrist hub ALREADY emits R-R intervals with a confidence value. The
 * driver already parses them; hpi_ppg_wr_data_t already carries them. Nothing read
 * them. HRV and stress therefore required a MANUAL ECG spot check -- the user had to
 * sit down and take a reading.
 *
 * Meanwhile the PPG is already running and we are already paying its power bill to
 * produce a heart-rate number. R-R falls out of the same FIFO for free. We were
 * keeping the sensor's least valuable output and discarding its most valuable one.
 *
 * This module turns that stream into HRV. PPG-derived R-R gives PULSE-rate
 * variability (PRV), which tracks HRV closely WHEN THE SUBJECT IS STILL -- which is
 * why Whoop / Oura / Garmin all compute overnight HRV from PPG rather than ECG. It
 * unlocks continuous/overnight HRV, proper stress (ours is EDA-only), recovery, and a
 * cleaner resting HR.
 *
 * Design (docs/HS_SYNC_REDESIGN_PLAN.md §3):
 *
 *   GATE HARD. Motion destroys PRV. An interval is accepted only when the hub's
 *   confidence is high, the watch is on-skin, the wearer is still, and the interval is
 *   physiologically plausible. A permissive gate does not produce "more HRV data" --
 *   it produces a plausible-looking trend made of artefacts, which is worse than no
 *   trend at all.
 *
 *   Compute in 5-MINUTE WINDOWS (the Task-Force short-term standard) and store the
 *   METRICS, not the beats. Raw R-R at 60 bpm is 86,400 intervals/day -- worse than
 *   the firehose P1 just removed. (Raw R-R belongs in the Record tier, which already
 *   has HPI_HS_SIG_HRV_RR wired for episodic capture.)
 *
 *   Emit COVERAGE with every window. Without it a client cannot distinguish a clean
 *   five minutes from a noisy one, and will happily plot motion artefacts as
 *   physiology.
 */

#ifndef HPI_HS_HRV_H
#define HPI_HS_HRV_H

#include <stdint.h>
#include <stdbool.h>

/* Feed one R-R interval as reported by the wrist hub.
 *
 * `rtor_ms`   — the hub's current R-R reading (0 = none).
 * `rtor_conf` — the hub's confidence, 0..100.
 * `on_skin`   — SCD reports skin contact.
 * `still`     — no IMU motion recently (see CONFIG_HPI_HS_HRV_QUIET_S).
 *
 * The hub REPEATS its last R-R on every FIFO sample until a new beat arrives, so this
 * is called far more often than the heart beats; the module de-duplicates. */
void hpi_hs_hrv_feed(uint16_t rtor_ms, uint8_t rtor_conf, bool on_skin, bool still,
                     int64_t ts_utc, int64_t now_ms);

/* Close any window whose time has elapsed. Without this a partial window sits open
 * forever once the wearer takes the watch off mid-window. */
void hpi_hs_hrv_tick(int64_t now_utc);
void hpi_hs_hrv_rr_record_start(void);
void hpi_hs_hrv_rr_record_stop(void);

#endif /* HPI_HS_HRV_H */
