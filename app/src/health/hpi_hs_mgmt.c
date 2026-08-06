/*
 * HealthyPi Move — Health Store: HPI_HS MCUmgr/SMP group (H4)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Registers the custom MCUmgr group (id HPI_HS_MGMT_GROUP_ID) that streams typed
 * health samples + the derived summary to any SMP client (the HealthyPi Move app,
 * the reference Python client, the web tool). Wire contract: docs/HPI_HS_API.md.
 * Commands: HELLO / TYPES / SYNC / SUMMARY / RECORDS (all READ) + ACK (WRITE).
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zephyr/mgmt/mcumgr/mgmt/handlers.h>
#include <zcbor_common.h>
#include <zcbor_encode.h>
#include <zcbor_decode.h>
#include <mgmt/mcumgr/util/zcbor_bulk.h>
#include <zephyr/drivers/hwinfo.h>
#include <stdio.h>
#include <string.h>

#include "health/hpi_hs_types.h"
#include "health/hpi_hs_sync.h"
#include "health/hpi_health_store.h"
#include "health/hpi_hs_record.h"
#include "hpi_sys.h"   /* hpi_sys_set_utc_offset() */
#include "hpi_storage_migrate.h" /* hpi_storage_erase_health_data() */
#include "hw_module.h" /* hpi_bpt_cal_* (BPT calibration control, group v2) */
#if defined(CONFIG_HPI_HS_SYNTH)
#include "health/hpi_hs_synth.h"
#endif

LOG_MODULE_REGISTER(hpi_hs_mgmt, LOG_LEVEL_DBG);

/* Keep the SYNC batch inside the SMP netbuf (1024 B): 40 * 18 = 720 B + CBOR. */
#define HS_SYNC_MAX_BATCH 40
/* TYPES is paged so the registry entries (with strings) fit the netbuf. */
#define HS_TYPES_PAGE     5

/* Per-unit id, hex of the SoC device id (nRF5340 FICR). `dev` below is a fixed
 * model string, so two watches paired to one phone would collide on a
 * (device, seq) key; clients should key on `uid` instead. Empty string if the
 * SoC gives us nothing. */
static void hs_device_uid(char *out, size_t out_sz)
{
    uint8_t id[8];
    ssize_t n = hwinfo_get_device_id(id, sizeof(id));

    out[0] = '\0';
    if (n <= 0) {
        return;
    }
    if ((size_t)n * 2u + 1u > out_sz) {
        n = (ssize_t)((out_sz - 1u) / 2u);
    }
    for (ssize_t i = 0; i < n; i++) {
        snprintf(out + i * 2, 3, "%02x", id[i]);
    }
}

/* HELLO (read): handshake / capabilities. */
static int hs_h_hello(struct smp_streamer *ctxt)
{
    zcbor_state_t *zse = ctxt->writer->zs;
    char uid[17];

    hs_device_uid(uid, sizeof(uid));

    uint32_t head = hpi_hs_head_seq();
    uint32_t oldest = hpi_hs_oldest_seq();   /* > head when the store is empty */

    bool ok = zcbor_tstr_put_lit(zse, "schema") && zcbor_uint32_put(zse, HPI_HS_SCHEMA_VERSION) &&
              zcbor_tstr_put_lit(zse, "group")  && zcbor_uint32_put(zse, HPI_HS_GROUP_VERSION) &&
              zcbor_tstr_put_lit(zse, "dev")    && zcbor_tstr_put_lit(zse, "healthypi-move") &&
              zcbor_tstr_put_lit(zse, "uid")    && zcbor_tstr_put_term(zse, uid, sizeof(uid)) &&
              zcbor_tstr_put_lit(zse, "head")   && zcbor_uint32_put(zse, head) &&
              zcbor_tstr_put_lit(zse, "oldest") && zcbor_uint32_put(zse, oldest) &&
              zcbor_tstr_put_lit(zse, "types")  && zcbor_uint32_put(zse, (uint32_t)hpi_hs_type_count());
    return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}

