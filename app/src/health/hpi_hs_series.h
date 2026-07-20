/*
 * HealthyPi Move — Health Store: series bucketing (P3)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * The pure half of hpi_hs_series(): map timestamped samples onto an evenly
 * spaced bucket grid over [from,to] and reduce each bucket to min/mean/max.
 * Deliberately standalone (stdint/stdbool only, no fs/zbus/kernel) so it is
 * unit-testable without linking the store — the same split hpi_hs_epoch.c uses.
 * hpi_health_store.c owns the I/O: it drives feed() from hs_durable_scan().
 *
 * Why buckets and not raw samples: the tiles plot ~24 points from a window that
 * holds thousands of epoch records, and the display thread must never do that
 * arithmetic (or the file I/O behind it).
 */

#ifndef HPI_HS_SERIES_H
#define HPI_HS_SERIES_H

#include <stdint.h>
#include <stdbool.h>

/* Kept small on purpose: app-core RAM is ~99% full, and the caller must also
 * hold a bucket array (24 B each) plus this module's int64 sum scratch. */
#define HPI_HS_SERIES_MAX_BUCKETS 32

/* One plot point.
 *   ts     - the bucket's window START (stable x, independent of which samples
 *            landed in it)
 *   count  - samples in the bucket. **count == 0 means NO DATA, not zero** —
 *            plot it as a gap. An off-skin charging window would otherwise
 *            chart as a dive to 0 bpm.
 *   min/max- observed extremes (meaningless when count == 0)
 *   mean   - DISCRETE: Σ/n over the bucket.
 *            CUMULATIVE (steps/energy): the **last** value in the bucket, not a
 *            sum. The P1 epoch reducer already stores cumulative signals as the
 *            window's last value, so summing them would multiply the total.
 */
struct hpi_hs_bucket { int64_t ts; int32_t min, mean, max; uint16_t count; };

struct hpi_hs_series_acc {
    struct hpi_hs_bucket *b;
    int64_t *sum;        /* caller-owned scratch, >= nb entries */
    uint16_t nb;
    int64_t from, span;
    bool cumulative;
    bool valid;
};

/* Returns false on bad args (null, nb == 0 or > MAX, to <= from); the acc is
 * then inert and feed/end are no-ops. Zeroes `buckets` and `sum_scratch`. */
bool hpi_hs_series_begin(struct hpi_hs_series_acc *a, struct hpi_hs_bucket *buckets,
                         int64_t *sum_scratch, uint16_t nb,
                         int64_t from, int64_t to, bool cumulative);

/* Feed one sample. Out-of-window samples are ignored, so the caller does not
 * have to pre-filter (hs_durable_scan already does, but a ring tail may not). */
void hpi_hs_series_feed(struct hpi_hs_series_acc *a, int64_t ts, int32_t value);

/* Finalize: compute means and stamp each bucket's window-start ts. */
void hpi_hs_series_end(struct hpi_hs_series_acc *a);

#endif /* HPI_HS_SERIES_H */
