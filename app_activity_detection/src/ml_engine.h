/*
 * TinyML Engine — C API wrapping TensorFlow Lite Micro
 *
 * Provides a C-callable interface for loading and running TFLM models
 * from the existing C codebase. The implementation is in C++ but all
 * public symbols use C linkage.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 ProtoCentral Electronics
 */

#ifndef ML_ENGINE_H_
#define ML_ENGINE_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ml_model_id {
	ML_MODEL_TEST = 0,
	ML_MODEL_ACTIVITY,
	ML_MODEL_HR_ENHANCE,
	ML_MODEL_SLEEP_STAGE,
	ML_MODEL_COUNT,
};

struct ml_engine_config {
	size_t arena_size;
};
struct ml_tensor_info {
	int      type;
	size_t   bytes;
	float    scale;
	int32_t  zero_point;
};

int ml_engine_get_io_info(enum ml_model_id id,
			  struct ml_tensor_info *in,
			  struct ml_tensor_info *out);
/**
 * @brief Initialize the ML engine with a tensor arena.
 *
 * @param cfg Configuration (arena_size = bytes for TFLM tensor arena).
 *            Pass NULL for defaults (4 KB arena).
 * @return 0 on success, negative errno on failure.
 */
int ml_engine_init(const struct ml_engine_config *cfg);

/**
 * @brief Load a model from a memory buffer (e.g. embedded C array).
 *
 * @param id      Model slot to load into.
 * @param model   Pointer to .tflite flatbuffer data.
 * @param size    Size of the model data in bytes.
 * @return 0 on success, negative errno on failure.
 */
int ml_engine_load_model(enum ml_model_id id, const uint8_t *model,
			 size_t size);

/**
 * @brief Run inference on a loaded model.
 *
 * @param id          Model slot.
 * @param input       Input data buffer.
 * @param input_size  Size of input data in bytes.
 * @param output      Output data buffer (caller-allocated).
 * @param output_size Size of output buffer in bytes.
 * @return 0 on success, negative errno on failure.
 */
int ml_engine_invoke(enum ml_model_id id, const void *input,
		     size_t input_size, void *output, size_t output_size);


/**
 * @brief Check if a model is loaded in a given slot.
 */
bool ml_engine_is_loaded(enum ml_model_id id);

int ml_engine_load_model_from_flash(enum ml_model_id id);

extern volatile uint32_t inf_time_ms;
#ifdef __cplusplus
}
#endif

#endif /* ML_ENGINE_H_ */