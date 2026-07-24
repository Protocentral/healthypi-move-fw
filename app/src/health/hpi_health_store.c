/*
 * HealthyPi Move — Health Store (H1: ingest + RAM ring)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * The single owner of health-data ingest. H1 stands up the ingest path and a
 * bounded RAM ring of typed samples: every metric flows in through ONE call
 * (hpi_hs_record), fed by a single set of zbus listeners here (replacing the
 * old scattered trend_*_lis / last-value setters). Query-time stats + derived
 * metrics + a durable log + MCUmgr sync come in H2/H3/H4 — the stats/read hooks
 * below are functional over the RAM ring for now.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/fs/fs.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include "health/hpi_health_store.h"
#include "health/hpi_hs_record.h"
#include "health/hpi_hs_epoch.h"
#include "health/hpi_hs_hrv.h"
#include "health/hpi_hs_stress.h"
#include "health/hpi_hs_readiness.h"
#include "health/hpi_hs_layout.h"
#include "hpi_common_types.h"
#include "hpi_dfu.h"     /* hpi_dfu_is_active() — /lfs shares the OTA's QSPI die */
#include "hpi_sys.h"
#include "hw_module.h"   /* hpi_hw_get_last_motion_s() for the LOW_MOTION quality tag */

LOG_MODULE_REGISTER(hpi_health_store, LOG_LEVEL_INF);

/* RAM ring: recent samples for on-device query + sync staging. Bounded; oldest
 * overwritten. The durable append-log (below) is the full history behind it. */
#define HS_RING_N 512

static struct hpi_hs_sample s_ring[HS_RING_N];
static uint32_t s_head;      /* next write index          */
static uint32_t s_count;     /* valid entries (<= HS_RING_N) */
static uint32_t s_seq;       /* monotonic sequence         */
static struct k_mutex s_lock;

/* ---- Durable storage (H3) ------------------------------------------------
 * Samples flow ring -> segment files under /lfs/hs. The seq cursor + current
 * segment index persist in /lfs/hs/meta so seq stays monotonic across reboots
 * (the sync cursor must never rewind). A latest-per-type snapshot (/lfs/hs/lat)
 * is restored at boot so the UI can show last-known values immediately. */
#define HS_DIR            "/lfs/hs"
#define HS_META           HS_DIR "/meta"
#define HS_LATEST         HS_DIR "/lat"
/* HS-2 P2: segments hold a FIXED record count, so seq -> (segment, byte offset)
 * is pure arithmetic and a read needs no index, no scan and no search:
 *
 *     seg = (seq - 1) / HS_SEG_RECORDS
 *     off = ((seq - 1) % HS_SEG_RECORDS) * HPI_HS_SAMPLE_WIRE_SIZE
 *
 * Previously segments rolled at ">= 8192 B after a variable-size batch", so they
 * were variable-length and nothing could locate a seq without reading every record
 * from the oldest segment forward -- O(since) per page, O(n^2) per sync
 * (~277 MB of flash reads and ~34,000 file opens to move 630 KB). */
#define HS_SEG_RECORDS    HPI_HS_SEG_RECORDS   /* see health/hpi_hs_layout.h */
/* Retention: 128 x 4480 = ~573,000 records (~10.3 MB of the 112 MB volume).
 *
 * (The comment here used to read "~57k samples" -- correct when a segment held 448
 * records, and left stale when P6 grew it 10x. It was off by an order of magnitude in
 * the reassuring direction, which is the worst way for a capacity note to be wrong.)
 *
 * At the post-P1 rate of ~5,500 records/day that is ~104 days; P3's continuous HRV adds
 * ~1,000/day (4 types x 288 five-minute windows, minus the ones the coverage gate
 * rejects), taking it to ~6,500/day and ~88 days. Still vastly past the 7-day window
 * anything actually reads, and nowhere near the ~1.2 days that made a missed sync
 * permanent data loss before P1. */
#define HS_MAX_SEGS       128
#define HS_FLUSH_MS       10000         /* durable flush cadence          */
#define HS_LATEST_N       HPI_HS_T__COUNT_HINT   /* index by type id (small, sparse) */

/* Bumped when the on-flash segment LAYOUT changes (not the wire schema, which is
 * HPI_HS_SCHEMA_VERSION and is the app's contract). Layout 1 = variable-length
 * segments; 2 = fixed 448-record segments; 3 = fixed 4480-record segments (P6
 * retention). A mismatch triggers hs_migrate_layout(), which discards the log and
 * restarts at a clean boundary WITHOUT rewinding seq. */
#define HS_LAYOUT_VER     3

struct hs_meta {
    uint16_t schema_ver;
    uint16_t layout_ver;  /* was _rsv; 0 in the pre-P2 format (see restore)   */
    uint32_t seq;         /* last-assigned seq at persist time */
    uint32_t seg_index;   /* current (open) segment index      */
    uint32_t base_seq;    /* lowest seq that can exist in a segment file      */
};

static struct hpi_hs_sample s_latest[HS_LATEST_N];
static bool     s_have_latest[HS_LATEST_N];
static bool     s_latest_dirty;
static uint32_t s_seg_index;      /* current segment file index      */
static uint32_t s_flushed_seq;    /* highest seq written to a segment */
/* Highest seq the client says it has durably stored (HPI_HS ACK). Advisory and
 * RAM-only: nothing drops data on it yet -- see hpi_hs_ack(). Deliberately not
 * persisted; a stale value would be worse than none. */
static uint32_t s_acked_seq;
static uint32_t s_base_seq;       /* lowest seq present in any segment (layout migration) */
static bool     s_storage_ready;  /* FS mounted + log opened          */

/* HS-2 P6: per-segment ts range, so a scan can SKIP whole segments that fall outside
 * its window without opening them.
 *
 * Without this, growing retention would undo P5: the summary's single pass reads the
 * WHOLE log, so a ~104-day window means ~10 MB every 5 minutes instead of ~0.7 MB.
 * The summary only ever needs the last 7 days.
 *
 * A rolled segment is immutable, so its range is cached forever after one read. The
 * OPEN segment is never cached (it is still growing). Indexed seg % HS_MAX_SEGS; the
 * stored `seg` disambiguates the wrap. */
struct hs_seg_ts { uint32_t seg; bool valid; int64_t first, last; };
static struct hs_seg_ts s_seg_ts[HS_MAX_SEGS];

/* Derived-metrics cache (H2): recomputed periodically by the store thread from
 * the durable log; hpi_hs_summary() returns a snapshot (no I/O on the caller). */
static struct hpi_hs_summary s_summary;
static struct k_mutex s_scan_lock;   /* serializes durable-log reads */

/* forward decls (definitions live with the durable-log code below) */
static void hs_seg_path(char *buf, size_t n, uint32_t idx);
typedef void (*hs_scan_cb)(const struct hpi_hs_sample *s, void *ud);
static void hs_durable_scan(uint8_t type, int64_t from, int64_t to,
                            uint8_t q_require, hs_scan_cb cb, void *ud);
static bool hs_seg_ts_get(uint32_t seg, int64_t *first, int64_t *last);
#if defined(CONFIG_HPI_HS_SYNTH)
static void hs_migrate_layout(uint16_t from_ver);
#endif

/* ---- P2: seq <-> position. Pure arithmetic; no index, no search.
 * The mapping itself lives in health/hpi_hs_layout.h so it can be unit-tested
 * without dragging in the filesystem and zbus. */
#define hs_seq_seg(seq)  hpi_hs_seg_of(seq)
#define hs_seq_idx(seq)  hpi_hs_idx_of(seq)

/* Lowest seq still retrievable from a segment file: bounded by BOTH the retention
 * window and the layout-migration base (segments below base_seq never existed in
 * this layout era). */
static uint32_t hs_seg_oldest_seq(void)
{
    uint32_t first_seg = (s_seg_index >= HS_MAX_SEGS) ? (s_seg_index - HS_MAX_SEGS + 1u) : 0u;
    uint32_t oldest = first_seg * HS_SEG_RECORDS + 1u;
    return (s_base_seq > oldest) ? s_base_seq : oldest;
}

int hpi_hs_init(void)
{
    /* Early: ring + lock only. FS isn't mounted yet — hpi_hs_storage_init()
     * does the durable-log open/restore after the mount. */
    k_mutex_init(&s_lock);
    k_mutex_init(&s_scan_lock);
    s_head = s_count = s_seq = 0;
    s_flushed_seq = 0;
    s_seg_index = 0;
    s_base_seq = 1;
    s_storage_ready = false;
    LOG_INF("health store ingest ready (ring=%d)", HS_RING_N);
    return 0;
}

void hpi_hs_record(uint8_t type, int32_t value, uint8_t quality, int64_t ts_utc)
{
    /* Gate: never store a sample without a valid timestamp (health store is
     * UTC-native; pre-RTC-sync data has no meaning). */
    if (!(quality & HPI_HS_Q_VALID)) {
        return;
    }

    k_mutex_lock(&s_lock, K_FOREVER);
    struct hpi_hs_sample *s = &s_ring[s_head];
    s->seq = ++s_seq;
    s->ts_utc = ts_utc;
    s->type = type;
    s->quality = quality;
    s->value = value;
    s_head = (s_head + 1) % HS_RING_N;
    if (s_count < HS_RING_N) {
        s_count++;
    }
    if (type < HS_LATEST_N) {
        s_latest[type] = *s;
        s_have_latest[type] = true;
        s_latest_dirty = true;
    }
    k_mutex_unlock(&s_lock);
}

