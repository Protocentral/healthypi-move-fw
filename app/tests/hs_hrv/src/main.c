/*
 * HS-2 P3 — HRV accumulator unit tests (no hardware).
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * hpi_hs_record() is stubbed, so the unit under test is hpi_hs_hrv.c alone.
 *
 * HRV is the most dangerous thing in this codebase to get quietly wrong: RMSSD and
 * SDNN are just numbers, and a wrong one still looks like a plausible recovery score.
 * There is no crash to notice. These pin the arithmetic against hand-computed values
 * and pin the gate against artefacts.
 */

#include <zephyr/ztest.h>
#include <math.h>
#include <string.h>

#include "health/hpi_hs_types.h"
#include "health/hpi_hs_hrv.h"

struct rec { uint8_t type; int32_t value; uint8_t quality; int64_t ts; };

#define MAX_REC 64
static struct rec s_rec[MAX_REC];
static int s_n;

void hpi_hs_record(uint8_t type, int32_t value, uint8_t quality, int64_t ts_utc, int64_t now_ms)
{
    zassert_true(s_n < MAX_REC, "capture overflow");
    s_rec[s_n++] = (struct rec){type, value, quality, ts_utc};
}

static const struct rec *rec_find(uint8_t type)
{
    for (int i = 0; i < s_n; i++) {
        if (s_rec[i].type == type) {
            return &s_rec[i];
        }
    }
    return NULL;
}

/* Module defaults (no Kconfig in this build). */
#define WIN_S      300
#define MIN_CONF   80
#define MIN_BEATS  30
/* A 5-minute window at ~820 ms/beat holds ~366 beats, so a window that is MEANT to be
 * accepted needs a realistic count: 40 beats is only ~11% coverage and is (correctly)
 * discarded by the coverage gate. Use 300 -> ~82%. */
#define N_FULL     300

#define GOOD_CONF  95
#define ON_SKIN    true
#define STILL      true

/* Feed `n` beats of `rr` ms inside window `win`, spaced so they stay in it. */
static void feed_run(int64_t win, const uint16_t *rr, int n, uint8_t conf,
                     bool on_skin, bool still)
{
    int64_t t = win * WIN_S;
    for (int i = 0; i < n; i++) {
        hpi_hs_hrv_feed(rr[i], conf, on_skin, still, t);
        t += 1;   /* stays well inside the 300 s window */
    }
}

static void setup(void *f)
{
    ARG_UNUSED(f);
    /* Drain any window a previous test left open, then clear the capture. */
    hpi_hs_hrv_tick((int64_t)1 << 40);
    s_n = 0;
    memset(s_rec, 0, sizeof(s_rec));
}

ZTEST_SUITE(hs_hrv, NULL, NULL, setup, NULL, NULL);

/* ------------------------------------------------------------------ the maths */

/* Hand-computed. 300 beats alternating 800/840 ms.
 *   mean   = 820
 *   SDNN   = 20                     (every value is exactly 20 off the mean)
 *   RMSSD  = 40                     (every successive difference is +-40)
 * If RMSSD and SDNN are ever swapped or mis-scaled, this catches it: they are
 * deliberately DIFFERENT numbers here. */
ZTEST(hs_hrv, test_rmssd_and_sdnn_are_correct_and_not_swapped)
{
    uint16_t rr[N_FULL];
    for (int i = 0; i < N_FULL; i++) {
        rr[i] = (i % 2) ? 840 : 800;
    }
    feed_run(1000, rr, N_FULL, GOOD_CONF, ON_SKIN, STILL);
    hpi_hs_hrv_tick(1001 * WIN_S + 1);

    const struct rec *sdnn  = rec_find(HPI_HS_T_HRV_SDNN);
    const struct rec *rmssd = rec_find(HPI_HS_T_HRV_RMSSD);
    const struct rec *mean  = rec_find(HPI_HS_T_HRV_MEAN_RR);

    zassert_not_null(sdnn, "no SDNN emitted");
    zassert_not_null(rmssd, "no RMSSD emitted");
    zassert_not_null(mean, "no mean RR emitted");

    zassert_equal(mean->value, 820, "mean RR = 820 ms, got %d", mean->value);
    /* both stored as ms x10 */
    zassert_within(sdnn->value, 200, 2, "SDNN = 20.0 ms (x10 = 200), got %d", sdnn->value);
    zassert_within(rmssd->value, 400, 2, "RMSSD = 40.0 ms (x10 = 400), got %d", rmssd->value);

    zassert_true(rmssd->value > sdnn->value,
                 "RMSSD (40) must exceed SDNN (20) here - if equal, they are swapped");
}