/* TYPES (read): {from} -> {next,total,types:[{id,key,unit,scale,class,derived,hk,hc}...]} */
static int hs_h_types(struct smp_streamer *ctxt)
{
    zcbor_state_t *zsd = ctxt->reader->zs;
    zcbor_state_t *zse = ctxt->writer->zs;
    uint32_t from = 0;
    size_t decoded;
    struct zcbor_map_decode_key_val dk[] = {
        ZCBOR_MAP_DECODE_KEY_DECODER("from", zcbor_uint32_decode, &from),
    };
    (void)zcbor_map_decode_bulk(zsd, dk, ARRAY_SIZE(dk), &decoded);   /* from optional */

    uint32_t total = (uint32_t)hpi_hs_type_count();
    uint32_t end = MIN(from + HS_TYPES_PAGE, total);

    bool ok = zcbor_tstr_put_lit(zse, "next")  && zcbor_uint32_put(zse, end) &&
              zcbor_tstr_put_lit(zse, "total") && zcbor_uint32_put(zse, total) &&
              zcbor_tstr_put_lit(zse, "types") && zcbor_list_start_encode(zse, HS_TYPES_PAGE);
    for (uint32_t i = from; ok && i < end; i++) {
        const struct hpi_hs_type_info *t = hpi_hs_type_at(i);
        ok = zcbor_map_start_encode(zse, 8) &&
             zcbor_tstr_put_lit(zse, "id")      && zcbor_uint32_put(zse, t->id) &&
             zcbor_tstr_put_lit(zse, "key")     && zcbor_tstr_put_term(zse, t->key, 16) &&
             zcbor_tstr_put_lit(zse, "unit")    && zcbor_tstr_put_term(zse, t->unit, 8) &&
             zcbor_tstr_put_lit(zse, "scale")   && zcbor_uint32_put(zse, t->scale) &&
             zcbor_tstr_put_lit(zse, "class")   && zcbor_uint32_put(zse, t->data_class) &&
             zcbor_tstr_put_lit(zse, "derived") && zcbor_bool_put(zse, t->derived) &&
             zcbor_tstr_put_lit(zse, "hk")      && zcbor_tstr_put_term(zse, t->hk_type, 48) &&
             zcbor_tstr_put_lit(zse, "hc")      && zcbor_tstr_put_term(zse, t->hc_type, 48) &&
             zcbor_map_end_encode(zse, 8);
    }
    ok = ok && zcbor_list_end_encode(zse, HS_TYPES_PAGE);
    return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}

/* SYNC (read): {since,max} -> {recs:bstr(N*18), n, next, more} */
static int hs_h_sync(struct smp_streamer *ctxt)
{
    zcbor_state_t *zsd = ctxt->reader->zs;
    zcbor_state_t *zse = ctxt->writer->zs;
    uint32_t since = 0, want = HS_SYNC_MAX_BATCH;
    size_t decoded;
    struct zcbor_map_decode_key_val dk[] = {
        ZCBOR_MAP_DECODE_KEY_DECODER("since", zcbor_uint32_decode, &since),
        ZCBOR_MAP_DECODE_KEY_DECODER("max",   zcbor_uint32_decode, &want),
    };
    (void)zcbor_map_decode_bulk(zsd, dk, ARRAY_SIZE(dk), &decoded);

    if (want == 0 || want > HS_SYNC_MAX_BATCH) {
        want = HS_SYNC_MAX_BATCH;
    }
    static uint8_t recs[HS_SYNC_MAX_BATCH * HPI_HS_SAMPLE_WIRE_SIZE];   /* mgmt thread only */
    uint16_t n = 0;
    uint32_t next = since;
    bool more = false;
    hpi_hs_read_since(since, recs, sizeof(recs), (uint16_t)want, &n, &next, &more);

    struct zcbor_string rs = { .value = recs, .len = (size_t)n * HPI_HS_SAMPLE_WIRE_SIZE };
    bool ok = zcbor_tstr_put_lit(zse, "recs") && zcbor_bstr_encode(zse, &rs) &&
              zcbor_tstr_put_lit(zse, "n")    && zcbor_uint32_put(zse, n) &&
              zcbor_tstr_put_lit(zse, "next") && zcbor_uint32_put(zse, next) &&
              zcbor_tstr_put_lit(zse, "more") && zcbor_bool_put(zse, more);
    return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}