bool hpi_hs_get_latest(uint8_t type, struct hpi_hs_sample *out)
{
    if (out == NULL || type >= HS_LATEST_N) {
        return false;
    }
    k_mutex_lock(&s_lock, K_FOREVER);
    bool have = s_have_latest[type];
    if (have) {
        *out = s_latest[type];
    }
    k_mutex_unlock(&s_lock);
    return have;
}

/* Iterate the ring oldest-first, calling cb for each sample. Caller holds lock. */
static void ring_foreach(void (*cb)(const struct hpi_hs_sample *, void *), void *ud)
{
    uint32_t idx = (s_head + HS_RING_N - s_count) % HS_RING_N;
    for (uint32_t i = 0; i < s_count; i++) {
        cb(&s_ring[idx], ud);
        idx = (idx + 1) % HS_RING_N;
    }
}

/* stats accumulator — hs_durable_scan already filtered by type/window/quality */
static void stats_cb(const struct hpi_hs_sample *s, void *ud)
{
    struct hpi_hs_stats *o = ud;
    if (o->count == 0) {
        o->min = o->max = s->value;
        o->first_ts = s->ts_utc;
    } else {
        if (s->value < o->min) o->min = s->value;
        if (s->value > o->max) o->max = s->value;
    }
    o->sum += s->value;
    o->last_ts = s->ts_utc;
    o->count++;
}

int hpi_hs_stats(uint8_t type, int64_t from, int64_t to,
                 uint8_t q_require, struct hpi_hs_stats *out)
{
    if (out == NULL) {
        return -EINVAL;
    }
    memset(out, 0, sizeof(*out));
    hs_durable_scan(type, from, to, (uint8_t)(q_require | HPI_HS_Q_VALID), stats_cb, out);
    if (out->count > 0) {
        out->mean = (int32_t)(out->sum / (int64_t)out->count);
    }
    /* percentiles: a sorted/histogram pass — resting HR uses a dedicated
     * histogram (hs_resting_hr); general percentiles are future work. */
    return 0;
}

static void series_cb(const struct hpi_hs_sample *s, void *ud)
{
    hpi_hs_series_feed((struct hpi_hs_series_acc *)ud, s->ts_utc, s->value);
}

/* ---- P3: cached trend series for the tiles ------------------------------
 * Sparklines need a windowed pass over the durable log (file I/O) — never on
 * the display thread. The store thread recomputes on the summary cadence
 * (~5 min); the UI reads a snapshot via hpi_hs_trend_get().
 *
 * Registry (not dual statics): adding SpO2/HRV is a table row. Each slot is
 * ~52 B of mean[] + valid bits (app-core RAM is tight). */
struct hs_trend_slot {
	uint8_t type;
	uint8_t n_buckets;   /* <= HPI_HS_TREND_N */
	int64_t window_s;    /* now - window_s .. now */
	struct hpi_hs_trend cache;
};

#define HS_HOUR  3600
#define HS_DAY   (24 * HS_HOUR)

/* Keep order stable for readers grepping the source. */
static struct hs_trend_slot s_trends[] = {
	/* Circadian HR curve vs resting/min/max chips (not "7 nights"). */
	{ .type = HPI_HS_T_HR,        .n_buckets = HPI_HS_TREND_N, .window_s = HS_DAY },
	/* Design: LAST 7 NIGHTS skin temp. */
	{ .type = HPI_HS_T_SKIN_TEMP, .n_buckets = 7,              .window_s = 7 * HS_DAY },
	/* SpO2 tile 7-night spark (spot-dense → sparse buckets OK). */
	{ .type = HPI_HS_T_SPO2,      .n_buckets = 7,              .window_s = 7 * HS_DAY },
	/* Stress history bars: last ten 5-min HRV windows (~50 min). */
	{ .type = HPI_HS_T_HRV_RMSSD, .n_buckets = 10,             .window_s = 10 * 300 },
};

#define HS_TREND_SLOT_N  (sizeof(s_trends) / sizeof(s_trends[0]))

static void hs_fill_trend(struct hpi_hs_trend *out, uint8_t type,
			  int64_t from, int64_t to, uint8_t nb)
{
	static struct hpi_hs_bucket bk[HPI_HS_TREND_N];

	struct hpi_hs_trend t;
	memset(&t, 0, sizeof(t));
	if (nb == 0 || nb > HPI_HS_TREND_N) {
		nb = HPI_HS_TREND_N;
	}
	t.n = nb;
	t.from = from;
	t.to = to;

	uint16_t got = 0;
	if (hpi_hs_series(type, from, to, bk, nb, &got) == 0) {
		for (uint16_t i = 0; i < got && i < HPI_HS_TREND_N; i++) {
			if (bk[i].count == 0) {
				continue;   /* leave the valid bit clear: a gap, not a zero */
			}
			int32_t m = bk[i].mean;
			if (m > INT16_MAX) {
				m = INT16_MAX;
			} else if (m < INT16_MIN) {
				m = INT16_MIN;
			}
			t.mean[i] = (int16_t)m;
			t.valid |= (1u << i);
		}
	}

	k_mutex_lock(&s_lock, K_FOREVER);
	*out = t;
	k_mutex_unlock(&s_lock);
}

static void hs_recompute_trends(void)
{
	/*
	 * Amortize: recompute ONE registered type per summary cycle (~5 min),
	 * round-robin. Running all four hpi_hs_series() scans back-to-back after
	 * hs_recompute_summary() blew the store thread's 4 KB stack (same class of
	 * fault that previously presented as a boot loop). Full rotation still
	 * finishes in ~20 min; UI gaps until first fill are expected.
	 */
	static size_t s_rr;

	int64_t now = hw_get_sys_time_ts();
	if (now <= 0 || !hpi_sys_is_time_valid() || HS_TREND_SLOT_N == 0) {
		return;
	}

	size_t i = s_rr % HS_TREND_SLOT_N;
	s_rr++;

	struct hs_trend_slot *s = &s_trends[i];
	int64_t from = now - s->window_s;
	if (from >= now) {
		return;
	}
	hs_fill_trend(&s->cache, s->type, from, now, s->n_buckets);
}

int hpi_hs_trend_get(uint8_t type, struct hpi_hs_trend *out)
{
	if (out == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&s_lock, K_FOREVER);
	for (size_t i = 0; i < HS_TREND_SLOT_N; i++) {
		if (s_trends[i].type == type) {
			*out = s_trends[i].cache;
			k_mutex_unlock(&s_lock);
			return 0;
		}
	}
	k_mutex_unlock(&s_lock);
	return -ENOENT;
}

int hpi_hs_series(uint8_t type, int64_t from, int64_t to,
                  struct hpi_hs_bucket *buckets, uint16_t max_buckets, uint16_t *out_n)
{
    /* Scratch for the sums (a bucket's mean is Σ/n and hpi_hs_bucket has no sum
     * field). Static, not stack: this runs on the store thread, whose 4 KB is
     * already sized around hs_recompute_summary's histogram — a heavy stack job
     * there once presented as a *boot loop*, not a crash. Safe because
     * hs_durable_scan holds s_scan_lock across the whole pass. */
    static int64_t s_series_sum[HPI_HS_SERIES_MAX_BUCKETS];

    if (out_n) {
        *out_n = 0;
    }
    if (out_n == NULL) {
        return -EINVAL;
    }

    const struct hpi_hs_type_info *ti = hpi_hs_type_lookup(type);
    if (ti == NULL) {
        return -ENOENT;
    }

    struct hpi_hs_series_acc a;
    if (!hpi_hs_series_begin(&a, buckets, s_series_sum, max_buckets, from, to,
                             ti->data_class == HPI_HS_CLASS_CUMULATIVE)) {
        return -EINVAL;
    }

    hs_durable_scan(type, from, to, HPI_HS_Q_VALID, series_cb, &a);
    hpi_hs_series_end(&a);

    *out_n = max_buckets;
    return 0;
}

int hpi_hs_summary(struct hpi_hs_summary *out)
{
    /* Return the cached derived metrics (recomputed periodically by the store
     * thread — see hs_recompute_summary). No file I/O on the caller. */
    if (out == NULL) {
        return -EINVAL;
    }
    k_mutex_lock(&s_lock, K_FOREVER);
    *out = s_summary;
    k_mutex_unlock(&s_lock);
    return 0;
}

uint32_t hpi_hs_head_seq(void)
{
    return s_seq;
}

uint32_t hpi_hs_oldest_seq(void)
{
    k_mutex_lock(&s_lock, K_FOREVER);
    uint32_t head = s_seq;
    uint32_t ring_oldest = (s_count > 0) ? (s_seq - s_count + 1u) : (head + 1u);
    k_mutex_unlock(&s_lock);

    /* P2: derivable, so no longer needs to open the oldest segment to read its
     * first record. Nothing durable yet -> whatever the ring holds. */
    if (s_flushed_seq == 0) {
        return ring_oldest;
    }
    return hs_seg_oldest_seq();
}

struct read_acc {
    uint32_t since;
    uint8_t *buf;
    uint16_t max, n;
    uint32_t next;
    size_t cap;
};

