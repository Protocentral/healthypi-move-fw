/*
 * HealthyPi Move — HRV beat-timing jitter correction 
 *
 * RMSSD_obs² = RMSSD_phys² + 6σ²
 *
 * Beat-timing jitter perturbs two consecutive intervals with opposite sign:
 * dRR = b[i+1] − 2·b[i] + b[i-1], giving variance 6σ² (not 2σ² — that would
 * be the interval-noise model, wrong one for a peak-detecting hub).
 *
 * All units x10 fixed point (e.g. rmssd_x10 = rmssd_ms * 10), matching
 * hpi_hs_stress.h's convention. Header-only, FPU-free.
 */
#ifndef HPI_HS_HRV_CORRECT_H
#define HPI_HS_HRV_CORRECT_H

#include <stdint.h>

/* Integer square root (Newton's method), 64-bit safe. */
static inline int32_t hpi_hs_isqrt64(int64_t v)
{
    if (v <= 0) {
        return 0;
    }
    int64_t x = v;
    int64_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + v / x) / 2;
    }
    return (int32_t)x;
}

/*
 * Subtract the jitter noise floor from an observed RMSSD, in quadrature.
 *
 * sigma_ms_x10 == 0 means correction is DISABLED (ship default) — the
 * observed value is returned unchanged.
 *
 * Returns -1 (sentinel: "cannot resolve") when the observed value is at or
 * below the noise floor. Callers MUST treat -1 as "withhold the score", never
 * clamp it to a small positive number — see the instability guard below.
 */
static inline int32_t hpi_hs_rmssd_dejitter(int32_t rmssd_x10, int32_t sigma_ms_x10)
{
    if (sigma_ms_x10 <= 0) {
        return rmssd_x10;          /* correction disabled */
    }
    if (rmssd_x10 <= 0) {
        return -1;
    }

    int64_t rmssd2      = (int64_t)rmssd_x10 * (int64_t)rmssd_x10;
    int64_t six_sigma2  = 6LL * (int64_t)sigma_ms_x10 * (int64_t)sigma_ms_x10;

    /* Instability guard: rmssd_obs² − 6σ² < 0.3·6σ²  →  rmssd2 < 1.3·6σ² */
    if (rmssd2 < (six_sigma2 * 13) / 10) {
        return -1;
    }

    return hpi_hs_isqrt64(rmssd2 - six_sigma2);
}

#endif /* HPI_HS_HRV_CORRECT_H */