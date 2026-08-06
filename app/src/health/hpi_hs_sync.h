/*
 * HealthyPi Move — Health Store: HPI_HS MCUmgr/SMP sync group (H0 spec freeze)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * A custom MCUmgr (SMP) management group that streams typed health samples to any
 * client — the HealthyPi Move app, the reference Python client, or a third-party
 * research client. Transport-agnostic by construction: it rides the same SMP
 * stack as DFU/FS, so it works over BLE (smp_bt) today and USB-CDC/UART (smp)
 * later with NO protocol change. Payloads are CBOR (zcbor), matching MCUmgr.
 *
 * Full wire spec + examples: docs/HPI_HS_API.md. This header is the frozen
 * command/id contract.
 */

#ifndef HPI_HS_SYNC_H
#define HPI_HS_SYNC_H

#include <stdint.h>

/* Management group id. MCUmgr reserves ids >= MGMT_GROUP_ID_PERUSER (64) for
 * vendor use; 0x1000 is well inside that range and unused by the DFU/FS/OS
 * groups. Permanent — clients address the group by this id. */
#define HPI_HS_MGMT_GROUP_ID   0x1000

/* Group protocol version (command set shape). Distinct from
 * HPI_HS_SCHEMA_VERSION (sample/type meaning). Both are returned by HELLO. */
#define HPI_HS_GROUP_VERSION   3   /* v3: ERASE cmd 12 */

/* Command ids within the group. Permanent; append only. */
enum hpi_hs_cmd_id {
    HPI_HS_CMD_HELLO   = 0,  /* READ  → handshake / capabilities                */
    HPI_HS_CMD_TYPES   = 1,  /* READ  → self-describing type registry           */
    HPI_HS_CMD_SYNC    = 2,  /* READ  {since_seq,max} → batch of samples         */
    HPI_HS_CMD_SUMMARY = 3,  /* READ  → today-summary + baselines                */
    HPI_HS_CMD_RECORDS = 4,  /* READ  index / fetch of bulk waveform records     */
    HPI_HS_CMD_ACK     = 5,  /* WRITE {acked_seq} → allow retention drop         */
    HPI_HS_CMD_SYNTH   = 6,  /* WRITE {days,wipe} → generate SYNTHETIC test data.
                              * Only exists when CONFIG_HPI_HS_SYNTH=y (off in
                              * release). Every sample it writes is branded
                              * HPI_HS_Q_SYNTHETIC so it can never be mistaken for a
                              * measurement. Returns immediately; generation runs in
                              * the background (a week takes ~100 s).             */
    HPI_HS_CMD_SET_TZ  = 7,  /* WRITE {off:int} → set the UTC offset in seconds
                              * east of UTC (DST-inclusive). Persisted. RTC stays
                              * UTC; this shifts only the display / local-calendar
                              * view. Send it alongside the MCUmgr os-datetime set. */
    /* BPT (finger blood-pressure) calibration control (group v2). Replaces the
     * removed BLE Command Service verbs 0x60/0x61/0x62. SMP is client→response
     * only, so live [status,progress] feedback is polled via CAL_STATUS (or a
     * notify char if re-added). Calibration is a fixed 3 points (idx 0..2). */
    HPI_HS_CMD_BPT_CAL_ENTER  = 8,  /* WRITE {}                     → {rc}          */
    HPI_HS_CMD_BPT_CAL_POINT  = 9,  /* WRITE {sys:u8,dia:u8,idx:u8} → {rc}          */
    HPI_HS_CMD_BPT_CAL_STATUS = 10, /* READ  {} → {st:u8,prog:u8,idx:u8,run:bool,
                                     *            done:u8,cal:u8}
                                     * idx  = the point the DEVICE is measuring.
                                     * done = points the device has COMPLETED this
                                     *        session (0..3), monotonic. Advance the
                                     *        UI on this. Do NOT infer completion
                                     *        from (prog==100 && !run): prog latches
                                     *        at 100 until the next CAL_POINT, so
                                     *        that pair is identical for "point 1
                                     *        just finished" and "point 2 just
                                     *        finished", and polling it twice skips
                                     *        a point.
                                     * cal  = bitmask of vectors STORED on the
                                     *        device (bit n = point n; 0x7 = fully
                                     *        calibrated). Survives reboot — this is
                                     *        how a client knows BP is set up.      */
    HPI_HS_CMD_BPT_CAL_END    = 11, /* WRITE {}                     → {rc}          */