static void read_cb(const struct hpi_hs_sample *s, void *ud)
{
    struct read_acc *a = ud;
    if (s->seq <= a->since || a->n >= a->max) {
        return;
    }
    if ((size_t)(a->n + 1) * HPI_HS_SAMPLE_WIRE_SIZE > a->cap) {
        return;
    }
    memcpy(a->buf + (size_t)a->n * HPI_HS_SAMPLE_WIRE_SIZE, s, HPI_HS_SAMPLE_WIRE_SIZE);
    a->n++;
    a->next = s->seq;
}

int hpi_hs_read_since(uint32_t since_seq, uint8_t *buf, size_t buf_sz,
                      uint16_t max, uint16_t *out_n, uint32_t *next_seq, bool *more)
{
    if (buf == NULL) {
        return -EINVAL;
    }
    uint16_t cap = (uint16_t)MIN((size_t)max, buf_sz / HPI_HS_SAMPLE_WIRE_SIZE);
    struct read_acc a = {.since = since_seq, .buf = buf, .max = cap,
                         .next = since_seq, .cap = buf_sz};

    k_mutex_lock(&s_lock, K_FOREVER);
    uint32_t head = s_seq;
    uint32_t oldest_ring = (s_count > 0) ? (s_seq - s_count + 1) : (head + 1);

    /* Nothing newer than the client's cursor: no work, no flash I/O. */
    bool caught_up = (since_seq >= head);

    /* The requested range lies entirely inside the RAM ring (the common
     * incremental-sync case): serve it hot, no flash I/O.
     *
     * `s_count > 0` is load-bearing. An empty ring does NOT mean an empty store:
     * the ring is volatile and zeroed at init while s_seq is restored from meta,
     * so after every reboot s_count == 0 with s_seq == head. Treating that as
     * "ring only" short-circuited to an empty ring and skipped the segment scan
     * below, making the entire flash-resident history unreachable. */
    bool in_ring = (s_count > 0) && ((uint64_t)since_seq + 1u >= oldest_ring);

    bool ring_only = caught_up || in_ring;
    if (ring_only && !caught_up) {
        ring_foreach(read_cb, &a);   /* oldest-first => ascending seq */
    }
    k_mutex_unlock(&s_lock);

    if (!ring_only) {
        /* Catch-up: the client is behind the RAM ring, so serve from the durable
         * segments.
         *
         * P2: seq -> (segment, byte offset) is ARITHMETIC (hs_seq_seg/hs_seq_idx),
         * so this is a seek, not a search. Previously this opened the oldest segment
         * and read EVERY record forward, discarding anything <= since_seq: O(since)
         * per page and O(n^2) per sync -- ~277 MB of reads and ~34,000 file opens to
         * move 630 KB, with a page at since=31,000 opening 69 files to return 720 B.
         * Now: one open + one seek + one read per segment touched. */
        k_mutex_lock(&s_scan_lock, K_FOREVER);

        uint32_t start  = since_seq + 1u;
        uint32_t oldest = hs_seg_oldest_seq();
        if (start < oldest) {
            /* Cursor is older than what retention still holds -- those samples are
             * gone. Resume at the oldest surviving one rather than looping. */
            start = oldest;
        }

        while (a.n < cap && start <= s_flushed_seq) {
            uint32_t seg   = hs_seq_seg(start);
            uint32_t idx   = hs_seq_idx(start);
            uint32_t room  = HS_SEG_RECORDS - idx;                  /* left in this file */
            uint32_t avail = s_flushed_seq - start + 1u;            /* left in the log   */
            uint32_t want  = MIN(room, MIN(avail, (uint32_t)(cap - a.n)));

            char path[40];
            hs_seg_path(path, sizeof(path), seg);
            struct fs_file_t f;
            fs_file_t_init(&f);
            if (fs_open(&f, path, FS_O_READ) != 0) {
                break;   /* aged out mid-read: stop, the client resumes from a.next */
            }
            if (fs_seek(&f, (off_t)idx * HPI_HS_SAMPLE_WIRE_SIZE, FS_SEEK_SET) != 0) {
                fs_close(&f);
                break;
            }
            int rd = fs_read(&f, a.buf + (size_t)a.n * HPI_HS_SAMPLE_WIRE_SIZE,
                             (size_t)want * HPI_HS_SAMPLE_WIRE_SIZE);
            fs_close(&f);
            if (rd <= 0) {
                break;
            }

            uint32_t got = (uint32_t)rd / HPI_HS_SAMPLE_WIRE_SIZE;   /* whole records only */
            if (got == 0) {
                break;
            }
            a.n   += (uint16_t)got;
            start += got;

            /* a.next must be the seq of the LAST record actually copied. Read it back
             * out of the buffer rather than computing it, so a short read can't
             * advance the client's cursor past data it never received. */
            memcpy(&a.next, a.buf + (size_t)(a.n - 1) * HPI_HS_SAMPLE_WIRE_SIZE,
                   sizeof(a.next));
        }

        k_mutex_unlock(&s_scan_lock);
    }

    if (out_n)    *out_n = a.n;
    if (next_seq) *next_seq = a.next;
    /* `more` must mean "another page is worth fetching", not just "the cursor is
     * below head". With n == 0 nothing advanced, so a client looping on `more`
     * alone would spin forever re-requesting the same cursor. Gate it on n > 0
     * (hs_flush breaks on n == 0 of its own accord, so its loop is unaffected). */
    if (more)     *more = (a.n > 0) && (a.next < head);
    return 0;
}

void hpi_hs_ack(uint32_t acked_seq)
{
    /* H4 intent was: drop durable-log segments fully <= acked_seq. NOT done, and
     * deliberately so -- but the header used to claim it in the present tense, so
     * this is the honest version.
     *
     * hs_retention() already bounds the log by SIZE, and after HS-2 P1 (epoch
     * aggregation) + P6 (bigger segments) that holds comfortably more than a week,
     * which was the whole point of P1. So acked-based dropping is an optimisation,
     * not a requirement, and deleting on a client's word is unrecoverable if the
     * client is wrong. That needs device validation before it lands, not a
     * speculative implementation.
     *
     * The wire contract permits this: docs/HPI_HS_API.md calls ACK "Optional but
     * recommended" and says the device "MAY drop retained raw <= that". The `rc:0`
     * in hs_h_ack() acknowledges RECEIPT, not deletion -- and a client learns what
     * is actually retrievable from HELLO's `oldest`, never from the ack. This is
     * NOT the shape of the H4 SYNC-returns-0 bug, where the client was told "here
     * is all your data" and handed nothing.
     *
     * Latch it so the value is observable and a future retention pass has it. */
    k_mutex_lock(&s_lock, K_FOREVER);
    if (acked_seq > s_acked_seq) {
        s_acked_seq = acked_seq;   /* only ever forward, like seq itself */
    }
    k_mutex_unlock(&s_lock);
    LOG_DBG("ACK seq=%u (retention is size-based; ack does not drop segments yet)",
            (unsigned)acked_seq);
}

/* ---- Durable log: segment files + meta/latest persistence (H3) ---------- */

static void hs_seg_path(char *buf, size_t n, uint32_t idx)
{
    snprintf(buf, n, HS_DIR "/s%06u", (unsigned)idx);
}

/* HS-2 P5: overwrite a FIXED-SIZE file IN PLACE — open, seek 0, write.
 *
 * The old path did unlink + create + write on every call, and `meta` is written on
 * every flush that wrote (~8,640 times/day). Three LittleFS metadata transactions
 * where one data write would do — pure wear and QSPI power, burned continuously
 * whether or not anyone ever opens the app.
 *
 * Safe ONLY because the payload is a fixed-size struct: LittleFS has no
 * truncate-on-open, so a shorter payload would leave stale trailing bytes. Use
 * hs_overwrite() for anything variable-length. */
static int hs_overwrite_fixed(const char *path, const void *buf, size_t len)
{
    struct fs_file_t f;
    fs_file_t_init(&f);
    int rc = fs_open(&f, path, FS_O_CREATE | FS_O_RDWR);
    if (rc != 0) {
        return rc;
    }
    rc = fs_seek(&f, 0, FS_SEEK_SET);
    if (rc == 0) {
        rc = fs_write(&f, buf, len);
    }
    fs_close(&f);
    return (rc < 0) ? rc : 0;
}

/* Overwrite a VARIABLE-length file (unlink + create; LittleFS has no
 * truncate-on-open, so a shorter payload must not leave stale bytes behind). */
static int hs_overwrite(const char *path, const void *buf, size_t len)
{
    struct fs_file_t f;
    fs_file_t_init(&f);
    /* Only unlink if it exists — fs_unlink on a missing path returns -ENOENT
     * and the fs subsystem logs it as an error (noisy on first boot). */
    struct fs_dirent ent;
    if (fs_stat(path, &ent) == 0) {
        fs_unlink(path);
    }
    int rc = fs_open(&f, path, FS_O_CREATE | FS_O_WRITE);
    if (rc != 0) {
        return rc;
    }
    rc = fs_write(&f, buf, len);
    fs_close(&f);
    return (rc < 0) ? rc : 0;
}

