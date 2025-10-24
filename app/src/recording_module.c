/*
 * Copyright (c) 2024 Protocentral Electronics
 * SPDX-License-Identifier: Apache-2.0
 *
 * HealthyPi Move - Recording Module Implementation
 * Multi-signal continuous recording with double-buffering
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h>
#include <string.h>
#include "recording_module.h"
#include "hpi_common_types.h"
#include "hpi_sys.h"

LOG_MODULE_REGISTER(recording_module, LOG_LEVEL_DBG);

/* Signal buffer structure for double-buffering */
struct signal_buffer {
	uint8_t buf_a[REC_BUFFER_SIZE];
	uint8_t buf_b[REC_BUFFER_SIZE];
	uint8_t *active_buf;
	uint8_t *write_buf;
	size_t buf_pos;
	struct k_sem sem_buf_ready;
	bool swap_needed;
};

/* Recording context - main state structure */
struct recording_context {
	struct recording_config config;
	struct recording_status status;
	struct fs_file_t file;
	
	/* Per-signal buffers */
	struct signal_buffer ppg_wrist;
	struct signal_buffer ppg_finger;
	struct signal_buffer imu;
	struct signal_buffer gsr;
	
	/* Workers and synchronization */
	struct k_work_delayable write_work;
	struct k_work_delayable status_work;
	struct k_mutex mutex;
	
	int64_t start_time_ms;
	uint64_t start_timestamp;
	bool is_active;
	bool is_paused;
	bool initialized;
};

static struct recording_context rec_ctx;

/* Forward declarations */
static void recording_write_worker(struct k_work *work);
static void recording_status_worker(struct k_work *work);
static int swap_and_flush_buffer(struct signal_buffer *buf);
static void flush_all_buffers(void);
static int write_file_header(void);
static int update_file_header(void);
static void init_signal_buffer(struct signal_buffer *buf);

/* Helper macro for file operations */
#define CHECK_FS_OP(op) do { \
	int ret = (op); \
	if (ret < 0) { \
		LOG_ERR("FS operation failed: %s (%d)", #op, ret); \
		return ret; \
	} \
} while (0)

/**
 * Initialize a signal buffer
 */
static void init_signal_buffer(struct signal_buffer *buf)
{
	buf->active_buf = buf->buf_a;
	buf->write_buf = buf->buf_b;
	buf->buf_pos = 0;
	buf->swap_needed = false;
	k_sem_init(&buf->sem_buf_ready, 0, 1);
}

/**
 * Worker function: async file write
 */
static void recording_write_worker(struct k_work *work)
{
	if (!rec_ctx.is_active) {
		return;
	}

	k_mutex_lock(&rec_ctx.mutex, K_FOREVER);

	/* Check each buffer for pending writes */
	if (rec_ctx.ppg_wrist.swap_needed && 
	    (rec_ctx.config.signal_mask & REC_SIGNAL_PPG_WRIST)) {
		swap_and_flush_buffer(&rec_ctx.ppg_wrist);
		rec_ctx.ppg_wrist.swap_needed = false;
	}

	if (rec_ctx.ppg_finger.swap_needed && 
	    (rec_ctx.config.signal_mask & REC_SIGNAL_PPG_FINGER)) {
		swap_and_flush_buffer(&rec_ctx.ppg_finger);
		rec_ctx.ppg_finger.swap_needed = false;
	}

	if (rec_ctx.imu.swap_needed && 
	    (rec_ctx.config.signal_mask & (REC_SIGNAL_IMU_ACCEL | REC_SIGNAL_IMU_GYRO))) {
		swap_and_flush_buffer(&rec_ctx.imu);
		rec_ctx.imu.swap_needed = false;
	}

	if (rec_ctx.gsr.swap_needed && 
	    (rec_ctx.config.signal_mask & REC_SIGNAL_GSR)) {
		swap_and_flush_buffer(&rec_ctx.gsr);
		rec_ctx.gsr.swap_needed = false;
	}

	k_mutex_unlock(&rec_ctx.mutex);
}

