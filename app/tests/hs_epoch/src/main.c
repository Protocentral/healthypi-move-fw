/*
 * HS-2 P1 — epoch aggregator unit tests (native_sim, no hardware).
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * hpi_hs_record() is stubbed here and captures what the aggregator emits, so the
 * unit under test is hpi_hs_epoch.c alone — no filesystem, no zbus, no sensors.
 *
 * These lock down the two behaviours that are easy to get silently wrong:
 *   1. HR peaks survive. The epoch MEAN destroys them; HR_MAX must carry them.
 *   2. The midnight counter reset force-flushes, so the day's final step total is
 *      not swallowed by the rate-limiting we introduced in P1.
 */

#include <zephyr/ztest.h>
#include <string.h>

#include "health/hpi_hs_types.h"
#include "health/hpi_hs_epoch.h"

/* ---- hpi_hs_record() stub: capture emitted records ----------------------- */

struct rec {
    uint8_t type;
    int32_t value;
    uint8_t quality;
    int64_t ts;
};

#define MAX_REC 256
static struct rec s_rec[MAX_REC];
static int s_n;

void hpi_hs_record(uint8_t type, int32_t value, uint8_t quality, int64_t ts_utc)
{
    zassert_true(s_n < MAX_REC, "record capture overflow");
    s_rec[s_n++] = (struct rec){type, value, quality, ts_utc};
}

static void rec_reset(void)
{
    memset(s_rec, 0, sizeof(s_rec));
    s_n = 0;
}

/* First captured record of `type`, or NULL. */
static const struct rec *rec_find(uint8_t type)
{
    for (int i = 0; i < s_n; i++) {
        if (s_rec[i].type == type) {
            return &s_rec[i];
        }
    }
    return NULL;
}

static int rec_count(uint8_t type)
{
    int n = 0;
    for (int i = 0; i < s_n; i++) {
        if (s_rec[i].type == type) {
            n++;
        }
    }
    return n;
}

#define Q  HPI_HS_Q_VALID

/* Epoch lengths the module defaults to (no Kconfig in this test build). */
#define EP_HR      60
#define EP_TEMP    300
#define EP_COUNTER 60

static void setup(void *f)
{
    ARG_UNUSED(f);
    rec_reset();
    /* Clear any window left open by a previous test. Flushing into a reset
     * capture buffer, then resetting again, leaves the aggregator idle. */
    hpi_hs_epoch_flush_all(0);
    rec_reset();
}

ZTEST_SUITE(hs_epoch, NULL, NULL, setup, NULL, NULL);

/* ------------------------------------------------------------------------- *
 * HR — the whole reason min/max exist
 * ------------------------------------------------------------------------- */

/* THE test. A 10-second spike to 150 bpm inside an otherwise-70 bpm minute. The
 * mean collapses it to ~83; HR_MAX must still report 150. If this regresses, the
 * device silently under-reports peak heart rate. */
ZTEST(hs_epoch, test_hr_peak_survives_the_mean)
{
    /* 20 samples across one minute at 3 s cadence (the real HR cadence). */
    for (int i = 0; i < 20; i++) {
        int64_t ts = 1000 * EP_HR + i * 3;      /* all inside window 1000 */
        /* samples 5..8 = a ~12 s burst to 150 */
        int32_t bpm = (i >= 5 && i <= 8) ? 150 : 70;
        hpi_hs_epoch_hr(bpm, Q, ts);
    }
    hpi_hs_epoch_flush_all(0);

    const struct rec *mean = rec_find(HPI_HS_T_HR);
    const struct rec *mx   = rec_find(HPI_HS_T_HR_MAX);
    const struct rec *mn   = rec_find(HPI_HS_T_HR_MIN);

    zassert_not_null(mean, "no HR mean emitted");
    zassert_not_null(mx, "no HR_MAX emitted");
    zassert_not_null(mn, "no HR_MIN emitted");

    /* mean = (16*70 + 4*150)/20 = (1120 + 600)/20 = 86 */
    zassert_equal(mean->value, 86, "mean was %d, expected 86", mean->value);

    /* The peak is GONE from the mean but preserved in HR_MAX. */
    zassert_true(mean->value < 100, "mean should have blurred the spike");
    zassert_equal(mx->value, 150, "HR_MAX must carry the true peak, got %d", mx->value);
    zassert_equal(mn->value, 70, "HR_MIN must carry the true trough, got %d", mn->value);
}

/* ts_utc on an epoch record is the END of the window — the app relies on this. */
ZTEST(hs_epoch, test_hr_epoch_ts_is_window_end)
{
    hpi_hs_epoch_hr(70, Q, 1000 * EP_HR + 5);
    hpi_hs_epoch_hr(72, Q, 1000 * EP_HR + 50);
    hpi_hs_epoch_flush_all(0);

    const struct rec *mean = rec_find(HPI_HS_T_HR);
    zassert_not_null(mean, "no HR emitted");
    zassert_equal(mean->ts, (int64_t)1001 * EP_HR,
                  "epoch ts must be the window END (%lld), got %lld",
                  (long long)(1001 * EP_HR), (long long)mean->ts);
}