static void hs_persist_meta(void)
{
    static struct hs_meta s_last;   /* store-thread only */
    struct hs_meta m = {
        .schema_ver = HPI_HS_SCHEMA_VERSION,
        .layout_ver = HS_LAYOUT_VER,
        .seq = s_seq,
        .seg_index = s_seg_index,
        .base_seq = s_base_seq,
    };

    /* Nothing moved -> nothing to write. Kills the idle churn outright. */
    if (memcmp(&m, &s_last, sizeof(m)) == 0) {
        return;
    }

    /* NOTE: this must stay on EVERY flush that wrote. `seq` is the sync cursor, and
     * SYNC can serve samples straight from the RAM ring -- so if the persisted seq
     * lagged, a power cut could rewind it below a seq the app had already stored, and
     * new samples would reuse it. That is a (device, seq) collision in the app's
     * database. The fix for the cost is to make the write CHEAP (in place), not RARE. */
    if (hs_overwrite_fixed(HS_META, &m, sizeof(m)) == 0) {
        s_last = m;
    }
}

static void hs_persist_latest(void)
{
    /* Pack the present latest-per-type samples back-to-back. */
    static uint8_t buf[HS_LATEST_N * HPI_HS_SAMPLE_WIRE_SIZE];
    size_t off = 0;
    k_mutex_lock(&s_lock, K_FOREVER);
    for (int t = 0; t < HS_LATEST_N; t++) {
        if (s_have_latest[t]) {
            memcpy(buf + off, &s_latest[t], HPI_HS_SAMPLE_WIRE_SIZE);
            off += HPI_HS_SAMPLE_WIRE_SIZE;
        }
    }
    k_mutex_unlock(&s_lock);
    if (off > 0) {
        (void)hs_overwrite(HS_LATEST, buf, off);
    }
}

/* Drop the segment that just aged out of the retention window. */
static void hs_retention(void)
{
    if (s_seg_index >= HS_MAX_SEGS) {
        char path[40];
        hs_seg_path(path, sizeof(path), s_seg_index - HS_MAX_SEGS);
        fs_unlink(path);   /* ok if already gone */
    }
}


/* Append all not-yet-flushed ring samples to the current segment; roll when it
 * gets big. Runs only in the store thread (never holds s_lock over file I/O). */
static void hs_flush(void)
{
    static uint8_t batch[256 * HPI_HS_SAMPLE_WIRE_SIZE];
    uint16_t n = 0;
    uint32_t next = s_flushed_seq;
    bool more = false;
    bool wrote = false;

    do {
        hpi_hs_read_since(next, batch, sizeof(batch), 256, &n, &next, &more);
        if (n == 0) {
            break;
        }

        /* The batch is a contiguous ascending seq run (hpi_hs_record assigns
         * ++s_seq per sample, so there are no gaps). Read the first seq straight
         * out of the record rather than assuming next+1. */
        uint32_t seq0;
        memcpy(&seq0, batch, sizeof(seq0));

        /* P2: a batch can straddle a segment boundary. Split it and write each
         * piece at its arithmetically-determined offset, so every segment ends up
         * holding exactly HS_SEG_RECORDS records and the seq->position mapping
         * stays exact. */
        bool fail = false;
        uint32_t done = 0;
        while (done < n) {
            uint32_t seq   = seq0 + done;
            uint32_t seg   = hs_seq_seg(seq);
            uint32_t idx   = hs_seq_idx(seq);
            uint32_t room  = HS_SEG_RECORDS - idx;
            uint32_t chunk = MIN(room, (uint32_t)n - done);

            char path[40];
            hs_seg_path(path, sizeof(path), seg);
            struct fs_file_t f;
            fs_file_t_init(&f);
            if (fs_open(&f, path, FS_O_CREATE | FS_O_WRITE) != 0) {
                LOG_WRN("hs: segment open failed (%s)", path);
                fail = true;
                break;
            }
            /* Seek, not append: the write position is derived from seq, not from
             * the file's current length. (They coincide while writing forward, but
             * deriving it keeps the invariant explicit and survives a short write.) */
            if (fs_seek(&f, (off_t)idx * HPI_HS_SAMPLE_WIRE_SIZE, FS_SEEK_SET) != 0) {
                fs_close(&f);
                LOG_WRN("hs: segment seek failed (%s)", path);
                fail = true;
                break;
            }
            int rc = fs_write(&f, batch + (size_t)done * HPI_HS_SAMPLE_WIRE_SIZE,
                              (size_t)chunk * HPI_HS_SAMPLE_WIRE_SIZE);
            fs_close(&f);
            if (rc < 0) {
                LOG_WRN("hs: segment write failed (%d)", rc);
                fail = true;
                break;
            }

            done += chunk;
            s_flushed_seq = seq + chunk - 1u;   /* durable up to here */
            wrote = true;

            if (idx + chunk >= HS_SEG_RECORDS) {   /* this segment is now full */
                s_seg_index = seg + 1u;
                hs_retention();
            } else {
                s_seg_index = seg;
            }
        }
        if (fail) {
            break;
        }
    } while (more);

    if (wrote) {
        hs_persist_meta();
    }
}

#if defined(CONFIG_HPI_HS_SYNTH)
void hpi_hs_test_wipe(void)
{
    if (s_storage_ready) {
        hs_migrate_layout(HS_LAYOUT_VER);   /* deletes every segment; seq moves forward only */
    }
}
#endif

void hpi_hs_flush_now(void)
{
    if (s_storage_ready) {
        hs_flush();
    }
}

/* ---- Query over the durable log (H2) ------------------------------------
 * Iterate samples of `type` in [from,to] passing q_require: all retained
 * segment files, then the unflushed ring tail (deduped by seq). Serialized by
 * s_scan_lock; the segment read is I/O so this runs off the hot path (store
 * thread / a sync work item), never the display or an ISR. */
static void hs_durable_scan(uint8_t type, int64_t from, int64_t to,
                            uint8_t q_require, hs_scan_cb cb, void *ud)
{
    static uint8_t buf[HPI_HS_SAMPLE_WIRE_SIZE * 64];

    k_mutex_lock(&s_scan_lock, K_FOREVER);

    /* P2: start at the oldest segment that actually EXISTS (retention unlinks
     * s_seg_index - HS_MAX_SEGS, and nothing below the migration base ever did). */
    uint32_t first = hs_seq_seg(hs_seg_oldest_seq());
    for (uint32_t idx = first; idx <= s_seg_index; idx++) {
        /* Skip a segment whose ENTIRE ts range falls outside [from,to] - the same
         * HS-2 P6 bound hs_recompute_summary() uses, applied here so that a
         * windowed caller (hpi_hs_series' 24 h sparkline, hpi_hs_stats' day
         * window) does not read all ~104 retained segments to answer a
         * one-day question. Without this the P5/P6 win evaporates as retention grows.
         *
         * Same guards as the summary: trust the range only if it is monotonic
         * (l_ts >= f_ts) - an RTC correction can break that, and scanning a
         * suspect segment is cheaper than silently dropping in-window data - and
         * never skip the OPEN segment, whose cached range is still moving. */
        if (idx < s_seg_index) {
            int64_t f_ts, l_ts;
            if (hs_seg_ts_get(idx, &f_ts, &l_ts) && l_ts >= f_ts &&
                (l_ts < from || f_ts > to)) {
                continue;
            }
        }

        char path[40];
        hs_seg_path(path, sizeof(path), idx);
        struct fs_file_t f;
        fs_file_t_init(&f);
        if (fs_open(&f, path, FS_O_READ) != 0) {
            continue;   /* rolled-away or not-yet-created */
        }
        int rd;
        while ((rd = fs_read(&f, buf, sizeof(buf))) > 0) {
            int nrec = rd / HPI_HS_SAMPLE_WIRE_SIZE;
            for (int i = 0; i < nrec; i++) {
                struct hpi_hs_sample s;
                memcpy(&s, buf + (size_t)i * HPI_HS_SAMPLE_WIRE_SIZE, HPI_HS_SAMPLE_WIRE_SIZE);
                if (s.type == type && s.ts_utc >= from && s.ts_utc <= to &&
                    (s.quality & q_require) == q_require) {
                    cb(&s, ud);
                }
            }
        }
        fs_close(&f);
    }

    /* unflushed ring tail (seq beyond what segments hold) */
    k_mutex_lock(&s_lock, K_FOREVER);
    uint32_t ix = (s_head + HS_RING_N - s_count) % HS_RING_N;
    for (uint32_t i = 0; i < s_count; i++) {
        struct hpi_hs_sample s = s_ring[ix];
        ix = (ix + 1) % HS_RING_N;
        if (s.seq > s_flushed_seq && s.type == type &&
            s.ts_utc >= from && s.ts_utc <= to &&
            (s.quality & q_require) == q_require) {
            cb(&s, ud);
        }
    }
    k_mutex_unlock(&s_lock);

    k_mutex_unlock(&s_scan_lock);
}

/* Read (and cache) a segment's first/last record timestamps. Returns false if the
 * segment cannot be read. One open + two 18-byte reads, once per segment, ever. */
