/*
 * HS-2 P2 — seq <-> segment/offset arithmetic unit tests (native_sim).
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * The mapping in health/hpi_hs_layout.h is what replaced the O(n^2) log rescan
 * with a seek. If it is wrong, the store hands the app STRUCTURALLY VALID GARBAGE
 * — records that parse fine but are the wrong samples. That is the worst failure
 * mode available to us, so it is worth pinning down exactly.
 */

#include <zephyr/ztest.h>
#include "health/hpi_hs_layout.h"
#include "health/hpi_hs_stress.h"

#define N HPI_HS_SEG_RECORDS   /* never hardcode this — P6 changed it 448 -> 4480 */

ZTEST_SUITE(hs_layout, NULL, NULL, NULL, NULL, NULL);

/* seq is 1-BASED (hpi_hs_record does ++s_seq, so the first sample is seq 1). An
 * off-by-one here would shift every record in the store by one slot. */
ZTEST(hs_layout, test_first_seq_is_segment_zero_slot_zero)
{
    zassert_equal(hpi_hs_seg_of(1), 0, "seq 1 lives in segment 0");
    zassert_equal(hpi_hs_idx_of(1), 0, "seq 1 is slot 0");
    zassert_equal(hpi_hs_off_of(1), 0, "seq 1 is at byte 0");
}

ZTEST(hs_layout, test_segment_boundaries)
{
    /* last record of segment 0 */
    zassert_equal(hpi_hs_seg_of(N), 0, "");
    zassert_equal(hpi_hs_idx_of(N), N - 1, "");
    zassert_equal(hpi_hs_off_of(N), (N - 1) * HPI_HS_SAMPLE_WIRE_SIZE, "");

    /* first record of segment 1 */
    zassert_equal(hpi_hs_seg_of(N + 1), 1, "");
    zassert_equal(hpi_hs_idx_of(N + 1), 0, "");
    zassert_equal(hpi_hs_off_of(N + 1), 0, "");

    /* last record of segment 1 */
    zassert_equal(hpi_hs_seg_of(2 * N), 1, "");
    zassert_equal(hpi_hs_idx_of(2 * N), N - 1, "");
}

/* seg -> first seq must invert seq -> seg exactly. */
ZTEST(hs_layout, test_seg_first_seq_round_trips)
{
    for (uint32_t seg = 0; seg < 200; seg++) {
        uint32_t first = hpi_hs_seg_first_seq(seg);
        zassert_equal(hpi_hs_seg_of(first), seg, "seg %u first-seq round trip", seg);
        zassert_equal(hpi_hs_idx_of(first), 0, "a segment's first seq is slot 0");
        if (seg > 0) {
            /* the seq just before it belongs to the previous segment */
            zassert_equal(hpi_hs_seg_of(first - 1), seg - 1, "");
        }
    }
}

/* Exhaustive over the first few segments: every seq maps into its own segment, and
 * the slot advances by exactly one each time. */
ZTEST(hs_layout, test_mapping_is_contiguous_and_monotonic)
{
    for (uint32_t seq = 1; seq <= 4 * N; seq++) {
        uint32_t seg = hpi_hs_seg_of(seq);
        uint32_t idx = hpi_hs_idx_of(seq);

        zassert_true(idx < N, "slot %u out of range for seq %u", idx, seq);
        zassert_equal(hpi_hs_seg_first_seq(seg) + idx, seq,
                      "seq %u must reconstruct from (seg %u, idx %u)", seq, seg, idx);
    }
}

/* hpi_hs_seg_room() drives the batch/page splitting on both the write and the read
 * side. If it is wrong, a flush writes past the end of a segment (corrupting the
 * next one) or a read returns short. */
ZTEST(hs_layout, test_seg_room)
{
    zassert_equal(hpi_hs_seg_room(1), N, "a fresh segment has room for all N");
    zassert_equal(hpi_hs_seg_room(N), 1, "the last slot has room for exactly 1");
    zassert_equal(hpi_hs_seg_room(N + 1), N, "the next segment is fresh again");
    zassert_equal(hpi_hs_seg_room(N / 2 + 1), N / 2, "");
}

