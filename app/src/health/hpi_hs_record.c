/*
 * HealthyPi Move — Health Store: Record tier (H-REC) implementation
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Episodic raw-signal capture sessions. Storage model (v1):
 *   /lfs/hs/rec/r000123  =  [ hpi_hs_record_hdr (packed) ][ raw payload ... ]
 * The header is written first with flags=0 (in-progress) and rewritten in place
 * with HPI_HS_REC_F_COMPLETE + final CRC/lengths on stop. A closed-record index
 * lives in RAM (id-sorted). On boot, any file still flags=0 was interrupted →
 * recovered as HPI_HS_REC_F_PARTIAL (byte_len/CRC recomputed from what survived).
 * A small monotonic id cursor (rmeta) keeps RECORDS ids non-reusing.
 *
 * Contract + rationale: hpi_hs_record.h, docs/HPI_HS_API.md (RECORDS).
 */

#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <zephyr/sys/crc.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "health/hpi_hs_record.h"
#include "hpi_sys.h"   /* hw_get_sys_time_ts() */

LOG_MODULE_REGISTER(hpi_hs_rec, LOG_LEVEL_INF);

#define HS_REC_DIR         "/lfs/hs/rec"
#define HS_REC_META        HS_REC_DIR "/rmeta"
#define HS_REC_MAX         24            /* closed records retained on flash/in index */
#define HS_REC_MAX_ACTIVE  2             /* concurrent open capture sessions          */
#define HS_HDR_SZ          (sizeof(struct hpi_hs_record_hdr))
#define HS_REC_IO_CHUNK    256           /* boot CRC re-scan buffer                   */

struct rec_active {
    bool     in_use;
    uint32_t id;
    struct fs_file_t f;
    uint32_t crc;        /* running CRC32 of payload (chained)  */
    uint32_t byte_len;   /* payload bytes appended so far        */
    uint8_t  signal;
    uint8_t  sfmt;
    uint8_t  channels;
    uint16_t rate;
    int64_t  start_ts;
};

static struct hpi_hs_record_hdr s_index[HS_REC_MAX];   /* closed records, id-ascending */
static size_t   s_index_n;
static struct rec_active s_active[HS_REC_MAX_ACTIVE];
static uint32_t s_next_id = 1;                          /* monotonic; 0 is "none"       */
static K_MUTEX_DEFINE(s_rec_lock);

static uint8_t sfmt_bytes(uint8_t sfmt)
{
    switch (sfmt) {
    case HPI_HS_SFMT_I32: return 4;
    case HPI_HS_SFMT_I16: return 2;
    case HPI_HS_SFMT_U16: return 2;
    default:              return 1;
    }
}

static uint32_t hdr_n_samples(const struct hpi_hs_record_hdr *h)
{
    uint32_t per = (uint32_t)MAX(h->channels, 1) * sfmt_bytes(h->sfmt);
    return per ? (h->byte_len / per) : 0;
}

static void rec_path(char *buf, size_t n, uint32_t id)
{
    snprintf(buf, n, HS_REC_DIR "/r%06u", (unsigned)id);
}

/* True only for a record payload file: 'r' followed by nothing but digits.
 *
 * The exclusion that matters is HS_REC_META ("rmeta"), which lives in the same
 * directory and ALSO starts with 'r'. It persists s_next_id, and deleting it
 * would rewind record ids after an erase -- letting a fresh capture collide with
 * a row the phone already holds, which is the one thing the id cursor exists to
 * prevent. A prefix test alone would take it out. */
static bool rec_name_is_record(const char *name)
{
    if (name[0] != 'r' || name[1] == '\0') {
        return false;
    }
    for (const char *p = name + 1; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }
    return true;
}

/* Scratch for the erase sweep: record basenames are "r" + 6 digits, so 16 bytes
 * is ample. Sized to HS_REC_MAX; the sweep loops until a pass finds nothing, so a
 * directory holding more than that still drains. Static because it is carried
 * across a closedir, and only ever touched under s_rec_lock. */
static char s_sweep[HS_REC_MAX][16];

