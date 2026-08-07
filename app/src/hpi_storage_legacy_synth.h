/*
 * HealthyPi Move — synthetic pre-3.0 storage tree (TEST ONLY)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Protocentral Electronics
 *
 * Builds the directory tree a watch upgraded from 2.x carries, so the one-shot
 * purge in hpi_storage_migrate.c can be exercised on a bench unit that was
 * flashed with 3.x and never had one.
 *
 * This exists because none of that purge was testable otherwise, which is
 * exactly how a batch of defects survived into review: a forward-progress bug in
 * the directory walker, a name-truncation hazard, and a revision stamped over a
 * tree that was still there. All three need a real tree to show up.
 *
 * Compiled out entirely unless CONFIG_HPI_STORAGE_LEGACY_SYNTH=y (excluded from
 * the build by app/CMakeLists.txt), and the SMP command that drives it does not
 * exist in a release image.
 */

#ifndef HPI_STORAGE_LEGACY_SYNTH_H
#define HPI_STORAGE_LEGACY_SYNTH_H

#include <stdint.h>

#if defined(CONFIG_HPI_STORAGE_LEGACY_SYNTH)

/* Create a synthetic pre-3.0 tree and clear the migration stamp so the next boot
 * purges it.
 *
 * `files_per_dir` files land in each legacy directory (clamped to a sane range).
 * One directory additionally gets the two entries the purge must survive without
 * deleting or looping on: a name too long for the walker's batch buffer, and a
 * stray subdirectory. That directory is expected to be LEFT IN PLACE by the
 * purge, with a warning; every other one should disappear.
 *
 * BLOCKS -- it creates a few hundred files on the external QSPI die. Runs on
 * hpi_sys_thread via hpi_storage_legacy_synth_submit(), never on the caller's.
 *
 * Returns the number of files created, or a negative errno.
 */
int hpi_storage_legacy_synth_run(uint32_t files_per_dir);

#endif /* CONFIG_HPI_STORAGE_LEGACY_SYNTH */

#endif /* HPI_STORAGE_LEGACY_SYNTH_H */
