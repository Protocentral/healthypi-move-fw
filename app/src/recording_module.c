/*
 * HealthyPi Move - Recording Module
 *
 * Background multi-signal recording with BLE configuration and MCUMgr retrieval.
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/timeutil.h>
#include <zephyr/zbus/zbus.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "recording_module.h"
#include "cmd_module.h"
#include "fs_module.h"
#include "hw_module.h"
#include "hpi_sys.h"

LOG_MODULE_REGISTER(recording_module, LOG_LEVEL_DBG);

/* Atomic state flags for fast path checks (accessed from data_thread) */
atomic_t g_recording_active = ATOMIC_INIT(0);
atomic_t g_recording_signal_mask = ATOMIC_INIT(0);

/* Synchronization primitives */
K_SEM_DEFINE(sem_rec_start, 0, 1);
K_SEM_DEFINE(sem_rec_stop, 0, 1);
K_SEM_DEFINE(sem_rec_flush, 0, 10);  /* Counted semaphore for flush requests */
K_MUTEX_DEFINE(mutex_rec_config);
K_MUTEX_DEFINE(mutex_rec_state);

/* Current configuration and session state */
static struct hpi_recording_config_t current_config;
static struct hpi_recording_session_t current_session;
static bool config_valid = false;

/* Per-signal dual buffers */
struct rec_signal_buffer {
    uint8_t  buffer_a[REC_BUFFER_SIZE];
    uint8_t  buffer_b[REC_BUFFER_SIZE];
    uint16_t write_idx;           /* Current write position in active buffer */
    uint8_t  active_buffer;       /* 0 = A, 1 = B */
    atomic_t flush_pending;       /* Buffer index ready to flush (0, 1, or -1 for none) */
    uint32_t total_samples;       /* Running count of samples written to file */
    struct fs_file_t file;        /* Open file handle */
    bool     file_open;           /* File is currently open */
    uint16_t sample_rate;         /* Sample rate for this signal */
    uint8_t  sample_size;         /* Size of one sample in bytes */
};

static struct rec_signal_buffer rec_buffers[REC_TYPE_COUNT];

/* Sample rate and size lookup tables */
static const uint16_t signal_sample_rates[REC_TYPE_COUNT] = {
    [REC_TYPE_PPG_WRIST]  = REC_SAMPLE_RATE_PPG_WRIST,
    [REC_TYPE_PPG_FINGER] = REC_SAMPLE_RATE_PPG_FINGER,
    [REC_TYPE_IMU_ACCEL]  = REC_SAMPLE_RATE_IMU_ACCEL,
    [REC_TYPE_IMU_GYRO]   = REC_SAMPLE_RATE_IMU_GYRO,
    [REC_TYPE_GSR]        = REC_SAMPLE_RATE_GSR,
};

static const uint8_t signal_sample_sizes[REC_TYPE_COUNT] = {
    [REC_TYPE_PPG_WRIST]  = REC_SAMPLE_SIZE_PPG_WRIST,
    [REC_TYPE_PPG_FINGER] = REC_SAMPLE_SIZE_PPG_FINGER,
    [REC_TYPE_IMU_ACCEL]  = REC_SAMPLE_SIZE_IMU_ACCEL,
    [REC_TYPE_IMU_GYRO]   = REC_SAMPLE_SIZE_IMU_GYRO,
    [REC_TYPE_GSR]        = REC_SAMPLE_SIZE_GSR,
};

static const char *signal_filenames[REC_TYPE_COUNT] = {
    [REC_TYPE_PPG_WRIST]  = "ppgw.bin",
    [REC_TYPE_PPG_FINGER] = "ppgf.bin",
    [REC_TYPE_IMU_ACCEL]  = "accel.bin",
    [REC_TYPE_IMU_GYRO]   = "gyro.bin",
    [REC_TYPE_GSR]        = "gsr.bin",
};

/* Signal type to bitmask mapping */
static const uint8_t signal_to_mask[REC_TYPE_COUNT] = {
    [REC_TYPE_PPG_WRIST]  = REC_SIGNAL_PPG_WRIST,
    [REC_TYPE_PPG_FINGER] = REC_SIGNAL_PPG_FINGER,
    [REC_TYPE_IMU_ACCEL]  = REC_SIGNAL_IMU_ACCEL,
    [REC_TYPE_IMU_GYRO]   = REC_SIGNAL_IMU_GYRO,
    [REC_TYPE_GSR]        = REC_SIGNAL_GSR,
};

