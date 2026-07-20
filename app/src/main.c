/*
 * HealthyPi Move
 * 
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Author: Ashwin Whitchurch, Protocentral Electronics
 * Contact: ashwin@protocentral.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


#include <zephyr/sys/reboot.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/fatal.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/logging/log.h>
#include <app_version.h>
#include <zephyr/sys/poweroff.h>

#include "hw_module.h"
#include "hpi_watchdog.h"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/*
 * Crash breadcrumb (P0 safety net).
 *
 * Stored in __noinit RAM so it survives the warm reboot issued by the fatal
 * handler / watchdog (RAM is retained across a soft reset). Writing flash from
 * the fatal context would be unsafe, so only this retained struct is touched.
 */
#define HPI_CRASH_MAGIC 0x48504943u /* 'HPIC' */

/* After this much continuous uptime the boot is considered stable and the
 * consecutive-fault counter is cleared (boot-loop detection). */
#define HPI_STABLE_UPTIME_MS 60000

struct hpi_crash_info {
	uint32_t magic;
	uint32_t reason;
	uint32_t count; /* consecutive fault-reboots before a stable run */
	char thread[16];
};

static __noinit struct hpi_crash_info hpi_crash;

static void hpi_mark_stable_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	if (hpi_crash.magic == HPI_CRASH_MAGIC && hpi_crash.count != 0) {
		LOG_INF("Boot stable; clearing fault counter (was %u)", hpi_crash.count);
	}
	hpi_crash.magic = HPI_CRASH_MAGIC;
	hpi_crash.count = 0;
	hpi_crash.reason = 0;
	hpi_crash.thread[0] = '\0';
}

static K_WORK_DELAYABLE_DEFINE(hpi_mark_stable_work, hpi_mark_stable_work_handler);

/* Expose the recovered-fault breadcrumb so the display can surface it as a toast
 * (the console shares USB lines with the finger sensor and is often unavailable
 * during a crash-repro). Returns false if the last boot was clean. Non-clearing:
 * the 60 s stable timer clears the record. */
bool hpi_crash_get_last(uint32_t *reason, char *thread, size_t thread_sz, uint32_t *count)
{
	if (hpi_crash.magic != HPI_CRASH_MAGIC || hpi_crash.count == 0) {
		return false;
	}
	if (reason != NULL) {
		*reason = hpi_crash.reason;
	}
	if (count != NULL) {
		*count = hpi_crash.count;
	}
	if (thread != NULL && thread_sz > 0) {
		const char *n = hpi_crash.thread[0] ? hpi_crash.thread : "?";
		strncpy(thread, n, thread_sz - 1);
		thread[thread_sz - 1] = '\0';
	}
	return true;
}

static void hpi_report_last_crash(void)
{
	if (hpi_crash.magic == HPI_CRASH_MAGIC && hpi_crash.count != 0) {
		LOG_ERR("Recovered from fault: reason=%u thread=%s consecutive=%u",
			hpi_crash.reason,
			hpi_crash.thread[0] ? hpi_crash.thread : "?",
			hpi_crash.count);
	} else {
		/* First boot (or cold/power-on): initialise the record. */
		hpi_crash.magic = HPI_CRASH_MAGIC;
		hpi_crash.count = 0;
		hpi_crash.reason = 0;
		hpi_crash.thread[0] = '\0';
	}
}

int main(void)
{
	hpi_report_last_crash();

	hw_module_init();

	LOG_INF("HealthyPi Move %d.%d.%d started!", APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_PATCHLEVEL);

	/* Start stall detection now that boot is complete. Monitored threads (lazily)
	 * register their watchdog channels once this has run. */
	hpi_watchdog_init();

	/* Consider the boot stable after continuous uptime; clears the fault counter. */
	k_work_schedule(&hpi_mark_stable_work, K_MSEC(HPI_STABLE_UPTIME_MS));

	// For power profiling. Full poweroff
	//sys_poweroff();

	return 0;
}

/*
 * Fatal error handler (P0): on an unrecoverable fault, record a breadcrumb in
 * retained RAM and cold-reboot instead of hanging. The previous behaviour
 * (no handler) left the device frozen with no recovery.
 */
void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	ARG_UNUSED(esf);

	LOG_PANIC();

	const char *name = NULL;
	struct k_thread *cur = k_current_get();

	if (cur != NULL) {
		name = k_thread_name_get(cur);
	}

	uint32_t count = (hpi_crash.magic == HPI_CRASH_MAGIC) ? hpi_crash.count : 0;

	hpi_crash.magic = HPI_CRASH_MAGIC;
	hpi_crash.reason = reason;
	hpi_crash.count = count + 1;
	hpi_crash.thread[0] = '\0';
	if (name != NULL) {
		strncpy(hpi_crash.thread, name, sizeof(hpi_crash.thread) - 1);
		hpi_crash.thread[sizeof(hpi_crash.thread) - 1] = '\0';
	}

	LOG_ERR("FATAL: reason=%u thread=%s (fault #%u) - rebooting", reason,
		name ? name : "?", hpi_crash.count);

	sys_reboot(SYS_REBOOT_COLD);

	CODE_UNREACHABLE;
}