/* Simulate the flush-side split: a 256-record batch straddling a boundary must be
 * cut so that no write ever crosses a segment, and every record lands exactly once
 * at its arithmetically-derived home. */
ZTEST(hs_layout, test_batch_straddling_a_boundary_splits_correctly)
{
    const uint32_t batch_n = 256;
    /* start 100 records before the end of segment 0 -> the batch must split 100/156 */
    const uint32_t seq0 = N - 100 + 1;

    uint32_t done = 0;
    uint32_t pieces = 0;
    uint32_t seen[2] = {0, 0};

    while (done < batch_n) {
        uint32_t seq   = seq0 + done;
        uint32_t seg   = hpi_hs_seg_of(seq);
        uint32_t room  = hpi_hs_seg_room(seq);
        uint32_t chunk = MIN(room, batch_n - done);

        zassert_true(pieces < 2, "expected exactly 2 pieces");
        zassert_equal(seg, pieces, "pieces must land in consecutive segments");
        seen[pieces] = chunk;

        /* the piece must not cross the segment end */
        zassert_true(hpi_hs_idx_of(seq) + chunk <= N, "write would cross a segment");

        done += chunk;
        pieces++;
    }

    zassert_equal(pieces, 2, "batch should split into exactly 2 pieces");
    zassert_equal(seen[0], 100, "first piece fills the rest of segment 0");
    zassert_equal(seen[1], 156, "second piece starts segment 1");
    zassert_equal(seen[0] + seen[1], batch_n, "every record accounted for exactly once");
}

/* A batch that exactly fills a segment must NOT emit an empty second piece. */
ZTEST(hs_layout, test_batch_exactly_filling_a_segment)
{
    uint32_t seq   = 1;
    uint32_t room  = hpi_hs_seg_room(seq);
    uint32_t chunk = MIN(room, N);

    zassert_equal(chunk, N, "");
    zassert_equal(hpi_hs_idx_of(seq) + chunk, N, "fills the segment exactly");
    /* the next seq starts a fresh segment */
    zassert_equal(hpi_hs_seg_of(seq + chunk), 1, "");
    zassert_equal(hpi_hs_idx_of(seq + chunk), 0, "");
}

/* The migration rounds seq UP to a segment boundary so the new era starts at slot 0 of
 * a fresh file with no partially-written hole ahead of it.
 *
 * Assert the INVARIANTS, not the constants: this test used to hardcode the 448-record
 * numbers (35088 -> 35392, segment 79) and duly failed the moment P6 grew the segment
 * to 4480. The properties below are what actually has to hold for ANY segment size --
 * and `seq must never move backwards` is the one that protects the app's
 * (device, seq) dedup from colliding with rows it already stored. */
ZTEST(hs_layout, test_migration_rounds_up_to_a_clean_boundary)
{
    const uint32_t cases[] = {1, 2, N - 1, N, N + 1, 35088, 100000, 573439};

    for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
        uint32_t old_seq = cases[i];
        uint32_t aligned = ((old_seq + N - 1u) / N) * N;

        zassert_true(aligned >= old_seq,
                     "seq must NEVER move backwards (%u -> %u)", old_seq, aligned);
        zassert_equal(aligned % N, 0, "the new era must start ON a segment boundary");
        zassert_true(aligned - old_seq < N, "must not skip a whole extra segment");

        /* the next sample written lands at slot 0 of a fresh segment — no hole */
        uint32_t next = aligned + 1;
        zassert_equal(hpi_hs_idx_of(next), 0, "the new era must start at slot 0");
        zassert_equal(hpi_hs_off_of(next), 0, "");
        zassert_equal(hpi_hs_seg_of(next), aligned / N, "");
    }
}

