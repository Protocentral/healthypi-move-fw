#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/sys/reboot.h>

#include <zephyr/pm/device.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/device_runtime.h>

#include <zephyr/drivers/sensor/npm1300_charger.h>
#include <zephyr/dt-bindings/regulator/npm1300.h>
#include <zephyr/drivers/mfd/npm1300.h>

LOG_MODULE_REGISTER(power_ctrl);

extern const struct device *display_dev;
extern const struct device *touch_dev;

static const struct device *pmic = DEVICE_DT_GET(DT_NODELABEL(npm_pmic));
static const struct device *charger = DEVICE_DT_GET(DT_NODELABEL(npm_pmic_charger));

void hpi_pwr_display_sleep(void)
{
#ifdef CONFIG_PM_DEVICE
    pm_device_action_run(display_dev, PM_DEVICE_ACTION_SUSPEND);
    // pm_device_action_run(touch_dev, PM_DEVICE_ACTION_SUSPEND);
#endif
}

void hpi_pwr_display_wake(void)
{
#ifdef CONFIG_PM_DEVICE
    pm_device_action_run(display_dev, PM_DEVICE_ACTION_RESUME);
    // pm_device_action_run(touch_dev, PM_DEVICE_ACTION_RESUME);
#endif
}

static void pmic_event_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    printk("Event detected\n");

    if (pins & BIT(NPM1300_EVENT_VBUS_DETECTED))
    {
        printk("Vbus connected\n");
        //vbus_connected = true;
    }

    if (pins & BIT(NPM1300_EVENT_VBUS_REMOVED))
    {
        printk("Vbus removed\n");
        //vbus_connected = false;
    }
}

/* Setup callback for shiphold button press */
static struct gpio_callback pmic_event_cb;

void setup_pmic_callbacks(void)
{
    if (!device_is_ready(pmic))
    {
        printk("PMIC device not ready.\n");
        return;
    }

    gpio_init_callback(&pmic_event_cb, pmic_event_handler,
                       BIT(NPM1300_EVENT_SHIPHOLD_PRESS) | BIT(NPM1300_EVENT_SHIPHOLD_RELEASE) |
                           BIT(NPM1300_EVENT_VBUS_DETECTED) |
                           BIT(NPM1300_EVENT_VBUS_REMOVED));

    mfd_npm1300_add_callback(pmic, &pmic_event_cb);

    /* Initialise vbus detection status */
    struct sensor_value val;
    int ret = sensor_attr_get(charger, SENSOR_CHAN_CURRENT, SENSOR_ATTR_UPPER_THRESH, &val);

    if (ret < 0)
    {
        return false;
    }

    //vbus_connected = (val.val1 != 0) || (val.val2 != 0);
}