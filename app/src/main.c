
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/reboot.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/fatal.h>

#include <zephyr/logging/log.h>

#include <app_version.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/services/hrs.h>

#include <zephyr/sys/poweroff.h>

#include "hw_module.h"

//#include "tf/main_functions.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
LOG_MODULE_REGISTER(healthypi_move, LOG_LEVEL);

//const struct device *display_dev;

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL,
		      BT_UUID_16_ENCODE(BT_UUID_HRS_VAL),
		      BT_UUID_16_ENCODE(BT_UUID_BAS_VAL),
		      BT_UUID_16_ENCODE(BT_UUID_DIS_VAL))
};


static void bt_ready(void)
{
	int err;

	LOG_DBG("Bluetooth initialized");

	err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return;
	}

	LOG_DBG("Advertising successfully started");
}

int main(void)
{
	int err=0;
	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)\n", err);
		return err;
	}

	bt_ready();

	hw_init();
	
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