/* Persist the id cursor so RECORDS ids never reuse across a full retention wipe. */
static void rec_persist_meta(void)
{
    struct fs_dirent ent;
    if (fs_stat(HS_REC_META, &ent) == 0) {
        fs_unlink(HS_REC_META);
    }
    struct fs_file_t f;
    fs_file_t_init(&f);
    if (fs_open(&f, HS_REC_META, FS_O_CREATE | FS_O_WRITE) == 0) {
        fs_write(&f, &s_next_id, sizeof(s_next_id));
        fs_close(&f);
    }
}

/* Remove the oldest closed record (index[0]) to free a retention slot. Caller
 * holds s_rec_lock and has verified s_index_n > 0. */
static void rec_evict_oldest(void)
{
    char path[40];
    rec_path(path, sizeof(path), s_index[0].id);
    fs_unlink(path);
    LOG_DBG("record %u evicted (retention)", (unsigned)s_index[0].id);
    memmove(&s_index[0], &s_index[1], (s_index_n - 1) * HS_HDR_SZ);
    s_index_n--;
}

/* Insert a finalized header into the id-ascending index (new id is the largest,
 * so it appends). Caller holds s_rec_lock. */
static void rec_index_add(const struct hpi_hs_record_hdr *h)
{
    while (s_index_n >= HS_REC_MAX) {
        rec_evict_oldest();
    }
    s_index[s_index_n++] = *h;
}

/* ---- storage init / boot recovery -------------------------------------- */

int hpi_hs_rec_storage_init(void)
{
    struct fs_dirent ent;
    if (fs_stat(HS_REC_DIR, &ent) != 0) {
        fs_mkdir(HS_REC_DIR);
    }

    /* restore the id cursor */
    struct fs_file_t f;
    if (fs_stat(HS_REC_META, &ent) == 0) {
        fs_file_t_init(&f);
        if (fs_open(&f, HS_REC_META, FS_O_READ) == 0) {
            uint32_t v = 0;
            if (fs_read(&f, &v, sizeof(v)) == (int)sizeof(v) && v >= 1) {
                s_next_id = v;
            }
            fs_close(&f);
        }
    }

    /* scan the record dir: load closed records, recover interrupted ones */
    struct fs_dir_t dir;
    fs_dir_t_init(&dir);
    if (fs_opendir(&dir, HS_REC_DIR) != 0) {
        return 0;   /* empty / not present yet — nothing to recover */
    }

    static uint8_t iobuf[HS_REC_IO_CHUNK];
    while (fs_readdir(&dir, &ent) == 0 && ent.name[0] != '\0') {
        /* Same predicate the erase sweep uses, so "is this a record file?" has
         * exactly one definition in this file. It excludes rmeta, which also
         * starts with 'r'. Stricter than the sscanf below on its own: that would
         * accept a trailing-garbage name like "r000123x" on a partial match. */
        if (ent.type != FS_DIR_ENTRY_FILE || !rec_name_is_record(ent.name)) {
            continue;
        }
        unsigned idv = 0;
        if (sscanf(ent.name, "r%06u", &idv) != 1 || idv == 0) {
            continue;
        }

        char path[40];
        snprintf(path, sizeof(path), HS_REC_DIR "/%s", ent.name);
        fs_file_t_init(&f);
        if (fs_open(&f, path, FS_O_RDWR) != 0) {
            continue;
        }
        struct hpi_hs_record_hdr h;
        if (fs_read(&f, &h, HS_HDR_SZ) != (int)HS_HDR_SZ) {
            fs_close(&f);
            fs_unlink(path);   /* too small to be a valid record */
            continue;
        }

        uint32_t payload = (ent.size >= HS_HDR_SZ) ? (uint32_t)(ent.size - HS_HDR_SZ) : 0;

        if (!(h.flags & HPI_HS_REC_F_COMPLETE)) {
            /* interrupted session — recover what survived, mark PARTIAL */
            uint32_t crc = 0, off = 0;
            fs_seek(&f, HS_HDR_SZ, FS_SEEK_SET);
            while (off < payload) {
                size_t want = MIN((size_t)(payload - off), sizeof(iobuf));
                int rd = fs_read(&f, iobuf, want);
                if (rd <= 0) {
                    break;
                }
                crc = crc32_ieee_update(crc, iobuf, rd);
                off += rd;
            }
            h.byte_len = off;               /* trust the bytes we could read */
            h.crc32 = crc;
            h.flags = (h.flags & ~HPI_HS_REC_F_COMPLETE) | HPI_HS_REC_F_PARTIAL;
            h.n_samples = hdr_n_samples(&h);
            fs_seek(&f, 0, FS_SEEK_SET);
            fs_write(&f, &h, HS_HDR_SZ);
            LOG_WRN("record %u recovered as PARTIAL (%u payload bytes)", idv, (unsigned)off);
        }
        fs_close(&f);

        if (idv >= s_next_id) {
            s_next_id = idv + 1;
        }
        if (s_index_n < HS_REC_MAX) {
            s_index[s_index_n++] = h;
        }
    }
    fs_closedir(&dir);

    /* readdir order is not guaranteed sorted — sort the index by id ascending
     * (small N, insertion sort). */
    for (size_t i = 1; i < s_index_n; i++) {
        struct hpi_hs_record_hdr key = s_index[i];
        size_t j = i;
        while (j > 0 && s_index[j - 1].id > key.id) {
            s_index[j] = s_index[j - 1];
            j--;
        }
        s_index[j] = key;
    }

    rec_persist_meta();
    LOG_INF("record tier: %u record(s) restored, next id %u",
            (unsigned)s_index_n, (unsigned)s_next_id);
    return 0;
}