static bool hs_seg_ts_get(uint32_t seg, int64_t *first, int64_t *last)
{
    struct hs_seg_ts *e = &s_seg_ts[seg % HS_MAX_SEGS];

    if (e->valid && e->seg == seg) {
        *first = e->first;
        *last  = e->last;
        return true;
    }

    char path[40];
    hs_seg_path(path, sizeof(path), seg);
    struct fs_file_t f;
    fs_file_t_init(&f);
    if (fs_open(&f, path, FS_O_READ) != 0) {
        return false;
    }

    struct hpi_hs_sample sm;
    bool ok = false;
    if (fs_read(&f, &sm, HPI_HS_SAMPLE_WIRE_SIZE) == (int)HPI_HS_SAMPLE_WIRE_SIZE) {
        int64_t fts = sm.ts_utc;
        int64_t lts = fts;

        if (fs_seek(&f, 0, FS_SEEK_END) == 0) {
            off_t sz = fs_tell(&f);
            if (sz >= (off_t)HPI_HS_SAMPLE_WIRE_SIZE &&
                fs_seek(&f, sz - (off_t)HPI_HS_SAMPLE_WIRE_SIZE, FS_SEEK_SET) == 0 &&
                fs_read(&f, &sm, HPI_HS_SAMPLE_WIRE_SIZE) == (int)HPI_HS_SAMPLE_WIRE_SIZE) {
                lts = sm.ts_utc;
            }
        }

        *first = fts;
        *last  = lts;
        /* Cache only a COMPLETE segment -- the open one is still being appended to. */
        if (seg < s_seg_index) {
            e->seg = seg; e->first = fts; e->last = lts; e->valid = true;
        }
        ok = true;
    }
    fs_close(&f);
    return ok;
}

/* HS-2 P5: ONE pass over the log, not nine.
 *
 * hs_recompute_summary() used to call hpi_hs_stats() / hs_resting_hr() NINE times
 * (HR, HR_MIN, HR_MAX, the resting-HR histogram, SpO2, temp today, temp 7-day
 * baseline, HRV today, HRV 7-day) -- and EVERY one of those was a full scan of every
 * retained segment. At ~690 KB of log that is ~6 MB of flash reads, every 5 minutes,
 * forever: ~1.8 GB/day of QSPI traffic on a battery-powered watch, burned whether or
 * not anyone ever opens the app.
 *
 * The data was always identical between the nine passes. Accumulate everything in a
 * single walk instead. No new persistent state, no new failure mode -- just stop
 * reading the same bytes nine times. */
#define HR_HIST_LO 30
#define HR_HIST_HI 220
#define HR_HIST_N  (HR_HIST_HI - HR_HIST_LO)

struct hs_sum_acc {
    int64_t now, day_from, week_from, base_to;

    int64_t hr_sum;    uint32_t hr_n;                 /* HR mean, 24 h            */
    int32_t  hrs_min, hrs_max;                        /* extremes OF the mean series
                                                       * -- only a FALLBACK, for a log
                                                       * with no HR_MIN/HR_MAX (pre-P1
                                                       * data). Real peaks come from the
                                                       * extremes series below. */
    uint16_t hr_bins[HR_HIST_N]; uint32_t hr_hist_n;  /* resting HR (10th pct)    */
    int32_t  hr_min;   bool hr_min_set;               /* from the HR_MIN series   */
    int32_t  hr_max;   bool hr_max_set;               /* from the HR_MAX series   */

    int64_t spo2_sum;  uint32_t spo2_n; int32_t spo2_min;

    int64_t t_sum;     uint32_t t_n;                  /* skin temp, 24 h          */
    int64_t tb_sum;    uint32_t tb_n;                 /* skin temp, 7-day baseline*/

    int64_t hrv_sum;   uint32_t hrv_n;                /* HRV SDNN, 24 h           */
    int64_t hrvb_sum;  uint32_t hrvb_n;               /* HRV SDNN, 7-day baseline */

    int64_t rms_sum;   uint32_t rms_n;                /* HRV RMSSD, 24 h          */
    int64_t rmsb_sum;  uint32_t rmsb_n;               /* HRV RMSSD, 7-day baseline*/
    int32_t rms_last;  int64_t  rms_last_ts;          /* most recent RMSSD window */
    /* H6 readiness: nightly (sleep-gated) means feeding the recovery score. */
    int64_t rms_sleep_sum; uint32_t rms_sleep_n;      /* today sleep RMSSD         */
    int64_t hr_sleep_sum;  uint32_t hr_sleep_n;       /* today sleep HR (RHR)      */
    int64_t hrb_sleep_sum; uint32_t hrb_sleep_n;      /* 7-day sleep HR (RHR base) */
    int32_t tz_off;                                   /* UTC offset for the sleep-window check */
};

/* Local sleep window [22:00, 08:00). No dedicated sleep detector yet, but "local
 * night hours" (with on-skin + still applied at ingest) is enough to make the
 * temp/HRV baselines nightly, the way Apple/Garmin measure them. Gating happens
 * here at aggregation (not on a sample quality bit) so it works uniformly for
 * temp (tagged via hs_quality) and HRV (which builds its own quality). */
#define HS_SLEEP_HOUR_START 22
#define HS_SLEEP_HOUR_END    8

static inline bool hs_ts_in_sleep_window(int64_t ts_utc, int32_t tz_off)
{
    int local_hour = (int)((((ts_utc + tz_off) % 86400 + 86400) % 86400) / 3600);
    return (local_hour >= HS_SLEEP_HOUR_START || local_hour < HS_SLEEP_HOUR_END);
}

static void hs_sum_feed(struct hs_sum_acc *a, const struct hpi_hs_sample *s)
{
    if (!(s->quality & HPI_HS_Q_VALID)) {
        return;
    }
    const int64_t ts = s->ts_utc;
    const bool in_day  = (ts >= a->day_from  && ts <= a->now);
    const bool in_week = (ts >= a->week_from && ts <= a->now);
    const bool in_base = (ts >= a->week_from && ts <= a->base_to);   /* 7d..1d ago */

    switch (s->type) {
    case HPI_HS_T_HR:
        if (in_day) {
            if (a->hr_n == 0 || s->value < a->hrs_min) { a->hrs_min = s->value; }
            if (a->hr_n == 0 || s->value > a->hrs_max) { a->hrs_max = s->value; }
            a->hr_sum += s->value;
            a->hr_n++;
            /* Resting HR = low percentile of STILL HR only (accelerometer-gated,
             * the wearable-standard method — motion inflates HR). The epoch carries
             * LOW_MOTION only if EVERY sample in its minute was still (q_and). If
             * too few still epochs accumulate, hr_hist_n stays < 20 and resting HR
             * reports invalid rather than a motion-biased guess. */
            if (s->quality & HPI_HS_Q_LOW_MOTION) {
                int v = s->value;
                if (v < HR_HIST_LO)  { v = HR_HIST_LO; }
                if (v >= HR_HIST_HI) { v = HR_HIST_HI - 1; }
                a->hr_bins[v - HR_HIST_LO]++;
                a->hr_hist_n++;
            }
        }
        /* H6 readiness RHR: nightly mean HR (sleep-gated), today + 7-day baseline. */
        if (hs_ts_in_sleep_window(ts, a->tz_off)) {
            if (in_day)  { a->hr_sleep_sum  += s->value; a->hr_sleep_n++; }
            if (in_base) { a->hrb_sleep_sum += s->value; a->hrb_sleep_n++; }
        }
        break;

    /* P1: HR carries the epoch MEAN, so min/max over it would be min/max-of-means
     * and would systematically UNDER-REPORT peaks. Take the true extremes from the
     * HR_MIN / HR_MAX epoch series. */
    case HPI_HS_T_HR_MIN:
        if (in_day && (!a->hr_min_set || s->value < a->hr_min)) {
            a->hr_min = s->value; a->hr_min_set = true;
        }
        break;
    case HPI_HS_T_HR_MAX:
        if (in_day && (!a->hr_max_set || s->value > a->hr_max)) {
            a->hr_max = s->value; a->hr_max_set = true;
        }
        break;

    case HPI_HS_T_SPO2:
        if (in_day) {
            a->spo2_sum += s->value;
            if (a->spo2_n == 0 || s->value < a->spo2_min) { a->spo2_min = s->value; }
            a->spo2_n++;
        }
        break;

    case HPI_HS_T_SKIN_TEMP:
        /* Temp deviation is a NIGHTLY metric — gate BOTH today and the baseline on
         * the sleep window so "+0.3 vs baseline" compares sleep-to-sleep, never
         * day-to-night (which would read as a spurious fever every afternoon). */
        if (hs_ts_in_sleep_window(ts, a->tz_off)) {
            if (in_day)  { a->t_sum  += s->value; a->t_n++; }
            if (in_base) { a->tb_sum += s->value; a->tb_n++; }
        }
        break;

    case HPI_HS_T_HRV_SDNN:
        if (in_day)  { a->hrv_sum  += s->value; a->hrv_n++; }
        /* Nightly baseline (Whoop/Oura model): sleep-gated. */
        if (in_week && hs_ts_in_sleep_window(ts, a->tz_off)) { a->hrvb_sum += s->value; a->hrvb_n++; }
        break;

    case HPI_HS_T_HRV_RMSSD:
        if (in_day)  { a->rms_sum  += s->value; a->rms_n++; }
        /* H6 readiness: today's NIGHTLY RMSSD (sleep-gated), vs the sleep baseline. */
        if (in_day && hs_ts_in_sleep_window(ts, a->tz_off)) {
            a->rms_sleep_sum += s->value; a->rms_sleep_n++;
        }
        /* Nightly baseline for the stress score: sleep-gated. rms_last (the CURRENT
         * window the score is measured against) is NOT gated — stress is "where am
         * I NOW versus my own nightly normal", not "on average today". */
        if (in_week && hs_ts_in_sleep_window(ts, a->tz_off)) { a->rmsb_sum += s->value; a->rmsb_n++; }
        if (in_week && ts >= a->rms_last_ts) {
            a->rms_last = s->value;
            a->rms_last_ts = ts;
        }
        break;

    default:
        break;
    }
}