/* Forward declarations */
static void rec_ctrl_thread_fn(void *, void *, void *);
static void rec_writer_thread_fn(void *, void *, void *);
static void rec_timer_thread_fn(void *, void *, void *);
static int create_session_directory(int64_t timestamp, char *path_out, size_t path_len);
static int open_signal_files(int64_t timestamp, uint8_t signal_mask);
static int close_signal_files(void);
static int flush_buffer(enum hpi_rec_signal_type signal_type, uint8_t buffer_idx);
static int write_file_headers(int64_t timestamp);
static int update_file_headers(void);

/* Thread definitions */
#define REC_CTRL_THREAD_STACKSIZE   2048  /* Needs stack for fs_mkdir, fs_open, fs_write */
#define REC_CTRL_THREAD_PRIORITY    7

#define REC_WRITER_THREAD_STACKSIZE 2048  /* Needs stack for fs_write calls */
#define REC_WRITER_THREAD_PRIORITY  9     /* Lower priority than data processing */

#define REC_TIMER_THREAD_STACKSIZE  1024  /* Needs stack for zbus_chan_pub */
#define REC_TIMER_THREAD_PRIORITY   8

K_THREAD_STACK_DEFINE(rec_ctrl_stack, REC_CTRL_THREAD_STACKSIZE);
K_THREAD_STACK_DEFINE(rec_writer_stack, REC_WRITER_THREAD_STACKSIZE);
K_THREAD_STACK_DEFINE(rec_timer_stack, REC_TIMER_THREAD_STACKSIZE);

static struct k_thread rec_ctrl_thread;
static struct k_thread rec_writer_thread;
static struct k_thread rec_timer_thread;

/* ZBus channel for recording status (declared extern in header) */
ZBUS_CHAN_DECLARE(recording_status_chan);

/*
 * Public API Implementation
 */

int hpi_recording_init(void)
{
    LOG_INF("Recording module initializing");

    /* Initialize buffers */
    for (int i = 0; i < REC_TYPE_COUNT; i++) {
        memset(&rec_buffers[i], 0, sizeof(struct rec_signal_buffer));
        fs_file_t_init(&rec_buffers[i].file);
        rec_buffers[i].sample_rate = signal_sample_rates[i];
        rec_buffers[i].sample_size = signal_sample_sizes[i];
        atomic_set(&rec_buffers[i].flush_pending, -1);
    }

    /* Initialize session state */
    memset(&current_session, 0, sizeof(current_session));
    current_session.state = REC_STATE_IDLE;

    /* Create recording base directory if it doesn't exist */
    struct fs_dirent entry;
    int ret = fs_stat(REC_BASE_PATH, &entry);
    if (ret == -ENOENT) {
        ret = fs_mkdir(REC_BASE_PATH);
        if (ret < 0 && ret != -EEXIST) {
            LOG_ERR("Failed to create recording directory: %d", ret);
            return ret;
        }
        LOG_INF("Created recording directory: %s", REC_BASE_PATH);
    }

    /* Start threads */
    k_thread_create(&rec_ctrl_thread, rec_ctrl_stack, REC_CTRL_THREAD_STACKSIZE,
                    rec_ctrl_thread_fn, NULL, NULL, NULL,
                    REC_CTRL_THREAD_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&rec_ctrl_thread, "rec_ctrl");

    k_thread_create(&rec_writer_thread, rec_writer_stack, REC_WRITER_THREAD_STACKSIZE,
                    rec_writer_thread_fn, NULL, NULL, NULL,
                    REC_WRITER_THREAD_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&rec_writer_thread, "rec_writer");

    k_thread_create(&rec_timer_thread, rec_timer_stack, REC_TIMER_THREAD_STACKSIZE,
                    rec_timer_thread_fn, NULL, NULL, NULL,
                    REC_TIMER_THREAD_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&rec_timer_thread, "rec_timer");

    LOG_INF("Recording module initialized");
    return 0;
}

