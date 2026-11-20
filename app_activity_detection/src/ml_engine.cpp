#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <tensorflow/lite/micro/micro_mutable_op_resolver.h>
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/micro/micro_log.h>
#include <tensorflow/lite/micro/system_setup.h>
#include <tensorflow/lite/schema/schema_generated.h>

#include "ml_engine.h"
#include "ml_flash.h"

#include <cmath>
#include <cstring>
#include <new>
#include <algorithm>

LOG_MODULE_REGISTER(ml_engine, CONFIG_LOG_DEFAULT_LEVEL);

#define ML_DEFAULT_ARENA_SIZE (16 * 1024)
#define ML_MAX_OPS 16
volatile uint32_t inf_time_ms;

namespace {

alignas(16) static uint8_t tensor_arena_buf[ML_DEFAULT_ARENA_SIZE];
alignas(16) static uint8_t *tensor_arena = tensor_arena_buf;
static size_t tensor_arena_size = sizeof(tensor_arena_buf);

struct ml_model_slot {
	const uint8_t *model_data;
	size_t model_size;
	const tflite::Model *model;
	tflite::MicroInterpreter *interpreter;
	bool loaded;
};

static struct ml_model_slot slots[ML_MODEL_COUNT];

/* One interpreter instance lives in static storage (no heap leak on reload).
 * One shared arena => one active model at a time. */
alignas(tflite::MicroInterpreter)
static uint8_t interp_storage[sizeof(tflite::MicroInterpreter)];

static tflite::MicroMutableOpResolver<ML_MAX_OPS> *resolver;

static struct k_mutex ml_lock;
static bool engine_initialized;

static void destroy_interpreter(struct ml_model_slot *slot)
{
	if (slot->interpreter != nullptr) {
		slot->interpreter->~MicroInterpreter();
		slot->interpreter = nullptr;
	}
	slot->loaded = false;
}

} /* namespace */

