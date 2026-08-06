/*
 * HealthyPi Move — one-shot storage layout migration
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Protocentral Electronics
 *
 * A watch upgraded from pre-3.0 firmware carries a whole tree of files no code
 * reads any more: the per-day trend files (/lfs/trhr, /lfs/trspo2, ...), the raw
 * recording dirs (/lfs/ecg, /lfs/gsr, /lfs/hrv) and the log index (/lfs/log).
 * Nothing deletes them today — fs_module_init() only builds the directory
 * structure when /lfs/sys is MISSING, which on an upgraded unit it never is — so
 * they sit on the 112 MB LittleFS volume forever.
 *
 * They are deleted, not converted. The pre-3.0 firmware had no SET_TZ and stored
 * wall-clock time, so those timestamps carry an unknown UTC offset (the user may
 * have travelled); folding them into the health store, whose wire contract is
 * UTC, would mean inventing an offset and publishing knowingly-wrong timestamps.
 * The old records are also already hour-aggregated, so they would land as one
 * synthetic point per hour with no quality bits. See docs/HPI_HS_API.md §5.
 */

#ifndef HPI_STORAGE_MIGRATE_H
#define HPI_STORAGE_MIGRATE_H

#include <stdint.h>

/* Bump when the on-flash layout changes in a way that needs a one-shot fixup.
 *
 *   1 — drop the pre-3.0 trend / recording / log tree.
 */
#define HPI_STORAGE_REV 1

/* Run any pending storage migration, then stamp the revision.
 *
 * Idempotent and safe to call on every boot: it reads /lfs/sys/storage_rev first
 * and returns immediately when already current. Call AFTER the filesystem is
 * mounted and off the boot critical path — unlinking thousands of files on the
 * external QSPI die takes seconds, and that die is shared with the DFU secondary
 * slots.
 *
 * Returns 0 when nothing was pending or the migration completed (revision
 * stamped), -EAGAIN when it was deferred because a DFU is in progress (nothing
 * stamped; it retries on the next boot), or a negative errno on failure.
 */
int hpi_storage_migrate_run(void);

/* Erase every health-data file the device holds: the health-store durable log,
 * the bulk record tier, and any pre-3.0 leftovers. Settings, the user profile and
 * BPT calibration are NOT touched.
 *
 * This is the user-facing "erase data" action, reachable from the watch's own
 * Settings screen and from the phone over HPI_HS_CMD_ERASE. Returns 0 on success
 * or -EBUSY when a DFU is in progress.
 */
int hpi_storage_erase_health_data(void);

#endif /* HPI_STORAGE_MIGRATE_H */
