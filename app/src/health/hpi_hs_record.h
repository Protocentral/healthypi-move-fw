/*
 * HealthyPi Move — Health Store: Record tier (H-REC)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * The store's SECOND tier: episodic raw-signal capture sessions (ECG, BioZ/GSR,
 * wrist/finger PPG, HRV R-R, IMU) — as opposed to the aggregatable Samples tier
 * (hpi_health_store.h). Each session is a self-describing header
 * (struct hpi_hs_record_hdr) + a raw payload, stored append-safe on LittleFS and
 * synced through the same HPI_HS group via the RECORDS command (list → chunked
 * get → ack). Contract: docs/HPI_HS_API.md (RECORDS).
 *
 * Capture API (producers — SMF/data threads):
 *   id = hpi_hs_rec_start(signal, sfmt, channels, rate)   open a session
 *   hpi_hs_rec_append(id, data, len)                      stream raw payload
 *   hpi_hs_rec_stop(id)                                   finalize (COMPLETE + CRC)
 *
 * Sync API (RECORDS command handlers in hpi_hs_mgmt.c):
 *   hpi_hs_rec_count(), hpi_hs_rec_list(), hpi_hs_rec_get(), hpi_hs_rec_ack()
 */

#ifndef HPI_HS_RECORD_H
#define HPI_HS_RECORD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "health/hpi_hs_types.h"

/* Storage init: called AFTER the filesystem is mounted (alongside
 * hpi_hs_storage_init). Creates /lfs/hs/rec, scans existing records into the
 * in-RAM index, recovers any interrupted session as PARTIAL, and restores the
 * monotonic record-id cursor. */
int hpi_hs_rec_storage_init(void);

/* ---- Capture (producer) API ---------------------------------------------
 * Start a session. Returns a positive record id, or negative errno (e.g.
 * -ENOSPC if all active slots are busy, -EIO on FS error). channels>=1;
 * sfmt is enum hpi_hs_sfmt; rate is the nominal sample rate in Hz. */
int hpi_hs_rec_start(uint8_t signal, uint8_t sfmt, uint8_t channels, uint16_t sample_rate_hz);

/* Append raw payload bytes to an open session (packed per sfmt/channels).
 * Returns 0 or negative errno. Safe to call from the capturing thread. */
int hpi_hs_rec_append(uint32_t id, const void *data, size_t len);

/* Finalize an open session: mark COMPLETE, write the final CRC/lengths into the
 * header, close the file. Returns 0 or negative errno. */
int hpi_hs_rec_stop(uint32_t id);

/* ---- Sync (RECORDS command) API -----------------------------------------
 * All operate on the closed-record index (COMPLETE or PARTIAL), oldest id
 * first. */
size_t hpi_hs_rec_count(void);

/* Copy up to `max` record headers into out[], starting at index `from` in the
 * id-sorted index. Returns the number copied. */
size_t hpi_hs_rec_list(struct hpi_hs_record_hdr *out, size_t from, size_t max);

/* Read up to `len` payload bytes of record `id` at payload-offset `off` into
 * buf. Returns bytes read (>=0), or negative errno (-ENOENT if no such record).
 * *eof is set true when this chunk reaches the payload end. */
int hpi_hs_rec_get(uint32_t id, uint32_t off, uint8_t *buf, size_t len, bool *eof);

/* Retention: drop record `id` (client has durably stored it). Returns 0, or
 * -ENOENT. An open (still-capturing) record is not droppable → -EBUSY. */
int hpi_hs_rec_ack(uint32_t id);

/* Drop EVERY closed record and its file. Returns the number deleted, or -EBUSY if
 * a capture is still in flight (stop it first — unlinking under an open writer
 * would strand the handle). Record ids are not rewound; see the implementation.
 * Used by the user-facing erase, not by retention. */
int hpi_hs_rec_delete_all(void);

#endif /* HPI_HS_RECORD_H */
