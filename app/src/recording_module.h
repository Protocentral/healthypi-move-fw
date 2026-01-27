/*
 * HealthyPi Move - Recording Module Header
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 */

#pragma once

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>

/* Signal selection bitmask */
#define REC_SIGNAL_PPG_WRIST    BIT(0)  /* Wrist PPG (IR, Red, Green @ 25 Hz) */
#define REC_SIGNAL_PPG_FINGER   BIT(1)  /* Finger PPG (IR, Red @ 25 Hz) */
#define REC_SIGNAL_IMU_ACCEL    BIT(2)  /* 3-axis accelerometer @ 100 Hz */
#define REC_SIGNAL_IMU_GYRO     BIT(3)  /* 3-axis gyroscope @ 100 Hz */
#define REC_SIGNAL_GSR          BIT(4)  /* Galvanic skin response @ 32 Hz */

#define REC_SIGNAL_ALL          (REC_SIGNAL_PPG_WRIST | REC_SIGNAL_PPG_FINGER | \
                                 REC_SIGNAL_IMU_ACCEL | REC_SIGNAL_IMU_GYRO | \
                                 REC_SIGNAL_GSR)

/* Signal type identifiers for file headers */
enum hpi_rec_signal_type {
    REC_TYPE_PPG_WRIST = 0,
    REC_TYPE_PPG_FINGER = 1,
    REC_TYPE_IMU_ACCEL = 2,
    REC_TYPE_IMU_GYRO = 3,
    REC_TYPE_GSR = 4,
    REC_TYPE_COUNT = 5,
};

/* Recording states */
enum hpi_recording_state {
    REC_STATE_IDLE = 0,
    REC_STATE_ARMED,        /* Configured, waiting for start */
    REC_STATE_RECORDING,    /* Active recording */
    REC_STATE_FINALIZING,   /* Flushing buffers, closing files */
    REC_STATE_ERROR,
};

/* Error codes */
enum hpi_recording_error {
    REC_ERR_NONE = 0,
    REC_ERR_STORAGE_FULL,       /* LittleFS partition full */
    REC_ERR_FILE_CREATE,        /* Failed to create recording file */
    REC_ERR_FILE_WRITE,         /* Failed to write data */
    REC_ERR_INVALID_CONFIG,     /* Invalid configuration parameters */
    REC_ERR_SENSOR_UNAVAILABLE, /* Required sensor not available */
    REC_ERR_BUFFER_OVERFLOW,    /* Data coming faster than write speed */
    REC_ERR_NOT_CONFIGURED,     /* Start called without configure */
    REC_ERR_ALREADY_RECORDING,  /* Start called while already recording */
    REC_ERR_NOT_RECORDING,      /* Stop called while not recording */
};

/* Recording configuration */
struct hpi_recording_config_t {
    uint16_t duration_s;        /* Recording duration in seconds (max 3600 = 1 hour) */
    uint8_t  signal_mask;       /* Bitmask of REC_SIGNAL_* flags */
    uint8_t  sample_decimation; /* Decimation factor (1 = full rate, 2 = half, etc.) */
};

/* Recording session metadata */
struct hpi_recording_session_t {
    int64_t  start_timestamp;   /* Unix timestamp when recording started */
    int64_t  end_timestamp;     /* Unix timestamp when recording ended (0 if active) */
    uint16_t duration_s;        /* Configured duration */
    uint16_t elapsed_s;         /* Current elapsed seconds */
    uint8_t  signal_mask;       /* Active signals */
    uint8_t  state;             /* enum hpi_recording_state */
    uint32_t samples_written;   /* Total samples written across all signals */
    uint8_t  error_code;        /* Error code if state == REC_STATE_ERROR */
};

/* Recording status for ZBus/UI (low-frequency updates) */
struct hpi_recording_status_t {
    uint16_t elapsed_s;         /* Seconds since recording start */
    uint16_t remaining_s;       /* Seconds remaining */
    uint16_t total_s;           /* Total configured duration */
    uint8_t  signal_mask;       /* Active signals being recorded */
    uint8_t  state;             /* Current recording state */
    bool     active;            /* Quick check if recording is active */
};

/* File format header - 32 bytes */
#define REC_FILE_MAGIC      0x48504952  /* "HPIR" - HealthyPi Recording */
#define REC_FILE_VERSION    1

