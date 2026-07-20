/*
 * HealthyPi Move — Health Store: on-flash segment layout (HS-2 P2)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Segments hold a FIXED record count, which makes seq -> (segment, byte offset)
 * pure arithmetic: no index file, no RAM index, nothing to persist or rebuild.
 *
 *     seg = (seq - 1) / HPI_HS_SEG_RECORDS
 *     off = ((seq - 1) % HPI_HS_SEG_RECORDS) * HPI_HS_SAMPLE_WIRE_SIZE
 *
 * Before P2 segments rolled at ">= 8192 B after a variable-size batch", so they
 * were variable-length and nothing could locate a seq without reading every record
 * from the oldest segment forward — O(since) per page, O(n^2) per sync.
 *
 * `seq` is 1-based (hpi_hs_record assigns ++s_seq, so the first sample is seq 1).
 * These live in a header of their own so the arithmetic can be unit-tested without
 * dragging in the filesystem, zbus and the rest of the store.
 */

#ifndef HPI_HS_LAYOUT_H
#define HPI_HS_LAYOUT_H

#include <stdint.h>
#include "health/hpi_hs_types.h"

/* HS-2 P6: 4480 * 18 B = 80,640 B per segment.
 *
 * Retention is HS_MAX_SEGS (128) segments, so segment SIZE is what sets the window:
 * 128 x 4480 = 573,440 records = 10.3 MB = ~104 days at the measured ~5,500 rec/day.
 * It was 128 x 448 = 57,344 records = ~10 days -- short enough that a user who did
 * not open the app for a fortnight lost data permanently.
 *
 * Why bigger segments rather than MORE of them: LittleFS directory lookup is O(entries),
 * and every fs_open pays it. Reaching ~100 days with 448-record segments would need
 * ~1,277 files in one flat directory, taxing every read. Growing the segment instead
 * keeps the file count at 128 -- unchanged -- so the open cost does not move at all. */
#define HPI_HS_SEG_RECORDS  4480
#define HPI_HS_SEG_BYTES    (HPI_HS_SEG_RECORDS * HPI_HS_SAMPLE_WIRE_SIZE)

/* Which segment file holds `seq`. */
static inline uint32_t hpi_hs_seg_of(uint32_t seq)
{
    return (seq - 1u) / HPI_HS_SEG_RECORDS;
}

/* Record index of `seq` within its segment (0 .. HPI_HS_SEG_RECORDS-1). */
static inline uint32_t hpi_hs_idx_of(uint32_t seq)
{
    return (seq - 1u) % HPI_HS_SEG_RECORDS;
}

/* Byte offset of `seq` within its segment file. */
static inline uint32_t hpi_hs_off_of(uint32_t seq)
{
    return hpi_hs_idx_of(seq) * HPI_HS_SAMPLE_WIRE_SIZE;
}

/* First seq stored in `seg`. */
static inline uint32_t hpi_hs_seg_first_seq(uint32_t seg)
{
    return seg * HPI_HS_SEG_RECORDS + 1u;
}

/* How many records of a run starting at `seq` fit in the rest of its segment.
 * A flush batch or a read page can straddle a boundary; callers split on this. */
static inline uint32_t hpi_hs_seg_room(uint32_t seq)
{
    return HPI_HS_SEG_RECORDS - hpi_hs_idx_of(seq);
}

#endif /* HPI_HS_LAYOUT_H */