/* ---- capture (producer) API -------------------------------------------- */

int hpi_hs_rec_start(uint8_t signal, uint8_t sfmt, uint8_t channels, uint16_t sample_rate_hz)
{
    if (channels == 0) {
        channels = 1;
    }
    k_mutex_lock(&s_rec_lock, K_FOREVER);

    struct rec_active *slot = NULL;
    for (int i = 0; i < HS_REC_MAX_ACTIVE; i++) {
        if (!s_active[i].in_use) {
            slot = &s_active[i];
            break;
        }
    }
    if (slot == NULL) {
        k_mutex_unlock(&s_rec_lock);
        return -ENOSPC;   /* all capture slots busy */
    }

    uint32_t id = s_next_id++;
    rec_persist_meta();

    struct hpi_hs_record_hdr h = {
        .id = id,
        .start_ts = hw_get_sys_time_ts(),
        .signal = signal,
        .sfmt = sfmt,
        .channels = channels,
        .flags = 0,               /* in-progress */
        .sample_rate_hz = sample_rate_hz,
        .n_samples = 0,
        .byte_len = 0,
        .crc32 = 0,
    };

    char path[40];
    rec_path(path, sizeof(path), id);
    fs_file_t_init(&slot->f);
    if (fs_open(&slot->f, path, FS_O_CREATE | FS_O_WRITE) != 0) {
        s_next_id--;   /* roll the id back — nothing was written */
        k_mutex_unlock(&s_rec_lock);
        return -EIO;
    }
    if (fs_write(&slot->f, &h, HS_HDR_SZ) != (int)HS_HDR_SZ) {
        fs_close(&slot->f);
        fs_unlink(path);
        s_next_id--;
        k_mutex_unlock(&s_rec_lock);
        return -EIO;
    }
    fs_sync(&slot->f);   /* land the in-progress header so boot recovery can find it */

    slot->in_use = true;
    slot->id = id;
    slot->crc = 0;
    slot->byte_len = 0;
    slot->signal = signal;
    slot->sfmt = sfmt;
    slot->channels = channels;
    slot->rate = sample_rate_hz;
    slot->start_ts = h.start_ts;

    k_mutex_unlock(&s_rec_lock);
    LOG_INF("record %u started (sig=%u fmt=%u ch=%u %uHz)",
            (unsigned)id, signal, sfmt, channels, sample_rate_hz);
    return (int)id;
}

static struct rec_active *find_active(uint32_t id)
{
    for (int i = 0; i < HS_REC_MAX_ACTIVE; i++) {
        if (s_active[i].in_use && s_active[i].id == id) {
            return &s_active[i];
        }
    }
    return NULL;
}