/* One record per statistic per window, and a new window rolls the old one out. */
ZTEST(hs_epoch, test_hr_window_roll_emits_once)
{
    hpi_hs_epoch_hr(60, Q, 1000 * EP_HR + 1);
    hpi_hs_epoch_hr(80, Q, 1000 * EP_HR + 30);
    /* crossing into the next wall-clock minute closes the first window */
    hpi_hs_epoch_hr(90, Q, 1001 * EP_HR + 1);

    zassert_equal(rec_count(HPI_HS_T_HR), 1, "expected exactly one closed HR epoch");
    zassert_equal(rec_count(HPI_HS_T_HR_MIN), 1, "");
    zassert_equal(rec_count(HPI_HS_T_HR_MAX), 1, "");

    const struct rec *mean = rec_find(HPI_HS_T_HR);
    zassert_equal(mean->value, 70, "mean of 60,80 = 70, got %d", mean->value);
    zassert_equal(rec_find(HPI_HS_T_HR_MAX)->value, 80, "");
}

/* Epochs are aligned to WALL CLOCK, not to the first sample. Two samples 2 s apart
 * that straddle a minute boundary must land in DIFFERENT epochs — otherwise epochs
 * would drift across a reboot and the app could not bucket deterministically. */
ZTEST(hs_epoch, test_epochs_are_wall_clock_aligned)
{
    hpi_hs_epoch_hr(60, Q, 1000 * EP_HR + 59);   /* last second of window 1000 */
    hpi_hs_epoch_hr(90, Q, 1001 * EP_HR + 0);    /* first second of window 1001 */
    hpi_hs_epoch_flush_all(0);

    zassert_equal(rec_count(HPI_HS_T_HR), 2, "two windows expected, got %d",
                  rec_count(HPI_HS_T_HR));
    zassert_equal(s_rec[0].value, 60, "");
}

/* A sample with no valid timestamp is dropped (it would corrupt the window index). */
ZTEST(hs_epoch, test_invalid_quality_dropped)
{
    hpi_hs_epoch_hr(70, 0 /* no VALID */, 1000 * EP_HR + 1);
    hpi_hs_epoch_flush_all(0);
    zassert_equal(s_n, 0, "a sample without HPI_HS_Q_VALID must not be stored");
}

/* Epoch quality is the AND of the window: a context bit is claimed only if EVERY
 * sample had it. Conservative by design. */
ZTEST(hs_epoch, test_quality_is_anded_across_the_window)
{
    hpi_hs_epoch_hr(70, Q | HPI_HS_Q_ON_SKIN | HPI_HS_Q_HIGH_CONF, 1000 * EP_HR + 1);
    hpi_hs_epoch_hr(72, Q | HPI_HS_Q_ON_SKIN,                      1000 * EP_HR + 4);
    hpi_hs_epoch_flush_all(0);

    const struct rec *mean = rec_find(HPI_HS_T_HR);
    zassert_not_null(mean, "");
    zassert_true(mean->quality & HPI_HS_Q_ON_SKIN, "ON_SKIN held for the whole window");
    zassert_false(mean->quality & HPI_HS_Q_HIGH_CONF,
                  "HIGH_CONF was NOT true for every sample — must not be claimed");
}

/* ------------------------------------------------------------------------- *
 * Skin temp — mean + count, deliberately NO min/max
 * ------------------------------------------------------------------------- */

ZTEST(hs_epoch, test_temp_emits_mean_and_count_only)
{
    for (int i = 0; i < 5; i++) {
        hpi_hs_epoch_temp(3400 + i, Q, 500 * EP_TEMP + i);   /* 34.00..34.04 degC */
    }
    hpi_hs_epoch_flush_all(0);

    const struct rec *mean = rec_find(HPI_HS_T_SKIN_TEMP);
    const struct rec *cnt  = rec_find(HPI_HS_T_SKIN_TEMP_CNT);
    zassert_not_null(mean, "no temp mean");
    zassert_not_null(cnt, "no temp count");
    zassert_equal(mean->value, 3402, "mean of 3400..3404 = 3402, got %d", mean->value);
    zassert_equal(cnt->value, 5, "count must back the mean, got %d", cnt->value);

    /* Temp is a SLOW signal: within-epoch extremes would be artifacts, not
     * physiology, so no min/max ids are emitted for it at all. */
    zassert_equal(rec_count(HPI_HS_T_HR_MIN), 0, "temp must not emit HR extremes");
    zassert_equal(s_n, 2, "temp emits exactly mean + count, got %d records", s_n);
}