int hpi_recording_configure(const struct hpi_recording_config_t *config)
{
    if (config == NULL) {
        return -EINVAL;
    }

    /* Validate parameters */
    if (config->duration_s == 0 || config->duration_s > REC_MAX_DURATION_S) {
        LOG_ERR("Invalid duration: %d (max %d)", config->duration_s, REC_MAX_DURATION_S);
        return -REC_ERR_INVALID_CONFIG;
    }

    if (config->signal_mask == 0) {
        LOG_ERR("No signals selected");
        return -REC_ERR_INVALID_CONFIG;
    }

    if ((config->signal_mask & ~REC_SIGNAL_ALL) != 0) {
        LOG_ERR("Invalid signal mask: 0x%02X", config->signal_mask);
        return -REC_ERR_INVALID_CONFIG;
    }

    k_mutex_lock(&mutex_rec_config, K_FOREVER);

    /* Check if recording is active */
    if (atomic_get(&g_recording_active)) {
        k_mutex_unlock(&mutex_rec_config);
        LOG_ERR("Cannot configure while recording");
        return -REC_ERR_ALREADY_RECORDING;
    }

    /* Store configuration */
    memcpy(&current_config, config, sizeof(current_config));
    if (current_config.sample_decimation == 0) {
        current_config.sample_decimation = 1;
    }
    config_valid = true;

    /* Update state */
    k_mutex_lock(&mutex_rec_state, K_FOREVER);
    current_session.state = REC_STATE_ARMED;
    current_session.signal_mask = config->signal_mask;
    current_session.duration_s = config->duration_s;
    k_mutex_unlock(&mutex_rec_state);

    k_mutex_unlock(&mutex_rec_config);

    LOG_INF("Recording configured: duration=%ds, signals=0x%02X, decimation=%d",
            config->duration_s, config->signal_mask, current_config.sample_decimation);

    return 0;
}

int hpi_recording_get_config(struct hpi_recording_config_t *config)
{
    if (config == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&mutex_rec_config, K_FOREVER);
    if (!config_valid) {
        k_mutex_unlock(&mutex_rec_config);
        return -REC_ERR_NOT_CONFIGURED;
    }
    memcpy(config, &current_config, sizeof(*config));
    k_mutex_unlock(&mutex_rec_config);

    return 0;
}

int hpi_recording_start(void)
{
    k_mutex_lock(&mutex_rec_state, K_FOREVER);

    if (!config_valid) {
        k_mutex_unlock(&mutex_rec_state);
        LOG_ERR("Recording not configured");
        return -REC_ERR_NOT_CONFIGURED;
    }

    if (current_session.state == REC_STATE_RECORDING) {
        k_mutex_unlock(&mutex_rec_state);
        LOG_ERR("Already recording");
        return -REC_ERR_ALREADY_RECORDING;
    }

    if (current_session.state != REC_STATE_ARMED) {
        k_mutex_unlock(&mutex_rec_state);
        LOG_ERR("Not in armed state");
        return -REC_ERR_NOT_CONFIGURED;
    }

    k_mutex_unlock(&mutex_rec_state);

    /* Signal control thread to start */
    k_sem_give(&sem_rec_start);

    LOG_INF("Recording start requested");
    return 0;
}

int hpi_recording_stop(void)
{
    if (!atomic_get(&g_recording_active)) {
        LOG_WRN("Not recording");
        return -REC_ERR_NOT_RECORDING;
    }

    /* Signal control thread to stop */
    k_sem_give(&sem_rec_stop);

    LOG_INF("Recording stop requested");
    return 0;
}

int hpi_recording_get_status(struct hpi_recording_status_t *status)
{
    if (status == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&mutex_rec_state, K_FOREVER);
    status->elapsed_s = current_session.elapsed_s;
    status->remaining_s = (current_session.duration_s > current_session.elapsed_s) ?
                          (current_session.duration_s - current_session.elapsed_s) : 0;
    status->total_s = current_session.duration_s;
    status->signal_mask = current_session.signal_mask;
    status->state = current_session.state;
    status->active = (current_session.state == REC_STATE_RECORDING);
    k_mutex_unlock(&mutex_rec_state);

    return 0;
}

bool hpi_recording_is_active(void)
{
    return atomic_get(&g_recording_active) != 0;
}

/*
 * Buffer API - Called from data_module.c
 */

