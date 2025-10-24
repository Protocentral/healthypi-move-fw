/*
 * Copyright (c) 2024 Protocentral Electronics
 * SPDX-License-Identifier: Apache-2.0
 *
 * HealthyPi Move - Recording Module
 * Multi-signal continuous recording (PPG, IMU, GSR)
 */

#ifndef RECORDING_MODULE_H
#define RECORDING_MODULE_H

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>

/* Signal selection bitmask */
#define REC_SIGNAL_PPG_WRIST   BIT(0)  // 0x01
#define REC_SIGNAL_PPG_FINGER  BIT(1)  // 0x02
#define REC_SIGNAL_IMU_ACCEL   BIT(2)  // 0x04
#define REC_SIGNAL_IMU_GYRO    BIT(3)  // 0x08
#define REC_SIGNAL_GSR         BIT(4)  // 0x10

/* Recording constants */
#define REC_BUFFER_SIZE        4096    // 4KB per signal buffer
#define HPR_MAGIC              0x48505221  // "HPR!"
#define HPR_VERSION            1
#define REC_BASE_PATH          "/lfs/rec/"
#define REC_MAX_FILENAME       64

/* Recording states */
enum recording_state {
	REC_STATE_IDLE = 0,
	REC_STATE_CONFIGURING,
	REC_STATE_RECORDING,
	REC_STATE_PAUSED,
	REC_STATE_STOPPING,
	REC_STATE_ERROR
};

/* Configuration structure */
struct recording_config {
	uint8_t signal_mask;              // Bitmask of REC_SIGNAL_* flags
	uint16_t duration_min;            // 1-120 minutes
	
	/* Per-signal sampling rates */
	uint16_t ppg_wrist_rate_hz;       // Default: 128
	uint16_t ppg_finger_rate_hz;      // Default: 128
	uint16_t imu_accel_rate_hz;       // Default: 100
	uint16_t imu_gyro_rate_hz;        // Default: 100
	uint16_t gsr_rate_hz;             // Default: 25
	
	char filename[REC_MAX_FILENAME];  // Auto-generated
} __packed;

/* Status structure (published to ZBus) */
struct recording_status {
	enum recording_state state;
	uint32_t elapsed_ms;
	uint32_t total_duration_ms;
	
	/* Sample counts */
	uint32_t ppg_wrist_samples;
	uint32_t ppg_finger_samples;
	uint32_t imu_accel_samples;
	uint32_t imu_gyro_samples;
	uint32_t gsr_samples;
	
	/* File info */
	uint32_t file_size_bytes;
	uint32_t estimated_size_bytes;
	int last_error;
	char filename[REC_MAX_FILENAME];
} __packed;

/* Sample structures */
struct recording_ppg_sample {
	uint32_t timestamp_ms;
	uint32_t red;
	uint32_t ir;
	uint32_t green;
} __packed;

struct recording_imu_sample {
	uint32_t timestamp_ms;
	int16_t accel_x, accel_y, accel_z;
	int16_t gyro_x, gyro_y, gyro_z;
} __packed;

struct recording_gsr_sample {
	uint32_t timestamp_ms;
	uint32_t resistance;
} __packed;

/* File header structure - 128 bytes aligned */
struct hpr_file_header {
	uint32_t magic;                   // 0x48505221 ("HPR!")
	uint16_t version;                 // HPR_VERSION
	uint16_t header_size;             // sizeof(struct hpr_file_header)
	
	/* Recording metadata */
	uint8_t signal_mask;              // Active signals bitmask
	uint8_t reserved1;
	uint16_t ppg_wrist_rate_hz;
	uint16_t ppg_finger_rate_hz;
	uint16_t imu_accel_rate_hz;
	uint16_t imu_gyro_rate_hz;
	uint16_t gsr_rate_hz;
	uint16_t reserved2;
	
	uint64_t start_timestamp;         // Unix timestamp (seconds)
	uint32_t duration_ms;             // Actual recording duration
	uint32_t total_samples;           // Total samples across all channels
	
	/* File offsets to data sections */
	uint32_t ppg_wrist_offset;
	uint32_t ppg_wrist_count;
	uint32_t ppg_finger_offset;
	uint32_t ppg_finger_count;
	uint32_t imu_offset;
	uint32_t imu_count;
	uint32_t gsr_offset;
	uint32_t gsr_count;
	
	/* User metadata */
	char session_label[32];
	char device_id[16];
	
	/* Reserved for future use */
	uint8_t reserved[20];
	
	uint32_t crc32;                   // CRC32 of header (excluding this field)
} __packed;

/* Public API */

/**
 * @brief Initialize recording module
 * @return 0 on success, negative errno on error
 */
int recording_module_init(void);

/**
 * @brief Start a new recording
 * @param config Recording configuration
 * @return 0 on success, negative errno on error
 */
int recording_start(const struct recording_config *config);

/**
 * @brief Stop active recording
 * @return 0 on success, negative errno on error
 */
int recording_stop(void);

/**
 * @brief Pause active recording
 * @return 0 on success, negative errno on error
 */
int recording_pause(void);

/**
 * @brief Resume paused recording
 * @return 0 on success, negative errno on error
 */
int recording_resume(void);

/**
 * @brief Get current recording status
 * @param status Pointer to status structure to fill
 * @return 0 on success, negative errno on error
 */
int recording_get_status(struct recording_status *status);

/**
 * @brief Check if recording is currently active
 * @return true if recording, false otherwise
 */
bool recording_is_active(void);

/**
 * @brief Get recording start time (for relative timestamps)
 * @return Start time in milliseconds since boot, or 0 if not recording
 */
int64_t recording_get_start_time(void);

/**
 * @brief Add a PPG sample to the recording
 * @param sample PPG sample data
 * @return 0 on success, negative errno on error
 */
int recording_add_ppg_sample(const struct recording_ppg_sample *sample);

/**
 * @brief Add an IMU sample to the recording
 * @param sample IMU sample data
 * @return 0 on success, negative errno on error
 */
int recording_add_imu_sample(const struct recording_imu_sample *sample);

/**
 * @brief Add a GSR sample to the recording
 * @param sample GSR sample data
 * @return 0 on success, negative errno on error
 */
int recording_add_gsr_sample(const struct recording_gsr_sample *sample);

/**
 * @brief Estimate file size for a given configuration
 * @param config Recording configuration
 * @return Estimated file size in bytes
 */
uint32_t recording_estimate_size(const struct recording_config *config);

/**
 * @brief Get available storage space for recordings
 * @param bytes_available Pointer to store available bytes
 * @return 0 on success, negative errno on error
 */
int recording_get_available_space(uint32_t *bytes_available);

#endif /* RECORDING_MODULE_H */