    HPI_HS_CMD_ERASE   = 12, /* WRITE {"confirm":"ERASE"} → {rc, head, oldest}
                              * Erase ALL health data on the device: the durable
                              * sample log, every bulk record, and any pre-3.0
                              * leftovers. Settings, the user profile and BPT
                              * calibration survive — this is "delete my data",
                              * not a factory reset.
                              *
                              * The confirm string is required and must match
                              * exactly; a bare {} is rejected with -EINVAL. A
                              * destructive, irreversible command reachable over
                              * an open SMP transport should not be one malformed
                              * CBOR map away from firing.
                              *
                              * seq is NOT rewound — it rounds UP to the next
                              * segment, so ids/seqs never collide with rows a
                              * client already stored. The response carries the
                              * post-erase head/oldest so the client can reset its
                              * cursor without a second HELLO. -EBUSY if a DFU or
                              * a capture is in flight.                          */
};

/*
 * ---- CBOR payloads (keys are text; see docs/HPI_HS_API.md for full detail) ----
 *
 * HELLO   req: {}                     (SMP READ, empty map)
 *         rsp: { "schema": uint,      // HPI_HS_SCHEMA_VERSION
 *                "group":  uint,      // HPI_HS_GROUP_VERSION
 *                "dev":    tstr,      // device id / serial
 *                "head":   uint,      // newest seq available
 *                "types":  uint }     // number of registry entries
 *
 * TYPES   req: { "from": uint (opt, default 0) }
 *         rsp: { "types": [ { "id":uint, "key":tstr, "unit":tstr, "scale":uint,
 *                             "class":uint, "derived":bool,
 *                             "hk":tstr, "hc":tstr }, ... ] }
 *
 * SYNC    req: { "since": uint,        // client cursor; 0 = from oldest retained
 *                "max":   uint }       // max samples this batch (device may cap)
 *         rsp: { "recs":  bstr,        // N packed hpi_hs_sample records (18 B each)
 *                "n":     uint,        // N
 *                "next":  uint,        // cursor to pass as next "since"
 *                "more":  bool }       // true if more remain past this batch
 *         (Client is idempotent on seq; resume after a dropped link with "next".)
 *
 * SUMMARY req: {}
 *         rsp: packed struct hpi_hs_summary fields as a CBOR map (see API doc).
 *
 * RECORDS req: { "op":"list" } → { "recs":[ {"id":uint,"type":tstr,
 *                                            "ts":uint,"len":uint}, ... ] }
 *         req: { "op":"get","id":uint,"off":uint,"len":uint }
 *                                     → { "id":uint,"off":uint,"data":bstr,"eof":bool }
 *
 * ACK     req: { "acked": uint }       // highest seq the client has durably stored
 *         rsp: { "rc": 0 }             // store may now drop samples <= acked
 *
 * SET_TZ  req: { "off": int }          // UTC offset in seconds east of UTC
 *                                      //   (India = 19800; US-Eastern EST = -18000)
 *         rsp: { "rc": 0 }             // persisted; display re-derives immediately
 *
 * Standard MCUmgr error handling applies (SMP header rc / "err" map).
 */

/* The group self-registers at boot via MCUMGR_HANDLER_DEFINE in hpi_hs_mgmt.c —
 * no explicit init call needed. */

#endif /* HPI_HS_SYNC_H */