/* SUMMARY (read): today-summary + baselines (mirrors struct hpi_hs_summary). */
static int hs_h_summary(struct smp_streamer *ctxt)
{
    zcbor_state_t *zse = ctxt->writer->zs;
    struct hpi_hs_summary s;
    hpi_hs_summary(&s);

    bool ok =
        zcbor_tstr_put_lit(zse, "day")      && zcbor_uint32_put(zse, (uint32_t)s.day_start_ts) &&
        zcbor_tstr_put_lit(zse, "hr_rest")  && zcbor_int32_put(zse, s.hr_resting) &&
        zcbor_tstr_put_lit(zse, "hr_min")   && zcbor_int32_put(zse, s.hr_min) &&
        zcbor_tstr_put_lit(zse, "hr_avg")   && zcbor_int32_put(zse, s.hr_avg) &&
        zcbor_tstr_put_lit(zse, "hr_max")   && zcbor_int32_put(zse, s.hr_max) &&
        zcbor_tstr_put_lit(zse, "spo2_avg") && zcbor_int32_put(zse, s.spo2_avg) &&
        zcbor_tstr_put_lit(zse, "spo2_min") && zcbor_int32_put(zse, s.spo2_min) &&
        zcbor_tstr_put_lit(zse, "temp_dev") && zcbor_int32_put(zse, s.temp_dev_x100) &&
        zcbor_tstr_put_lit(zse, "temp_n")   && zcbor_uint32_put(zse, s.temp_baseline_nights) &&
        zcbor_tstr_put_lit(zse, "hrv")      && zcbor_int32_put(zse, s.hrv_sdnn_x10) &&
        zcbor_tstr_put_lit(zse, "hrv_base") && zcbor_int32_put(zse, s.hrv_sdnn_base_x10) &&
        /* P3: RMSSD + the HRV-derived stress score (0..100 vs the user's own baseline).
         * `stress_hrv_v` false = NO score yet (the baseline is still forming) -- it does
         * NOT mean zero stress. */
        zcbor_tstr_put_lit(zse, "rmssd")      && zcbor_int32_put(zse, s.hrv_rmssd_x10) &&
        zcbor_tstr_put_lit(zse, "rmssd_base") && zcbor_int32_put(zse, s.hrv_rmssd_base_x10) &&
        zcbor_tstr_put_lit(zse, "hrv_wins")   && zcbor_uint32_put(zse, s.hrv_baseline_windows) &&
        zcbor_tstr_put_lit(zse, "stress_hrv")   && zcbor_int32_put(zse, s.stress_hrv) &&
        zcbor_tstr_put_lit(zse, "stress_hrv_v") && zcbor_bool_put(zse, s.stress_hrv_valid) &&
        /* H6 readiness 0..100; `readiness_v` false = still forming (treat as no score). */
        zcbor_tstr_put_lit(zse, "readiness")   && zcbor_int32_put(zse, s.readiness) &&
        zcbor_tstr_put_lit(zse, "readiness_v") && zcbor_bool_put(zse, s.readiness_valid) &&
        zcbor_tstr_put_lit(zse, "steps")    && zcbor_uint32_put(zse, s.steps_today) &&
        zcbor_tstr_put_lit(zse, "energy")   && zcbor_uint32_put(zse, s.energy_today_kcal) &&
        zcbor_tstr_put_lit(zse, "stress")   && zcbor_int32_put(zse, s.stress_last);
    return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}

/* RECORDS (read): the Record tier (H-REC) — episodic raw-signal sessions.
 *   {"op":"list","from":u}  -> {"next","total","recs":[{id,sig,fmt,ch,rate,ns,
 *                                                        len,crc,flags,ts}...]}
 *   {"op":"get","id","off","len"} -> {"id","off","data":bstr,"eof"}
 *   {"op":"ack","id"}       -> {"rc"}
 * `list` is paged (fit the 1024 B netbuf); `get` returns a bounded payload chunk. */
#define HS_REC_LIST_PAGE 6
#define HS_REC_GET_MAX   512

static bool op_is(const struct zcbor_string *op, const char *lit)
{
    size_t n = strlen(lit);
    return op->len == n && memcmp(op->value, lit, n) == 0;
}

