/*
 * HealthyPi Move — H6 readiness score tests
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Readiness is a number the user will act on ("am I recovered enough to train?"),
 * so its two failure modes both matter: a premature score built on a thin baseline,
 * and a score whose direction is wrong (higher HRV / lower RHR must read as MORE
 * recovered). These pin both, plus the fixed-point weighting and clamps.
 */

#include <zephyr/ztest.h>
#include "health/hpi_hs_readiness.h"

/* Enough baseline to pass the gate in the value tests. */
#define OKW  HPI_HS_READINESS_MIN_HRV_WINDOWS
#define OKN  HPI_HS_READINESS_MIN_RHR_N

ZTEST(hs_readiness, test_neutral_is_50)
{
    /* HRV at baseline (ratio 1 -> 50) and RHR at baseline (-> 50) => 50. */
    zassert_equal(hpi_hs_readiness(400, 400, 60, 60, OKW, OKN), 50, NULL);
}

ZTEST(hs_readiness, test_good_recovery_high)
{
    /* HRV 1.5x baseline (hrv 100) + RHR 5 bpm below (rhr 75) => .6*100+.4*75 = 90. */
    zassert_equal(hpi_hs_readiness(600, 400, 55, 60, OKW, OKN), 90, NULL);
}

ZTEST(hs_readiness, test_poor_recovery_low)
{
    /* HRV 0.5x baseline (hrv 0) + RHR 5 bpm above (rhr 25) => .6*0+.4*25 = 10. */
    zassert_equal(hpi_hs_readiness(200, 400, 65, 60, OKW, OKN), 10, NULL);
}

ZTEST(hs_readiness, test_hrv_weighted_60)
{
    /* HRV 1.5x (100) but RHR 10 bpm above baseline (rhr score 0) => .6*100 = 60.
     * Proves HRV carries 60% of the weight. */
    zassert_equal(hpi_hs_readiness(600, 400, 70, 60, OKW, OKN), 60, NULL);
}

ZTEST(hs_readiness, test_clamps_at_100_and_0)
{
    /* Extreme good: HRV 2.5x + RHR 20 below -> both sub-scores clamp to 100 -> 100. */
    zassert_equal(hpi_hs_readiness(1000, 400, 40, 60, OKW, OKN), 100, NULL);
    /* Extreme bad: HRV 0.2x + RHR 20 above -> both clamp to 0 -> 0. */
    zassert_equal(hpi_hs_readiness(80, 400, 80, 60, OKW, OKN), 0, NULL);
}

ZTEST(hs_readiness, test_thin_baseline_is_sentinel)
{
    /* One window/epoch short of the minimum on either baseline => -1, not a number. */
    zassert_equal(hpi_hs_readiness(400, 400, 60, 60, OKW - 1, OKN), -1, NULL);
    zassert_equal(hpi_hs_readiness(400, 400, 60, 60, OKW, OKN - 1), -1, NULL);
}

ZTEST(hs_readiness, test_invalid_inputs_are_sentinel)
{
    /* Non-positive inputs cannot support a score. */
    zassert_equal(hpi_hs_readiness(0,   400, 60, 60, OKW, OKN), -1, NULL);
    zassert_equal(hpi_hs_readiness(400, 0,   60, 60, OKW, OKN), -1, NULL);
    zassert_equal(hpi_hs_readiness(400, 400, 0,  60, OKW, OKN), -1, NULL);
    zassert_equal(hpi_hs_readiness(400, 400, 60, 0,  OKW, OKN), -1, NULL);
}

ZTEST_SUITE(hs_readiness, NULL, NULL, NULL, NULL, NULL);
