/*

* HealthyPi Move — HRV-derived stress score tests.

*

* SPDX-License-Identifier: MIT

* Copyright (c) 2025 Protocentral Electronics

*/

#include <zephyr/ztest.h>
#include "health/hpi_hs_stress.h"
#include "health/hpi_hs_types.h"

ZTEST(hs_stress, test_stress_score_maps_hrv_against_baseline)
{
    zassert_equal(hpi_hs_stress_from_hrv(400, 400, 20), 50, NULL);
    zassert_equal(hpi_hs_stress_from_hrv(200, 400, 20), 100, NULL);
    zassert_equal(hpi_hs_stress_from_hrv(600, 400, 20), 0, NULL);
}

ZTEST(hs_stress, test_stress_score_requires_enough_windows)
{
    zassert_equal(hpi_hs_stress_from_hrv(400, 400, 19), -1, NULL);
    zassert_equal(hpi_hs_stress_from_hrv(0, 400, 20), -1, NULL);
    zassert_equal(hpi_hs_stress_from_hrv(400, 0, 20), -1, NULL);
}

ZTEST(hs_stress, test_synthetic_quality_bit_is_distinct)
{
    uint8_t q = hpi_hs_stress_record_quality(HPI_HS_Q_VALID | HPI_HS_Q_SYNTHETIC);
    zassert_true(q & HPI_HS_Q_SYNTHETIC, "synthetic bit must be preserved");
    zassert_true(q & HPI_HS_Q_VALID, "valid bit must remain set");
    zassert_true(q & HPI_HS_Q_ON_SKIN, "skin-contact bit must remain set");
    zassert_true(q & HPI_HS_Q_LOW_MOTION, "low-motion bit must remain set");
}

ZTEST_SUITE(hs_stress, NULL, NULL, NULL, NULL, NULL);