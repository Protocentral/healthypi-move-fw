/*
 * HealthyPi Move — P3 series bucketing tests
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * The bucketing is what turns thousands of epoch records into the ~24 points a
 * sparkline plots, so its failure mode is a chart that looks plausible and is
 * wrong. These tests pin the cases that produce exactly that: gaps that must not
 * read as zeros, cumulative types that must not be summed, and boundary samples
 * that must not be double-counted by adjacent queries.
 */

#include <zephyr/ztest.h>
#include <string.h>

#include "health/hpi_hs_series.h"

#define NB      24
#define T0      1000000LL
#define HOUR    3600LL
#define DAY     (24 * HOUR)

static struct hpi_hs_bucket b[HPI_HS_SERIES_MAX_BUCKETS];
static int64_t sums[HPI_HS_SERIES_MAX_BUCKETS];

static struct hpi_hs_series_acc g_acc;

static bool g_begin(uint16_t nb, int64_t from, int64_t to, bool cumulative)
{
    return hpi_hs_series_begin(&g_acc, b, sums, nb, from, to, cumulative);
}

ZTEST(hs_series, test_mean_min_max_per_bucket)
{
    zassert_true(g_begin(NB, T0, T0 + DAY, false));

    /* hour 0 gets 60/80/100 -> mean 80; hour 5 gets a single 150 */
    hpi_hs_series_feed(&g_acc, T0 + 10, 60);
    hpi_hs_series_feed(&g_acc, T0 + 20, 80);
    hpi_hs_series_feed(&g_acc, T0 + 30, 100);
    hpi_hs_series_feed(&g_acc, T0 + 5 * HOUR + 1, 150);
    hpi_hs_series_end(&g_acc);

    zassert_equal(b[0].count, 3, "bucket 0 should hold 3 samples");
    zassert_equal(b[0].mean, 80, "mean should be 80, got %d", b[0].mean);
    zassert_equal(b[0].min, 60);
    zassert_equal(b[0].max, 100);

    zassert_equal(b[5].count, 1);
    zassert_equal(b[5].mean, 150);
    zassert_equal(b[5].min, 150);
    zassert_equal(b[5].max, 150);
}

/* The one that matters most: an off-skin/charging gap must stay count==0 so the
 * UI can skip it. If a gap silently became mean==0 the sparkline would plot a
 * dive to 0 bpm and read as a physiological event. */
ZTEST(hs_series, test_empty_bucket_is_a_gap_not_zero)
{
    zassert_true(g_begin(NB, T0, T0 + DAY, false));
    hpi_hs_series_feed(&g_acc, T0 + 1, 70);
    hpi_hs_series_end(&g_acc);

    zassert_equal(b[0].count, 1);
    for (int i = 1; i < NB; i++) {
        zassert_equal(b[i].count, 0, "bucket %d should be an empty gap", i);
    }
}

/* Peak fidelity: the HS-2 P1 argument (a minute whose true max was 164 has a
 * mean of 136) applies again at bucket scale — min/max must survive the
 * reduction, not be flattened into the mean. */
ZTEST(hs_series, test_peaks_survive_the_bucket)
{
    zassert_true(g_begin(NB, T0, T0 + DAY, false));
    for (int i = 0; i < 59; i++) {
        hpi_hs_series_feed(&g_acc, T0 + i, 60);
    }
    hpi_hs_series_feed(&g_acc, T0 + 59, 164);   /* one exercise spike */
    hpi_hs_series_end(&g_acc);

    zassert_equal(b[0].count, 60);
    zassert_equal(b[0].max, 164, "the peak must not be averaged away");
    zassert_equal(b[0].min, 60);
    zassert_true(b[0].mean < 70, "mean should sit near the resting mass");
}

/* CUMULATIVE (steps/energy) are stored by the P1 epoch reducer as the window's
 * LAST value. Summing them would multiply the day's total. */
ZTEST(hs_series, test_cumulative_takes_last_not_sum)
{
    zassert_true(g_begin(NB, T0, T0 + DAY, true));
    hpi_hs_series_feed(&g_acc, T0 + 10, 1000);
    hpi_hs_series_feed(&g_acc, T0 + 20, 1200);
    hpi_hs_series_feed(&g_acc, T0 + 30, 1500);   /* newest in bucket 0 */
    hpi_hs_series_end(&g_acc);

    zassert_equal(b[0].count, 3);
    zassert_equal(b[0].mean, 1500, "cumulative bucket must report the LAST value (got %d)",
                  b[0].mean);
}

/* Out-of-order arrival: the ring tail is scanned after the segments, so a
 * cumulative bucket must still end up with the newest *timestamp*, not the
 * last-fed sample. */