static int hs_h_records(struct smp_streamer *ctxt)
{
    zcbor_state_t *zsd = ctxt->reader->zs;
    zcbor_state_t *zse = ctxt->writer->zs;

    struct zcbor_string op = {0};
    uint32_t from = 0, id = 0, off = 0, want = 0;
    size_t decoded;
    struct zcbor_map_decode_key_val dk[] = {
        ZCBOR_MAP_DECODE_KEY_DECODER("op",   zcbor_tstr_decode,   &op),
        ZCBOR_MAP_DECODE_KEY_DECODER("from", zcbor_uint32_decode, &from),
        ZCBOR_MAP_DECODE_KEY_DECODER("id",   zcbor_uint32_decode, &id),
        ZCBOR_MAP_DECODE_KEY_DECODER("off",  zcbor_uint32_decode, &off),
        ZCBOR_MAP_DECODE_KEY_DECODER("len",  zcbor_uint32_decode, &want),
    };
    (void)zcbor_map_decode_bulk(zsd, dk, ARRAY_SIZE(dk), &decoded);

    /* get: {id,off,len} -> payload chunk */
    if (op_is(&op, "get")) {
        static uint8_t getbuf[HS_REC_GET_MAX];   /* mgmt thread only */
        if (want == 0 || want > HS_REC_GET_MAX) {
            want = HS_REC_GET_MAX;
        }
        bool eof = false;
        int rd = hpi_hs_rec_get(id, off, getbuf, want, &eof);
        if (rd < 0) {
            return MGMT_ERR_ENOENT;
        }
        struct zcbor_string ds = { .value = getbuf, .len = (size_t)rd };
        bool ok = zcbor_tstr_put_lit(zse, "id")   && zcbor_uint32_put(zse, id) &&
                  zcbor_tstr_put_lit(zse, "off")  && zcbor_uint32_put(zse, off) &&
                  zcbor_tstr_put_lit(zse, "data") && zcbor_bstr_encode(zse, &ds) &&
                  zcbor_tstr_put_lit(zse, "eof")  && zcbor_bool_put(zse, eof);
        return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
    }

    /* ack: {id} -> drop the record */
    if (op_is(&op, "ack")) {
        int rc = hpi_hs_rec_ack(id);
        bool ok = zcbor_tstr_put_lit(zse, "rc") && zcbor_int32_put(zse, rc);
        return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
    }

    /* list (default): a page of record headers, oldest id first */
    uint32_t total = (uint32_t)hpi_hs_rec_count();
    struct hpi_hs_record_hdr page[HS_REC_LIST_PAGE];
    size_t n = hpi_hs_rec_list(page, from, HS_REC_LIST_PAGE);

    bool ok = zcbor_tstr_put_lit(zse, "next")  && zcbor_uint32_put(zse, from + (uint32_t)n) &&
              zcbor_tstr_put_lit(zse, "total") && zcbor_uint32_put(zse, total) &&
              zcbor_tstr_put_lit(zse, "recs")  && zcbor_list_start_encode(zse, HS_REC_LIST_PAGE);
    for (size_t i = 0; ok && i < n; i++) {
        const struct hpi_hs_record_hdr *h = &page[i];
        ok = zcbor_map_start_encode(zse, 10) &&
             zcbor_tstr_put_lit(zse, "id")    && zcbor_uint32_put(zse, h->id) &&
             zcbor_tstr_put_lit(zse, "sig")   && zcbor_uint32_put(zse, h->signal) &&
             zcbor_tstr_put_lit(zse, "fmt")   && zcbor_uint32_put(zse, h->sfmt) &&
             zcbor_tstr_put_lit(zse, "ch")    && zcbor_uint32_put(zse, h->channels) &&
             zcbor_tstr_put_lit(zse, "rate")  && zcbor_uint32_put(zse, h->sample_rate_hz) &&
             zcbor_tstr_put_lit(zse, "ns")    && zcbor_uint32_put(zse, h->n_samples) &&
             zcbor_tstr_put_lit(zse, "len")   && zcbor_uint32_put(zse, h->byte_len) &&
             zcbor_tstr_put_lit(zse, "crc")   && zcbor_uint32_put(zse, h->crc32) &&
             zcbor_tstr_put_lit(zse, "flags") && zcbor_uint32_put(zse, h->flags) &&
             zcbor_tstr_put_lit(zse, "ts")    && zcbor_int64_put(zse, h->start_ts) &&
             zcbor_map_end_encode(zse, 10);
    }
    ok = ok && zcbor_list_end_encode(zse, HS_REC_LIST_PAGE);
    return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}