struct hpi_recording_file_header_t {
    uint32_t magic;             /* REC_FILE_MAGIC */
    uint8_t  version;           /* File format version */
    uint8_t  signal_type;       /* Which signal this file contains */
    uint16_t sample_rate_hz;    /* Sample rate for this signal */
    int64_t  start_timestamp;   /* Unix timestamp */
    uint32_t num_samples;       /* Total samples in file (updated at finalize) */
    uint32_t reserved[2];       /* Future use */
} __packed;

#define REC_FILE_HEADER_SIZE 32

/* Session index entry for BLE listing */
struct hpi_recording_index_t {
    int64_t  session_timestamp; /* Session start timestamp */
    uint16_t duration_s;        /* Recording duration */
    uint8_t  signal_mask;       /* Signals recorded */
    uint32_t total_size;        /* Total file size in bytes */
} __packed;

#define REC_INDEX_SIZE 15

struct hpi_storage_info_t {
    uint64_t total_bytes;      /* Total storage capacity */
    uint64_t used_bytes;       /* Bytes currently used */
    uint64_t free_bytes;       /* Bytes available */
    uint64_t available_bytes;  /* Bytes available to application */
    uint32_t percent_used;     /* Usage percentage 0-100 */
    bool     is_low_space;     /* Flag when space is low */
};

/* Sample rate definitions */
#define REC_SAMPLE_RATE_PPG_WRIST   25
#define REC_SAMPLE_RATE_PPG_FINGER  25
#define REC_SAMPLE_RATE_IMU_ACCEL   100
#define REC_SAMPLE_RATE_IMU_GYRO    100
#define REC_SAMPLE_RATE_GSR         32

/* Sample sizes in bytes */
#define REC_SAMPLE_SIZE_PPG_WRIST   12  /* 3 x uint32 (IR, Red, Green) */
#define REC_SAMPLE_SIZE_PPG_FINGER  8   /* 2 x uint32 (IR, Red) */
#define REC_SAMPLE_SIZE_IMU_ACCEL   6   /* 3 x int16 (X, Y, Z) */
#define REC_SAMPLE_SIZE_IMU_GYRO    6   /* 3 x int16 (X, Y, Z) */
#define REC_SAMPLE_SIZE_GSR         4   /* 1 x int32 */

/* Buffer configuration - reduced to save RAM (5 signals x 2 buffers x size) */
#define REC_BUFFER_SIZE  1024  /* 1KB per buffer (10KB total for all signals) */

/* Maximum recording duration in seconds */
#define REC_MAX_DURATION_S  3600  /* 1 hour */

/* Recording directory path */
#define REC_BASE_PATH "/lfs/rec/"

/*
 * Public API
 */

/* Configuration */
int hpi_recording_configure(const struct hpi_recording_config_t *config);
int hpi_recording_get_config(struct hpi_recording_config_t *config);

/* Control */
int hpi_recording_start(void);
int hpi_recording_stop(void);

/* Status */
int hpi_recording_get_status(struct hpi_recording_status_t *status);
bool hpi_recording_is_active(void);

/* Fast inline check for data_thread - no mutex, uses atomic */
static inline bool hpi_recording_is_signal_enabled(uint8_t signal);

/* Buffer API (called from data_module.c) */
void hpi_rec_add_ppg_wrist_samples(const uint32_t *ir, const uint32_t *red,
                                    const uint32_t *green, uint8_t num_samples);
void hpi_rec_add_ppg_finger_samples(const uint32_t *ir, const uint32_t *red,
                                     uint8_t num_samples);
void hpi_rec_add_gsr_samples(const int32_t *samples, uint8_t num_samples);
void hpi_rec_add_imu_accel_samples(const int16_t *x, const int16_t *y,
                                    const int16_t *z, uint8_t num_samples);
void hpi_rec_add_imu_gyro_samples(const int16_t *x, const int16_t *y,
                                   const int16_t *z, uint8_t num_samples);

/* Session management */
int hpi_recording_get_session_count(void);
int hpi_recording_get_session_list(void);  /* Sends via BLE */
int hpi_recording_delete_session(int64_t timestamp);
int hpi_recording_wipe_all(void);

/* Module initialization (called from main) */
int hpi_recording_init(void);

/*
 * External declarations for atomic state flags
 * These are defined in recording_module.c and used for fast path checks
 */
extern atomic_t g_recording_active;
extern atomic_t g_recording_signal_mask;

/* Inline implementation for fast path check */
static inline bool hpi_recording_is_signal_enabled(uint8_t signal)
{
    if (!atomic_get(&g_recording_active)) {
        return false;
    }
    return (atomic_get(&g_recording_signal_mask) & signal) != 0;
}