/* A perfectly CONSTANT R-R is not a physiological signal, it is a stuck sensor -- and
 * the de-dup (the hub repeats its last R-R until a new beat) collapses it to a single
 * accepted beat, so the window is correctly discarded. This also guards the NaN path:
 * variance is E[x^2] - mean^2, which can land slightly negative on float rounding and
 * would sqrt() to NaN if it ever reached the maths. */
ZTEST(hs_hrv, test_constant_rr_gives_zero_not_nan)
{
    uint16_t rr[N_FULL];
    for (int i = 0; i < N_FULL; i++) {
        rr[i] = 800;
    }
    /* NB: the de-dup drops repeats, so a constant series yields only ONE accepted
     * beat and the window is correctly discarded as too sparse. That is itself the
     * assertion: no record, and certainly no NaN. */
    feed_run(2000, rr, N_FULL, GOOD_CONF, ON_SKIN, STILL);
    hpi_hs_hrv_tick(2001 * WIN_S + 1);

    zassert_equal(s_n, 0, "a constant R-R series de-dups to one beat -> too sparse to emit");
}

/* ------------------------------------------------------------------- the gate */

ZTEST(hs_hrv, test_low_confidence_beats_are_rejected)
{
    uint16_t rr[N_FULL];
    for (int i = 0; i < N_FULL; i++) {
        rr[i] = (i % 2) ? 840 : 800;
    }
    feed_run(3000, rr, N_FULL, MIN_CONF - 1, ON_SKIN, STILL);
    hpi_hs_hrv_tick(3001 * WIN_S + 1);
    zassert_equal(s_n, 0, "beats below the confidence threshold must not produce HRV");
}

ZTEST(hs_hrv, test_off_skin_is_rejected)
{
    uint16_t rr[N_FULL];
    for (int i = 0; i < N_FULL; i++) {
        rr[i] = (i % 2) ? 840 : 800;
    }
    feed_run(4000, rr, N_FULL, GOOD_CONF, false /* off skin */, STILL);
    hpi_hs_hrv_tick(4001 * WIN_S + 1);
    zassert_equal(s_n, 0, "off-skin beats must not produce HRV");
}

/* THE gate. Motion destroys pulse-rate variability; a moving wearer must not generate
 * an HRV number, because a plausible-looking trend made of artefacts is worse than no
 * trend at all. */
ZTEST(hs_hrv, test_motion_is_rejected)
{
    uint16_t rr[N_FULL];
    for (int i = 0; i < N_FULL; i++) {
        rr[i] = (i % 2) ? 840 : 800;
    }
    feed_run(5000, rr, N_FULL, GOOD_CONF, ON_SKIN, false /* moving */);
    hpi_hs_hrv_tick(5001 * WIN_S + 1);
    zassert_equal(s_n, 0, "beats taken while moving must not produce HRV");
}

ZTEST(hs_hrv, test_implausible_intervals_are_rejected)
{
    /* 200 ms = 300 bpm, 2000 ms = 30 bpm: neither is a real resting beat. */
    uint16_t rr[N_FULL];
    for (int i = 0; i < N_FULL; i++) {
        rr[i] = (i % 2) ? 2000 : 200;
    }
    feed_run(6000, rr, N_FULL, GOOD_CONF, ON_SKIN, STILL);
    hpi_hs_hrv_tick(6001 * WIN_S + 1);
    zassert_equal(s_n, 0, "out-of-range intervals must not produce HRV");
}

/* ------------------------------------------------------- coverage & sparseness */

/* A handful of beats scattered through five minutes says nothing about anyone's
 * nervous system. It must be DISCARDED, not stored as a data point -- storing it is
 * how a noisy window becomes a "recovery dip" in the app. */
ZTEST(hs_hrv, test_low_coverage_window_is_discarded)
{
    /* 100 beats clears MIN_BEATS (30) easily -- but 100 x 820 ms = 82 s of a 300 s
     * window is only ~27% coverage. This isolates the COVERAGE gate: a window can have
     * plenty of beats and still be mostly silence, and its RMSSD would be meaningless. */
    uint16_t rr[100];
    for (int i = 0; i < 100; i++) {
        rr[i] = (i % 2) ? 840 : 800;
    }
    feed_run(7000, rr, 100, GOOD_CONF, ON_SKIN, STILL);
    hpi_hs_hrv_tick(7001 * WIN_S + 1);
    zassert_equal(s_n, 0,
                  "a low-coverage window must be DISCARDED, not stored: storing it is how "
                  "a noisy five minutes becomes a 'recovery dip' in the app");
}