static void add_samples_to_buffer(enum hpi_rec_signal_type type,
                                   const void *samples, uint16_t size)
{
    if (!atomic_get(&g_recording_active)) {
        return;
    }

    struct rec_signal_buffer *buf = &rec_buffers[type];
    uint8_t *active = (buf->active_buffer == 0) ? buf->buffer_a : buf->buffer_b;

    /* Check if adding samples would overflow */
    if (buf->write_idx + size > REC_BUFFER_SIZE) {
        /* Mark current buffer for flush */
        atomic_set(&buf->flush_pending, buf->active_buffer);

        /* Swap to other buffer */
        buf->active_buffer ^= 1;
        buf->write_idx = 0;
        active = (buf->active_buffer == 0) ? buf->buffer_a : buf->buffer_b;

        /* Signal writer thread */
        k_sem_give(&sem_rec_flush);
    }

    /* Copy samples to active buffer */
    memcpy(&active[buf->write_idx], samples, size);
    buf->write_idx += size;
}

void hpi_rec_add_ppg_wrist_samples(const uint32_t *ir, const uint32_t *red,
                                    const uint32_t *green, uint8_t num_samples)
{
    if (!(atomic_get(&g_recording_signal_mask) & REC_SIGNAL_PPG_WRIST)) {
        return;
    }

    /* Pack samples: IR, Red, Green interleaved */
    uint8_t packed[REC_SAMPLE_SIZE_PPG_WRIST * 8];  /* Max 8 samples per call */
    uint16_t offset = 0;

    for (uint8_t i = 0; i < num_samples && i < 8; i++) {
        memcpy(&packed[offset], &ir[i], sizeof(uint32_t));
        offset += sizeof(uint32_t);
        memcpy(&packed[offset], &red[i], sizeof(uint32_t));
        offset += sizeof(uint32_t);
        memcpy(&packed[offset], &green[i], sizeof(uint32_t));
        offset += sizeof(uint32_t);
    }

    add_samples_to_buffer(REC_TYPE_PPG_WRIST, packed, offset);
}

void hpi_rec_add_ppg_finger_samples(const uint32_t *ir, const uint32_t *red,
                                     uint8_t num_samples)
{
    if (!(atomic_get(&g_recording_signal_mask) & REC_SIGNAL_PPG_FINGER)) {
        return;
    }

    /* Pack samples: IR, Red interleaved */
    uint8_t packed[REC_SAMPLE_SIZE_PPG_FINGER * 32];  /* Max 32 samples per call */
    uint16_t offset = 0;

    for (uint8_t i = 0; i < num_samples && i < 32; i++) {
        memcpy(&packed[offset], &ir[i], sizeof(uint32_t));
        offset += sizeof(uint32_t);
        memcpy(&packed[offset], &red[i], sizeof(uint32_t));
        offset += sizeof(uint32_t);
    }

    add_samples_to_buffer(REC_TYPE_PPG_FINGER, packed, offset);
}

void hpi_rec_add_gsr_samples(const int32_t *samples, uint8_t num_samples)
{
    if (!(atomic_get(&g_recording_signal_mask) & REC_SIGNAL_GSR)) {
        return;
    }

    add_samples_to_buffer(REC_TYPE_GSR, samples, num_samples * sizeof(int32_t));
}

void hpi_rec_add_imu_accel_samples(const int16_t *x, const int16_t *y,
                                    const int16_t *z, uint8_t num_samples)
{
    if (!(atomic_get(&g_recording_signal_mask) & REC_SIGNAL_IMU_ACCEL)) {
        return;
    }

    /* Pack samples: X, Y, Z interleaved */
    uint8_t packed[REC_SAMPLE_SIZE_IMU_ACCEL * 16];
    uint16_t offset = 0;

    for (uint8_t i = 0; i < num_samples && i < 16; i++) {
        memcpy(&packed[offset], &x[i], sizeof(int16_t));
        offset += sizeof(int16_t);
        memcpy(&packed[offset], &y[i], sizeof(int16_t));
        offset += sizeof(int16_t);
        memcpy(&packed[offset], &z[i], sizeof(int16_t));
        offset += sizeof(int16_t);
    }

    add_samples_to_buffer(REC_TYPE_IMU_ACCEL, packed, offset);
}