int hpi_hs_rec_append(uint32_t id, const void *data, size_t len)
{
    if (data == NULL || len == 0) {
        return -EINVAL;
    }
    k_mutex_lock(&s_rec_lock, K_FOREVER);
    struct rec_active *slot = find_active(id);
    if (slot == NULL) {
        k_mutex_unlock(&s_rec_lock);
        return -ENOENT;
    }
    int wr = fs_write(&slot->f, data, len);
    if (wr != (int)len) {
        k_mutex_unlock(&s_rec_lock);
        return (wr < 0) ? wr : -EIO;
    }
    slot->crc = crc32_ieee_update(slot->crc, data, len);
    slot->byte_len += (uint32_t)len;
    k_mutex_unlock(&s_rec_lock);
    return 0;
}

int hpi_hs_rec_stop(uint32_t id)
{
    k_mutex_lock(&s_rec_lock, K_FOREVER);
    struct rec_active *slot = find_active(id);
    if (slot == NULL) {
        k_mutex_unlock(&s_rec_lock);
        return -ENOENT;
    }

    struct hpi_hs_record_hdr h = {
        .id = slot->id,
        .start_ts = slot->start_ts,
        .signal = slot->signal,
        .sfmt = slot->sfmt,
        .channels = slot->channels,
        .flags = HPI_HS_REC_F_COMPLETE,
        .sample_rate_hz = slot->rate,
        .byte_len = slot->byte_len,
        .crc32 = slot->crc,
    };
    h.n_samples = hdr_n_samples(&h);

    fs_seek(&slot->f, 0, FS_SEEK_SET);
    int wr = fs_write(&slot->f, &h, HS_HDR_SZ);
    fs_close(&slot->f);
    slot->in_use = false;

    if (wr != (int)HS_HDR_SZ) {
        k_mutex_unlock(&s_rec_lock);
        LOG_ERR("record %u header finalize failed (%d)", (unsigned)id, wr);
        return -EIO;
    }

    rec_index_add(&h);
    k_mutex_unlock(&s_rec_lock);
    LOG_INF("record %u complete (%u bytes, %u samples)",
            (unsigned)id, (unsigned)h.byte_len, (unsigned)h.n_samples);
    return 0;
}

/* ---- sync (RECORDS command) API ---------------------------------------- */

size_t hpi_hs_rec_count(void)
{
    k_mutex_lock(&s_rec_lock, K_FOREVER);
    size_t n = s_index_n;
    k_mutex_unlock(&s_rec_lock);
    return n;
}

size_t hpi_hs_rec_list(struct hpi_hs_record_hdr *out, size_t from, size_t max)
{
    if (out == NULL) {
        return 0;
    }
    k_mutex_lock(&s_rec_lock, K_FOREVER);
    size_t n = 0;
    for (size_t i = from; i < s_index_n && n < max; i++) {
        out[n++] = s_index[i];
    }
    k_mutex_unlock(&s_rec_lock);
    return n;
}

int hpi_hs_rec_get(uint32_t id, uint32_t off, uint8_t *buf, size_t len, bool *eof)
{
    if (buf == NULL) {
        return -EINVAL;
    }
    k_mutex_lock(&s_rec_lock, K_FOREVER);

    const struct hpi_hs_record_hdr *h = NULL;
    for (size_t i = 0; i < s_index_n; i++) {
        if (s_index[i].id == id) {
            h = &s_index[i];
            break;
        }
    }
    if (h == NULL) {
        k_mutex_unlock(&s_rec_lock);
        return -ENOENT;
    }

    if (off >= h->byte_len) {           /* nothing left */
        if (eof) {
            *eof = true;
        }
        k_mutex_unlock(&s_rec_lock);
        return 0;
    }
    size_t want = MIN(len, (size_t)(h->byte_len - off));

    char path[40];
    rec_path(path, sizeof(path), id);
    struct fs_file_t f;
    fs_file_t_init(&f);
    if (fs_open(&f, path, FS_O_READ) != 0) {
        k_mutex_unlock(&s_rec_lock);
        return -EIO;
    }
    int rd = -EIO;
    if (fs_seek(&f, HS_HDR_SZ + off, FS_SEEK_SET) == 0) {
        rd = fs_read(&f, buf, want);
    }
    fs_close(&f);
    if (rd < 0) {
        k_mutex_unlock(&s_rec_lock);
        return rd;
    }
    if (eof) {
        *eof = (off + (uint32_t)rd >= h->byte_len);
    }
    k_mutex_unlock(&s_rec_lock);
    return rd;
}

