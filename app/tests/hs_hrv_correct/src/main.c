/* H3 — HRV beat-timing jitter de-jitter unit tests. §3.4 of the test plan, U11–U14. */
#include <zephyr/ztest.h>
#include "health/hpi_hs_hrv_correct.h"

ZTEST_SUITE(hs_hrv_correct, NULL, NULL, NULL, NULL, NULL);

/* U11: rmssd=63.3ms, sigma=22.6ms -> sqrt(63.3^2 - 6*22.6^2) ~= 30.8ms.
 * This reproduces the analysis report's own decomposition exactly. */
ZTEST(hs_hrv_correct, test_U11_reproduces_report_decomposition)
{
    int32_t result = hpi_hs_rmssd_dejitter(633, 226);   /* x10 fixed point */
    zassert_true(result >= 305 && result <= 310,
                 "expected ~30.8ms (308 x10), got %d", result);
}

/* U12: sigma=0 -> exact passthrough, not "subtraction of zero-ish". */
ZTEST(hs_hrv_correct, test_U12_disabled_is_exact_passthrough)
{
    zassert_equal(hpi_hs_rmssd_dejitter(633, 0), 633, "sigma=0 must not alter the value");
}

/* U13: rmssd=56ms, sigma=22.6ms — AT the floor. Must be -1, never a small
 * positive. (6*sigma^2 = 3064.56, sqrt ~= 55.4ms — 56ms is right at/near it.) */
ZTEST(hs_hrv_correct, test_U13_at_floor_returns_sentinel)
{
    zassert_equal(hpi_hs_rmssd_dejitter(560, 226), -1,
                 "at the floor must be -1, never a small positive number");
}

/* U14: rmssd < sqrt(6)*sigma entirely -> -1, no negative radicand, no
 * integer underflow (this is the case a naive unsigned subtraction would
 * wrap on, rather than correctly detecting "unresolvable"). */
ZTEST(hs_hrv_correct, test_U14_below_floor_no_underflow)
{
    zassert_equal(hpi_hs_rmssd_dejitter(300, 226), -1,
                 "well below sqrt(6)*sigma must be -1 with no underflow");
}