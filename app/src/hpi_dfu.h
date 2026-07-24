/*
 * HealthyPi Move — DFU (firmware-update) system mode
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * A single system-wide "a BLE/SMP firmware update is in progress" flag, set from
 * the MCUmgr img-mgmt callbacks (hpi_sys_module.c) and polled by every subsystem
 * that must get out of the way during an OTA:
 *   - the display shows a modal update screen and stays awake (smf_display.c),
 *   - the LittleFS writers (recording/trends/day-stats/health-store) stop writing
 *     `/lfs` — it shares the external QSPI die with the MCUboot secondary slot the
 *     image is landing in, so concurrent writes throttle the upload,
 *   - the sensor SMFs suspend sampling to free CPU/bus and cut power.
 *
 * A plain polled atomic (not a zbus channel) is deliberate: the flag is read from
 * many thread contexts, and polling avoids listener re-entrancy / publish-context
 * constraints. Progress is a 0..100 percent for the display.
 */

#ifndef HPI_DFU_H
#define HPI_DFU_H

#include <stdbool.h>
#include <stdint.h>

enum hpi_dfu_state {
    HPI_DFU_IDLE = 0,      /* no update in progress                          */
    HPI_DFU_ACTIVE,        /* image upload running                           */
    HPI_DFU_FINALIZING,    /* upload complete, image pending → reboot soon   */
    HPI_DFU_FAILED,        /* upload stopped/failed → resume normal operation*/
    HPI_DFU_LOW_BATTERY,   /* rejected up front: battery too low, not charging*/
};

void hpi_dfu_set_state(enum hpi_dfu_state state);
enum hpi_dfu_state hpi_dfu_get_state(void);

void hpi_dfu_set_progress(int percent);
int  hpi_dfu_get_progress(void);

/* Milliseconds since the last upload activity (state change / chunk). The display
 * fails a transfer that stalls past a timeout — the recovery path for an app that
 * drops off mid-upload without a clean DFU_STOPPED event. */
uint32_t hpi_dfu_ms_since_activity(void);

/* True while an upload is running or finalizing — the quiesce predicate for the
 * sensor SMFs and the LittleFS writers. */
bool hpi_dfu_is_active(void);

#endif /* HPI_DFU_H */