static void hs_recompute_summary(void)
{
    int64_t now = hw_get_sys_time_ts();
    if (now <= 0 || !hpi_sys_is_time_valid()) {
        return;
    }
    const int64_t DAY = 86400;

    /* Static: ~450 B of histogram has no business on the store thread's stack. */
    static struct hs_sum_acc a;
    static uint8_t buf[HPI_HS_SAMPLE_WIRE_SIZE * 64];

    /* Align "today" and the baseline to the user's LOCAL calendar day, not a
     * rolling 24 h / UTC midnight — so HR/SpO2/temp "today" match the day the UI
     * shows and the steps counter (which already re-baselines on local tm_mday).
     * local = UTC + offset; seconds into the local day = (now+off) mod DAY, so
     * local midnight (as a UTC epoch) = now - that. */
    const int32_t tz_off = hpi_sys_get_utc_offset();
    const int64_t local_day_start = now - (((now + tz_off) % DAY + DAY) % DAY);

    memset(&a, 0, sizeof(a));
    a.now       = now;
    a.day_from  = local_day_start;               /* since local midnight = today   */
    a.week_from = local_day_start - 7 * DAY;      /* 7 days before today            */
    a.base_to   = local_day_start;               /* baseline excludes today        */
    a.tz_off    = tz_off;                        /* for the sleep-window baseline gate */

    /* ---- the single pass: every retained segment, then the unflushed ring ---- */
    k_mutex_lock(&s_scan_lock, K_FOREVER);

    uint32_t first = hs_seq_seg(hs_seg_oldest_seq());
    uint32_t scanned = 0, skipped = 0;

    for (uint32_t idx = first; idx <= s_seg_index; idx++) {
        /* HS-2 P6: skip a segment whose ENTIRE ts range predates the window. The
         * summary never looks further back than 7 days, but retention now holds ~104,
         * so without this the single pass would read ~15x more than it needs -- and the
         * P5 win would evaporate as retention grew.
         *
         * Guarded on last >= first: if a segment's timestamps are not monotonic (an RTC
         * correction, say) the range is not trustworthy, so scan it rather than risk
         * skipping in-window data. Never skip the OPEN segment. */
        if (idx < s_seg_index) {
            int64_t f_ts, l_ts;
            if (hs_seg_ts_get(idx, &f_ts, &l_ts) &&
                l_ts >= f_ts && l_ts < a.week_from) {
                skipped++;
                continue;
            }
        }

        char path[40];
        hs_seg_path(path, sizeof(path), idx);
        struct fs_file_t f;
        fs_file_t_init(&f);
        if (fs_open(&f, path, FS_O_READ) != 0) {
            continue;   /* rolled away, or not created yet */
        }
        scanned++;
        int rd;
        while ((rd = fs_read(&f, buf, sizeof(buf))) > 0) {
            int nrec = rd / HPI_HS_SAMPLE_WIRE_SIZE;
            for (int i = 0; i < nrec; i++) {
                struct hpi_hs_sample sm;
                memcpy(&sm, buf + (size_t)i * HPI_HS_SAMPLE_WIRE_SIZE,
                       HPI_HS_SAMPLE_WIRE_SIZE);
                hs_sum_feed(&a, &sm);
            }
        }
        fs_close(&f);
    }

    /* unflushed ring tail (seq beyond what the segments hold) */
    k_mutex_lock(&s_lock, K_FOREVER);
    uint32_t ix = (s_head + HS_RING_N - s_count) % HS_RING_N;
    for (uint32_t i = 0; i < s_count; i++) {
        struct hpi_hs_sample sm = s_ring[ix];
        ix = (ix + 1) % HS_RING_N;
        if (sm.seq > s_flushed_seq) {
            hs_sum_feed(&a, &sm);
        }
    }
    k_mutex_unlock(&s_lock);
    k_mutex_unlock(&s_scan_lock);

    LOG_DBG("summary: scanned %u segment(s), skipped %u (outside the 7-day window)",
            scanned, skipped);

    /* ---- derive ---- */
    struct hpi_hs_summary sum;
    memset(&sum, 0, sizeof(sum));
    sum.day_start_ts = local_day_start;     /* local midnight, as a UTC epoch */

    if (a.hr_n > 0) {
        sum.hr_avg = (int32_t)(a.hr_sum / a.hr_n);
        /* Fallback only (see hrs_min/hrs_max): min/max OF THE MEANS under-reports
         * peaks, which is exactly the thing P1's HR_MIN/HR_MAX exist to prevent. It
         * is used solely when the extremes series is absent. */
        sum.hr_min = a.hrs_min;
        sum.hr_max = a.hrs_max;
    }
    if (a.hr_min_set) { sum.hr_min = a.hr_min; }   /* the TRUE trough */
    if (a.hr_max_set) { sum.hr_max = a.hr_max; }   /* the TRUE peak   */

    /* resting HR = 10th percentile of the HR series, from the histogram */
    if (a.hr_hist_n >= 20) {
        uint32_t target = (a.hr_hist_n * 10u) / 100u;
        uint32_t cum = 0;
        for (int i = 0; i < HR_HIST_N; i++) {
            cum += a.hr_bins[i];
            if (cum >= target) {
                sum.hr_resting = HR_HIST_LO + i;
                break;
            }
        }
    }
    sum.hr_resting_valid = (sum.hr_resting > 0);

    if (a.spo2_n > 0) {
        sum.spo2_avg = (int32_t)(a.spo2_sum / a.spo2_n);
        sum.spo2_min = a.spo2_min;
        sum.spo2_valid = true;
    }

    if (a.t_n > 0 && a.tb_n > 0) {
        sum.temp_dev_x100 = (int32_t)(a.t_sum / a.t_n) - (int32_t)(a.tb_sum / a.tb_n);
        sum.temp_dev_valid = true;
        sum.temp_baseline_nights = 7;   /* 7-day window, now sleep-gated (local night) */
    }

    if (a.hrv_n > 0) {
        sum.hrv_sdnn_x10 = (int32_t)(a.hrv_sum / a.hrv_n);
        sum.hrv_valid = true;
    }
    if (a.hrvb_n > 0) {
        sum.hrv_sdnn_base_x10 = (int32_t)(a.hrvb_sum / a.hrvb_n);
    }

    /* ---- P3 follow-on: HRV-derived stress, scored against the user's OWN baseline.
     *
     * Stress used to be EDA-only, from a MANUAL 30-second GSR spot check, scored on
     * ABSOLUTE skin conductance -- a number not comparable to another person, or even
     * to the same person yesterday. RMSSD relative to a personal rolling baseline is
     * what Whoop/Oura/Garmin actually use, and it needs no user action at all.
     *
     * The stress score uses the MOST RECENT window, not today's mean: the question is
     * "where am I now versus my own normal", not "where was I on average today". */
    if (a.rms_n > 0)  { sum.hrv_rmssd_x10      = (int32_t)(a.rms_sum  / a.rms_n); }
    if (a.rmsb_n > 0) { sum.hrv_rmssd_base_x10 = (int32_t)(a.rmsb_sum / a.rmsb_n); }
    sum.hrv_baseline_windows = (uint16_t)MIN(a.rmsb_n, (uint32_t)UINT16_MAX);

    int32_t stress = hpi_hs_stress_from_hrv(a.rms_last, sum.hrv_rmssd_base_x10, a.rmsb_n);
    /* Stale HRV is not current stress. If the newest window is older than an hour the
     * wearer has been moving or off-skin, and yesterday's number must not be presented
     * as now. */
    if (stress >= 0 && (now - a.rms_last_ts) <= 3600) {
        sum.stress_hrv = stress;
        sum.stress_hrv_valid = true;

        /* Record it so it trends and syncs. NOT flagged MANUAL -- that bit distinguishes
         * this continuous score from the EDA spot check, which still records its own
         * HPI_HS_T_STRESS sample and is left completely untouched. */
        hpi_hs_record(HPI_HS_T_STRESS, stress,
                      HPI_HS_Q_VALID | HPI_HS_Q_ON_SKIN | HPI_HS_Q_LOW_MOTION,
                      now);
    }

    /* H6: morning readiness / recovery. Last night's sleep RMSSD + resting HR vs
     * the user's own 7-day sleep baselines (Whoop/Oura model), sleep-gated on both
     * sides so it reflects overnight recovery, not daytime load. Invalid until the
     * baselines exist — never presented as 0. */

    /* Warm-up progress: 50 points per baseline, each proportional to how much of
     * the required data has accrued. Drives the "learning baseline" caption on the
     * Recovery tile while readiness is still invalid. */
    {
        uint32_t hw = MIN(a.rmsb_n, (uint32_t)HPI_HS_READINESS_MIN_HRV_WINDOWS);
        uint32_t rw = MIN(a.hrb_sleep_n, (uint32_t)HPI_HS_READINESS_MIN_RHR_N);
        sum.readiness_warmup_pct = (uint8_t)((hw * 50) / HPI_HS_READINESS_MIN_HRV_WINDOWS +
                                             (rw * 50) / HPI_HS_READINESS_MIN_RHR_N);
    }

    if (a.rms_sleep_n > 0 && a.hr_sleep_n > 0 && a.hrb_sleep_n > 0) {
        int32_t rmssd_today = (int32_t)(a.rms_sleep_sum / a.rms_sleep_n);
        int32_t rhr_today   = (int32_t)(a.hr_sleep_sum  / a.hr_sleep_n);
        int32_t rhr_base    = (int32_t)(a.hrb_sleep_sum / a.hrb_sleep_n);
        int32_t readiness = hpi_hs_readiness(rmssd_today, sum.hrv_rmssd_base_x10,
                                             rhr_today, rhr_base,
                                             a.rmsb_n, a.hrb_sleep_n);
        if (readiness >= 0) {
            sum.readiness = readiness;
            sum.readiness_valid = true;
        }
    }

    /* cumulative / latest */
    struct hpi_hs_sample sm;
    if (hpi_hs_get_latest(HPI_HS_T_STEPS, &sm))         { sum.steps_today = (uint32_t)sm.value; }
    if (hpi_hs_get_latest(HPI_HS_T_ACTIVE_ENERGY, &sm)) { sum.energy_today_kcal = (uint32_t)sm.value; }
    if (hpi_hs_get_latest(HPI_HS_T_STRESS, &sm))        { sum.stress_last = sm.value; sum.stress_valid = true; }

    k_mutex_lock(&s_lock, K_FOREVER);
    s_summary = sum;
    k_mutex_unlock(&s_lock);
}

