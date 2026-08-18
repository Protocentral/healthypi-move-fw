/*
 * HealthyPi Move — HRV pooling 
 *
 * Pool sufficient statistics across windows instead of averaging window
 * RMSSDs: sqrt(Σ n_dd·RMSSD² / Σ n_dd). Averaging square roots directly
 * (naive mean) is a Jensen's-inequality bias and weights a 30-beat window
 * equally with a 300-beat one — see handoff H2.
 *
 * Header-only, FPU-free, integer accumulation (RMSSD is stored x10 fixed
 * point; this recovers the underlying Σdd² and pools on that, matching how
 * hs_recompute_summary already accumulates rms_sum_dd2 / rms_pairs).
 */
#ifndef HPI_HS_HRV_POOL_H
#define HPI_HS_HRV_POOL_H

#include <stdint.h>
#include "hpi_hs_hrv_correct.h"   /* reuse hpi_hs_isqrt64 */

struct hs_rmssd_pool_acc {
    uint64_t sum_dd2;
    uint32_t pairs;
};

static inline void hs_rmssd_pool_reset(struct hs_rmssd_pool_acc *acc)
{
    acc->sum_dd2 = 0;
    acc->pairs = 0;
}

/* Add one window's contribution. n_dd == 0 is ignored (no divide-by-zero,
 * no skew from a window that produced no pairs at all). */
static inline void hs_rmssd_pool_add(struct hs_rmssd_pool_acc *acc,
                                     int32_t rmssd_x10, uint32_t n_dd)
{
    if (n_dd == 0) {
        return;
    }
    /* RMSSD² · n_dd = Σdd² for that window, same fixed-point scale recovery
     * hs_recompute_summary already does at the call site. */
    acc->sum_dd2 += (uint64_t)rmssd_x10 * (uint64_t)rmssd_x10 * n_dd;
    acc->pairs += n_dd;
}

/* Returns -1 if no pairs were ever added (empty pool), never 0/NaN. */
static inline int32_t hs_rmssd_pool_result(const struct hs_rmssd_pool_acc *acc)
{
    if (acc->pairs == 0) {
        return -1;
    }
    return (int32_t)hpi_hs_isqrt64((int64_t)(acc->sum_dd2 / acc->pairs));
}

#endif /* HPI_HS_HRV_POOL_H */