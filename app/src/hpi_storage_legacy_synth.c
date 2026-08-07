/*
 * HealthyPi Move — synthetic pre-3.0 storage tree (TEST ONLY)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Protocentral Electronics
 *
 * Rationale and contract: see the header.
 */

#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "hpi_storage_legacy_synth.h"
#include "hpi_storage_migrate.h"

#if defined(CONFIG_HPI_STORAGE_LEGACY_SYNTH)

LOG_MODULE_REGISTER(hpi_storage_synth, LOG_LEVEL_INF);

/* Deliberately its OWN list, not shared with hpi_storage_migrate.c.
 *
 * If the generator and the purge read the same constant, a directory missing
 * from that constant is missing from both -- the test would build a tree the
 * purge happens to cover and report success, which is precisely the bug it
 * exists to catch. These are transcribed independently from the pre-3.0
 * fs_module.c (the fs_mkdir calls in hpi_init_fs_struct, plus the /lfs/hrv the
 * repair path created). Any divergence between the two lists is a finding. */
static const char *const s_synth_dirs[] = {
    "/lfs/trhr", "/lfs/trspo2", "/lfs/trtemp", "/lfs/trsteps", "/lfs/trbpt",
    "/lfs/ecg",  "/lfs/gsr",    "/lfs/hrv",    "/lfs/log",
};

/* The directory that also gets the two awkward entries. The purge is expected to
 * empty what it can here and then LEAVE THE DIRECTORY IN PLACE with a warning --
 * unlike the other eight, which should vanish. */
#define AWKWARD_DIR "/lfs/log"

/* Longer than purge_dir()'s NAME_MAX_L (48), so it cannot be carried in the
 * walker's batch buffer. The correct behaviour is to skip it, say so, and still
 * terminate -- the earlier code truncated it, unlinked a path that named nothing,
 * counted that as progress and re-collected it forever. */
#define LONG_NAME "1712345678_this_name_is_far_too_long_for_the_purge_batch_buffer_to_hold"

/* Files per directory is clamped: enough to force purge_dir()'s multi-pass loop
 * (its batch is 8) without spending minutes creating them. */
#define FILES_MIN 1u
#define FILES_MAX 64u
#define FILES_DEFAULT 12u

/* Plausible pre-3.0 payload. The old trends.c wrote a day of hour-aggregated
 * points per file; the exact bytes do not matter here, only that the files
 * occupy real blocks so the reclaimed-KB reporting has something to report. */
static uint8_t s_payload[256];

static int synth_file(const char *dir, const char *name)
{
    char path[128];
    struct fs_file_t f;

    if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path)) {
        return -ENAMETOOLONG;
    }

    fs_file_t_init(&f);
    int rc = fs_open(&f, path, FS_O_CREATE | FS_O_WRITE);
    if (rc < 0) {
        LOG_ERR("synth: open %s: %d", path, rc);
        return rc;
    }
    rc = fs_write(&f, s_payload, sizeof(s_payload));
    fs_close(&f);

    /* Yield: a few hundred creates on QSPI would otherwise hold this thread for
     * seconds without letting the display run. */
    k_yield();
    return (rc == (int)sizeof(s_payload)) ? 0 : -EIO;
}

int hpi_storage_legacy_synth_run(uint32_t files_per_dir)
{
    if (files_per_dir == 0u) {
        files_per_dir = FILES_DEFAULT;
    }
    files_per_dir = CLAMP(files_per_dir, FILES_MIN, FILES_MAX);

    memset(s_payload, 0xA5, sizeof(s_payload));

    LOG_WRN("synth: building a synthetic pre-3.0 tree (%u file(s) per dir)",
            (unsigned)files_per_dir);

    int made = 0;

    for (size_t i = 0; i < ARRAY_SIZE(s_synth_dirs); i++) {
        int rc = fs_mkdir(s_synth_dirs[i]);
        if (rc < 0 && rc != -EEXIST) {
            LOG_ERR("synth: mkdir %s: %d", s_synth_dirs[i], rc);
            return rc;
        }

        /* Named the way pre-3.0 named them: the day-start epoch second. */
        uint32_t day = 1712345678u;
        for (uint32_t n = 0; n < files_per_dir; n++, day += 86400u) {
            char name[24];
            snprintf(name, sizeof(name), "%u", (unsigned)day);
            if (synth_file(s_synth_dirs[i], name) == 0) {
                made++;
            }
        }
    }

    /* The two entries the purge has to cope with rather than choke on. */
    if (synth_file(AWKWARD_DIR, LONG_NAME) == 0) {
        made++;
        LOG_WRN("synth: planted an over-long name in " AWKWARD_DIR
                " - the purge must skip it and still terminate");
    }
    if (fs_mkdir(AWKWARD_DIR "/straydir") == 0) {
        LOG_WRN("synth: planted a subdirectory in " AWKWARD_DIR
                " - the purge must report it and leave the directory alone");
    }

    /* Clear the stamp last: until this point the tree is half-built, and a reset
     * in the middle would have the next boot purge an incomplete fixture. */
    int rc = hpi_storage_rev_clear();
    if (rc < 0) {
        LOG_ERR("synth: could not clear the migration stamp (%d) - the purge will "
                "NOT re-run on the next boot", rc);
        return rc;
    }

    LOG_WRN("synth: %d file(s) created across %u dir(s); migration stamp cleared. "
            "Reboot to run the purge. Expect every directory to disappear except "
            AWKWARD_DIR ", which keeps its subdirectory and its over-long file.",
            made, (unsigned)ARRAY_SIZE(s_synth_dirs));
    return made;
}

#endif /* CONFIG_HPI_STORAGE_LEGACY_SYNTH */
