#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <math.h>

#include "gravity_filter.h"
#include "ml_engine.h"
#include "activity_model.h"    
#include "acc_recorder.h"
#include "activity.h"

LOG_MODULE_REGISTER(activity, LOG_LEVEL_INF);

#define MODEL_DATA  activity_model
#define MODEL_LEN   ACTIVITY_MODEL_LEN

#define TFLITE_TYPE_INT8    9   /* kTfLiteInt8 */

#define IN_COUNT (ACTIVITY_WINDOW_LEN * 3)

#define ACTIVITY_WINDOW_LEN  24     /* st_gmp_wl_24 */
#define ACTIVITY_NUM_AXES    3
#define ACTIVITY_NUM_CLASSES 4

#define SAMPLE_PERIOD_MS 50     /* 20 Hz (WISDM).  */
#define IN_COUNT (ACTIVITY_WINDOW_LEN * 3)

#define ACT_STACK_SIZE    8192
#define ACT_PRIORITY      6

static atomic_t res_idx  = ATOMIC_INIT(ACT_RES_NONE);
static atomic_t res_conf = ATOMIC_INIT(0);
static atomic_t res_ms   = ATOMIC_INIT(0);
static atomic_t res_seq = ATOMIC_INIT(0);

/* set once in activity_model_init() from the tensor info */
static float   in_inv_scale;
static int32_t in_zp;
static float   out_scale;
static int32_t out_zp;


static const char *const labels[ACTIVITY_NUM_CLASSES] = {
	"jogging", "stationary", "stairs", "walking"
};

static const struct device *accel = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(bmi323));

static struct ml_tensor_info in_info, out_info;

void activity_get_result(struct activity_result *r)
{
	int idx = (int)atomic_get(&res_idx);

	r->idx = idx;
	r->label = (idx >= 0 && idx < ACTIVITY_NUM_CLASSES) ? labels[idx] : NULL;
	r->conf_pct = (uint8_t)atomic_get(&res_conf);
	r->infer_ms = (uint32_t)atomic_get(&res_ms);
    r->seq = (uint32_t)atomic_get(&res_seq);
}

int activity_model_init(void)
{
	int ret = ml_engine_init(NULL);

	if (ret < 0) {
		LOG_ERR("ml_engine_init failed: %d", ret);
		return ret;
	}

	ret = ml_engine_load_model(ML_MODEL_ACTIVITY, MODEL_DATA, MODEL_LEN);
	if (ret < 0) {
		LOG_ERR("load model failed: %d", ret);
		return ret;
	}

	ret = ml_engine_get_io_info(ML_MODEL_ACTIVITY, &in_info, &out_info);
	if (ret < 0) {
		LOG_ERR("get_io_info failed: %d", ret);
		return ret;
	}

	LOG_INF("IN : type=%d bytes=%u   OUT: type=%d bytes=%u",
		in_info.type, (unsigned)in_info.bytes,
		out_info.type, (unsigned)out_info.bytes);

	if (in_info.type != TFLITE_TYPE_INT8 ||
	    out_info.type != TFLITE_TYPE_INT8) {
		LOG_ERR("Model is not int8 (in=%d out=%d)",
			in_info.type, out_info.type);
		return -EINVAL;
	}
	if (in_info.bytes != IN_COUNT * sizeof(int8_t) ||
	    out_info.bytes != ACTIVITY_NUM_CLASSES * sizeof(int8_t)) {
		LOG_ERR("Size mismatch: in=%u (want %u) out=%u (want %u)",
			(unsigned)in_info.bytes,
			(unsigned)(IN_COUNT * sizeof(int8_t)),
			(unsigned)out_info.bytes,
			(unsigned)(ACTIVITY_NUM_CLASSES * sizeof(int8_t)));
		return -EINVAL;
	}
	in_inv_scale = 1.0f / in_info.scale;
	in_zp        = in_info.zero_point;
	out_scale    = out_info.scale;
	out_zp       = out_info.zero_point;

	LOG_INF("in scale=%.6f zp=%d  out scale=%.6f zp=%d",
		(double)in_info.scale, (int)in_zp, (double)out_scale, (int)out_zp);
	return 0;
}