/* ------------------------------------------------------------------------- *
 * Steps — cumulative, and the midnight reset that would otherwise lose a day
 * ------------------------------------------------------------------------- */

/* THE other test. Steps reset to 0 at local midnight. If the reset lands mid-window
 * and we only emit the last value, we emit 0 and LOSE the day's final total. The
 * aggregator must force-close on a DECREASE and emit the pre-reset peak first. */
ZTEST(hs_epoch, test_midnight_reset_flushes_pre_reset_peak)
{
    int64_t base = 1000 * EP_COUNTER;

    hpi_hs_epoch_steps(7800, Q, base + 10);
    hpi_hs_epoch_steps(8000, Q, base + 20);   /* the day's final total */
    hpi_hs_epoch_steps(0,    Q, base + 30);   /* midnight reset, SAME window */

    /* The pre-reset peak must already be out, without any window roll or flush. */
    zassert_equal(rec_count(HPI_HS_T_STEPS), 1,
                  "the reset must force-close the window, got %d records",
                  rec_count(HPI_HS_T_STEPS));
    zassert_equal(s_rec[0].value, 8000,
                  "the day's FINAL total (8000) must survive the reset, got %d",
                  s_rec[0].value);

    /* ...and the new day continues from 0. */
    hpi_hs_epoch_steps(120, Q, base + 40);
    hpi_hs_epoch_flush_all(0);
    zassert_equal(rec_count(HPI_HS_T_STEPS), 2, "");
    zassert_equal(s_rec[1].value, 120, "new day accumulates from 0, got %d",
                  s_rec[1].value);
}

/* Cumulative: only the LAST value in the window is meaningful. Never a mean (the
 * mean of a monotonic ramp is the midpoint of the ramp — a meaningless number). */
ZTEST(hs_epoch, test_steps_emits_last_value_not_mean)
{
    int64_t base = 2000 * EP_COUNTER;
    hpi_hs_epoch_steps(100, Q, base + 1);
    hpi_hs_epoch_steps(200, Q, base + 20);
    hpi_hs_epoch_steps(300, Q, base + 40);
    hpi_hs_epoch_flush_all(0);

    zassert_equal(rec_count(HPI_HS_T_STEPS), 1, "");
    const struct rec *r = rec_find(HPI_HS_T_STEPS);
    zassert_equal(r->value, 300,
                  "cumulative epoch must emit the LAST value (300), not the mean (200); got %d",
                  r->value);
    /* ts is the sample's own, not the window end — a force-close on reset would
     * otherwise stamp the record with a time in the future. */
    zassert_equal(r->ts, base + 40, "cumulative ts must be the sample's own");
}

/* ------------------------------------------------------------------------- *
 * tick() — an epoch must not stay open forever once its signal stops
 * ------------------------------------------------------------------------- */

/* Watch taken off mid-window: no more samples ever arrive. Without the tick the
 * partial epoch would sit open and never be written. */
ZTEST(hs_epoch, test_tick_closes_a_stale_window)
{
    hpi_hs_epoch_hr(65, Q, 1000 * EP_HR + 10);
    zassert_equal(s_n, 0, "window still open, nothing emitted yet");

    hpi_hs_epoch_tick(1000 * EP_HR + 30);        /* same window — must NOT close */
    zassert_equal(s_n, 0, "tick inside the window must not close it");

    hpi_hs_epoch_tick(1001 * EP_HR + 5);         /* window elapsed — must close */
    zassert_equal(rec_count(HPI_HS_T_HR), 1, "tick must close the elapsed window");
    zassert_equal(rec_find(HPI_HS_T_HR)->value, 65, "");
}

/* A single-sample (partial) epoch is still emitted, and its count says so. */
ZTEST(hs_epoch, test_partial_epoch_still_emitted)
{
    hpi_hs_epoch_temp(3500, Q, 700 * EP_TEMP + 1);
    hpi_hs_epoch_flush_all(0);

    zassert_equal(rec_find(HPI_HS_T_SKIN_TEMP)->value, 3500, "");
    zassert_equal(rec_find(HPI_HS_T_SKIN_TEMP_CNT)->value, 1,
                  "count must reveal that only one sample backed this mean");
}

/* Sanity: one minute of real-cadence HR produces 3 records, not 20. This is the
 * whole point of P1 (~46k -> ~6k samples/day). */
ZTEST(hs_epoch, test_rate_reduction)
{
    for (int i = 0; i < 20; i++) {           /* 20 publishes at the real 3 s cadence */
        hpi_hs_epoch_hr(70 + i, Q, 3000 * EP_HR + i * 3);
    }
    hpi_hs_epoch_flush_all(0);

    zassert_equal(s_n, 3,
                  "one HR minute must emit exactly mean+min+max = 3 records, got %d", s_n);
}
