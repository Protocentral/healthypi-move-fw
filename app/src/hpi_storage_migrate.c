/*
 * HealthyPi Move — one-shot storage layout migration + user-facing data erase
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Protocentral Electronics
 *
 * Rationale for deleting rather than converting the pre-3.0 tree: see the header.
 */

#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "hpi_storage_migrate.h"
#include "hpi_dfu.h"
#include "health/hpi_health_store.h"
#include "health/hpi_hs_record.h"

LOG_MODULE_REGISTER(hpi_storage_migrate, LOG_LEVEL_INF);

#define REV_FILE  "/lfs/sys/storage_rev"
#define REV_MAGIC 0x48505352u /* "HPSR" */

struct rev_file {
    uint32_t magic;
    uint16_t rev;
    uint16_t pad;
};

/* The pre-3.0 directory tree, from the fs_module.c that shipped before the NCS
 * 3.4 rework. `lfs/hrv` has no leading slash on purpose: the old
 * hpi_init_fs_struct() had that typo (it created "/lfs/hrv" only via a later
 * repair path), so a unit may carry either spelling. fs_opendir on a missing
 * path just returns -ENOENT and we move on. */
static const char *const s_legacy_dirs[] = {
    "/lfs/trhr", "/lfs/trspo2", "/lfs/trtemp", "/lfs/trsteps", "/lfs/trbpt",
    "/lfs/ecg",  "/lfs/gsr",    "/lfs/hrv",    "/lfs/log",     "lfs/hrv",
};

/* Static, not stack: hpi_sys_thread runs on 2 KB and a struct fs_dirent alone is
 * ~264 B (it carries LFS_NAME_MAX). Only one purge can be in flight — it is
 * called from a single thread at boot and serialised with the erase path by
 * s_purge_lock — so sharing these is safe. */
static K_MUTEX_DEFINE(s_purge_lock);
static struct fs_dirent s_ent;

/* Names are collected in batches so no unlink ever happens while a directory
 * handle is open. LittleFS gives no ordering guarantee for readdir across a
 * concurrent remove, and a purge that silently skips entries would leave the
 * volume half-full with no error anywhere. */
#define BATCH_N    8
#define NAME_MAX_L 48
static char s_batch[BATCH_N][NAME_MAX_L];

/* Delete every file directly under `path`, then the directory itself.
 *
 * Deliberately NOT recursive. Every pre-3.0 directory is flat — the old
 * trends.c wrote one file per day named by its day-start timestamp — so
 * recursion would buy nothing real, while costing two things that matter here:
 * the batch/dirent buffers above are static (a 2 KB thread stack cannot hold a
 * struct fs_dirent per frame) and so a nested call would clobber its caller's,
 * and a subdirectory that cannot be emptied would make the rescan loop spin
 * forever. A stray subdirectory is instead reported and left alone; the rmdir
 * below then fails harmlessly and the next boot tries again.
 *
 * Returns the number of files unlinked, or a negative errno. -ENOENT is not an
 * error: most units will not have most of these directories. */
static int purge_dir(const char *path)
{
    struct fs_dir_t dir;
    int total = 0;
    bool saw_subdir = false;

    for (;;) {
        int n = 0;

        fs_dir_t_init(&dir);
        int rc = fs_opendir(&dir, path);
        if (rc == -ENOENT) {
            return total;
        }
        if (rc < 0) {
            LOG_WRN("opendir %s: %d", path, rc);
            return rc;
        }

        /* Collect a batch of names, then close the handle before unlinking any
         * of them — see the note on s_batch. */
        while (n < BATCH_N) {
            rc = fs_readdir(&dir, &s_ent);
            if (rc < 0 || s_ent.name[0] == '\0') {
                break;
            }
            if (s_ent.type == FS_DIR_ENTRY_DIR) {
                saw_subdir = true;
                continue;
            }
            strncpy(s_batch[n], s_ent.name, NAME_MAX_L - 1);
            s_batch[n][NAME_MAX_L - 1] = '\0';
            n++;
        }
        fs_closedir(&dir);

        if (n == 0) {
            break; /* no files left — every pass makes progress, so this ends */
        }

        for (int i = 0; i < n; i++) {
            char full[128];
            int len = snprintf(full, sizeof(full), "%s/%s", path, s_batch[i]);
            if (len < 0 || len >= (int)sizeof(full)) {
                /* Cannot happen with the fixed dir list above, but unlinking a
                 * TRUNCATED path would delete the wrong file. Skip loudly. */
                LOG_ERR("storage: path too long under %s - skipping", path);
                continue;
            }
            rc = fs_unlink(full);
            if (rc < 0 && rc != -ENOENT) {
                LOG_WRN("unlink %s: %d", full, rc);
                /* Give up on this directory rather than rescanning forever: a
                 * file we cannot remove would be re-collected on every pass. */
                return total;
            }
            total++;
            /* Yield between unlinks: an erase on the external QSPI die can block
             * for milliseconds and the display thread must keep running. */
            k_yield();
        }
    }

    if (saw_subdir) {
        LOG_WRN("storage: %s has subdirectories - leaving it in place", path);
        return total;
    }

    int rc = fs_unlink(path); /* now empty — remove the directory itself */
    if (rc < 0 && rc != -ENOENT) {
        LOG_DBG("rmdir %s: %d", path, rc);
    }
    return total;
}