/* Layout migration. Both known upgrades force a wipe, for DIFFERENT reasons:
 *
 *   v1 -> v2 (P2): v1 segments were VARIABLE-length (they rolled at ">= 8192 B after
 *                  a batch"), so nothing could address them arithmetically at all.
 *   v2 -> v3 (P6): v2 segments were fixed-length but held 448 records; v3 holds 4480.
 *                  Same scheme, different divisor -- so seq -> (segment, offset) maps
 *                  somewhere ELSE entirely.
 *
 * In both cases reading the old files with the new mapping returns the WRONG RECORDS
 * -- samples that parse perfectly and are simply not the ones asked for. Handing the
 * app structurally-valid garbage is worse than any crash, so the old log goes.
 *
 * So: delete every segment, and restart the log at a clean segment boundary.
 *
 *   - seq is NEVER rewound. The app dedups on (device, seq); reusing a seq would
 *     collide with rows it has already stored. Instead we round seq UP to the next
 *     segment boundary, so the new era starts at index 0 of a fresh file with no
 *     partially-written hole ahead of it. The skipped seqs simply never existed --
 *     harmless, the client just never sees them.
 *   - base_seq records where the new era starts, so hs_seg_oldest_seq() never points
 *     a reader at a segment index that predates the migration.
 *
 * Cost: the unsynced tail of the old log is lost (bounded by retention, ~1 day).
 * Acceptable: the alternative is silently corrupt data, and HELLO.oldest tells the
 * app exactly where to resume.
 */
static void hs_migrate_layout(uint16_t from_ver)
{
    /* ASCII only: the em-dash that used to be here rendered as mojibake on the
     * serial console. */
    LOG_WRN("hs: segment layout v%u -> v%u - discarding the old durable log "
            "(the seq->offset mapping differs, so old segments would decode to the "
            "WRONG records, not to an error)",
            (unsigned)from_ver, (unsigned)HS_LAYOUT_VER);

    struct fs_dir_t dir;
    fs_dir_t_init(&dir);
    if (fs_opendir(&dir, HS_DIR) == 0) {
        struct fs_dirent ent;
        while (fs_readdir(&dir, &ent) == 0 && ent.name[0] != '\0') {
            if (ent.type == FS_DIR_ENTRY_FILE && ent.name[0] == 's') {
                char path[64];
                snprintf(path, sizeof(path), HS_DIR "/%s", ent.name);
                fs_unlink(path);
            }
        }
        fs_closedir(&dir);
    }

    /* Round up to a segment boundary so the next sample lands at index 0 of a new
     * file. seq only ever moves forward. */
    uint32_t aligned = ((s_seq + HS_SEG_RECORDS - 1u) / HS_SEG_RECORDS) * HS_SEG_RECORDS;
    s_seq         = aligned;
    s_flushed_seq = aligned;
    s_base_seq    = aligned + 1u;
    s_seg_index   = aligned / HS_SEG_RECORDS;

    hs_persist_meta();
    LOG_WRN("hs: log restarted at seq %u (segment %u); older samples are gone",
            (unsigned)s_base_seq, (unsigned)s_seg_index);
}

int hpi_hs_storage_init(void)
{
    /* Called after the FS mount. Restore the persisted seq cursor + latest
     * snapshot; open the durable log for flushing. fs_stat() is quiet on
     * -ENOENT, so gate mkdir/open with it to avoid the fs layer logging benign
     * "already exists"/"not found" at ERR on every boot. */
    struct fs_dirent ent;
    if (fs_stat(HS_DIR, &ent) != 0) {
        fs_mkdir(HS_DIR);
    }

    struct fs_file_t f;
    struct hs_meta m;
    bool need_migrate = false;
    uint16_t found_layout = 0;

    if (fs_stat(HS_META, &ent) == 0) {
        fs_file_t_init(&f);
        if (fs_open(&f, HS_META, FS_O_READ) == 0) {
            /* The pre-P2 record is SHORTER (no base_seq) and its layout_ver slot was
             * a zeroed _rsv. Read what is there and leave the rest zeroed, so an old
             * meta restores cleanly as layout 0 instead of failing the size check --
             * which would zero s_seq and REWIND the sync cursor, colliding with rows
             * the app has already stored. */
            memset(&m, 0, sizeof(m));
            size_t want = MIN((size_t)ent.size, sizeof(m));
            int rd = fs_read(&f, &m, want);
            fs_close(&f);

            if (rd > 0 && (size_t)rd >= offsetof(struct hs_meta, base_seq) &&
                m.schema_ver == HPI_HS_SCHEMA_VERSION) {
                s_seq = m.seq;              /* keep the sync cursor monotonic  */
                s_flushed_seq = m.seq;      /* everything persisted is flushed */
                s_seg_index = m.seg_index;
                s_base_seq = m.base_seq ? m.base_seq : 1u;

                found_layout = m.layout_ver;
                need_migrate = (m.layout_ver != HS_LAYOUT_VER);
            }
        }
    }

    if (fs_stat(HS_LATEST, &ent) == 0) {
        fs_file_t_init(&f);
        if (fs_open(&f, HS_LATEST, FS_O_READ) == 0) {
            struct hpi_hs_sample s;
            while (fs_read(&f, &s, HPI_HS_SAMPLE_WIRE_SIZE) == HPI_HS_SAMPLE_WIRE_SIZE) {
                if (s.type < HS_LATEST_N) {
                    s_latest[s.type] = s;
                    s_have_latest[s.type] = true;
                }
            }
            fs_close(&f);
        }
    }

    /* P2: must run BEFORE the log is served. Layout-1 segments are variable-length
     * and the new arithmetic would misread them into structurally-valid garbage. */
    if (need_migrate) {
        hs_migrate_layout(found_layout);
    } else if (s_flushed_seq > 0 && s_base_seq == 0) {
        s_base_seq = 1u;
    }

    s_storage_ready = true;
    LOG_INF("health store durable log ready (seq=%u seg=%u base=%u layout=%u)",
            (unsigned)s_seq, (unsigned)s_seg_index,
            (unsigned)s_base_seq, (unsigned)HS_LAYOUT_VER);

    /* Bring up the Record tier (episodic raw-signal sessions) on the same FS —
     * scans /lfs/hs/rec, recovers any interrupted session as PARTIAL. */
    hpi_hs_rec_storage_init();
    return 0;
}

static void hpi_hs_thread(void)
{
    /* Start at 0 so the first full summary+trend pass is ~5 min after storage
     * is ready — not on the first 10 s tick stacked with boot FS traffic.
     * (Was 29: forced recompute almost immediately and contributed to store-
     * thread stack faults looking like a boot loop.) */
    int tick = 0;
    for (;;) {
        k_sleep(K_MSEC(HS_FLUSH_MS));
        if (!s_storage_ready) {
            continue;
        }

        /* DFU quiesce — same reasoning data_module.c already applies to the
         * record tier: the MCUboot secondary slot and /lfs share one QSPI die,
         * so every flush here (open + seek + write + close, plus LittleFS's own
         * metadata writes) contends with the OTA's block erases. A 10 s cadence
         * against a multi-minute erase/write stream stalls SMP responses long
         * enough for the phone to time out and drop the link.
         *
         * Ingest is unaffected: hpi_hs_record() is RAM-only, and the 512-sample
         * ring holds far more than an update's worth. The samples land on the
         * next flush once the flag clears — no explicit resume needed.
         *
         * The 5-minute summary/trend pass below is skipped for the same reason:
         * hs_recompute_* re-read segments off the same die. */
        if (hpi_dfu_is_active()) {
            continue;
        }

        /* HS-2 P1: close any epoch whose window has elapsed. Without this an epoch
         * stays open forever once its signal stops (watch taken off mid-window). */
        if (hpi_sys_is_time_valid()) {
            int64_t now = hw_get_sys_time_ts();
            hpi_hs_epoch_tick(now);
            /* P3: same reason -- close an HRV window stranded by the watch coming off
             * mid-window, instead of leaving it open forever. */
            hpi_hs_hrv_tick(now);
        }

        hs_flush();

        /* HS-2 P5: `lat` used to be rewritten (unlink + create + write, it is
         * variable-length) on EVERY 10 s flush that touched a type -- ~8,640 times a
         * day. Unlike `meta` it is NOT the sync cursor: it is a UI convenience, the
         * last-known value per type used to repaint the tiles at boot before fresh
         * data arrives. Losing up to 5 minutes of it costs nothing, so persist it on
         * the summary cadence instead. (`meta` still writes on every flush -- it
         * carries `seq`, and a stale seq could rewind the sync cursor below one the
         * app already stored. That one was made CHEAP, not RARE.) */
        if (++tick >= 30) {   /* ~5 min (30 * 10 s) */
            tick = 0;
            if (s_latest_dirty) {
                hs_persist_latest();
                s_latest_dirty = false;
            }
            hs_recompute_summary();
            hs_recompute_trends();
        }
    }
}

