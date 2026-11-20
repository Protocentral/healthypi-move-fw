#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "boot.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
	bool ok = boot_run();

	LOG_INF("Boot finished: recording %s", ok ? "available" : "NOT available");

	/* Everything else runs in threads: the UI thread (app_ui) switches to the
	 * home screen by itself, the recorder thread (acc_recorder) waits for the
	 * Start button. */
	return 0;
}