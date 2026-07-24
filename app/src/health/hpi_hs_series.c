/*
 * HealthyPi Move — Health Store: series bucketing (P3)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * See hpi_hs_series.h. Pure arithmetic — no kernel, fs or zbus, so app/tests
 * can link it on its own.
 */

#include <string.h>

#include "hpi_hs_series.h"

bool hpi_hs_series_begin(struct hpi_hs_series_acc *a, struct hpi_hs_bucket *buckets,
                         int64_t *sum_scratch, uint16_t nb,
                         int64_t from, int64_t to, bool cumulative)
{
    if (a == NULL) {
        return false;
    }
    memset(a, 0, sizeof(*a));

    if (buckets == NULL || sum_scratch == NULL || nb == 0 ||
        nb > HPI_HS_SERIES_MAX_BUCKETS || to <= from) {
        return false;
    }

    memset(buckets, 0, (size_t)nb * sizeof(*buckets));
    memset(sum_scratch, 0, (size_t)nb * sizeof(*sum_scratch));

    a->b = buckets;
    a->sum = sum_scratch;
    a->nb = nb;
    a->from = from;
    a->span = to - from;
    a->cumulative = cumulative;
    a->valid = true;
    return true;
}

void hpi_hs_series_feed(struct hpi_hs_series_acc *a, int64_t ts, int32_t value)
{
    if (a == NULL || !a->valid) {
        return;
    }

    /* Half-open [from, to): a sample exactly at `to` belongs to the next window,
     * not to the last bucket — otherwise back-to-back queries double-count it. */
    int64_t rel = ts - a->from;
    if (rel < 0 || rel >= a->span) {
        return;
    }

    uint16_t i = (uint16_t)((rel * (int64_t)a->nb) / a->span);
    if (i >= a->nb) {          /* defensive: span/nb rounding can't reach here */
        i = a->nb - 1u;
    }
    struct hpi_hs_bucket *b = &a->b[i];

    if (b->count == 0) {
        b->min = b->max = value;
        /* CUMULATIVE reduces to the bucket's LAST value (see the header), so
         * track the newest ts seen. `ts` is scratch until hpi_hs_series_end()
         * overwrites it with the bucket's window start. */
        b->ts = ts;
        b->mean = value;
    } else {
        if (value < b->min) {
            b->min = value;
        }
        if (value > b->max) {
            b->max = value;
        }
        if (a->cumulative && ts >= b->ts) {
            b->mean = value;
            b->ts = ts;
        }
    }

    a->sum[i] += value;
    if (b->count < UINT16_MAX) {
        b->count++;
    }
}

void hpi_hs_series_end(struct hpi_hs_series_acc *a)
{
    if (a == NULL || !a->valid) {
        return;
    }

    for (uint16_t i = 0; i < a->nb; i++) {
        if (a->b[i].count > 0 && !a->cumulative) {
            a->b[i].mean = (int32_t)(a->sum[i] / (int64_t)a->b[i].count);
        }
        /* Stamp the window start last: feed() used ts as CUMULATIVE scratch. */
        a->b[i].ts = a->from + (a->span * (int64_t)i) / (int64_t)a->nb;
    }
}
