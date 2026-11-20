/*
 * ML Flash Model Storage — LittleFS implementation
 *
 * Models stored as .tflite files on LittleFS, mounted on the ml-models
 * partition (968 KB internal flash). Models are read into k_malloc'd
 * RAM buffers on demand — TFLM reads from the RAM copy during inference.
 *
 * Upload path: mcumgr fs upload (BLE → SMP gateway → fs_mgmt → LittleFS)
 * Boot path:   ml_flash_init() mounts FS, ml_flash_get_model_ptr() reads files
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 ProtoCentral Electronics
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/logging/log.h>
#include <string.h>

#if defined(CONFIG_MCUMGR_GRP_FS)
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>
#include <zephyr/mgmt/mcumgr/grp/fs_mgmt/fs_mgmt_callbacks.h>
#endif

#include "ml_flash.h"
#include "ml_engine.h"

LOG_MODULE_REGISTER(ml_flash, CONFIG_LOG_DEFAULT_LEVEL);

/* Fixed-partition ID for the ml-models partition */
#define ML_PARTITION_ID FIXED_PARTITION_ID(ml_model_partition)

/* LittleFS storage definition — binds to the ml-models partition */
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(ml_fs_cfg);

static struct fs_mount_t ml_mount = {
	.type = FS_LITTLEFS,
	.fs_data = &ml_fs_cfg,
	.storage_dev = (void *)ML_PARTITION_ID,
	.mnt_point = ML_FS_MOUNT_POINT,
};

/* Model file paths indexed by ml_model_id */
static const char *model_paths[ML_MODEL_COUNT] = {
	[ML_MODEL_TEST]       = ML_FS_MOUNT_POINT "/test.tflite",
	[ML_MODEL_ACTIVITY]   = ML_FS_MOUNT_POINT "/activity_detection_model.tflite",
	[ML_MODEL_HR_ENHANCE] = ML_FS_MOUNT_POINT "/hr_denoise.tflite",
	[ML_MODEL_SLEEP_STAGE] = ML_FS_MOUNT_POINT "/sleep.tflite",
};

/* Per-model RAM buffers (k_malloc'd on first load, freed on erase) */
static uint8_t *model_bufs[ML_MODEL_COUNT];
static size_t model_sizes[ML_MODEL_COUNT];

static bool mounted;

/**
 * @brief Count .tflite files in /ml/ directory.
 */
static int count_model_files(void)
{
	struct fs_dir_t dir;
	struct fs_dirent entry;
	int count = 0;
	int ret;

	fs_dir_t_init(&dir);
	ret = fs_opendir(&dir, ML_FS_MOUNT_POINT);
	if (ret < 0) {
		return 0;
	}

	while (fs_readdir(&dir, &entry) == 0 && entry.name[0] != '\0') {
		if (entry.type == FS_DIR_ENTRY_FILE) {
			size_t len = strlen(entry.name);
			if (len > 7 &&
			    strcmp(&entry.name[len - 7], ".tflite") == 0) {
				count++;
			}
		}
	}

	fs_closedir(&dir);
	return count;
}

int ml_flash_init(void)
{
	int ret;

	if (mounted) {
		return 0;
	}

	/* Mount LittleFS — auto-formats if partition is empty/corrupt */
	ret = fs_mount(&ml_mount);
	if (ret < 0) {
		LOG_WRN("LittleFS mount failed (%d) — formatting", ret);
		ret = fs_mkfs(FS_LITTLEFS, (uintptr_t)ML_PARTITION_ID,
			      NULL, 0);
		if (ret < 0) {
			LOG_ERR("LittleFS format failed: %d", ret);
			return ret;
		}
		ret = fs_mount(&ml_mount);
		if (ret < 0) {
			LOG_ERR("LittleFS mount after format failed: %d", ret);
			return ret;
		}
	}

	mounted = true;

	int model_count = count_model_files();
	LOG_INF("ML flash initialized (LittleFS on ml-models, models=%d)",
		model_count);

	return 0;
}

int ml_flash_get_model_ptr(enum ml_model_id id, const uint8_t **data_out,
			   size_t *size_out)
{
	struct fs_dirent dirent;
	struct fs_file_t file;
	int ret;

	if (!mounted) {
		return -EAGAIN;
	}

	if (id >= ML_MODEL_COUNT) {
		return -EINVAL;
	}

	/* If already loaded into RAM, return cached pointer */
	if (model_bufs[id] != NULL && model_sizes[id] > 0) {
		*data_out = model_bufs[id];
		*size_out = model_sizes[id];
		return 0;
	}

	/* Check if file exists */
	ret = fs_stat(model_paths[id], &dirent);
	if (ret < 0) {
		return -ENOENT;
	}

	if (dirent.size == 0 || dirent.size > ML_MODEL_MAX_SIZE) {
		LOG_ERR("Model %d invalid size: %zu", id, dirent.size);
		return -EINVAL;
	}

	/* Allocate RAM buffer for this model */
	uint8_t *buf = k_aligned_alloc(16, dirent.size);
	if (buf == NULL) {
		LOG_ERR("Failed to allocate %zu bytes for model %d",
			dirent.size, id);
		return -ENOMEM;
	}

	/* Read file into RAM */
	fs_file_t_init(&file);
	ret = fs_open(&file, model_paths[id], FS_O_READ);
	if (ret < 0) {
		LOG_ERR("Failed to open %s: %d", model_paths[id], ret);
		k_free(buf);
		return ret;
	}

	ssize_t bytes_read = fs_read(&file, buf, dirent.size);
	fs_close(&file);

	if (bytes_read != (ssize_t)dirent.size) {
		LOG_ERR("Model %d read: expected %zu got %zd",
			id, dirent.size, bytes_read);
		k_free(buf);
		return -EIO;
	}

	/* Cache the buffer */
	if (model_bufs[id] != NULL) {
		k_free(model_bufs[id]);
	}
	model_bufs[id] = buf;
	model_sizes[id] = dirent.size;

	*data_out = buf;
	*size_out = dirent.size;

	LOG_INF("Model %d loaded from %s (%zu bytes)",
		id, model_paths[id], dirent.size);

	return 0;
}

