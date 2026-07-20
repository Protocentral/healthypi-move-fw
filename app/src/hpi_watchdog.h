/*
 * HealthyPi Move - stall detection (P0 safety net)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Thin wrapper over the Zephyr task watchdog (hardware-backed). A monitored
 * thread registers a channel with a timeout and feeds it from its loop; if a
 * channel is not fed within its timeout the stalled thread is logged and the
 * device cold-reboots. The hardware fallback also resets the device if the
 * kernel itself hangs.
 */

#ifndef HPI_WATCHDOG_H
#define HPI_WATCHDOG_H

#include <stdint.h>

/* Initialise the hardware-backed task watchdog. Call once, after boot has
 * completed, so the watchdog does not run during the (longer, bounded) boot
 * sequence. Returns 0 on success, negative on error (stall detection then
 * stays disabled and the feed/register calls degrade to no-ops). */
int hpi_watchdog_init(void);

/* Register a monitored task. Returns a channel id (>= 0), or a negative value
 * if the watchdog is not yet initialised/available - callers should treat a
 * negative result as "not registered yet" and retry (lazy registration).
 * `name` must point to a long-lived string (used in the timeout log). */
int hpi_watchdog_register(const char *name, uint32_t timeout_ms);

/* Feed a registered channel. No-op for a negative channel or before init. */
void hpi_watchdog_feed(int channel);

#endif /* HPI_WATCHDOG_H */