ZTEST(hs_series, test_cumulative_out_of_order)
{
    zassert_true(g_begin(NB, T0, T0 + DAY, true));
    hpi_hs_series_feed(&g_acc, T0 + 30, 1500);   /* newest ts, fed first */
    hpi_hs_series_feed(&g_acc, T0 + 10, 1000);
    hpi_hs_series_end(&g_acc);

    zassert_equal(b[0].mean, 1500, "newest ts must win regardless of feed order");
}

/* [from,to) is half-open: a sample exactly at `to` belongs to the next window.
 * If it landed in the last bucket, two back-to-back queries would both count it. */
ZTEST(hs_series, test_window_is_half_open)
{
    zassert_true(g_begin(NB, T0, T0 + DAY, false));
    hpi_hs_series_feed(&g_acc, T0 + DAY, 99);        /* exactly `to` -> dropped */
    hpi_hs_series_feed(&g_acc, T0 - 1, 98);          /* before `from` -> dropped */
    hpi_hs_series_feed(&g_acc, T0 + DAY - 1, 97);    /* last instant -> last bucket */
    hpi_hs_series_end(&g_acc);

    zassert_equal(b[NB - 1].count, 1, "last instant belongs to the last bucket");
    zassert_equal(b[NB - 1].mean, 97);

    uint32_t total = 0;
    for (int i = 0; i < NB; i++) {
        total += b[i].count;
    }
    zassert_equal(total, 1, "out-of-window samples must be dropped, got %u", total);
}

ZTEST(hs_series, test_bucket_ts_is_window_start)
{
    zassert_true(g_begin(NB, T0, T0 + DAY, false));
    hpi_hs_series_feed(&g_acc, T0 + 5 * HOUR + 123, 70);   /* off-grid ts */
    hpi_hs_series_end(&g_acc);

    /* x must be the grid, not wherever the sample happened to land */
    zassert_equal(b[0].ts, T0);
    zassert_equal(b[5].ts, T0 + 5 * HOUR, "bucket ts must be its window start");
    zassert_equal(b[NB - 1].ts, T0 + 23 * HOUR);
}

ZTEST(hs_series, test_bad_args_rejected)
{
    zassert_false(g_begin(NB, T0, T0, false), "to == from must be rejected");
    zassert_false(g_begin(NB, T0, T0 - 1, false), "to < from must be rejected");
    zassert_false(g_begin(0, T0, T0 + DAY, false), "nb == 0 must be rejected");
    zassert_false(g_begin(HPI_HS_SERIES_MAX_BUCKETS + 1, T0, T0 + DAY, false),
                  "nb > MAX must be rejected");
    zassert_false(hpi_hs_series_begin(&g_acc, NULL, sums, NB, T0, T0 + DAY, false));
    zassert_false(hpi_hs_series_begin(&g_acc, b, NULL, NB, T0, T0 + DAY, false));

    /* an inert acc must not write through null pointers */
    hpi_hs_series_feed(&g_acc, T0 + 1, 70);
    hpi_hs_series_end(&g_acc);
}

/* A rejected begin() must leave no stale state from a previous good run. */
ZTEST(hs_series, test_failed_begin_is_inert)
{
    zassert_true(g_begin(NB, T0, T0 + DAY, false));
    hpi_hs_series_feed(&g_acc, T0 + 1, 70);

    zassert_false(g_begin(NB, T0, T0, false));
    hpi_hs_series_feed(&g_acc, T0 + 1, 999);   /* must be a no-op, not a crash */
    hpi_hs_series_end(&g_acc);
}

ZTEST(hs_series, test_single_bucket_spans_whole_window)
{
    zassert_true(g_begin(1, T0, T0 + DAY, false));
    hpi_hs_series_feed(&g_acc, T0, 60);
    hpi_hs_series_feed(&g_acc, T0 + DAY / 2, 80);
    hpi_hs_series_feed(&g_acc, T0 + DAY - 1, 100);
    hpi_hs_series_end(&g_acc);

    zassert_equal(b[0].count, 3);
    zassert_equal(b[0].mean, 80);
    zassert_equal(b[0].ts, T0);
}

/* Negative values (temp deviation is signed fixed-point) must not break the
 * integer-division mean. */
ZTEST(hs_series, test_negative_values)
{
    zassert_true(g_begin(NB, T0, T0 + DAY, false));
    hpi_hs_series_feed(&g_acc, T0 + 1, -200);
    hpi_hs_series_feed(&g_acc, T0 + 2, -100);
    hpi_hs_series_end(&g_acc);

    zassert_equal(b[0].mean, -150);
    zassert_equal(b[0].min, -200);
    zassert_equal(b[0].max, -100);
}

ZTEST_SUITE(hs_series, NULL, NULL, NULL, NULL, NULL);
