/*
 * HealthyPi Move
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 */

#pragma once

#include <stdint.h>

int haptic_module_init(void);
int haptic_send_alert(uint8_t value);
void haptic_process_gsr(const int32_t *samples, uint8_t num_samples, uint8_t lead_off);