void hpi_rec_add_imu_gyro_samples(const int16_t *x, const int16_t *y,
                                   const int16_t *z, uint8_t num_samples)
{
    if (!(atomic_get(&g_recording_signal_mask) & REC_SIGNAL_IMU_GYRO)) {
        return;
    }

    /* Pack samples: X, Y, Z interleaved */
    uint8_t packed[REC_SAMPLE_SIZE_IMU_GYRO * 16];
    uint16_t offset = 0;

    for (uint8_t i = 0; i < num_samples && i < 16; i++) {
        memcpy(&packed[offset], &x[i], sizeof(int16_t));
        offset += sizeof(int16_t);
        memcpy(&packed[offset], &y[i], sizeof(int16_t));
        offset += sizeof(int16_t);
        memcpy(&packed[offset], &z[i], sizeof(int16_t));
        offset += sizeof(int16_t);
    }

    add_samples_to_buffer(REC_TYPE_IMU_GYRO, packed, offset);
}

/*
 * Session Management
 */

int hpi_recording_get_session_count(void)
{
    struct fs_dir_t dir;
    struct fs_dirent entry;
    int count = 0;

    fs_dir_t_init(&dir);

    int ret = fs_opendir(&dir, REC_BASE_PATH);
    if (ret < 0) {
        LOG_ERR("Failed to open recording directory: %d", ret);
        return 0;
    }

    while (fs_readdir(&dir, &entry) == 0 && entry.name[0] != '\0') {
        if (entry.type == FS_DIR_ENTRY_DIR) {
            count++;
        }
    }

    fs_closedir(&dir);
    return count;
}

int hpi_recording_get_session_list(void)
{
    struct fs_dir_t dir;
    struct fs_dirent entry;

    fs_dir_t_init(&dir);

    int ret = fs_opendir(&dir, REC_BASE_PATH);
    if (ret < 0) {
        LOG_ERR("Failed to open recording directory: %d", ret);
        return ret;
    }

    while (fs_readdir(&dir, &entry) == 0 && entry.name[0] != '\0') {
        if (entry.type == FS_DIR_ENTRY_DIR) {
            /* Parse timestamp from directory name */
            int64_t timestamp = strtoll(entry.name, NULL, 10);
            if (timestamp > 0) {
                /* Read metadata to get duration and signals */
                uint32_t total_size = 0;
                uint16_t duration = 0;
                uint8_t signal_mask = 0;

                /* Get total size of all files in session */
                char session_path[48];
                snprintf(session_path, sizeof(session_path), "%s%s/",
                         REC_BASE_PATH, entry.name);

                struct fs_dir_t session_dir;
                struct fs_dirent file_entry;
                fs_dir_t_init(&session_dir);

                if (fs_opendir(&session_dir, session_path) == 0) {
                    while (fs_readdir(&session_dir, &file_entry) == 0 &&
                           file_entry.name[0] != '\0') {
                        if (file_entry.type == FS_DIR_ENTRY_FILE) {
                            total_size += file_entry.size;

                            /* Determine signal from filename */
                            if (strcmp(file_entry.name, "ppgw.bin") == 0) {
                                signal_mask |= REC_SIGNAL_PPG_WRIST;
                            } else if (strcmp(file_entry.name, "ppgf.bin") == 0) {
                                signal_mask |= REC_SIGNAL_PPG_FINGER;
                            } else if (strcmp(file_entry.name, "accel.bin") == 0) {
                                signal_mask |= REC_SIGNAL_IMU_ACCEL;
                            } else if (strcmp(file_entry.name, "gyro.bin") == 0) {
                                signal_mask |= REC_SIGNAL_IMU_GYRO;
                            } else if (strcmp(file_entry.name, "gsr.bin") == 0) {
                                signal_mask |= REC_SIGNAL_GSR;
                            }
                        }
                    }
                    fs_closedir(&session_dir);
                }

                /* Send index entry via BLE */
                struct hpi_recording_index_t idx = {
                    .session_timestamp = timestamp,
                    .duration_s = duration,
                    .signal_mask = signal_mask,
                    .total_size = total_size,
                };

                cmdif_send_ble_data_idx((uint8_t *)&idx, sizeof(idx));

                k_sleep(K_MSEC(50));  /* Delay between packets */
            }
        }
    }

    fs_closedir(&dir);
    return 0;
}