/* Coverage is emitted and is the share of the window actually backed by beats. */
ZTEST(hs_hrv, test_coverage_is_reported)
{
    /* 300 beats x ~820 ms = 246 s of a 300 s window ~= 82% */
    uint16_t rr[300];
    for (int i = 0; i < 300; i++) {
        rr[i] = (i % 2) ? 840 : 800;
    }
    feed_run(8000, rr, 300, GOOD_CONF, ON_SKIN, STILL);
    hpi_hs_hrv_tick(8001 * WIN_S + 1);

    const struct rec *cov = rec_find(HPI_HS_T_HRV_COVERAGE);
    zassert_not_null(cov, "coverage MUST be emitted - without it a client cannot tell a "
                          "clean window from a noisy one");
    zassert_within(cov->value, 82, 3, "coverage ~82%%, got %d", cov->value);
    zassert_true(cov->value <= 100, "coverage must never exceed 100%%");
}

/* The emitted window claims LOW_MOTION/ON_SKIN/HIGH_CONF, because by construction
 * every beat in it passed those gates. (Nothing set HPI_HS_Q_LOW_MOTION before P3 --
 * the bit existed with no producer at all.) */
ZTEST(hs_hrv, test_quality_bits_reflect_the_gate)
{
    uint16_t rr[N_FULL];
    for (int i = 0; i < N_FULL; i++) {
        rr[i] = (i % 2) ? 840 : 800;
    }
    feed_run(9000, rr, N_FULL, GOOD_CONF, ON_SKIN, STILL);
    hpi_hs_hrv_tick(9001 * WIN_S + 1);

    const struct rec *r = rec_find(HPI_HS_T_HRV_RMSSD);
    zassert_not_null(r, "");
    zassert_true(r->quality & HPI_HS_Q_VALID, "");
    zassert_true(r->quality & HPI_HS_Q_ON_SKIN, "");
    zassert_true(r->quality & HPI_HS_Q_LOW_MOTION, "");
    zassert_true(r->quality & HPI_HS_Q_HIGH_CONF, "");
}

/* ts on an HRV record is the END of the window, like every other epoch type. */
ZTEST(hs_hrv, test_ts_is_window_end)
{
    uint16_t rr[N_FULL];
    for (int i = 0; i < N_FULL; i++) {
        rr[i] = (i % 2) ? 840 : 800;
    }
    feed_run(10000, rr, N_FULL, GOOD_CONF, ON_SKIN, STILL);
    hpi_hs_hrv_tick(10001 * WIN_S + 1);

    const struct rec *r = rec_find(HPI_HS_T_HRV_RMSSD);
    zassert_not_null(r, "");
    zassert_equal(r->ts, (int64_t)10001 * WIN_S,
                  "HRV ts must be the window END (%lld), got %lld",
                  (long long)(10001 * WIN_S), (long long)r->ts);
}

/* THE subtle one. RMSSD is the RMS of SUCCESSIVE differences. If a beat is rejected
 * mid-run (motion, low confidence) and we then pair the next accepted beat with the
 * one from BEFORE the gap, we measure a difference across a hole in time. That
 * inflates RMSSD -- in the direction that reads as "better recovery". The chain must
 * break at a rejection.
 *
 * Here: a clean 800/840 run, a burst of rejected beats, then another clean 800/840
 * run. RMSSD must still be 40 (the real beat-to-beat difference), NOT contaminated by
 * a spurious pairing across the gap. */
ZTEST(hs_hrv, test_rejected_beat_breaks_the_successive_difference_chain)
{
    int64_t t = 11000 * (int64_t)WIN_S;

    /* clean run: ...800, 840, 800, 840 */
    for (int i = 0; i < 150; i++) {
        hpi_hs_hrv_feed((i % 2) ? 840 : 800, GOOD_CONF, ON_SKIN, STILL, t++);
    }
    /* the wearer moves: these are rejected. Note the values are WILD -- if any of them
     * leaked into the difference chain, RMSSD would blow up. */
    for (int i = 0; i < 10; i++) {
        hpi_hs_hrv_feed((i % 2) ? 1400 : 320, GOOD_CONF, ON_SKIN, false /* moving */, t++);
    }
    /* clean again */
    for (int i = 0; i < 150; i++) {
        hpi_hs_hrv_feed((i % 2) ? 840 : 800, GOOD_CONF, ON_SKIN, STILL, t++);
    }
    hpi_hs_hrv_tick(11001 * (int64_t)WIN_S + 1);

    const struct rec *rmssd = rec_find(HPI_HS_T_HRV_RMSSD);
    zassert_not_null(rmssd, "");
    zassert_within(rmssd->value, 400, 20,
                   "RMSSD must stay ~40.0 ms (x10 = 400): a rejected beat must BREAK the "
                   "successive-difference chain, not be bridged across. Got %d",
                   rmssd->value);
}