/* 2048 was too thin. With round-robin trends (one series fill per cycle) 4 KB
 * is enough again; keep the reclaimed 4 KB for the LVGL object pool (glyph
 * draw OOM after heavier carousel tiles). */
#define HS_THREAD_STACK 4096
#define HS_THREAD_PRIO  6
K_THREAD_DEFINE(hpi_hs_thread_id, HS_THREAD_STACK, hpi_hs_thread, NULL, NULL, NULL,
                HS_THREAD_PRIO, 0, 2000);

/* ---- Ingest: one listener set feeds hpi_hs_record from the data channels ----
 * Registered as observers in hpi_zbus_channels.c (hs_*_lis). Timestamps are UTC
 * seconds; quality carries VALID only once the RTC is synced. */

/* Stillness gate: no BMI323 any-motion assertion for this long => LOW_MOTION.
 * The motion hook (CONFIG_HPI_IMU_MOTION_WAKE) stamps hpi_hw_get_last_motion_s()
 * on every physical motion; long quiet => resting. */
#define HS_LOW_MOTION_QUIET_S 30

/* Tag every ingested sample with its capture context so aggregation can exclude
 * junk (§3.1): VALID (RTC synced), ON_SKIN (worn), LOW_MOTION (still). Resting HR
 * and the temp/HRV baselines gate on these; the phone gets the flags too. */
static inline uint8_t hs_quality(void)
{
    if (!hpi_sys_is_time_valid()) {
        return 0;
    }
    uint8_t q = HPI_HS_Q_VALID;
    if (hpi_sys_get_device_on_skin()) {
        q |= HPI_HS_Q_ON_SKIN;
    }
#ifdef CONFIG_HPI_IMU_MOTION_WAKE
    /* last_motion_s = uptime (s) of the most recent motion; <=0 = none since boot
     * (device sitting still). Quiet for >= HS_LOW_MOTION_QUIET_S => still. */
    int64_t now_s = k_uptime_get() / 1000;
    int64_t last_motion_s = hpi_hw_get_last_motion_s();
    if (last_motion_s <= 0 || (now_s - last_motion_s) >= HS_LOW_MOTION_QUIET_S) {
        q |= HPI_HS_Q_LOW_MOTION;
    }
#else
    /* No motion signal available -> can't gate on stillness; mark LOW_MOTION so
     * resting HR falls back to the un-gated percentile instead of never forming. */
    q |= HPI_HS_Q_LOW_MOTION;
#endif
    /* DURING_SLEEP tag for synced samples + phone-side filtering: on-skin + still +
     * local night. (The on-device temp/HRV baselines gate on the ts window at
     * aggregation, so they don't depend on this bit — HRV builds its own quality.) */
    if ((q & HPI_HS_Q_ON_SKIN) && (q & HPI_HS_Q_LOW_MOTION) &&
        hs_ts_in_sleep_window(hw_get_sys_time_ts(), hpi_sys_get_utc_offset())) {
        q |= HPI_HS_Q_DURING_SLEEP;
    }
    return q;
}

static void hs_hr_listener(const struct zbus_channel *chan)
{
    const struct hpi_hr_t *m = zbus_chan_const_msg(chan);
    if (m->hr > 0) {
        /* HS-2 P1: HR publishes every ~3 s. Aggregate into a 1-minute epoch
         * (mean + min + max) instead of storing every publish -- HR is spiky, so
         * the mean alone would under-report peaks. */
        hpi_hs_epoch_hr(m->hr, hs_quality(), hw_get_sys_time_ts());
    }
}
ZBUS_LISTENER_DEFINE(hs_hr_lis, hs_hr_listener);

static void hs_spo2_listener(const struct zbus_channel *chan)
{
    const struct hpi_spo2_point_t *m = zbus_chan_const_msg(chan);
    if (m->spo2 > 0) {
        hpi_hs_record(HPI_HS_T_SPO2, m->spo2, hs_quality(), hw_get_sys_time_ts());
    }
}
ZBUS_LISTENER_DEFINE(hs_spo2_lis, hs_spo2_listener);

static void hs_temp_listener(const struct zbus_channel *chan)
{
    const struct hpi_temp_t *m = zbus_chan_const_msg(chan);
    if (m->temp_c > 0.0) {
        /* HS-2 P1: temp publishes every 5 s. Aggregate into a 5-minute epoch
         * (mean + count). No min/max: skin temp is slow, so the mean represents
         * the window faithfully and an extreme would be an artifact. */
        hpi_hs_epoch_temp((int32_t)(m->temp_c * 100.0),
                          hs_quality() | HPI_HS_Q_ON_SKIN, hw_get_sys_time_ts());
    }
}
ZBUS_LISTENER_DEFINE(hs_temp_lis, hs_temp_listener);

static void hs_steps_listener(const struct zbus_channel *chan)
{
    const struct hpi_steps_t *m = zbus_chan_const_msg(chan);
    /* steps is a CUMULATIVE daily total that only advances (and resets to 0 at
     * local midnight); the hw thread republishes the same value every 5 s and
     * only advances it at the ~60 s flush. Record a sample only when the total
     * actually changes, so the durable log / sync stream isn't packed with
     * identical (and idle-zero) samples. The midnight reset 8000->0 is a change,
     * so the day boundary is still captured. */
    static int32_t s_last_steps = -1;
    if ((int32_t)m->steps != s_last_steps) {
        s_last_steps = (int32_t)m->steps;
        /* HS-2 P1: rate-limit to one record per epoch. STEPS stays CUMULATIVE --
         * only the last value in the window is meaningful (max == last; the mean of
         * a monotonic ramp is not). The aggregator force-closes the window on a
         * DECREASE, so the local-midnight reset (8000 -> 0) still emits the day's
         * final total instead of swallowing it. */
        hpi_hs_epoch_steps((int32_t)m->steps, hs_quality(), hw_get_sys_time_ts());
    }
}
ZBUS_LISTENER_DEFINE(hs_steps_lis, hs_steps_listener);

static void hs_bpt_listener(const struct zbus_channel *chan)
{
    const struct hpi_bpt_t *m = zbus_chan_const_msg(chan);
    /* record a spot check only when a measurement completes */
    if (m->status == 2 && m->progress == 100) {
        int64_t ts = hw_get_sys_time_ts();
        uint8_t q = hs_quality() | HPI_HS_Q_MANUAL;
        hpi_hs_record(HPI_HS_T_BP_SYS, m->sys, q, ts);
        hpi_hs_record(HPI_HS_T_BP_DIA, m->dia, q, ts);
    }
}
ZBUS_LISTENER_DEFINE(hs_bpt_lis, hs_bpt_listener);

static void hs_ecg_listener(const struct zbus_channel *chan)
{
    const struct hpi_ecg_status_t *m = zbus_chan_const_msg(chan);
    /* Spot-check event: one sample per successful recording. Streaming
     * publishes also carry a live HR for the display path — do not log those
     * as separate history points. */
    if (m->status == HPI_ECG_STATUS_COMPLETE && m->hr > 0) {
        int64_t ts = m->ts_complete > 0 ? m->ts_complete : hw_get_sys_time_ts();
        hpi_hs_record(HPI_HS_T_ECG_HR, m->hr, hs_quality() | HPI_HS_Q_MANUAL, ts);
    }
}
ZBUS_LISTENER_DEFINE(hs_ecg_lis, hs_ecg_listener);

#if defined(CONFIG_HPI_GSR_STRESS_INDEX)
static void hs_gsr_listener(const struct zbus_channel *chan)
{
    const struct hpi_gsr_stress_index_t *m = zbus_chan_const_msg(chan);
    if (m->stress_data_ready) {
        int64_t ts = hw_get_sys_time_ts();
        uint8_t q = hs_quality() | HPI_HS_Q_MANUAL;
        hpi_hs_record(HPI_HS_T_EDA_SCL, m->tonic_level_x100, q, ts);   /* already x100 */
        hpi_hs_record(HPI_HS_T_EDA_SCR_RATE, m->peaks_per_minute, q, ts);
        hpi_hs_record(HPI_HS_T_STRESS, m->stress_level, q, ts);
    }
}
ZBUS_LISTENER_DEFINE(hs_gsr_lis, hs_gsr_listener);
#endif