int hpi_recording_delete_session(int64_t timestamp)
{
    char session_path[48];
    snprintf(session_path, sizeof(session_path), "%s%" PRId64 "/",
             REC_BASE_PATH, timestamp);

    LOG_INF("Deleting session: %s", session_path);

    /* Delete all files in the session directory */
    struct fs_dir_t dir;
    struct fs_dirent entry;
    char file_path[80];

    fs_dir_t_init(&dir);

    int ret = fs_opendir(&dir, session_path);
    if (ret < 0) {
        LOG_ERR("Session not found: %d", ret);
        return -ENOENT;
    }

    while (fs_readdir(&dir, &entry) == 0 && entry.name[0] != '\0') {
        if (entry.type == FS_DIR_ENTRY_FILE) {
            snprintf(file_path, sizeof(file_path), "%s%s", session_path, entry.name);
            fs_unlink(file_path);
        }
    }

    fs_closedir(&dir);

    /* Remove the directory itself */
    /* Note: Need to remove trailing slash for rmdir */
    session_path[strlen(session_path) - 1] = '\0';
    ret = fs_unlink(session_path);
    if (ret < 0) {
        LOG_ERR("Failed to remove session directory: %d", ret);
    }

    return ret;
}

int hpi_recording_wipe_all(void)
{
    struct fs_dir_t dir;
    struct fs_dirent entry;

    fs_dir_t_init(&dir);

    int ret = fs_opendir(&dir, REC_BASE_PATH);
    if (ret < 0) {
        return ret;
    }

    while (fs_readdir(&dir, &entry) == 0 && entry.name[0] != '\0') {
        if (entry.type == FS_DIR_ENTRY_DIR) {
            int64_t timestamp = strtoll(entry.name, NULL, 10);
            if (timestamp > 0) {
                hpi_recording_delete_session(timestamp);
            }
        }
    }

    fs_closedir(&dir);
    LOG_INF("All recordings wiped");
    return 0;
}

/*
 * Internal Functions
 */

static int create_session_directory(int64_t timestamp, char *path_out, size_t path_len)
{
    snprintf(path_out, path_len, "%s%" PRId64, REC_BASE_PATH, timestamp);

    int ret = fs_mkdir(path_out);
    if (ret < 0 && ret != -EEXIST) {
        LOG_ERR("Failed to create session directory: %d", ret);
        return ret;
    }

    /* Add trailing slash for file operations */
    size_t len = strlen(path_out);
    if (len < path_len - 1) {
        path_out[len] = '/';
        path_out[len + 1] = '\0';
    }

    return 0;
}