/* An already-aligned seq must not be pushed forward a whole extra segment. */
ZTEST(hs_layout, test_migration_is_idempotent_on_a_boundary)
{
    uint32_t old_seq = 3 * N;   /* already exactly on a boundary */
    uint32_t aligned = ((old_seq + N - 1u) / N) * N;
    zassert_equal(aligned, old_seq, "an aligned seq must not skip a segment");
}

/* ------------------------------------------------------------------------- *
 * HRV-derived stress (P3 follow-on). Pure arithmetic, so it lives here.
 *
 * The dangerous failure is not a wrong number -- it is a number AT ALL when there is
 * no baseline. Stress is meaningless in absolute terms; only deviation from the user's
 * own baseline means anything, and a premature score is one the user would believe.
 * ------------------------------------------------------------------------- */

#define OK_WINS  HPI_HS_STRESS_MIN_BASELINE_WINDOWS

/* At your own baseline you are, by definition, at your own normal: the midpoint. */
ZTEST(hs_layout, test_stress_at_baseline_is_neutral)
{
    zassert_equal(hpi_hs_stress_from_hrv(400, 400, OK_WINS), 50,
                  "RMSSD == baseline must score 50 (neutral)");
}

/* HRV suppressed -> stress up. HRV above baseline -> stress down. Getting this
 * BACKWARDS is the classic bug, and it would still produce a plausible-looking chart. */
ZTEST(hs_layout, test_stress_direction_is_not_inverted)
{
    int32_t low  = hpi_hs_stress_from_hrv(200, 400, OK_WINS);   /* HRV halved      */
    int32_t norm = hpi_hs_stress_from_hrv(400, 400, OK_WINS);
    int32_t high = hpi_hs_stress_from_hrv(600, 400, OK_WINS);   /* HRV 1.5x        */

    zassert_true(low > norm, "SUPPRESSED HRV must mean HIGHER stress (got %d vs %d)",
                 low, norm);
    zassert_true(high < norm, "ELEVATED HRV must mean LOWER stress (got %d vs %d)",
                 high, norm);
    zassert_equal(low, 100, "RMSSD at half baseline -> 100");
    zassert_equal(high, 0, "RMSSD at 1.5x baseline -> 0");
}

ZTEST(hs_layout, test_stress_is_clamped)
{
    zassert_equal(hpi_hs_stress_from_hrv(10, 400, OK_WINS), 100, "must clamp at 100");
    zassert_equal(hpi_hs_stress_from_hrv(4000, 400, OK_WINS), 0, "must clamp at 0");
}

/* THE test. Without enough baseline windows there is NO score -- and -1 must never be
 * mistaken for "zero stress", which is why the API returns a sentinel rather than 0. */
ZTEST(hs_layout, test_no_score_without_a_baseline)
{
    zassert_equal(hpi_hs_stress_from_hrv(400, 400, OK_WINS - 1), -1,
                  "a baseline of fewer than %d windows is a guess, not a baseline: "
                  "report NOTHING rather than a number the user would believe",
                  OK_WINS);
    zassert_equal(hpi_hs_stress_from_hrv(400, 400, 0), -1, "no baseline at all -> no score");

    /* and the sentinel is NOT zero -- zero is a real, valid, very-relaxed score */
    zassert_not_equal(hpi_hs_stress_from_hrv(400, 400, 0), 0,
                      "'no data' must be distinguishable from 'completely relaxed'");
}

ZTEST(hs_layout, test_garbage_inputs_produce_no_score)
{
    zassert_equal(hpi_hs_stress_from_hrv(0, 400, OK_WINS), -1, "zero RMSSD -> no score");
    zassert_equal(hpi_hs_stress_from_hrv(400, 0, OK_WINS), -1, "zero baseline -> no score "
                                                               "(and no divide-by-zero)");
    zassert_equal(hpi_hs_stress_from_hrv(-5, 400, OK_WINS), -1, "negative RMSSD -> no score");
}