/**
 * Worker function: periodic status update
 */
static void recording_status_worker(struct k_work *work)
{
	if (!rec_ctx.is_active) {
		return;
	}

	k_mutex_lock(&rec_ctx.mutex, K_FOREVER);

	/* Update elapsed time */
	rec_ctx.status.elapsed_ms = (uint32_t)(k_uptime_get() - rec_ctx.start_time_ms);

	/* Check if duration limit reached */
	if (rec_ctx.status.elapsed_ms >= rec_ctx.status.total_duration_ms) {
		LOG_INF("Recording duration limit reached, auto-stopping");
		k_mutex_unlock(&rec_ctx.mutex);
		recording_stop();
		return;
	}

	k_mutex_unlock(&rec_ctx.mutex);

	/* Publish status update via ZBus (if integrated) */
	// TODO: zbus_chan_pub(&recording_status_chan, &rec_ctx.status, K_NO_WAIT);

	/* Schedule next status update */
	k_work_schedule(&rec_ctx.status_work, K_MSEC(1000));
}

/**
 * Swap buffer and write to file
 */
static int swap_and_flush_buffer(struct signal_buffer *buf)
{
	uint8_t *temp = buf->active_buf;
	buf->active_buf = buf->write_buf;
	buf->write_buf = temp;
	
	size_t write_size = buf->buf_pos;
	buf->buf_pos = 0;

	if (write_size == 0) {
		return 0;
	}

	/* Write filled buffer to file */
	ssize_t written = fs_write(&rec_ctx.file, buf->write_buf, write_size);
	if (written < 0) {
		LOG_ERR("File write failed: %d", (int)written);
		rec_ctx.status.last_error = (int)written;
		rec_ctx.status.state = REC_STATE_ERROR;
		return (int)written;
	}

	rec_ctx.status.file_size_bytes += written;
	return 0;
}

/**
 * Flush all pending buffers to file
 */
static void flush_all_buffers(void)
{
	if (rec_ctx.ppg_wrist.buf_pos > 0 && 
	    (rec_ctx.config.signal_mask & REC_SIGNAL_PPG_WRIST)) {
		fs_write(&rec_ctx.file, rec_ctx.ppg_wrist.active_buf, 
		         rec_ctx.ppg_wrist.buf_pos);
		rec_ctx.status.file_size_bytes += rec_ctx.ppg_wrist.buf_pos;
	}

	if (rec_ctx.ppg_finger.buf_pos > 0 && 
	    (rec_ctx.config.signal_mask & REC_SIGNAL_PPG_FINGER)) {
		fs_write(&rec_ctx.file, rec_ctx.ppg_finger.active_buf, 
		         rec_ctx.ppg_finger.buf_pos);
		rec_ctx.status.file_size_bytes += rec_ctx.ppg_finger.buf_pos;
	}

	if (rec_ctx.imu.buf_pos > 0 && 
	    (rec_ctx.config.signal_mask & (REC_SIGNAL_IMU_ACCEL | REC_SIGNAL_IMU_GYRO))) {
		fs_write(&rec_ctx.file, rec_ctx.imu.active_buf, rec_ctx.imu.buf_pos);
		rec_ctx.status.file_size_bytes += rec_ctx.imu.buf_pos;
	}

	if (rec_ctx.gsr.buf_pos > 0 && 
	    (rec_ctx.config.signal_mask & REC_SIGNAL_GSR)) {
		fs_write(&rec_ctx.file, rec_ctx.gsr.active_buf, rec_ctx.gsr.buf_pos);
		rec_ctx.status.file_size_bytes += rec_ctx.gsr.buf_pos;
	}
}

/**
 * Write initial file header
 */