static int open_signal_files(int64_t timestamp, uint8_t signal_mask)
{
    char session_path[48];
    char file_path[80];

    snprintf(session_path, sizeof(session_path), "%s%" PRId64 "/",
             REC_BASE_PATH, timestamp);

    for (int i = 0; i < REC_TYPE_COUNT; i++) {
        if (signal_mask & signal_to_mask[i]) {
            snprintf(file_path, sizeof(file_path), "%s%s",
                     session_path, signal_filenames[i]);

            fs_file_t_init(&rec_buffers[i].file);
            int ret = fs_open(&rec_buffers[i].file, file_path,
                              FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
            if (ret < 0) {
                LOG_ERR("Failed to open %s: %d", file_path, ret);
                return ret;
            }

            rec_buffers[i].file_open = true;
            rec_buffers[i].write_idx = 0;
            rec_buffers[i].active_buffer = 0;
            rec_buffers[i].total_samples = 0;
            atomic_set(&rec_buffers[i].flush_pending, -1);

            LOG_DBG("Opened: %s", file_path);
        }
    }

    return 0;
}

static int write_file_headers(int64_t timestamp)
{
    uint8_t signal_mask = current_session.signal_mask;

    for (int i = 0; i < REC_TYPE_COUNT; i++) {
        if ((signal_mask & signal_to_mask[i]) && rec_buffers[i].file_open) {
            struct hpi_recording_file_header_t header = {
                .magic = REC_FILE_MAGIC,
                .version = REC_FILE_VERSION,
                .signal_type = i,
                .sample_rate_hz = signal_sample_rates[i],
                .start_timestamp = timestamp,
                .num_samples = 0,  /* Will be updated at finalize */
                .reserved = {0, 0},
            };

            int ret = fs_write(&rec_buffers[i].file, &header, sizeof(header));
            if (ret < 0) {
                LOG_ERR("Failed to write header for signal %d: %d", i, ret);
                return ret;
            }
        }
    }

    return 0;
}

static int update_file_headers(void)
{
    uint8_t signal_mask = current_session.signal_mask;

    for (int i = 0; i < REC_TYPE_COUNT; i++) {
        if ((signal_mask & signal_to_mask[i]) && rec_buffers[i].file_open) {
            /* Seek to num_samples field in header */
            int ret = fs_seek(&rec_buffers[i].file,
                              offsetof(struct hpi_recording_file_header_t, num_samples),
                              FS_SEEK_SET);
            if (ret < 0) {
                LOG_ERR("Failed to seek in file for signal %d: %d", i, ret);
                continue;
            }

            /* Write updated sample count */
            uint32_t num_samples = rec_buffers[i].total_samples;
            ret = fs_write(&rec_buffers[i].file, &num_samples, sizeof(num_samples));
            if (ret < 0) {
                LOG_ERR("Failed to update header for signal %d: %d", i, ret);
            }

            LOG_INF("Signal %d: %u samples written", i, num_samples);
        }
    }

    return 0;
}

static int close_signal_files(void)
{
    for (int i = 0; i < REC_TYPE_COUNT; i++) {
        if (rec_buffers[i].file_open) {
            fs_sync(&rec_buffers[i].file);
            fs_close(&rec_buffers[i].file);
            rec_buffers[i].file_open = false;
            LOG_DBG("Closed signal %d file", i);
        }
    }

    return 0;
}

static int flush_buffer(enum hpi_rec_signal_type signal_type, uint8_t buffer_idx)
{
    struct rec_signal_buffer *buf = &rec_buffers[signal_type];

    if (!buf->file_open) {
        return -ENOENT;
    }

    uint8_t *data = (buffer_idx == 0) ? buf->buffer_a : buf->buffer_b;
    uint16_t size = REC_BUFFER_SIZE;  /* Flush full buffer */

    /* For partial buffer at end, this would need adjustment */
    int ret = fs_write(&buf->file, data, size);
    if (ret < 0) {
        LOG_ERR("Failed to write buffer for signal %d: %d", signal_type, ret);
        return ret;
    }

    /* Update sample count */
    buf->total_samples += size / buf->sample_size;

    return 0;
}

static int flush_remaining_buffers(void)
{
    uint8_t signal_mask = current_session.signal_mask;

    for (int i = 0; i < REC_TYPE_COUNT; i++) {
        if ((signal_mask & signal_to_mask[i]) && rec_buffers[i].file_open) {
            struct rec_signal_buffer *buf = &rec_buffers[i];

            /* Flush any remaining data in active buffer */
            if (buf->write_idx > 0) {
                uint8_t *data = (buf->active_buffer == 0) ? buf->buffer_a : buf->buffer_b;

                int ret = fs_write(&buf->file, data, buf->write_idx);
                if (ret < 0) {
                    LOG_ERR("Failed to flush remaining for signal %d: %d", i, ret);
                } else {
                    buf->total_samples += buf->write_idx / buf->sample_size;
                }
            }
        }
    }

    return 0;
}

/*
 * Thread Functions
 */

static void rec_ctrl_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("Recording control thread started");

    while (1) {
        /* Wait for start signal */
        k_sem_take(&sem_rec_start, K_FOREVER);

        LOG_INF("Starting recording...");

        /* Get current timestamp */
        struct tm sys_time = hpi_sys_get_sys_time();
        int64_t start_ts = timeutil_timegm64(&sys_time);

        /* Create session directory */
        char session_path[48];
        int ret = create_session_directory(start_ts, session_path, sizeof(session_path));
        if (ret < 0) {
            LOG_ERR("Failed to create session directory");
            current_session.state = REC_STATE_ERROR;
            current_session.error_code = REC_ERR_FILE_CREATE;
            continue;
        }

        /* Open signal files */
        ret = open_signal_files(start_ts, current_session.signal_mask);
        if (ret < 0) {
            LOG_ERR("Failed to open signal files");
            current_session.state = REC_STATE_ERROR;
            current_session.error_code = REC_ERR_FILE_CREATE;
            continue;
        }

        /* Write file headers */
        ret = write_file_headers(start_ts);
        if (ret < 0) {
            LOG_ERR("Failed to write file headers");
            close_signal_files();
            current_session.state = REC_STATE_ERROR;
            current_session.error_code = REC_ERR_FILE_WRITE;
            continue;
        }

        /* Update session state */
        k_mutex_lock(&mutex_rec_state, K_FOREVER);
        current_session.start_timestamp = start_ts;
        current_session.end_timestamp = 0;
        current_session.elapsed_s = 0;
        current_session.samples_written = 0;
        current_session.state = REC_STATE_RECORDING;
        current_session.error_code = REC_ERR_NONE;
        k_mutex_unlock(&mutex_rec_state);

        /* Enable recording in data path */
        atomic_set(&g_recording_signal_mask, current_session.signal_mask);
        atomic_set(&g_recording_active, 1);

        LOG_INF("Recording started: session=%" PRId64 ", signals=0x%02X, duration=%ds",
                start_ts, current_session.signal_mask, current_session.duration_s);

        /* Wait for stop signal or timeout */
        ret = k_sem_take(&sem_rec_stop, K_SECONDS(current_session.duration_s));

        /* Disable recording in data path */
        atomic_set(&g_recording_active, 0);
        atomic_set(&g_recording_signal_mask, 0);

        LOG_INF("Recording stopped, finalizing...");

        /* Update state */
        k_mutex_lock(&mutex_rec_state, K_FOREVER);
        current_session.state = REC_STATE_FINALIZING;
        k_mutex_unlock(&mutex_rec_state);

        /* Flush any pending buffers */
        while (k_sem_take(&sem_rec_flush, K_NO_WAIT) == 0) {
            /* Process pending flushes */
            for (int i = 0; i < REC_TYPE_COUNT; i++) {
                int pending = atomic_get(&rec_buffers[i].flush_pending);
                if (pending >= 0) {
                    flush_buffer(i, pending);
                    atomic_set(&rec_buffers[i].flush_pending, -1);
                }
            }
        }

        /* Flush remaining data in active buffers */
        flush_remaining_buffers();

        /* Update file headers with final sample counts */
        update_file_headers();

        /* Close all files */
        close_signal_files();

        /* Update session state */
        k_mutex_lock(&mutex_rec_state, K_FOREVER);
        sys_time = hpi_sys_get_sys_time();
        current_session.end_timestamp = timeutil_timegm64(&sys_time);
        current_session.state = REC_STATE_IDLE;
        config_valid = false;  /* Require reconfiguration for next recording */
        k_mutex_unlock(&mutex_rec_state);

        LOG_INF("Recording finalized: duration=%ds", current_session.elapsed_s);

        /* Publish final status */
        struct hpi_recording_status_t status;
        hpi_recording_get_status(&status);
        zbus_chan_pub(&recording_status_chan, &status, K_MSEC(100));
    }
}

