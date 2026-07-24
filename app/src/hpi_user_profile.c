/*
 * HealthyPi Move — user profile helpers (height/weight/MET, step energy)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 */

#include "hpi_user_profile.h"
#include "hpi_user_settings_api.h"

uint16_t hpi_user_get_height_cm(void)
{
	return hpi_user_settings_get_height();
}

uint16_t hpi_user_get_weight_kg(void)
{
	return hpi_user_settings_get_weight();
}

double hpi_user_get_met(void)
{
	/* Walking MET used for step→energy interim estimate. */
	return 3.5;
}

uint16_t hpi_get_kcals_from_steps(uint16_t steps)
{
	/*
	 * Interim active-energy estimate until HPI_HS_T_ACTIVE_ENERGY is
	 * consistently populated for the day. Rough walking model:
	 *   kcal ≈ steps × weight_kg × 0.0005
	 * (≈ 0.035 kcal/step at 70 kg). Prefer store energy_today when the
	 * UI has a fresher summary path.
	 */
	uint16_t weight = hpi_user_get_weight_kg();

	if (weight < 30) {
		weight = 70;
	}
	if (weight > 200) {
		weight = 200;
	}

	uint32_t kcal = ((uint32_t)steps * (uint32_t)weight) / 2000u;
	if (kcal > 65535u) {
		kcal = 65535u;
	}
	return (uint16_t)kcal;
}
