/*
 * HealthyPi Move — ECG capture timer state
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Thread-safe running/paused state for the ECG capture, shared between the ECG
 * SMF (smf_ecg_bioz) and the HRV eval flow. Extracted from the retired legacy
 * full-screen ECG plot (scr_ecg_scr2) so that screen could be removed while
 * these helpers live on.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdbool.h>

#include "ui/move_ui.h"

LOG_MODULE_REGISTER(hpi_ecg_timer, LOG_LEVEL_INF);

K_MUTEX_DEFINE(timer_state_mutex);
static bool timer_running = false;
static bool timer_paused  = true;   // start paused, wait for lead ON
static bool lead_on_detected = false;

void hpi_ecg_timer_start(void)
{
    k_mutex_lock(&timer_state_mutex, K_FOREVER);
    timer_running = true;
    timer_paused = false;
    k_mutex_unlock(&timer_state_mutex);

    LOG_INF("ECG timer STARTED - leads detected (running=%s, paused=%s)",
            timer_running ? "true" : "false", timer_paused ? "true" : "false");
}

void hpi_ecg_timer_pause(void)
{
    k_mutex_lock(&timer_state_mutex, K_FOREVER);
    timer_paused = true;
    k_mutex_unlock(&timer_state_mutex);

    LOG_INF("ECG timer PAUSED - leads off (running=%s, paused=%s)",
            timer_running ? "true" : "false", timer_paused ? "true" : "false");
}

void hpi_ecg_timer_reset(void)
{
    k_mutex_lock(&timer_state_mutex, K_FOREVER);
    timer_running = false;
    timer_paused = true;
    lead_on_detected = false;
    k_mutex_unlock(&timer_state_mutex);

    LOG_INF("ECG timer RESET - ready for fresh start (running=%s, paused=%s)",
            timer_running ? "true" : "false", timer_paused ? "true" : "false");
}

bool hpi_ecg_timer_is_running(void)
{
    k_mutex_lock(&timer_state_mutex, K_FOREVER);
    bool is_running = timer_running && !timer_paused;
    k_mutex_unlock(&timer_state_mutex);
    return is_running;
}