int ml_flash_write_model(enum ml_model_id id, const uint8_t *data,
			 size_t size)
{
	struct fs_file_t file;
	int ret;

	if (!mounted) {
		return -EAGAIN;
	}

	if (id >= ML_MODEL_COUNT || data == NULL || size == 0) {
		return -EINVAL;
	}

	if (size > ML_MODEL_MAX_SIZE) {
		LOG_ERR("Model %d too large: %zu bytes", id, size);
		return -ENOSPC;
	}

	fs_file_t_init(&file);
	ret = fs_open(&file, model_paths[id], FS_O_CREATE | FS_O_WRITE);
	if (ret < 0) {
		LOG_ERR("Failed to open %s for write: %d",
			model_paths[id], ret);
		return ret;
	}
    ret = fs_truncate(&file, 0);
    if (ret < 0) { fs_close(&file); return ret; }

	ssize_t written = fs_write(&file, data, size);
	fs_close(&file);

	if (written != (ssize_t)size) {
		LOG_ERR("Model %d write: expected %zu got %zd",
			id, size, written);
		return -EIO;
	}
    ml_flash_invalidate_cache(id);

	LOG_INF("Model %d written to %s (%zu bytes)", id,
		model_paths[id], size);

	return 0;
}
void ml_flash_invalidate_cache(enum ml_model_id id)
{
	if (id < ML_MODEL_COUNT && model_bufs[id] != NULL) {
		k_free(model_bufs[id]);
		model_bufs[id] = NULL;
		model_sizes[id] = 0;
	}
}
int ml_flash_erase_model(enum ml_model_id id)
{
	int ret;

	if (!mounted || id >= ML_MODEL_COUNT) {
		return -EINVAL;
	}

	ret = fs_unlink(model_paths[id]);
	if (ret < 0 && ret != -ENOENT) {
		LOG_ERR("Failed to delete %s: %d", model_paths[id], ret);
		return ret;
	}

	/* Free cached RAM buffer */
	if (model_bufs[id] != NULL) {
		k_free(model_bufs[id]);
		model_bufs[id] = NULL;
		model_sizes[id] = 0;
	}

	LOG_INF("Model %d erased (%s)", id, model_paths[id]);
	return 0;
}

int ml_flash_get_model_count(void)
{
	if (!mounted) {
		return 0;
	}
	return count_model_files();
}

bool ml_flash_registry_valid(void)
{
	return mounted;
}

/*
 * fs_mgmt hot-reload hook — triggered when SMP fs_mgmt finishes a file
 * upload. If the file is a .tflite in /ml/, invalidate the cached RAM
 * buffer and schedule a deferred reload on the system work queue.
 *
 * The reload is deferred because ml_engine_load_model_from_flash() calls
 * TFLM C++ code (AllocateTensors) which needs ~3 KB stack. The SMP work
 * queue thread that calls this hook has a small stack. The system work
 * queue (CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=4096) is sufficient.
 */
#if defined(CONFIG_MCUMGR_GRP_FS)
static int pending_reload_id = -1;

static void ml_reload_work_fn(struct k_work *work)
{
	int id = pending_reload_id;

	if (id < 0 || id >= ML_MODEL_COUNT) {
		return;
	}

	pending_reload_id = -1;

	int ret = ml_engine_load_model_from_flash(id);
	if (ret == 0) {
		LOG_INF("Model %d hot-reloaded", id);
	} else {
		LOG_ERR("Model %d hot-reload failed: %d", id, ret);
	}
}

static K_WORK_DEFINE(ml_reload_work, ml_reload_work_fn);

static enum mgmt_cb_return fs_mgmt_file_access_cb(uint32_t event,
		enum mgmt_cb_return prev_status, int32_t *rc,
		uint16_t *group, bool *abort_more, void *data,
		size_t data_size)
{
	if (event != MGMT_EVT_OP_FS_MGMT_FILE_ACCESS_DONE) {
		return MGMT_CB_OK;
	}

	const struct fs_mgmt_file_access *access = data;

	/* Only act on write completions */
	if (access->access != FS_MGMT_FILE_ACCESS_WRITE) {
		return MGMT_CB_OK;
	}

	/* Match filename against known model paths */
	for (int i = 0; i < ML_MODEL_COUNT; i++) {
		if (strcmp(access->filename, model_paths[i]) == 0) {
			LOG_INF("SMP upload complete: %s → reloading model %d",
				access->filename, i);

			/* Invalidate RAM cache so get_model_ptr re-reads */
			if (model_bufs[i] != NULL) {
				k_free(model_bufs[i]);
				model_bufs[i] = NULL;
				model_sizes[i] = 0;
			}

			/* Defer reload to system work queue (larger stack) */
			pending_reload_id = i;
			k_work_submit(&ml_reload_work);
			break;
		}
	}

	return MGMT_CB_OK;
}

static struct mgmt_callback fs_mgmt_cb = {
	.callback = fs_mgmt_file_access_cb,
	.event_id = MGMT_EVT_OP_FS_MGMT_FILE_ACCESS_DONE,
};

/* Register the hook at system init (after kernel, before application) */
static int ml_flash_register_hooks(void)
{
	mgmt_callback_register(&fs_mgmt_cb);
	return 0;
}

SYS_INIT(ml_flash_register_hooks, APPLICATION, 90);
#endif /* CONFIG_MCUMGR_GRP_FS */