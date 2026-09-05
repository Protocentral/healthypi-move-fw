/*
 * H2 — HRV pooling unit tests (no hardware). §3.3 of the test plan, U7–U10.
 *
 * hpi_hs_hrv_pool.h is a header-only pure function (stdint/math only), so
 * nothing from the store's filesystem/zbus/hw deps is linked in.
 */
#include <zephyr/ztest.h>
#include "health/hpi_hs_hrv_pool.h"

ZTEST_SUITE(hs_hrv_pool, NULL, NULL, NULL, NULL, NULL);

/* U7: (n_dd=100, RMSSD=40) + (n_dd=100, RMSSD=60) -> pooled ~50.99ms, NOT the
 * naive mean of 50. Pooling is sqrt(mean of squares), not mean of sqrt. */
ZTEST(hs_hrv_pool, test_U7_pooled_not_naive_mean)
{
    struct hs_rmssd_pool_acc acc;
    hs_rmssd_pool_reset(&acc);
    hs_rmssd_pool_add(&acc, 400, 100);   /* 40.0ms x10, 100 pairs */
    hs_rmssd_pool_add(&acc, 600, 100);   /* 60.0ms x10, 100 pairs */

    int32_t pooled = hs_rmssd_pool_result(&acc);

    zassert_true(pooled >= 505 && pooled <= 515,
                "expected ~510 (50.99ms x10), got %d", pooled);
    zassert_not_equal(pooled, 500, "must not equal the naive mean of 40 and 60");
}

/* U8: (n_dd=10, RMSSD=100) + (n_dd=1000, RMSSD=30) -> pooled ~31.1ms.
 * Naive averaging gives 65ms - roughly 2x error. The tiny 10-pair window
 * must barely move the pooled value away from the 1000-pair one. */
ZTEST(hs_hrv_pool, test_U8_small_window_barely_moves_pooled_value)
{
    struct hs_rmssd_pool_acc acc;
    hs_rmssd_pool_reset(&acc);
    hs_rmssd_pool_add(&acc, 1000, 10);   /* 100.0ms x10, only 10 pairs */
    hs_rmssd_pool_add(&acc, 300, 1000);  /* 30.0ms x10, 1000 pairs */

    int32_t pooled = hs_rmssd_pool_result(&acc);

    zassert_true(pooled >= 305 && pooled <= 320,
                "expected ~311 (31.1ms x10), got %d", pooled);
    zassert_true(pooled < 500,
                "must stay close to the 1000-pair window, not the naive mean of 65");
}

/* U9: a window with n_dd=0 must be fully ignored - no divide-by-zero, no skew
 * from a window that produced no usable pairs at all. */
ZTEST(hs_hrv_pool, test_U9_zero_pairs_window_ignored)
{
    struct hs_rmssd_pool_acc acc;
    hs_rmssd_pool_reset(&acc);
    hs_rmssd_pool_add(&acc, 9999, 0);    /* garbage value, but 0 pairs -> must be ignored */
    hs_rmssd_pool_add(&acc, 500, 50);    /* the only real contribution */

    int32_t pooled = hs_rmssd_pool_result(&acc);

    zassert_equal(pooled, 500, "the zero-pairs window must not contribute at all");
}

/* U10: baseline gate is on total PAIRS accumulated, not on window/beat count -
 * 599 pairs then one more crossing to 600 must be an exact, observable edge. */
ZTEST(hs_hrv_pool, test_U10_gate_is_on_pairs_not_windows)
{
    struct hs_rmssd_pool_acc acc;
    hs_rmssd_pool_reset(&acc);

    hs_rmssd_pool_add(&acc, 500, 599);
    zassert_true(acc.pairs < 600, "sanity: still below the H2 baseline gate of 600");

    hs_rmssd_pool_add(&acc, 500, 1);
    zassert_equal(acc.pairs, 600, "one more pair must cross the gate exactly");
}

/* Empty pool - never fed anything - must return the sentinel, not 0 or NaN. */
ZTEST(hs_hrv_pool, test_empty_pool_returns_sentinel)
{
    struct hs_rmssd_pool_acc acc;
    hs_rmssd_pool_reset(&acc);

    zassert_equal(hs_rmssd_pool_result(&acc), -1,
                 "an empty pool must return -1, never 0 or an undefined value");
}