static int write_file_header(void)
{
	struct hpr_file_header hdr;
	memset(&hdr, 0, sizeof(hdr));

	hdr.magic = HPR_MAGIC;
	hdr.version = HPR_VERSION;
	hdr.header_size = sizeof(hdr);
	hdr.signal_mask = rec_ctx.config.signal_mask;
	hdr.ppg_wrist_rate_hz = rec_ctx.config.ppg_wrist_rate_hz;
	hdr.ppg_finger_rate_hz = rec_ctx.config.ppg_finger_rate_hz;
	hdr.imu_accel_rate_hz = rec_ctx.config.imu_accel_rate_hz;
	hdr.imu_gyro_rate_hz = rec_ctx.config.imu_gyro_rate_hz;
	hdr.gsr_rate_hz = rec_ctx.config.gsr_rate_hz;
	hdr.start_timestamp = rec_ctx.start_timestamp;
	
	/* Offsets will be updated when recording stops */
	hdr.ppg_wrist_offset = sizeof(hdr);
	
	strncpy(hdr.session_label, "HealthyPi Recording", sizeof(hdr.session_label) - 1);
	strncpy(hdr.device_id, "HPI-MOVE", sizeof(hdr.device_id) - 1);

	/* Write at beginning of file */
	CHECK_FS_OP(fs_seek(&rec_ctx.file, 0, FS_SEEK_SET));
	ssize_t written = fs_write(&rec_ctx.file, &hdr, sizeof(hdr));
	if (written != sizeof(hdr)) {
		LOG_ERR("Failed to write header: %d", (int)written);
		return -EIO;
	}

	return 0;
}

/**
 * Update file header with final statistics
 */
static int update_file_header(void)
{
	struct hpr_file_header hdr;
	
	/* Read existing header */
	CHECK_FS_OP(fs_seek(&rec_ctx.file, 0, FS_SEEK_SET));
	ssize_t read_bytes = fs_read(&rec_ctx.file, &hdr, sizeof(hdr));
	if (read_bytes != sizeof(hdr)) {
		LOG_ERR("Failed to read header for update: %d", (int)read_bytes);
		return -EIO;
	}

	/* Update fields */
	hdr.duration_ms = rec_ctx.status.elapsed_ms;
	hdr.total_samples = rec_ctx.status.ppg_wrist_samples + 
	                     rec_ctx.status.ppg_finger_samples +
	                     rec_ctx.status.imu_accel_samples +
	                     rec_ctx.status.imu_gyro_samples +
	                     rec_ctx.status.gsr_samples;
	hdr.ppg_wrist_count = rec_ctx.status.ppg_wrist_samples;
	hdr.ppg_finger_count = rec_ctx.status.ppg_finger_samples;
	hdr.imu_count = rec_ctx.status.imu_accel_samples;  // Combined accel+gyro
	hdr.gsr_count = rec_ctx.status.gsr_samples;

	/* Calculate CRC32 of header (excluding CRC field itself) */
	// TODO: Implement CRC32 calculation
	hdr.crc32 = 0;

	/* Write back */
	CHECK_FS_OP(fs_seek(&rec_ctx.file, 0, FS_SEEK_SET));
	ssize_t written = fs_write(&rec_ctx.file, &hdr, sizeof(hdr));
	if (written != sizeof(hdr)) {
		LOG_ERR("Failed to update header: %d", (int)written);
		return -EIO;
	}

	return 0;
}

/* ========== Public API Implementation ========== */

