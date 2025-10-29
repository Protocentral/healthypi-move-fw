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

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

int main(void)
{
	hw_module_init();

	LOG_INF("HealthyPi Move %d.%d.%d started!", APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_PATCHLEVEL);

	// For power profiling. Full poweroff
	//sys_poweroff();

	return 0;	
}

void k_sys_fatal_error_handler(unsigned int reason, const z_arch_esf_t *esf)
{
    LOG_PANIC();

    LOG_ERR("Fatal error: %u. Rebooting...", reason);
    sys_reboot(SYS_REBOOT_COLD);

    CODE_UNREACHABLE;
}