/* ACK (write): {acked} -> {rc:0}. Lets the device retire retained data <= acked. */
static int hs_h_ack(struct smp_streamer *ctxt)
{
    zcbor_state_t *zsd = ctxt->reader->zs;
    zcbor_state_t *zse = ctxt->writer->zs;
    uint32_t acked = 0;
    size_t decoded;
    struct zcbor_map_decode_key_val dk[] = {
        ZCBOR_MAP_DECODE_KEY_DECODER("acked", zcbor_uint32_decode, &acked),
    };
    if (zcbor_map_decode_bulk(zsd, dk, ARRAY_SIZE(dk), &decoded) != 0) {
        return MGMT_ERR_EINVAL;
    }
    hpi_hs_ack(acked);
    bool ok = zcbor_tstr_put_lit(zse, "rc") && zcbor_int32_put(zse, 0);
    return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}

/* SET_TZ (write): {off:int32 sec east of UTC} -> {rc:0}. The device keeps the RTC
 * in UTC and stores samples in UTC; this offset shifts only the on-watch clock and
 * local-day boundaries. Persisted, so it survives reboot; the app re-sends it on
 * connect / DST change. Out-of-range values are rejected inside hpi_sys. */
static int hs_h_set_tz(struct smp_streamer *ctxt)
{
    zcbor_state_t *zsd = ctxt->reader->zs;
    zcbor_state_t *zse = ctxt->writer->zs;
    int32_t off = 0;
    size_t decoded;
    struct zcbor_map_decode_key_val dk[] = {
        ZCBOR_MAP_DECODE_KEY_DECODER("off", zcbor_int32_decode, &off),
    };
    if (zcbor_map_decode_bulk(zsd, dk, ARRAY_SIZE(dk), &decoded) != 0) {
        return MGMT_ERR_EINVAL;
    }
    hpi_sys_set_utc_offset(off);
    bool ok = zcbor_tstr_put_lit(zse, "rc") && zcbor_int32_put(zse, 0);
    return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}

/* ERASE (write): {"confirm":"ERASE"} -> {rc, head, oldest}
 *
 * The user-facing "delete everything on the watch". Wipes the durable sample log,
 * every bulk record and any pre-3.0 leftovers; leaves settings, the user profile
 * and BPT calibration alone.
 *
 * The confirm string is mandatory and compared exactly. This is irreversible and
 * reachable by anything that can open an SMP session, so it must not be one
 * malformed CBOR map (or one mis-dispatched command id) away from firing — an
 * absent or wrong `confirm` is -EINVAL, and there is no default.
 *
 * Runs synchronously on the SMP thread. Unlike SYNTH it does not need a worker:
 * an erase is a bounded number of unlinks (retention caps records at HS_REC_MAX
 * and segments at HS_MAX_SEGS), and the client genuinely wants to know it
 * finished before it clears its own cursor.
 *
 * The response repeats the post-erase head/oldest so a client can reset its
 * cursor without a follow-up HELLO. Note seq does NOT go back to zero — it rounds
 * up to the next segment boundary, so `oldest > head` is the expected "store is
 * empty" answer afterwards. */
static int hs_h_erase(struct smp_streamer *ctxt)
{
    zcbor_state_t *zsd = ctxt->reader->zs;
    zcbor_state_t *zse = ctxt->writer->zs;
    struct zcbor_string confirm = {0};
    size_t decoded;
    struct zcbor_map_decode_key_val dk[] = {
        ZCBOR_MAP_DECODE_KEY_DECODER("confirm", zcbor_tstr_decode, &confirm),
    };

    if (zcbor_map_decode_bulk(zsd, dk, ARRAY_SIZE(dk), &decoded) != 0) {
        return MGMT_ERR_EINVAL;
    }
    if (confirm.value == NULL || confirm.len != 5 ||
        memcmp(confirm.value, "ERASE", 5) != 0) {
        LOG_WRN("HS ERASE (cmd 12) rejected - missing/!= \"ERASE\" confirm");
        return MGMT_ERR_EINVAL;
    }

    LOG_WRN("HS ERASE (cmd 12) accepted - erasing all health data");
    int rc = hpi_storage_erase_health_data();

    bool ok = zcbor_tstr_put_lit(zse, "rc")     && zcbor_int32_put(zse, rc) &&
              zcbor_tstr_put_lit(zse, "head")   && zcbor_uint32_put(zse, hpi_hs_head_seq()) &&
              zcbor_tstr_put_lit(zse, "oldest") && zcbor_uint32_put(zse, hpi_hs_oldest_seq());
    return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}