int recording_module_init(void)
{
	if (rec_ctx.initialized) {
		LOG_INF("Recording module already initialized");
		return 0;
	}

	LOG_INF("Initializing recording module");

	k_mutex_init(&rec_ctx.mutex);
	k_work_init_delayable(&rec_ctx.write_work, recording_write_worker);
	k_work_init_delayable(&rec_ctx.status_work, recording_status_worker);

	/* Initialize signal buffers */
	init_signal_buffer(&rec_ctx.ppg_wrist);
	init_signal_buffer(&rec_ctx.ppg_finger);
	init_signal_buffer(&rec_ctx.imu);
	init_signal_buffer(&rec_ctx.gsr);

	/* Create /lfs/rec directory if it doesn't exist */
	int ret = fs_mkdir(REC_BASE_PATH);
	if (ret < 0 && ret != -EEXIST) {
		LOG_ERR("Failed to create recording directory: %d", ret);
		return ret;
	}

	rec_ctx.status.state = REC_STATE_IDLE;
	rec_ctx.initialized = true;

	LOG_INF("Recording module initialized successfully");
	return 0;
}

int recording_start(const struct recording_config *config)
{
	if (!rec_ctx.initialized) {
		LOG_ERR("Recording module not initialized");
		return -EINVAL;
	}

	if (rec_ctx.is_active) {
		LOG_ERR("Recording already active");
		return -EBUSY;
	}

	if (!config || config->signal_mask == 0) {
		LOG_ERR("Invalid configuration");
		return -EINVAL;
	}

	LOG_INF("Starting recording: signals=0x%02x, duration=%d min",
	        config->signal_mask, config->duration_min);

	k_mutex_lock(&rec_ctx.mutex, K_FOREVER);

	/* Copy configuration */
	memcpy(&rec_ctx.config, config, sizeof(*config));

	/* Initialize status */
	memset(&rec_ctx.status, 0, sizeof(rec_ctx.status));
	rec_ctx.status.state = REC_STATE_RECORDING;
	rec_ctx.status.total_duration_ms = config->duration_min * 60 * 1000;

	/* Generate filename based on current time */
	rec_ctx.start_time_ms = k_uptime_get();
	rec_ctx.start_timestamp = (uint64_t)hw_get_sys_time_ts();
	
	snprintf(rec_ctx.status.filename, sizeof(rec_ctx.status.filename),
	         "%s%llu.hpr", REC_BASE_PATH, rec_ctx.start_timestamp);
	snprintf(rec_ctx.config.filename, sizeof(rec_ctx.config.filename),
	         "%llu.hpr", rec_ctx.start_timestamp);

	/* Open file for writing */
	fs_file_t_init(&rec_ctx.file);
	int ret = fs_open(&rec_ctx.file, rec_ctx.status.filename, 
	                  FS_O_CREATE | FS_O_RDWR);
	if (ret < 0) {
		LOG_ERR("Failed to create recording file: %d", ret);
		rec_ctx.status.state = REC_STATE_ERROR;
		rec_ctx.status.last_error = ret;
		k_mutex_unlock(&rec_ctx.mutex);
		return ret;
	}

	/* Write initial header (will be updated on stop) */
	ret = write_file_header();
	if (ret < 0) {
		fs_close(&rec_ctx.file);
		k_mutex_unlock(&rec_ctx.mutex);
		return ret;
	}

	rec_ctx.status.file_size_bytes = sizeof(struct hpr_file_header);

	/* Reset buffer positions */
	rec_ctx.ppg_wrist.buf_pos = 0;
	rec_ctx.ppg_finger.buf_pos = 0;
	rec_ctx.imu.buf_pos = 0;
	rec_ctx.gsr.buf_pos = 0;

	rec_ctx.is_active = true;
	rec_ctx.is_paused = false;

	k_mutex_unlock(&rec_ctx.mutex);

	/* Start status update worker (every 1 second) */
	k_work_schedule(&rec_ctx.status_work, K_MSEC(1000));

	LOG_INF("Recording started: %s", rec_ctx.status.filename);
	return 0;
}