static int rev_read(uint16_t *out)
{
    struct fs_file_t f;
    struct rev_file rf;

    fs_file_t_init(&f);
    int rc = fs_open(&f, REV_FILE, FS_O_READ);
    if (rc < 0) {
        return rc; /* -ENOENT on a device that has never been stamped */
    }

    rc = fs_read(&f, &rf, sizeof(rf));
    fs_close(&f);

    if (rc != (int)sizeof(rf) || rf.magic != REV_MAGIC) {
        return -EINVAL;
    }
    *out = rf.rev;
    return 0;
}

static int rev_write(uint16_t rev)
{
    struct fs_file_t f;
    struct rev_file rf = {.magic = REV_MAGIC, .rev = rev, .pad = 0};

    fs_file_t_init(&f);
    int rc = fs_open(&f, REV_FILE, FS_O_CREATE | FS_O_WRITE);
    if (rc < 0) {
        LOG_ERR("open %s for write: %d", REV_FILE, rc);
        return rc;
    }

    rc = fs_write(&f, &rf, sizeof(rf));
    fs_close(&f);
    return (rc == (int)sizeof(rf)) ? 0 : -EIO;
}

/* Free bytes on the LittleFS volume, or 0 if it cannot be read. Used only to
 * report what a purge reclaimed — this is the answer to "where did my storage
 * go?" when a field unit is investigated months later. */
static uint64_t lfs_free_bytes(void)
{
    struct fs_statvfs st;

    if (fs_statvfs("/lfs", &st) < 0) {
        return 0;
    }
    return (uint64_t)st.f_bfree * st.f_frsize;
}

static int purge_legacy_tree(void)
{
    int files = 0;

    for (size_t i = 0; i < ARRAY_SIZE(s_legacy_dirs); i++) {
        /* Re-check between directories, not just once up front: a purge can run
         * for seconds and the phone may start an OTA at any point. The external
         * QSPI die holds both the DFU secondary slots and this filesystem. */
        if (hpi_dfu_is_active()) {
            LOG_WRN("storage: DFU started mid-purge - deferring the rest to next boot");
            return -EAGAIN;
        }

        int n = purge_dir(s_legacy_dirs[i]);
        if (n > 0) {
            LOG_INF("storage: purged %d file(s) from %s", n, s_legacy_dirs[i]);
            files += n;
        }
    }
    return files;
}

int hpi_storage_migrate_run(void)
{
    uint16_t rev = 0;
    int rc = rev_read(&rev);

    if (rc == 0 && rev >= HPI_STORAGE_REV) {
        LOG_DBG("storage rev %u - up to date", (unsigned)rev);
        return 0;
    }

    /* -ENOENT means "never stamped", which is both a fresh unit and every unit
     * upgraded from pre-3.0. Both want the same thing: run the purge (a no-op on
     * a fresh unit, since none of those directories exist) and stamp. */
    LOG_INF("storage: migrating rev %u -> %u", (unsigned)rev, HPI_STORAGE_REV);

    if (hpi_dfu_is_active()) {
        LOG_WRN("storage: DFU in progress - deferring migration to next boot");
        return -EAGAIN;
    }

    k_mutex_lock(&s_purge_lock, K_FOREVER);
    uint64_t before = lfs_free_bytes();
    int files = purge_legacy_tree();
    uint64_t after = lfs_free_bytes();
    k_mutex_unlock(&s_purge_lock);

    if (files == -EAGAIN) {
        return -EAGAIN; /* leave the revision unstamped so it retries */
    }

    if (files > 0) {
        LOG_WRN("storage: removed %d pre-3.0 file(s), reclaimed %u KB",
                files, (unsigned)((after - before) / 1024u));
    }

    rc = rev_write(HPI_STORAGE_REV);
    if (rc < 0) {
        /* The purge succeeded but the stamp did not. Harmless: the next boot
         * re-runs a purge that now finds nothing. Do not report failure upward. */
        LOG_ERR("storage: could not stamp rev (%d) - will re-check next boot", rc);
    }
    return 0;
}

int hpi_storage_erase_health_data(void)
{
    if (hpi_dfu_is_active()) {
        LOG_WRN("storage: erase refused - DFU in progress");
        return -EBUSY;
    }

    LOG_WRN("storage: erasing ALL health data on user request");

    k_mutex_lock(&s_purge_lock, K_FOREVER);
    uint64_t before = lfs_free_bytes();

    /* Order matters. Drop the bulk records first: they are the large files, and
     * if the erase is cut short by a reset the user at least gets the space back.
     * The sample log goes second because wiping it moves `seq` forward, which is
     * the part the phone observes via HELLO.oldest.
     *
     * A refusal here (a capture is still running) aborts the whole erase rather
     * than wiping the samples and leaving the records — a partial erase reported
     * as success is worse than a clear "stop recording first". */
    int recs = hpi_hs_rec_delete_all();
    if (recs < 0) {
        k_mutex_unlock(&s_purge_lock);
        LOG_WRN("storage: erase aborted - record tier busy (%d)", recs);
        return recs;
    }
    hpi_hs_wipe_all();

    /* Anything a pre-3.0 unit left behind goes too — "erase my data" has to mean
     * all of it, not just the part this firmware generation wrote. */
    int legacy = purge_legacy_tree();

    uint64_t after = lfs_free_bytes();
    k_mutex_unlock(&s_purge_lock);

    LOG_WRN("storage: erased %d record(s)%s, reclaimed %u KB",
            recs, (legacy > 0) ? " + pre-3.0 leftovers" : "",
            (unsigned)((after - before) / 1024u));

    /* Stamp the revision: the legacy tree is provably gone now. */
    (void)rev_write(HPI_STORAGE_REV);
    return 0;
}
