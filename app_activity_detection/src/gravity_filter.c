/*
 * Adapted from STMicroelectronics filter_gravity.c (AIS - MCD Team).
 * 4th-order IIR high-pass (coefficients designed for 26 Hz, as used by ST in
 * both training and firmware), transposed direct form II.
 */
#include <math.h>
#include "gravity_filter.h"

#define N_COEF   5
#define N_STATE  (N_COEF - 1)

// Hi-pass IIR filter to separate the low-varying signal
/* For 20HZ */
static const float A[N_COEF] = {1.0, -3.83582554, 5.52081914, -3.53353522, 0.848556};
static const float B[N_COEF] = {0.92117099, -3.68468397, 5.52702596, -3.68468397, 0.92117099};

/* Steady-state init (scaled by the first sample) -> no start-up transient */
static const float Z_INIT[N_STATE] = {
	-0.936528250873f, 2.809571532101f, -2.809559172096f, 0.936515859573f
};

struct hp_filter {
	float z[N_STATE];
};

static struct hp_filter hp[3];
static int first_sample = 1;

void gravity_filter_reset(void)
{
	first_sample = 1;
}

static void hp_init(struct hp_filter *f, float first)
{
	float scale = (first == 0.0f) ? 1.0f : first;

	for (int i = 0; i < N_STATE; i++) {
		f->z[i] = scale * Z_INIT[i];
	}
}

static float hp_step(struct hp_filter *f, float x)
{
	float y = B[0] * x + f->z[0];

	for (int i = 1; i < N_STATE; i++) {
		f->z[i - 1] = f->z[i] + B[i] * x - A[i] * y;
	}
	f->z[N_STATE - 1] = B[N_STATE] * x - A[N_STATE] * y;

	return y;
}

accel3_t gravity_suppress_rotate(accel3_t in)
{
	if (first_sample) {
		hp_init(&hp[0], in.x);
		hp_init(&hp[1], in.y);
		hp_init(&hp[2], in.z);
		first_sample = 0;
	}

	/* dynamic acceleration = high-passed signal, gravity = the rest */
	float dx = hp_step(&hp[0], in.x);
	float dy = hp_step(&hp[1], in.y);
	float dz = hp_step(&hp[2], in.z);

	float gx = in.x - dx;
	float gy = in.y - dy;
	float gz = in.z - dz;

	float gm2 = gx * gx + gy * gy + gz * gz;
	accel3_t out = { dx, dy, dz };

	if (gm2 < 1e-12f) {
		return out;                      /* no gravity estimate yet */
	}

	float inv = 1.0f / sqrtf(gm2);
	gx *= inv;
	gy *= inv;
	gz *= inv;

	float s2 = 1.0f - gz * gz;
	float sin_t = (s2 > 0.0f) ? sqrtf(s2) : 0.0f;
	float cos_t = -gz;

	/* rotation axis = z x g, normalised (0 if gravity is along z) */
	float vx = 0.0f, vy = 0.0f;

	if (sin_t > 1e-6f) {
		vx = -gy / sin_t;
		vy = gx / sin_t;
	} else {
		sin_t = 0.0f;
	}

	/* Rodrigues' rotation of the dynamic acceleration */
	float vf = (vx * dx + vy * dy) * (1.0f - cos_t);

	out.x = dx * cos_t + vy * dz * sin_t + vx * vf;
	out.y = dy * cos_t - vx * dz * sin_t + vy * vf;
	out.z = dz * cos_t + (vx * dy - vy * dx) * sin_t;

	return out;
}