int recording_stop(void)
{
	if (!rec_ctx.is_active) {
		return -EINVAL;
	}

	LOG_INF("Stopping recording");

	k_mutex_lock(&rec_ctx.mutex, K_FOREVER);

	rec_ctx.status.state = REC_STATE_STOPPING;

	/* Cancel workers */
	k_work_cancel_delayable(&rec_ctx.write_work);
	k_work_cancel_delayable(&rec_ctx.status_work);

	/* Flush all pending buffers */
	flush_all_buffers();

	/* Update header with final statistics */
	update_file_header();

	/* Sync and close file */
	fs_sync(&rec_ctx.file);
	fs_close(&rec_ctx.file);

	rec_ctx.is_active = false;
	rec_ctx.status.state = REC_STATE_IDLE;

	k_mutex_unlock(&rec_ctx.mutex);

	LOG_INF("Recording stopped. Samples: PPG_W=%u, IMU=%u, GSR=%u, Size=%u bytes",
	        rec_ctx.status.ppg_wrist_samples,
	        rec_ctx.status.imu_accel_samples,
	        rec_ctx.status.gsr_samples,
	        rec_ctx.status.file_size_bytes);

	return 0;
}

int recording_pause(void)
{
	if (!rec_ctx.is_active || rec_ctx.is_paused) {
		return -EINVAL;
	}

	k_mutex_lock(&rec_ctx.mutex, K_FOREVER);
	rec_ctx.is_paused = true;
	rec_ctx.status.state = REC_STATE_PAUSED;
	k_mutex_unlock(&rec_ctx.mutex);

	LOG_INF("Recording paused");
	return 0;
}

int recording_resume(void)
{
	if (!rec_ctx.is_active || !rec_ctx.is_paused) {
		return -EINVAL;
	}

	k_mutex_lock(&rec_ctx.mutex, K_FOREVER);
	rec_ctx.is_paused = false;
	rec_ctx.status.state = REC_STATE_RECORDING;
	k_mutex_unlock(&rec_ctx.mutex);

	LOG_INF("Recording resumed");
	return 0;
}

int recording_get_status(struct recording_status *status)
{
	if (!status) {
		return -EINVAL;
	}

	k_mutex_lock(&rec_ctx.mutex, K_FOREVER);
	
	if (rec_ctx.is_active) {
		rec_ctx.status.elapsed_ms = (uint32_t)(k_uptime_get() - rec_ctx.start_time_ms);
	}
	
	memcpy(status, &rec_ctx.status, sizeof(*status));
	k_mutex_unlock(&rec_ctx.mutex);

	return 0;
}

bool recording_is_active(void)
{
	return rec_ctx.is_active && !rec_ctx.is_paused;
}

int64_t recording_get_start_time(void)
{
	return rec_ctx.is_active ? rec_ctx.start_time_ms : 0;
}

int recording_add_ppg_sample(const struct recording_ppg_sample *sample)
{
	if (!rec_ctx.is_active || rec_ctx.is_paused || !sample) {
		return 0;
	}

	if (!(rec_ctx.config.signal_mask & REC_SIGNAL_PPG_WRIST)) {
		return 0;
	}

	k_mutex_lock(&rec_ctx.mutex, K_MSEC(10));

	size_t sample_size = sizeof(*sample);
	
	/* Check if buffer needs swapping */
	if (rec_ctx.ppg_wrist.buf_pos + sample_size > REC_BUFFER_SIZE) {
		rec_ctx.ppg_wrist.swap_needed = true;
		k_work_schedule(&rec_ctx.write_work, K_NO_WAIT);
		/* Continue using active buffer after marking for swap */
	}

	/* Add sample to active buffer if space available */
	if (rec_ctx.ppg_wrist.buf_pos + sample_size <= REC_BUFFER_SIZE) {
		memcpy(rec_ctx.ppg_wrist.active_buf + rec_ctx.ppg_wrist.buf_pos, 
		       sample, sample_size);
		rec_ctx.ppg_wrist.buf_pos += sample_size;
		rec_ctx.status.ppg_wrist_samples++;
	}

	k_mutex_unlock(&rec_ctx.mutex);
	return 0;
}

