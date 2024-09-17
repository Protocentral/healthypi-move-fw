#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/sys/reboot.h>

#include <zephyr/pm/device.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/device_runtime.h>

extern const struct device *display_dev;
extern const struct device *touch_dev;

void hpi_pwr_display_sleep(void)
{
#ifdef CONFIG_PM_DEVICE
    pm_device_action_run(display_dev, PM_DEVICE_ACTION_SUSPEND);
    //pm_device_action_run(touch_dev, PM_DEVICE_ACTION_SUSPEND);
#endif
}

void hpi_pwr_display_wake(void)
{
#ifdef CONFIG_PM_DEVICE
    pm_device_action_run(display_dev, PM_DEVICE_ACTION_RESUME);
    //pm_device_action_run(touch_dev, PM_DEVICE_ACTION_RESUME);
#endif
}