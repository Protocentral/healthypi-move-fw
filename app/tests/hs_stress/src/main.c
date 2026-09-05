/*
 * H3/H2 integration — stress-score unit tests (no hardware). §3.5 of the
 * test plan, U15, plus the existing guard behavior of hpi_hs_stress_from_hrv.
 *
 * Both headers are pure (stdint/math only) - no filesystem/zbus/hw deps.
 */
#include <zephyr/ztest.h>
#include "health/hpi_hs_stress.h"
#include "health/hpi_hs_hrv_correct.h"

ZTEST_SUITE(hs_stress, NULL, NULL, NULL, NULL, NULL);

/* Comfortably above whatever HPI_HS_STRESS_MIN_BASELINE_PAIRS is set to, so
 * these tests exercise the SCORING logic, not the baseline-gate logic. */
#define AMPLE_PAIRS (HPI_HS_STRESS_MIN_BASELINE_PAIRS + 100)

/* U15 — the test that protects H3's own §1.3 requirement: correction must be
 * applied to BOTH sides of the ratio, or the ratio gets worse, not better.
 * Values from the handoff's own worked example: sigma=22.6ms,
 * observed rmssd=60.4ms, baseline=63.5ms. */
ZTEST(hs_stress, test_U15_correction_must_be_symmetric)
{
    int32_t sigma = 226;

    int32_t uncorrected = hpi_hs_stress_from_hrv(604, 635, AMPLE_PAIRS);
    zassert_true(uncorrected >= 0, "uncorrected score must be valid");

    int32_t rmssd_c    = hpi_hs_rmssd_dejitter(604, sigma);
    int32_t baseline_c = hpi_hs_rmssd_dejitter(635, sigma);
    int32_t corrected  = hpi_hs_stress_from_hrv(rmssd_c, baseline_c, AMPLE_PAIRS);

    zassert_true(corrected > uncorrected,
                "a real physiological drop must show a LARGER score once jitter "
                "is removed from both sides, not a smaller or equal one");

    /* the trap: correcting ONLY the observed side */
    int32_t one_sided = hpi_hs_stress_from_hrv(rmssd_c, 635, AMPLE_PAIRS);
    zassert_not_equal(one_sided, corrected,
                      "one-sided correction must diverge from full correction — "
                      "this is exactly what a future refactor could silently break");
}

/* Formula sanity: ratio == 1.0 (rmssd == baseline) must land exactly on 50,
 * the documented "at your own normal" midpoint. */
ZTEST(hs_stress, test_score_at_own_baseline_is_50)
{
    int32_t score = hpi_hs_stress_from_hrv(500, 500, AMPLE_PAIRS);
    zassert_equal(score, 50, "rmssd == baseline must map to exactly 50, got %d", score);
}

/* Clamp behavior: an extreme low ratio (rmssd << baseline) must clamp at
 * 100, never overflow past it. */
ZTEST(hs_stress, test_score_clamps_at_100)
{
    int32_t score = hpi_hs_stress_from_hrv(50, 1000, AMPLE_PAIRS);
    zassert_equal(score, 100, "an extreme low ratio must clamp to 100, got %d", score);
}

/* Clamp behavior: an extreme high ratio (rmssd >> baseline, well-rested/
 * relaxed) must clamp at 0, never go negative. */
ZTEST(hs_stress, test_score_clamps_at_0)
{
    int32_t score = hpi_hs_stress_from_hrv(2000, 500, AMPLE_PAIRS);
    zassert_equal(score, 0, "an extreme high ratio must clamp to 0, got %d", score);
}

/* Guard: rmssd <= 0 must be sentinel, never treated as valid input. */
ZTEST(hs_stress, test_nonpositive_rmssd_is_sentinel)
{
    zassert_equal(hpi_hs_stress_from_hrv(0, 500, AMPLE_PAIRS), -1);
    zassert_equal(hpi_hs_stress_from_hrv(-10, 500, AMPLE_PAIRS), -1);
}

/* Guard: baseline <= 0 must be sentinel - a not-yet-established baseline
 * must never look like "you're extremely stressed" (divide-by-near-zero). */
ZTEST(hs_stress, test_nonpositive_baseline_is_sentinel)
{
    zassert_equal(hpi_hs_stress_from_hrv(500, 0, AMPLE_PAIRS), -1);
}

/* Guard: fewer than HPI_HS_STRESS_MIN_BASELINE_PAIRS must be sentinel, no
 * matter how clean rmssd/baseline look — an under-built baseline must not
 * produce a confident-looking score. */
ZTEST(hs_stress, test_insufficient_baseline_pairs_is_sentinel)
{
    zassert_equal(hpi_hs_stress_from_hrv(500, 500, HPI_HS_STRESS_MIN_BASELINE_PAIRS - 1), -1);
    zassert_true(hpi_hs_stress_from_hrv(500, 500, HPI_HS_STRESS_MIN_BASELINE_PAIRS) >= 0,
                "exactly at the gate must be valid");
}