/*
 * HealthyPi Move — DFU (firmware-update) system mode
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * See hpi_dfu.h. State + progress are plain atomics: set from the MCUmgr img-mgmt
 * callback context, read from the display / sensor / filesystem threads.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include "hpi_dfu.h"

static atomic_t s_state    = ATOMIC_INIT(HPI_DFU_IDLE);
static atomic_t s_progress = ATOMIC_INIT(0);
/* k_uptime_get_32() of the last upload activity (state change / chunk). The
 * display uses it to fail a stalled transfer when the app drops off without a
 * clean DFU_STOPPED (a raw BLE disconnect mid-upload emits no img-mgmt event). */
static atomic_t s_last_activity_ms = ATOMIC_INIT(0);

void hpi_dfu_set_state(enum hpi_dfu_state state)
{
    atomic_set(&s_last_activity_ms, (atomic_val_t)k_uptime_get_32());
    atomic_set(&s_state, (atomic_val_t)state);
}

enum hpi_dfu_state hpi_dfu_get_state(void)
{
    return (enum hpi_dfu_state)atomic_get(&s_state);
}

void hpi_dfu_set_progress(int percent)
{
    if (percent < 0) {
        percent = 0;
    } else if (percent > 100) {
        percent = 100;
    }
    atomic_set(&s_last_activity_ms, (atomic_val_t)k_uptime_get_32());
    atomic_set(&s_progress, percent);
}

int hpi_dfu_get_progress(void)
{
    return (int)atomic_get(&s_progress);
}

uint32_t hpi_dfu_ms_since_activity(void)
{
    return k_uptime_get_32() - (uint32_t)atomic_get(&s_last_activity_ms);
}

bool hpi_dfu_is_active(void)
{
    atomic_val_t st = atomic_get(&s_state);
    return st == HPI_DFU_ACTIVE || st == HPI_DFU_FINALIZING;
}