int recording_add_imu_sample(const struct recording_imu_sample *sample)
{
	if (!rec_ctx.is_active || rec_ctx.is_paused || !sample) {
		return 0;
	}

	if (!(rec_ctx.config.signal_mask & (REC_SIGNAL_IMU_ACCEL | REC_SIGNAL_IMU_GYRO))) {
		return 0;
	}

	k_mutex_lock(&rec_ctx.mutex, K_MSEC(10));

	size_t sample_size = sizeof(*sample);
	
	if (rec_ctx.imu.buf_pos + sample_size > REC_BUFFER_SIZE) {
		rec_ctx.imu.swap_needed = true;
		k_work_schedule(&rec_ctx.write_work, K_NO_WAIT);
	}

	if (rec_ctx.imu.buf_pos + sample_size <= REC_BUFFER_SIZE) {
		memcpy(rec_ctx.imu.active_buf + rec_ctx.imu.buf_pos, 
		       sample, sample_size);
		rec_ctx.imu.buf_pos += sample_size;
		rec_ctx.status.imu_accel_samples++;
	}

	k_mutex_unlock(&rec_ctx.mutex);
	return 0;
}

int recording_add_gsr_sample(const struct recording_gsr_sample *sample)
{
	if (!rec_ctx.is_active || rec_ctx.is_paused || !sample) {
		return 0;
	}

	if (!(rec_ctx.config.signal_mask & REC_SIGNAL_GSR)) {
		return 0;
	}

	k_mutex_lock(&rec_ctx.mutex, K_MSEC(10));

	size_t sample_size = sizeof(*sample);
	
	if (rec_ctx.gsr.buf_pos + sample_size > REC_BUFFER_SIZE) {
		rec_ctx.gsr.swap_needed = true;
		k_work_schedule(&rec_ctx.write_work, K_NO_WAIT);
	}

	if (rec_ctx.gsr.buf_pos + sample_size <= REC_BUFFER_SIZE) {
		memcpy(rec_ctx.gsr.active_buf + rec_ctx.gsr.buf_pos, 
		       sample, sample_size);
		rec_ctx.gsr.buf_pos += sample_size;
		rec_ctx.status.gsr_samples++;
	}

	k_mutex_unlock(&rec_ctx.mutex);
	return 0;
}

uint32_t recording_estimate_size(const struct recording_config *config)
{
	if (!config) {
		return 0;
	}

	uint32_t duration_sec = config->duration_min * 60;
	uint32_t total_size = sizeof(struct hpr_file_header);

	/* PPG wrist: 16 bytes per sample */
	if (config->signal_mask & REC_SIGNAL_PPG_WRIST) {
		total_size += config->ppg_wrist_rate_hz * duration_sec * 16;
	}

	/* PPG finger: 16 bytes per sample */
	if (config->signal_mask & REC_SIGNAL_PPG_FINGER) {
		total_size += config->ppg_finger_rate_hz * duration_sec * 16;
	}

	/* IMU: 16 bytes per sample */
	if (config->signal_mask & (REC_SIGNAL_IMU_ACCEL | REC_SIGNAL_IMU_GYRO)) {
		total_size += config->imu_accel_rate_hz * duration_sec * 16;
	}

	/* GSR: 8 bytes per sample */
	if (config->signal_mask & REC_SIGNAL_GSR) {
		total_size += config->gsr_rate_hz * duration_sec * 8;
	}

	return total_size;
}

int recording_get_available_space(uint32_t *bytes_available)
{
	if (!bytes_available) {
		return -EINVAL;
	}

	struct fs_statvfs stats;
	int ret = fs_statvfs(REC_BASE_PATH, &stats);
	if (ret < 0) {
		LOG_ERR("Failed to get filesystem stats: %d", ret);
		return ret;
	}

	*bytes_available = stats.f_bfree * stats.f_frsize;
	return 0;
}
