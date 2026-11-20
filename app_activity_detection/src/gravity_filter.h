/*
 * Gravity suppression + rotation for ST HAR models (st_gmp / st_ign).
 * Port of ST's filter_gravity.c (stm32ai-modelzoo-services, Dpu/Src) and the
 * Python training code (preprocessing.py: gravity_rotation).
 *
 * Input  : raw accelerometer in m/s^2 (what Zephyr's sensor API returns).
 *          ST's firmware scales raw data to m/s^2 before this filter.
 * Output : dynamic (gravity-free) acceleration rotated so gravity points
 *          along the z axis, same unit as the input.
 *
 * Call once per sample, in time order, at the model's sample rate.
 */
#ifndef GRAVITY_FILTER_H
#define GRAVITY_FILTER_H

typedef struct {
	float x;
	float y;
	float z;
} accel3_t;

/* Forget the filter history; next sample re-initialises the filter. */
void gravity_filter_reset(void);

accel3_t gravity_suppress_rotate(accel3_t in);

#endif /* GRAVITY_FILTER_H */