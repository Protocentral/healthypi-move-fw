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
#include "hpi_storage_legacy_synth.h"
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
 * 3.4 rework. fs_opendir on a missing path returns -ENOENT and we move on, so
 * listing a directory a given unit never had costs nothing.
 *
 * There is deliberately NO "lfs/hrv" entry for the old hpi_init_fs_struct()
 * typo (it called fs_mkdir("lfs/hrv"), without the leading slash). Zephyr's fs
 * layer rejects any relative path up front -- fs_mkdir and fs_opendir both
 * return -EINVAL and log "invalid directory name!!" -- so that mkdir never
 * created anything and no unit can carry that spelling. Listing it would only
 * produce a spurious ERR+WRN pair on every migrating unit. The repair path in
 * the same file created the real "/lfs/hrv", which is covered below. */
static const char *const s_legacy_dirs[] = {
    "/lfs/trhr", "/lfs/trspo2", "/lfs/trtemp", "/lfs/trsteps", "/lfs/trbpt",
    "/lfs/ecg",  "/lfs/gsr",    "/lfs/hrv",    "/lfs/log",
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
    bool saw_longname = false;

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
            /* Skip rather than truncate. A truncated name builds a path that
             * names a DIFFERENT file (or none), so unlinking it is either wrong
             * or a no-op that gets re-collected on the next pass. No pre-3.0
             * file comes close to this -- the old trends.c named files by their
             * day-start timestamp -- so this is a guard, not a code path. */
            if (strlen(s_ent.name) >= NAME_MAX_L) {
                LOG_ERR("storage: name too long in %s - leaving it", path);
                saw_longname = true;
                continue;
            }
            strcpy(s_batch[n], s_ent.name);
            n++;
        }
        fs_closedir(&dir);

        if (n == 0) {
            break; /* nothing left to collect */
        }

        /* Deletions actually made this pass. The loop reopens the directory from
         * the start each time, so it only terminates if every pass removes at
         * least one of the names it collected. Anything that leaves a collected
         * entry in place -- a truncated name, an unlink that reports -ENOENT --
         * would otherwise re-collect it forever. Count real removals only, and
         * bail on a pass that achieves none. */
        int deleted = 0;

        for (int i = 0; i < n; i++) {
            char full[128];
            int len = snprintf(full, sizeof(full), "%s/%s", path, s_batch[i]);
            if (len < 0 || len >= (int)sizeof(full)) {
                /* Unlinking a TRUNCATED path would delete the wrong file. Skip it;
                 * the zero-progress guard below ends the loop if that is all we
                 * had. (Also reachable if a name was truncated into s_batch.) */
                LOG_ERR("storage: path too long under %s - skipping", path);
                continue;
            }
            rc = fs_unlink(full);
            if (rc < 0) {
                /* -ENOENT included: the name came straight out of readdir, so it
                 * means the entry did not match what we built (a truncated name),
                 * not that the work is done. Treating it as success is what used
                 * to spin this loop forever. */
                LOG_WRN("unlink %s: %d", full, rc);
                /* Give up on this directory rather than rescanning forever. */
                return total;
            }
            total++;
            deleted++;
            /* Yield between unlinks: an erase on the external QSPI die can block
             * for milliseconds and the display thread must keep running. */
            k_yield();
        }

        if (deleted == 0) {
            LOG_ERR("storage: no progress purging %s - stopping", path);
            return total;
        }
    }

    if (saw_subdir || saw_longname) {
        LOG_WRN("storage: %s still has entries this purge will not touch - "
                "leaving the directory in place", path);
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

#if defined(CONFIG_HPI_STORAGE_LEGACY_SYNTH)
int hpi_storage_rev_clear(void)
{
    /* Unstamping is the only way to make the migration run a second time on a
     * unit that has already been through it, which is what makes the synthetic
     * fixture testable at all. Test builds only -- in a release image there is no
     * legitimate reason to re-run a one-shot fixup, and exposing a way to do it
     * would mean an unstamped unit rescanning nine directories every boot. */
    int rc = fs_unlink(REV_FILE);
    if (rc == -ENOENT) {
        return 0;   /* already absent -- the next boot will migrate regardless */
    }
    if (rc < 0) {
        LOG_ERR("storage: could not clear %s: %d", REV_FILE, rc);
    }
    return rc;
}
#endif

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

/* Bytes freed between two lfs_free_bytes() readings, in KB, for logging.
 *
 * The subtraction needs the guard: lfs_free_bytes() returns 0 when fs_statvfs
 * fails, so a failed SECOND reading makes (after - before) underflow uint64 and
 * print ~18 exabytes -- which is exactly the number a field investigation reads
 * months later when trying to work out where the storage went. */
static unsigned reclaimed_kb(uint64_t before, uint64_t after)
{
    return (after > before) ? (unsigned)((after - before) / 1024u) : 0u;
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

    /* A read failure means "treat as never stamped": -ENOENT on a fresh unit or
     * one upgraded from pre-3.0, -EINVAL on a stamp that did not survive. Both
     * want every step re-run, and every step is idempotent, so 0 is the safe
     * floor. */
    if (rc < 0) {
        rev = 0;
    }

    if (rev >= HPI_STORAGE_REV) {
        LOG_DBG("storage rev %u - up to date", (unsigned)rev);
        return 0;
    }

    LOG_INF("storage: migrating rev %u -> %u", (unsigned)rev, HPI_STORAGE_REV);

    if (hpi_dfu_is_active()) {
        LOG_WRN("storage: DFU in progress - deferring migration to next boot");
        return -EAGAIN;
    }

    k_mutex_lock(&s_purge_lock, K_FOREVER);
    uint64_t before = lfs_free_bytes();

    /* ---- one-shot steps, applied in order --------------------------------
     *
     * Each step is guarded by the revision it introduces, so a unit that has
     * already had step 1 gets only step 2 when HPI_STORAGE_REV moves to 2.
     *
     * Add the next fixup as its own `if (rev < 2) { ... }` block below and bump
     * HPI_STORAGE_REV. Do NOT fold new work into an existing step: every unit
     * already stamped at that revision has passed it and will never run it
     * again, so the new work would silently skip exactly the fleet that needs
     * it. This is the whole reason the stamp is a number and not a flag. */

    int files = 0;

    /* rev < 1: drop the pre-3.0 trend / recording / log tree. A no-op on a fresh
     * unit, since none of those directories exist. */
    if (rev < 1) {
        files = purge_legacy_tree();
    }

    uint64_t after = lfs_free_bytes();
    k_mutex_unlock(&s_purge_lock);

    if (files == -EAGAIN) {
        return -EAGAIN; /* leave the revision unstamped so it retries */
    }

    if (files > 0) {
        LOG_WRN("storage: removed %d pre-3.0 file(s), reclaimed %u KB",
                files, reclaimed_kb(before, after));
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
            reclaimed_kb(before, after));

    /* Stamp the revision ONLY if the legacy sweep actually completed. It returns
     * -EAGAIN when a DFU starts mid-erase (the entry check above passed, but an
     * OTA can begin at any point during a multi-second erase). Stamping then
     * would record "pre-3.0 tree is gone" over a tree that is still there, and
     * because the stamp is what makes the migration idempotent, no later boot
     * would ever look again. */
    if (legacy >= 0) {
        (void)rev_write(HPI_STORAGE_REV);
    } else {
        LOG_WRN("storage: legacy sweep deferred (%d) - rev left unstamped so the "
                "next boot retries it", legacy);
    }
    return 0;
}

/* ---- job dispatch --------------------------------------------------------
 *
 * Long filesystem jobs run here, on hpi_sys_thread. An erase unlinks every
 * segment and record file on the external QSPI die and takes seconds; the two
 * callers that want one -- the Settings row (display thread) and
 * HPI_HS_CMD_ERASE (SMP thread) -- must not be the thread that does it. The
 * display thread would blow its task watchdog, and the SMP thread would be doing
 * littlefs I/O on a stack sized for CBOR.
 *
 * It runs on hpi_sys_thread instead of a dedicated workqueue, for two reasons.
 * App-core RAM is at ~97.5% with about 10 KB free, so a new thread stack is the
 * most expensive way to buy this; and hpi_sys_thread already exists, is
 * preemptible at priority 5, and otherwise sleeps forever once init is done.
 * The system workqueue is the wrong home for the same job -- it is cooperative
 * by default and shared with BLE, sensor and settings work, so parking a
 * multi-second erase there stalls all of it.
 *
 * One semaphore signals "something to do" and a bitmask says what. The semaphore
 * is capped at 1, which is correct here rather than lossy: the mask is cleared
 * and every set bit serviced in one wake-up, so two submits between wake-ups
 * both run. A submit landing after the clear leaves the count at 1 and is picked
 * up on the next pass.
 */
#define JOB_ERASE        BIT(0)
#define JOB_LEGACY_SYNTH BIT(1)

static K_SEM_DEFINE(s_job_req, 0, 1);
static atomic_t s_jobs        = ATOMIC_INIT(0);
static atomic_t s_erase_state = ATOMIC_INIT(HPI_STORAGE_ERASE_IDLE);
static atomic_t s_erase_rc    = ATOMIC_INIT(0);
#if defined(CONFIG_HPI_STORAGE_LEGACY_SYNTH)
static atomic_t s_synth_files = ATOMIC_INIT(0);   /* argument for the queued synth */
#endif

void hpi_storage_erase_submit(void)
{
    /* IDLE or DONE -> PENDING. Accepting DONE matters: there are two independent
     * consumers of a result (the Settings row and HPI_HS_CMD_ERASE) and neither
     * is guaranteed to be present, so requiring an ack before the next submit
     * would let an unobserved DONE wedge the erase path permanently.
     *
     * PENDING/RUNNING is dropped -- a second erase would only repeat idempotent
     * work, and re-queueing one mid-run is how the old k_work_submit path ended
     * up reporting the first erase's result for the second. */
    if (!atomic_cas(&s_erase_state, HPI_STORAGE_ERASE_IDLE, HPI_STORAGE_ERASE_PENDING) &&
        !atomic_cas(&s_erase_state, HPI_STORAGE_ERASE_DONE, HPI_STORAGE_ERASE_PENDING)) {
        LOG_WRN("storage: erase already queued or running - ignoring request");
        return;
    }
    atomic_set(&s_erase_rc, 0);
    atomic_or(&s_jobs, JOB_ERASE);
    k_sem_give(&s_job_req);
}

int hpi_storage_erase_state(void)
{
    return (int)atomic_get(&s_erase_state);
}

int hpi_storage_erase_result(void)
{
    return (int)atomic_get(&s_erase_rc);
}

void hpi_storage_erase_ack(void)
{
    /* Advisory only -- submit() clears a stale DONE by itself, so nothing depends
     * on this being called. It just returns the row to "ERASE" once the outcome
     * has been shown. Only the terminal state is clearable: acking a
     * PENDING/RUNNING erase would let a second submit through mid-run.
     *
     * hpi_storage_erase_result() deliberately survives the ack, so whichever of
     * the two consumers acks first does not blind the other to the outcome. */
    atomic_cas(&s_erase_state, HPI_STORAGE_ERASE_DONE, HPI_STORAGE_ERASE_IDLE);
}

#if defined(CONFIG_HPI_STORAGE_LEGACY_SYNTH)
void hpi_storage_legacy_synth_submit(uint32_t files_per_dir)
{
    atomic_set(&s_synth_files, (atomic_val_t)files_per_dir);
    atomic_or(&s_jobs, JOB_LEGACY_SYNTH);
    k_sem_give(&s_job_req);
}
#endif

void hpi_storage_service(k_timeout_t timeout)
{
    if (k_sem_take(&s_job_req, timeout) != 0) {
        return;
    }

    /* Clear and service every set bit in one pass -- see the note on the
     * semaphore cap above. */
    atomic_val_t jobs = atomic_clear(&s_jobs);

#if defined(CONFIG_HPI_STORAGE_LEGACY_SYNTH)
    /* Before the erase, so a single wake-up carrying both bits builds the
     * fixture and then wipes it, which is the order that makes sense if anyone
     * ever asks for both. */
    if (jobs & JOB_LEGACY_SYNTH) {
        (void)hpi_storage_legacy_synth_run((uint32_t)atomic_get(&s_synth_files));
    }
#endif

    if (!(jobs & JOB_ERASE)) {
        return;
    }

    atomic_set(&s_erase_state, HPI_STORAGE_ERASE_RUNNING);
    int rc = hpi_storage_erase_health_data();
    atomic_set(&s_erase_rc, rc);
    atomic_set(&s_erase_state, HPI_STORAGE_ERASE_DONE);

    /* Report the stack high-water mark right after the deepest filesystem work
     * this thread ever does. This is the number that justifies
     * HPI_SYS_THREAD_STACKSIZE -- it cannot be derived statically (the littlefs
     * call chain dominates it), so measure it on a unit that actually has files
     * to delete and size the stack from the log rather than from a guess.
     *
     * Both symbols are required: zephyr/kernel/thread.c:940 gates
     * z_impl_k_thread_stack_space_get() on the pair, and THREAD_STACK_INFO alone
     * (which this build already sets) compiles the call but fails to link.
     * Neither is on in a normal build -- add CONFIG_INIT_STACKS=y temporarily
     * when tuning the stack, then take it back out. */
#if defined(CONFIG_INIT_STACKS) && defined(CONFIG_THREAD_STACK_INFO)
    size_t unused = 0;
    if (k_thread_stack_space_get(k_current_get(), &unused) == 0) {
        LOG_INF("storage: erase done (rc %d); hpi_sys_thread stack unused %u B",
                rc, (unsigned)unused);
    } else {
        LOG_INF("storage: erase done (rc %d)", rc);
    }
#else
    LOG_INF("storage: erase done (rc %d)", rc);
#endif
}
