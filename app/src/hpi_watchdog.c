/*
 * HealthyPi Move - stall detection (P0 safety net)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>

#include "hpi_watchdog.h"

LOG_MODULE_REGISTER(hpi_watchdog, CONFIG_LOG_DEFAULT_LEVEL);

#define HPI_WDT_NODE DT_NODELABEL(wdt0)

#if defined(CONFIG_TASK_WDT) && DT_NODE_HAS_STATUS(HPI_WDT_NODE, okay)
#include <zephyr/task_wdt/task_wdt.h>
#define HPI_WDT_SUPPORTED 1
#else
#define HPI_WDT_SUPPORTED 0
#endif

static bool wdt_ready;

#if HPI_WDT_SUPPORTED
static void hpi_wdt_timeout_cb(int channel_id, void *user_data)
{
	const char *name = (user_data != NULL) ? (const char *)user_data : "?";

	/* Put logging into panic (synchronous) mode so this reaches the console
	 * before the reset. */
	LOG_PANIC();
	LOG_ERR("Task watchdog timeout: '%s' (channel %d) stalled - rebooting", name, channel_id);

	sys_reboot(SYS_REBOOT_COLD);
}
#endif

int hpi_watchdog_init(void)
{
#if HPI_WDT_SUPPORTED
	const struct device *wdt = DEVICE_DT_GET(HPI_WDT_NODE);
	int ret;

	if (!device_is_ready(wdt)) {
		LOG_ERR("Watchdog device not ready; stall detection disabled");
		return -ENODEV;
	}

	ret = task_wdt_init(wdt);
	if (ret != 0) {
		LOG_ERR("task_wdt_init failed (%d); stall detection disabled", ret);
		return ret;
	}

	wdt_ready = true;
	LOG_INF("Task watchdog initialised");
	return 0;
#else
	LOG_WRN("Task watchdog unsupported (CONFIG_TASK_WDT off or wdt0 disabled)");
	return -ENOTSUP;
#endif
}

int hpi_watchdog_register(const char *name, uint32_t timeout_ms)
{
#if HPI_WDT_SUPPORTED
	int ch;

	if (!wdt_ready) {
		return -EBUSY; /* not initialised yet - caller should retry */
	}

	ch = task_wdt_add(timeout_ms, hpi_wdt_timeout_cb, (void *)name);
	if (ch < 0) {
		LOG_ERR("task_wdt_add('%s') failed (%d)", (name != NULL) ? name : "?", ch);
	} else {
		LOG_INF("Watchdog channel %d registered for '%s' (%u ms)", ch,
			(name != NULL) ? name : "?", timeout_ms);
	}
	return ch;
#else
	ARG_UNUSED(name);
	ARG_UNUSED(timeout_ms);
	return -ENOTSUP;
#endif
}

void hpi_watchdog_feed(int channel)
{
#if HPI_WDT_SUPPORTED
	if (wdt_ready && channel >= 0) {
		task_wdt_feed(channel);
	}
#else
	ARG_UNUSED(channel);
#endif
}