static inline int8_t quantize_i8(float x)
{
	int32_t q = (int32_t)lroundf(x * in_inv_scale) + in_zp;

	if (q > 127)  { q = 127;  }
	if (q < -128) { q = -128; }
	return (int8_t)q;
}

static void activity_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

	static int8_t window[IN_COUNT];
	static int8_t out[ACTIVITY_NUM_CLASSES];

    for (int i = 0; i < 100 && !device_is_ready(accel); i++) {
		k_sleep(K_MSEC(1000));
	}
	if (!device_is_ready(accel)) {
		LOG_ERR("BMI323 not ready");
		return;
	}

	/* A failed model must NOT stop sampling: the recorder depends on it */
	bool model_ok = (activity_model_init() == 0);

	if (!model_ok) {
		LOG_ERR("Model unavailable - sampling only");
	}

	gravity_filter_reset();

	int n = 0;
	struct k_timer tick;

	k_timer_init(&tick, NULL, NULL);
	k_timer_start(&tick, K_MSEC(SAMPLE_PERIOD_MS), K_MSEC(SAMPLE_PERIOD_MS));

	LOG_INF("IMU sampling at %d Hz started", ACC_SAMPLE_HZ);

	for (;;) {
		k_timer_status_sync(&tick);

		struct sensor_value v[3];

		if (sensor_sample_fetch_chan(accel, SENSOR_CHAN_ACCEL_XYZ) < 0 ||
		    sensor_channel_get(accel, SENSOR_CHAN_ACCEL_XYZ, v) < 0) {
			LOG_WRN("accel read failed");
			continue;
		}

		accel3_t raw = {
			.x = sensor_value_to_float(&v[0]),
			.y = sensor_value_to_float(&v[1]),
			.z = sensor_value_to_float(&v[2]),
		};

		/* 1) feed recorder, only while recording (never blocks) */
		if (acc_rec_get_state() == ACC_REC_RECORDING) {
			struct acc_sample s = {
				.t_ms = (uint32_t)k_uptime_get(),
				.x = raw.x, .y = raw.y, .z = raw.z,
			};

			(void)k_msgq_put(&acc_rec_q, &s, K_NO_WAIT);
		}

		/* 2) feed the model */
		if (!model_ok) {
			continue;
		}

		accel3_t p = gravity_suppress_rotate(raw);

		window[n * 3 + 0] = quantize_i8(p.x);
		window[n * 3 + 1] = quantize_i8(p.y);
		window[n * 3 + 2] = quantize_i8(p.z);
		

		if (++n < ACTIVITY_WINDOW_LEN) {
			continue;
		}
		n = 0;

		int irc = ml_engine_invoke(ML_MODEL_ACTIVITY, window, sizeof(window),
					   out, sizeof(out));
       
		if (irc < 0) {
			LOG_ERR("Inference failed");
			continue;
		}

		int best = 0;

		for (int i = 1; i < ACTIVITY_NUM_CLASSES; i++) {
			if (out[i] > out[best]) {
				best = i;
			}
		}

		float pr[ACTIVITY_NUM_CLASSES];

		for (int i = 0; i < ACTIVITY_NUM_CLASSES; i++) {
			pr[i] = ((int32_t)out[i] - out_zp) * out_scale;
		}

		atomic_set(&res_conf, (atomic_val_t)(pr[best] * 100.0f + 0.5f));
		atomic_set(&res_ms,   (atomic_val_t)inf_time_ms);
		atomic_set(&res_idx,  best);
		atomic_inc(&res_seq);

		LOG_INF("%s (%.0f%%) [%.2f %.2f %.2f %.2f]", labels[best],
			(double)(pr[best] * 100.0f),
			(double)pr[0], (double)pr[1], (double)pr[2], (double)pr[3]);
	}
}
K_THREAD_DEFINE(activity_thread_id, ACT_STACK_SIZE , activity_thread, NULL, NULL, NULL, ACT_PRIORITY, 0, 0);