#if defined(CONFIG_HPI_HS_SYNTH)
/* SYNTH (write, TEST BUILDS ONLY): {days, wipe} -> {rc}
 *
 * Generates backdated synthetic data so trends, the 7-day skin-temp baseline and
 * sync-at-scale can be tested without wearing the watch for a week. Every sample it
 * writes carries HPI_HS_Q_SYNTHETIC -- fabricated data must never be mistakable for a
 * measurement, so the client MUST filter it out of anything user-facing.
 *
 * Returns immediately. Generation takes ~100 s for a week and runs on its own thread:
 * doing it here would block the BLE/SMP thread, stall the connection and trip the
 * watchdog. Poll HELLO.head to watch it grow.
 *
 * This command does not exist in a release build (CONFIG_HPI_HS_SYNTH=n). */
static int hs_h_synth(struct smp_streamer *ctxt)
{
    zcbor_state_t *zsd = ctxt->reader->zs;
    zcbor_state_t *zse = ctxt->writer->zs;
    uint32_t days = 7;
    bool wipe = true;
    size_t decoded;
    struct zcbor_map_decode_key_val dk[] = {
        ZCBOR_MAP_DECODE_KEY_DECODER("days", zcbor_uint32_decode, &days),
        ZCBOR_MAP_DECODE_KEY_DECODER("wipe", zcbor_bool_decode,   &wipe),
    };
    (void)zcbor_map_decode_bulk(zsd, dk, ARRAY_SIZE(dk), &decoded);   /* both optional */

    int rc = hpi_hs_synth_request(days, wipe);

    bool ok = zcbor_tstr_put_lit(zse, "rc")   && zcbor_int32_put(zse, rc) &&
              zcbor_tstr_put_lit(zse, "days") && zcbor_uint32_put(zse, days) &&
              zcbor_tstr_put_lit(zse, "wipe") && zcbor_bool_put(zse, wipe);
    return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}
#endif

/* ---- BPT calibration (group v2, cmds 8-11) — thin CBOR wrappers over the finger
 * SMF control API (smf_ppg_finger.c). The engine work runs on the finger SMF
 * thread; these only decode/encode and post. `rc` = 0 ok, negative errno on
 * error (-EINVAL bad/missing idx|sys|dia, -EBUSY a point already running).
 * Finger-sensor-gated end to end; the CBOR paths + rc are desk-testable. ---- */
static int hs_h_bpt_enter(struct smp_streamer *ctxt)
{
    zcbor_state_t *zse = ctxt->writer->zs;
    int rc = hpi_bpt_cal_enter();
    LOG_INF("HS BPT_CAL_ENTER (cmd 8) received -> rc=%d", rc);
    bool ok = zcbor_tstr_put_lit(zse, "rc") && zcbor_int32_put(zse, rc);
    return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}

static int hs_h_bpt_point(struct smp_streamer *ctxt)
{
    zcbor_state_t *zsd = ctxt->reader->zs;
    zcbor_state_t *zse = ctxt->writer->zs;
    /* Defaults out of range so a missing field fails validation below. */
    uint32_t sys = 0x1000, dia = 0x1000, idx = 0x1000;
    size_t decoded;
    struct zcbor_map_decode_key_val dk[] = {
        ZCBOR_MAP_DECODE_KEY_DECODER("sys", zcbor_uint32_decode, &sys),
        ZCBOR_MAP_DECODE_KEY_DECODER("dia", zcbor_uint32_decode, &dia),
        ZCBOR_MAP_DECODE_KEY_DECODER("idx", zcbor_uint32_decode, &idx),
    };
    if (zcbor_map_decode_bulk(zsd, dk, ARRAY_SIZE(dk), &decoded) != 0 ||
        sys > 255 || dia > 255 || idx > 255) {
        LOG_WRN("HS BPT_CAL_POINT (cmd 9) bad args: sys=%u dia=%u idx=%u -> EINVAL",
                sys, dia, idx);
        return MGMT_ERR_EINVAL;
    }
    int rc = hpi_bpt_cal_point((uint8_t)sys, (uint8_t)dia, (uint8_t)idx);
    LOG_INF("HS BPT_CAL_POINT (cmd 9) sys=%u dia=%u idx=%u -> rc=%d",
            sys, dia, idx, rc);
    bool ok = zcbor_tstr_put_lit(zse, "rc") && zcbor_int32_put(zse, rc);
    return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}