static void rec_writer_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("Recording writer thread started");

    while (1) {
        /* Wait for flush request */
        k_sem_take(&sem_rec_flush, K_FOREVER);

        if (!atomic_get(&g_recording_active)) {
            continue;  /* Recording stopped, skip flush */
        }

        /* Check all signals for pending flushes */
        for (int i = 0; i < REC_TYPE_COUNT; i++) {
            int pending = atomic_get(&rec_buffers[i].flush_pending);
            if (pending >= 0) {
                flush_buffer(i, pending);
                atomic_set(&rec_buffers[i].flush_pending, -1);
            }
        }
    }
}

static void rec_timer_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("Recording timer thread started");

    while (1) {
        k_sleep(K_SECONDS(1));

        if (!atomic_get(&g_recording_active)) {
            continue;
        }

        /* Update elapsed time */
        k_mutex_lock(&mutex_rec_state, K_FOREVER);
        current_session.elapsed_s++;

        /* Check for duration timeout */
        if (current_session.elapsed_s >= current_session.duration_s) {
            k_mutex_unlock(&mutex_rec_state);
            k_sem_give(&sem_rec_stop);
            continue;
        }
        k_mutex_unlock(&mutex_rec_state);

        /* Publish status update via ZBus */
        struct hpi_recording_status_t status;
        hpi_recording_get_status(&status);
        zbus_chan_pub(&recording_status_chan, &status, K_MSEC(100));
    }
}
