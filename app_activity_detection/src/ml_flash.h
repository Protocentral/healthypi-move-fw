/*
 * ML Flash Model Storage — LittleFS-backed model files
 *
 * Models stored as files on a LittleFS filesystem mounted on the
 * ml-models partition (968 KB @ 0x100000). Models are read into RAM
 * at boot or on-demand via fs_read(). Upload via SMP fs_mgmt (BLE).
 *
 * File layout:
 *   /ml/test.tflite       — ML_MODEL_TEST
 *   /ml/activity_detection_model.tflite   — ML_MODEL_ACTIVITY
 *   /ml/hr_denoise.tflite — ML_MODEL_HR_ENHANCE
 *   /ml/sleep.tflite      — ML_MODEL_SLEEP_STAGE
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 ProtoCentral Electronics
 */

#ifndef ML_FLASH_H_
#define ML_FLASH_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "ml_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum model file size (128 KB — largest expected is HR denoiser ~80 KB) */
#define ML_MODEL_MAX_SIZE (128 * 1024)

/* LittleFS mount point */
#define ML_FS_MOUNT_POINT "/ml"

/**
 * @brief Initialize model storage (mount LittleFS, scan for models).
 *
 * Mounts LittleFS on the ml-models partition. Auto-formats on first
 * boot (empty/unformatted partition). Scans for .tflite files and
 * reports count.
 *
 * @return 0 on success, negative errno on failure.
 */
int ml_flash_init(void);

/**
 * @brief Get a pointer to model data loaded into RAM.
 *
 * Reads the model file from LittleFS into a per-model RAM buffer
 * (allocated on first access). The pointer remains valid until the
 * model is erased or replaced.
 *
 * @param id        Model ID to look up.
 * @param data_out  Pointer to model data in RAM (set on success).
 * @param size_out  Model size in bytes (set on success).
 * @return 0 if model found, -ENOENT if file doesn't exist, negative errno on error.
 */
int ml_flash_get_model_ptr(enum ml_model_id id, const uint8_t **data_out,
			   size_t *size_out);

/**
 * @brief Write a model file to LittleFS.
 *
 * Creates or overwrites the model file. Used by local code;
 * SMP fs_mgmt writes bypass this (direct LittleFS access).
 *
 * @param id    Model slot ID.
 * @param data  Model data (.tflite flatbuffer).
 * @param size  Size in bytes.
 * @return 0 on success, negative errno on failure.
 */
int ml_flash_write_model(enum ml_model_id id, const uint8_t *data,
			 size_t size);

/**
 * @brief Erase a model file from LittleFS.
 *
 * @param id  Model slot ID.
 * @return 0 on success, negative errno on failure.
 */
int ml_flash_erase_model(enum ml_model_id id);

/**
 * @brief Get the number of model files present in /ml/.
 */
int ml_flash_get_model_count(void);

/**
 * @brief Check if the filesystem is mounted and ready.
 */
bool ml_flash_registry_valid(void);
void ml_flash_invalidate_cache(enum ml_model_id id);
#ifdef __cplusplus
}
#endif

#endif /* ML_FLASH_H_ */