int hpi_hs_rec_delete_all(void)
{
    k_mutex_lock(&s_rec_lock, K_FOREVER);

    /* A capture in flight owns an open file handle and will keep appending to it.
     * Unlinking underneath it would leave a writer pointed at a deleted inode and
     * a header that never lands, so refuse outright rather than half-erase. The
     * caller surfaces this as "stop recording first". */
    for (size_t i = 0; i < HS_REC_MAX_ACTIVE; i++) {
        if (s_active[i].in_use) {
            k_mutex_unlock(&s_rec_lock);
            LOG_WRN("rec: erase refused - record %u still capturing",
                    (unsigned)s_active[i].id);
            return -EBUSY;
        }
    }

    /* Sweep the DIRECTORY, not s_index. The index holds at most HS_REC_MAX (24)
     * entries and boot recovery fills it from an unsorted readdir, so any file
     * beyond the 24th is absent from it -- walking the index would leave those
     * orphans on flash and still report success. "Erase everything" has to mean
     * the directory. */
    int n = 0;
    for (;;) {
        struct fs_dir_t dir;
        struct fs_dirent ent;
        size_t k = 0;

        fs_dir_t_init(&dir);
        if (fs_opendir(&dir, HS_REC_DIR) != 0) {
            break;   /* -ENOENT on a unit that never captured anything */
        }
        /* Collect names first and close the handle before unlinking any of them:
         * LittleFS gives no ordering guarantee for readdir across a concurrent
         * remove, so unlinking with the handle open can silently skip entries. */
        while (k < ARRAY_SIZE(s_sweep) &&
               fs_readdir(&dir, &ent) == 0 && ent.name[0] != '\0') {
            if (ent.type != FS_DIR_ENTRY_FILE || !rec_name_is_record(ent.name)) {
                continue;
            }
            /* Skip rather than truncate: a truncated name resolves to a
             * DIFFERENT file, and unlinking that would delete the wrong record.
             * Unreachable for "r" + up to 10 digits, so this is a guard. */
            if (strlen(ent.name) >= sizeof(s_sweep[0])) {
                LOG_ERR("rec: name too long in " HS_REC_DIR " - leaving it");
                continue;
            }
            strcpy(s_sweep[k], ent.name);
            k++;
        }
        fs_closedir(&dir);

        if (k == 0) {
            break;   /* nothing left that matches -- every pass deletes, so this ends */
        }

        for (size_t i = 0; i < k; i++) {
            char path[40];
            if (snprintf(path, sizeof(path), HS_REC_DIR "/%s", s_sweep[i]) >= (int)sizeof(path)) {
                continue;   /* unreachable for "r" + digits; never unlink a truncated path */
            }
            if (fs_unlink(path) != 0) {
                /* Give up rather than re-collect the same file on every pass. */
                LOG_WRN("rec: unlink %s failed - stopping sweep", path);
                goto swept;
            }
            n++;
            /* The QSPI erase behind each unlink can block for milliseconds. */
            k_yield();
        }
    }
swept:
    s_index_n = 0;

    /* s_next_id is deliberately NOT reset. Record ids must stay unique for the
     * lifetime of the unit: the phone keys its local copies on id, and reusing
     * one after an erase would make a fresh capture collide with a row the app
     * already has. Same reasoning as seq in the sample log. */
    rec_persist_meta();

    k_mutex_unlock(&s_rec_lock);
    LOG_WRN("rec: erased %d record(s); next id stays %u", n, (unsigned)s_next_id);
    return n;
}

int hpi_hs_rec_ack(uint32_t id)
{
    k_mutex_lock(&s_rec_lock, K_FOREVER);
    if (find_active(id) != NULL) {
        k_mutex_unlock(&s_rec_lock);
        return -EBUSY;   /* still capturing */
    }
    for (size_t i = 0; i < s_index_n; i++) {
        if (s_index[i].id == id) {
            char path[40];
            rec_path(path, sizeof(path), id);
            fs_unlink(path);
            memmove(&s_index[i], &s_index[i + 1], (s_index_n - i - 1) * HS_HDR_SZ);
            s_index_n--;
            k_mutex_unlock(&s_rec_lock);
            LOG_INF("record %u acked + dropped", (unsigned)id);
            return 0;
        }
    }
    k_mutex_unlock(&s_rec_lock);
    return -ENOENT;
}