static int hs_h_bpt_status(struct smp_streamer *ctxt)
{
    zcbor_state_t *zse = ctxt->writer->zs;
    uint8_t st = 0, prog = 0, idx = 0;
    bool run = false;
    hpi_bpt_cal_status(&st, &prog, &idx, &run);
    /* `done` and `cal` are additive keys; a client that does not know them just
     * ignores them. See hw_module.h for why a client must not infer completion
     * from (prog == 100 && !run) — that pair is ambiguous across points. */
    uint8_t done = hpi_bpt_cal_points_done();
    uint8_t cal  = hpi_bpt_cal_vectors();
    LOG_DBG("HS BPT_CAL_STATUS (cmd 10) -> st=%u prog=%u idx=%u run=%d done=%u cal=0x%02x",
            st, prog, idx, run, done, cal);
    bool ok = zcbor_tstr_put_lit(zse, "st")   && zcbor_uint32_put(zse, st)   &&
              zcbor_tstr_put_lit(zse, "prog") && zcbor_uint32_put(zse, prog) &&
              zcbor_tstr_put_lit(zse, "idx")  && zcbor_uint32_put(zse, idx)  &&
              zcbor_tstr_put_lit(zse, "run")  && zcbor_bool_put(zse, run)    &&
              zcbor_tstr_put_lit(zse, "done") && zcbor_uint32_put(zse, done) &&
              zcbor_tstr_put_lit(zse, "cal")  && zcbor_uint32_put(zse, cal);
    return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}

static int hs_h_bpt_end(struct smp_streamer *ctxt)
{
    zcbor_state_t *zse = ctxt->writer->zs;
    int rc = hpi_bpt_cal_end();
    LOG_INF("HS BPT_CAL_END (cmd 11) received -> rc=%d", rc);
    bool ok = zcbor_tstr_put_lit(zse, "rc") && zcbor_int32_put(zse, rc);
    return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}

static const struct mgmt_handler hpi_hs_handlers[] = {
    [HPI_HS_CMD_HELLO]   = { hs_h_hello,   NULL },
    [HPI_HS_CMD_TYPES]   = { hs_h_types,   NULL },
    [HPI_HS_CMD_SYNC]    = { hs_h_sync,    NULL },
    [HPI_HS_CMD_SUMMARY] = { hs_h_summary, NULL },
    [HPI_HS_CMD_RECORDS] = { hs_h_records, NULL },
    [HPI_HS_CMD_ACK]     = { NULL,         hs_h_ack },
    [HPI_HS_CMD_SET_TZ]  = { NULL,         hs_h_set_tz },
    [HPI_HS_CMD_ERASE]   = { NULL,         hs_h_erase },
    [HPI_HS_CMD_BPT_CAL_ENTER]  = { NULL,          hs_h_bpt_enter },
    [HPI_HS_CMD_BPT_CAL_POINT]  = { NULL,          hs_h_bpt_point },
    [HPI_HS_CMD_BPT_CAL_STATUS] = { hs_h_bpt_status, NULL },
    [HPI_HS_CMD_BPT_CAL_END]    = { NULL,          hs_h_bpt_end },
#if defined(CONFIG_HPI_HS_SYNTH)
    [HPI_HS_CMD_SYNTH]   = { NULL,         hs_h_synth },
#endif
};

static struct mgmt_group hpi_hs_group = {
    .mg_handlers = hpi_hs_handlers,
    .mg_handlers_count = ARRAY_SIZE(hpi_hs_handlers),
    .mg_group_id = HPI_HS_MGMT_GROUP_ID,
};

static void hpi_hs_mgmt_register(void)
{
    mgmt_register_group(&hpi_hs_group);
}

MCUMGR_HANDLER_DEFINE(hpi_hs_mgmt, hpi_hs_mgmt_register);