extern "C" {

int ml_engine_init(const struct ml_engine_config *cfg)
{
	if (engine_initialized) {
		return 0;
	}

	if (cfg && cfg->arena_size > sizeof(tensor_arena_buf)) {
		LOG_ERR("Requested arena %zu > static arena %zu",
			cfg->arena_size, sizeof(tensor_arena_buf));
		return -ENOMEM;
	}
	if (cfg && cfg->arena_size) {
		tensor_arena_size = cfg->arena_size;
	}

	k_mutex_init(&ml_lock);

	static tflite::MicroMutableOpResolver<ML_MAX_OPS> static_resolver;
	resolver = &static_resolver;

	const struct {
		const char *name;
		TfLiteStatus (*add_fn)(void);
	} ops[] = {
		{"FullyConnected", [](){ return resolver->AddFullyConnected(); }},
		{"Conv2D",         [](){ return resolver->AddConv2D(); }},
		{"DepthwiseConv2D",[](){ return resolver->AddDepthwiseConv2D(); }},
		{"Softmax",        [](){ return resolver->AddSoftmax(); }},
		{"ReduceMax",      [](){ return resolver->AddReduceMax(); }},
		{"Mean",           [](){ return resolver->AddMean(); }},
		{"Quantize",       [](){ return resolver->AddQuantize(); }},
		{"Dequantize",     [](){ return resolver->AddDequantize(); }},
		{"Reshape",        [](){ return resolver->AddReshape(); }},
		{"ExpandDims",     [](){ return resolver->AddExpandDims(); }},
		{"MaxPool2D",      [](){ return resolver->AddMaxPool2D(); }},
		{"Relu",           [](){ return resolver->AddRelu(); }},
		{"Pad",            [](){ return resolver->AddPad(); }},
		{"Add",            [](){ return resolver->AddAdd(); }},
	};

	for (const auto &op : ops) {
		if (op.add_fn() != kTfLiteOk) {
			LOG_ERR("Failed to add %s op", op.name);
			return -EINVAL;
		}
	}

	memset(slots, 0, sizeof(slots));

	engine_initialized = true;
	LOG_INF("ML engine initialized (arena=%zu bytes, max_ops=%d)",
		tensor_arena_size, ML_MAX_OPS);
	return 0;
}

int ml_engine_load_model(enum ml_model_id id, const uint8_t *model_data,
			 size_t size)
{
	if (!engine_initialized) {
		return -EAGAIN;
	}
	if (id >= ML_MODEL_COUNT) {
		return -EINVAL;
	}
	if (model_data == nullptr || size < 8) {
		LOG_ERR("Invalid model data: ptr=%p size=%zu", model_data, size);
		return -EINVAL;
	}

	k_mutex_lock(&ml_lock, K_FOREVER);

	struct ml_model_slot *slot = &slots[id];

	/* Drop any previous interpreter before reusing arena/storage */
	destroy_interpreter(slot);

	const tflite::Model *tfl_model = tflite::GetModel(model_data);

	if (tfl_model->version() != TFLITE_SCHEMA_VERSION) {
		LOG_ERR("Model schema version %u != supported %d",
			(unsigned)tfl_model->version(), TFLITE_SCHEMA_VERSION);
		k_mutex_unlock(&ml_lock);
		return -EINVAL;
	}

	const auto *codes = tfl_model->operator_codes();

	if (codes) {
		for (unsigned i = 0; i < codes->size(); i++) {
			const auto *c = codes->Get(i);
			int code = std::max<int>(c->deprecated_builtin_code(),
						 c->builtin_code());
         LOG_INF("Model %d opcode[%u]: %s (%d)", id, i,
			tflite::EnumNameBuiltinOperator(
				static_cast<tflite::BuiltinOperator>(code)),
			code);
		}
	}
    /* Also print the actual execution order (op by op) */
    const auto *sg = tfl_model->subgraphs()->Get(0);
    const auto *sg_ops = sg->operators();
    for (unsigned i = 0; sg_ops && i < sg_ops->size(); i++) {
        const auto *op = sg_ops->Get(i);
        const auto *c = codes->Get(op->opcode_index());
        int code = std::max<int>(c->deprecated_builtin_code(), c->builtin_code());
        LOG_INF("  op[%u] = %s", i,
            tflite::EnumNameBuiltinOperator(
                static_cast<tflite::BuiltinOperator>(code)));
    }

	tflite::MicroInterpreter *interp = new (interp_storage)
		tflite::MicroInterpreter(tfl_model, *resolver, tensor_arena,
					 tensor_arena_size);

	if (interp->AllocateTensors() != kTfLiteOk) {
		LOG_ERR("AllocateTensors failed for model %d (missing op or arena too small)",
			id);
		interp->~MicroInterpreter();
		k_mutex_unlock(&ml_lock);
		return -ENOMEM;
	}

	slot->model_data = model_data;
	slot->model_size = size;
	slot->model = tfl_model;
	slot->interpreter = interp;
	slot->loaded = true;

	LOG_INF("Model %d loaded (%zu bytes, arena used=%zu/%zu)",
		id, size, interp->arena_used_bytes(), tensor_arena_size);

	k_mutex_unlock(&ml_lock);
	return 0;
}

int ml_engine_load_model_from_flash(enum ml_model_id id)
{
	if (!engine_initialized) {
		return -EAGAIN;
	}
	if (id >= ML_MODEL_COUNT) {
		return -EINVAL;
	}

	k_mutex_lock(&ml_lock, K_FOREVER);   /* recursive: load_model locks too */

	/* 1. stop using the old model BEFORE its RAM copy is freed */
	destroy_interpreter(&slots[id]);
	ml_flash_invalidate_cache(id);

	/* 2. read the (possibly new) file from flash into RAM */
	const uint8_t *data = nullptr;
	size_t size = 0;
	int ret = ml_flash_get_model_ptr(id, &data, &size);

	if (ret < 0) {
		LOG_WRN("Model %d not available in flash: %d", id, ret);
		k_mutex_unlock(&ml_lock);
		return ret;
	}

	/* 3. build the interpreter on it */
	ret = ml_engine_load_model(id, data, size);

	k_mutex_unlock(&ml_lock);
	return ret;
}

int ml_engine_invoke(enum ml_model_id id, const void *input,
		     size_t input_size, void *output, size_t output_size)
{
	if (id >= ML_MODEL_COUNT) {
		return -EINVAL;
	}
	if (k_mutex_lock(&ml_lock, K_MSEC(500)) != 0) {
		return -EBUSY;                    /* reload in progress */
	}
	if (!slots[id].loaded) {
		k_mutex_unlock(&ml_lock);
		return -EINVAL;
	}

	tflite::MicroInterpreter *interp = slots[id].interpreter;
	TfLiteTensor *in_tensor = interp->input(0);
	TfLiteTensor *out_tensor = interp->output(0);
	int ret = 0;

	size_t in_bytes = in_tensor->bytes;

	if (input_size < in_bytes) {
		LOG_ERR("Input too small: %zu < %zu", input_size, in_bytes);
		ret = -EINVAL;
	} else {
		memcpy(in_tensor->data.data, input, in_bytes);
        uint32_t c0 = k_uptime_get();
        TfLiteStatus status = interp->Invoke();
        inf_time_ms = (uint32_t) k_uptime_get() - c0;
		//LOG_INF("inf_time : %u", inf_time_ms);
        if(status != kTfLiteOk) {
			LOG_ERR("Invoke failed for model %d", id);
			ret = -EIO;
		} else {
			size_t out_bytes = out_tensor->bytes;
			size_t copy_bytes = (output_size < out_bytes) ?
					    output_size : out_bytes;

			memcpy(output, out_tensor->data.data, copy_bytes);
		}
	}

	k_mutex_unlock(&ml_lock);
	return ret;
}

bool ml_engine_is_loaded(enum ml_model_id id)
{
	if (id >= ML_MODEL_COUNT) {
		return false;
	}
	return slots[id].loaded;
}
int ml_engine_get_io_info(enum ml_model_id id,
			  struct ml_tensor_info *in,
			  struct ml_tensor_info *out)
{
	if (id >= ML_MODEL_COUNT || !slots[id].loaded) {
		return -EINVAL;
	}
	TfLiteTensor *i = slots[id].interpreter->input(0);
	TfLiteTensor *o = slots[id].interpreter->output(0);

	if (in) {
		in->type = i->type;
		in->bytes = i->bytes;
		in->scale = i->params.scale;
		in->zero_point = i->params.zero_point;
	}
	if (out) {
		out->type = o->type;
		out->bytes = o->bytes;
		out->scale = o->params.scale;
		out->zero_point = o->params.zero_point;
	}
	return 0;
}

} /* extern "C" */