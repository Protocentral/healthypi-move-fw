#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/rtc.h>

#include <zephyr/dfu/mcuboot.h>
#include <stdio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/fuel_gauge.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/usbd.h>

#include <zephyr/drivers/sensor/npm1300_charger.h>
#include <zephyr/dt-bindings/regulator/npm1300.h>
#include <zephyr/drivers/mfd/npm1300.h>
#include <zephyr/drivers/regulator.h>

#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/settings/settings.h>
#include <zephyr/fs/fs.h>

#include <zephyr/sys/reboot.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/device_runtime.h>

#include <time.h>
#include <zephyr/posix/time.h>
#include <zephyr/sys/timeutil.h>

#include <nrfx_clock.h>
#include <nrfx_spim.h>

#include "max30001.h"
#include "max32664d.h"
#include "bmi323_hpi.h"
#include "max32664c.h"
#include "nrf_fuel_gauge.h"
#include "display_sh8601.h"

#include "hw_module.h"
#include "fs_module.h"
#include "ui/move_ui.h"
#include "hpi_common_types.h"
#include "ble_module.h"
#include "hpi_sys.h"
#include "hpi_user_settings_api.h"

#include <max32664_updater.h>

LOG_MODULE_REGISTER(hw_module, LOG_LEVEL_DBG);

// Battery cutoff thresholds - voltage based (typical Li-ion voltages)
// These values can be adjusted based on the specific battery characteristics
#define HPI_BATTERY_CRITICAL_VOLTAGE 3.2f // Show critical low battery screen (V)
#define HPI_BATTERY_SHUTDOWN_VOLTAGE 3.0f // Auto shutdown level (V) - prevents over-discharge
#define HPI_BATTERY_RECOVERY_VOLTAGE 3.4f // Recovery threshold when charging (V) - allows hysteresis

char curr_string[40];

// Peripheral Device Pointers
static const struct device *max30208_dev = DEVICE_DT_GET_ANY(maxim_max30208);

const struct device *max32664d_dev = DEVICE_DT_GET_ANY(maxim_max32664);
const struct device *max32664c_dev = DEVICE_DT_GET_ANY(maxim_max32664c);
const struct device *imu_dev = DEVICE_DT_GET(DT_NODELABEL(bmi323));
const struct device *const max30001_dev = DEVICE_DT_GET(DT_ALIAS(max30001));
const struct device *rtc_dev = DEVICE_DT_GET(DT_ALIAS(rtc));
const struct device *usb_cdc_uart_dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);
const struct device *const gpio_keys_dev = DEVICE_DT_GET(DT_NODELABEL(gpiokeys));
const struct device *const w25_flash_dev = DEVICE_DT_GET(DT_NODELABEL(w25q01jv));

// PMIC Device Pointers
static const struct device *regulators = DEVICE_DT_GET(DT_NODELABEL(npm_pmic_regulators));
static const struct device *ldsw_disp_unit = DEVICE_DT_GET(DT_NODELABEL(npm_pmic_ldo1));
static const struct device *ldsw_sens_1_8 = DEVICE_DT_GET(DT_NODELABEL(npm_pmic_ldo2));
static const struct device *charger = DEVICE_DT_GET(DT_NODELABEL(npm_pmic_charger));
static const struct device *pmic = DEVICE_DT_GET(DT_NODELABEL(npm_pmic));

const struct device *display_dev = DEVICE_DT_GET(DT_NODELABEL(sh8601)); // DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
const struct device *touch_dev = DEVICE_DT_GET_ONE(chipsemi_chsc5816);

// LED Power DC/DC Enable
static const struct gpio_dt_spec dcdc_5v_en = GPIO_DT_SPEC_GET(DT_NODELABEL(sensor_dcdc_en), gpios);

volatile bool max30001_device_present = false;
volatile bool max32664c_device_present = false;
volatile bool max32664d_device_present = false;

static volatile bool vbus_connected;
static int64_t ref_time;

// Low battery state tracking
static bool low_battery_screen_active = false;
static bool critical_battery_notified = false;
static uint32_t low_battery_last_update = 0;
static uint8_t last_battery_level = 100;  // Store last known battery level
static float last_battery_voltage = 4.2f; // Store last known battery voltage

// USB CDC UART
#define RING_BUF_SIZE 1024
uint8_t ring_buffer[RING_BUF_SIZE];
struct ring_buf ringbuf_usb_cdc;
static bool rx_throttled;

K_SEM_DEFINE(sem_hw_inited, 0, 1);
K_SEM_DEFINE(sem_start_cal, 0, 1);

// Signals to start dependent threads
K_SEM_DEFINE(sem_disp_smf_start, 0, 1);
K_SEM_DEFINE(sem_imu_smf_start, 0, 1);
K_SEM_DEFINE(sem_ecg_bioz_sm_start, 0, 1);
K_SEM_DEFINE(sem_ppg_wrist_sm_start, 0, 2);
K_SEM_DEFINE(sem_ppg_finger_sm_start, 0, 1);
K_SEM_DEFINE(sem_hw_thread_start, 0, 1);
K_SEM_DEFINE(sem_ble_thread_start, 0, 1);
K_SEM_DEFINE(sem_hpi_sys_thread_start, 0, 1);

K_SEM_DEFINE(sem_disp_boot_complete, 0, 1);
K_SEM_DEFINE(sem_crown_key_pressed, 0, 1);

K_SEM_DEFINE(sem_boot_update_req, 0, 1);

ZBUS_CHAN_DECLARE(sys_time_chan, batt_chan);
ZBUS_CHAN_DECLARE(steps_chan);
ZBUS_CHAN_DECLARE(temp_chan);

static const struct battery_model battery_model = {
#include "battery_profile_200.inc"
};

static float max_charge_current;
static float term_charge_current;

static uint16_t today_total_steps = 0;
K_MUTEX_DEFINE(mutex_today_steps);

static struct hpi_version_desc_t hpi_max32664c_req_ver = {
    .major = 13,
    .minor = 31,
};

static struct hpi_version_desc_t hpi_max32664d_req_ver = {
    .major = 6,
    .minor = 0,
};

/*******EXTERNS******/
extern struct k_msgq q_session_cmd_msg;
extern struct k_sem sem_disp_ready;
extern struct k_msgq q_disp_boot_msg;

extern struct k_msgq q_steps_trend;

static int today_add_steps(uint16_t steps)
{
    k_mutex_lock(&mutex_today_steps, K_FOREVER);
    today_total_steps += steps;
    k_mutex_unlock(&mutex_today_steps);

    return 0;
}

static uint16_t today_get_steps(void)
{
    uint16_t steps = 0;
    k_mutex_lock(&mutex_today_steps, K_FOREVER);
    steps = today_total_steps;
    k_mutex_unlock(&mutex_today_steps);

    return steps;
}

void today_init_steps(uint16_t steps)
{
    k_mutex_lock(&mutex_today_steps, K_FOREVER);
    today_total_steps = steps;
    k_mutex_unlock(&mutex_today_steps);
}

// Low battery management functions
bool hw_is_low_battery(void)
{
    return low_battery_screen_active;
}

bool hw_is_critical_battery(void)
{
    return (last_battery_voltage <= HPI_BATTERY_CRITICAL_VOLTAGE);
}

void hw_reset_low_battery_state(void)
{
    low_battery_screen_active = false;
    critical_battery_notified = false;
}

uint8_t hw_get_current_battery_level(void)
{
    return last_battery_level;
}

float hw_get_current_battery_voltage(void)
{
    return last_battery_voltage;
}

static void today_reset_steps(void)
{
    k_mutex_lock(&mutex_today_steps, K_FOREVER);
    today_total_steps = 0;
    k_mutex_unlock(&mutex_today_steps);
    LOG_INF("Daily step counter reset");
}

static void gpio_keys_cb_handler(struct input_event *evt, void *user_data)
{
    // printk("GPIO_KEY %s pressed, zephyr_code=%u, value=%d type=%d\n",
    //        evt->dev->name, evt->code, evt->value, evt->type);

    if (evt->value == 1)
    {
        switch (evt->code)
        {
        case INPUT_KEY_UP:
            LOG_INF("Crown Key Pressed");
            k_sem_give(&sem_crown_key_pressed);
            break;
        case INPUT_KEY_HOME:
            LOG_INF("Extra Key Pressed");
            sys_reboot(SYS_REBOOT_COLD);
            // printk("Entering Ship Mode\n");
            // regulator_parent_ship_mode(regulators);
            break;
        default:
            break;
        }
    }
}

void hpi_hw_pmic_off(void)
{
    LOG_INF("Entering Ship Mode");
    k_msleep(1000);
    regulator_parent_ship_mode(regulators);
}

void send_usb_cdc(const char *buf, size_t len)
{
    int rb_len;
    rb_len = ring_buf_put(&ringbuf_usb_cdc, buf, len);
    uart_irq_tx_enable(usb_cdc_uart_dev);
}

#if 0 // USB CDC interrupt handler - not currently used
static void usb_cdc_uart_interrupt_handler(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);

    while (uart_irq_update(dev) && uart_irq_is_pending(dev))
    {
        if (!rx_throttled && uart_irq_rx_ready(dev))
        {
            int recv_len, rb_len;
            uint8_t buffer[64];
            size_t len = MIN(ring_buf_space_get(&ringbuf_usb_cdc),
                             sizeof(buffer));

            if (len == 0)
            {

                uart_irq_rx_disable(dev);
                rx_throttled = true;
                continue;
            }

            recv_len = uart_fifo_read(dev, buffer, len);
            if (recv_len < 0)
            {
                LOG_ERR("Failed to read UART FIFO");
                recv_len = 0;
            };

            rb_len = ring_buf_put(&ringbuf_usb_cdc, buffer, recv_len);
            if (rb_len < recv_len)
            {
                LOG_ERR("Drop %u bytes", recv_len - rb_len);
            }

            LOG_DBG("tty fifo -> ringbuf %d bytes", rb_len);
            if (rb_len)
            {
                uart_irq_tx_enable(dev);
            }
        }

        if (uart_irq_tx_ready(dev))
        {
            uint8_t buffer[64];
            int rb_len, send_len;

            rb_len = ring_buf_get(&ringbuf_usb_cdc, buffer, sizeof(buffer));
            if (!rb_len)
            {
                LOG_DBG("Ring buffer empty, disable TX IRQ");
                uart_irq_tx_disable(dev);
                continue;
            }

            if (rx_throttled)
            {
                uart_irq_rx_enable(dev);
                rx_throttled = false;
            }

            send_len = uart_fifo_fill(dev, buffer, rb_len);
            if (send_len < rb_len)
            {
                LOG_ERR("Drop %d bytes", rb_len - send_len);
            }

            LOG_DBG("ringbuf -> tty fifo %d bytes", send_len);
        }
    }
}
#endif

static int npm_read_sensors(const struct device *charger,
                            float *voltage, float *current, float *temp, int32_t *chg_status)
{
    struct sensor_value value;
    int ret;

    ret = sensor_sample_fetch(charger);
    if (ret < 0)
    {
        return ret;
    }

    sensor_channel_get(charger, SENSOR_CHAN_GAUGE_VOLTAGE, &value);
    *voltage = (float)value.val1 + ((float)value.val2 / 1000000);

    sensor_channel_get(charger, SENSOR_CHAN_GAUGE_TEMP, &value);
    *temp = (float)value.val1 + ((float)value.val2 / 1000000);

    sensor_channel_get(charger, SENSOR_CHAN_GAUGE_AVG_CURRENT, &value);
    *current = (float)value.val1 + ((float)value.val2 / 1000000);

    sensor_channel_get(charger, SENSOR_CHAN_NPM1300_CHARGER_STATUS, &value);
    *chg_status = value.val1;

    return 0;
}

int npm_fuel_gauge_init(const struct device *charger)
{
    struct sensor_value value;
    struct nrf_fuel_gauge_init_parameters parameters = {
        .model = &battery_model,
        .opt_params = NULL,
    };
    int32_t chg_status;
    int ret;

    LOG_DBG("nRF Fuel Gauge version: %s", nrf_fuel_gauge_version);

    ret = npm_read_sensors(charger, &parameters.v0, &parameters.i0, &parameters.t0, &chg_status);
    if (ret < 0)
    {
        return ret;
    }

    /* Store charge nominal and termination current, needed for ttf calculation */
    sensor_channel_get(charger, SENSOR_CHAN_GAUGE_DESIRED_CHARGING_CURRENT, &value);
    max_charge_current = (float)value.val1 + ((float)value.val2 / 1000000);
    term_charge_current = max_charge_current / 10.f;

    nrf_fuel_gauge_init(&parameters, NULL);

    ref_time = k_uptime_get();

    return 0;
}

int npm_fuel_gauge_update(const struct device *charger, bool vbus_connected, uint8_t *batt_level, bool *batt_charging, float *batt_voltage)
{
    /* nPM1300 CHARGER.BCHGCHARGESTATUS.CONSTANTCURRENT register bitmask */
#define NPM1300_CHG_STATUS_CC_MASK BIT_MASK(3)
    float voltage;
    float current;
    float temp;
    float soc;
    float tte;
    float ttf;
    float delta;
    int32_t chg_status;
    bool cc_charging;
    int ret;

    ret = npm_read_sensors(charger, &voltage, &current, &temp, &chg_status);
    if (ret < 0)
    {
        printk("Error: Could not read from charger device\n");
        return ret;
    }

    cc_charging = (chg_status & NPM1300_CHG_STATUS_CC_MASK) != 0;

    delta = (float)k_uptime_delta(&ref_time) / 1000.f;

    /*soc = nrf_fuel_gauge_process(voltage, current, temp, delta, vbus_connected, NULL);
    tte = nrf_fuel_gauge_tte_get();
    ttf = nrf_fuel_gauge_ttf_get(cc_charging, term_charge_current);
    */

    // printk("V: %.3f, I: %.3f, T: %.2f, ", voltage, current, temp);
    // printk("SoC: %.2f, TTE: %.0f, TTF: %.0f, ", soc, tte, ttf);
    // printk("Charge status: %d\n", chg_status);
    *batt_level = (uint8_t)soc;
    *batt_charging = ((chg_status & NPM1300_CHG_STATUS_CC_MASK) != 0);
    *batt_voltage = voltage; // Return the battery voltage
    return 0;
}

#if 0 // USB init function - not currently used
static int usb_init()
{
    int ret = 0;

    ret = usb_enable(NULL);
    if (ret != 0)
    {
        LOG_ERR("Failed to enable USB");
        // return;
    }

    /* Enabled USB CDC interrupts */

    ring_buf_init(&ringbuf_usb_cdc, sizeof(ring_buffer), ring_buffer);

    uint32_t dtr = 0U;

    uart_line_ctrl_get(usb_cdc_uart_dev, UART_LINE_CTRL_DTR, &dtr);

    k_msleep(100);
    uart_irq_callback_set(usb_cdc_uart_dev, usb_cdc_uart_interrupt_handler);
    uart_irq_rx_enable(usb_cdc_uart_dev);

    printk("\nUSB Init complete\n\n");

    return ret;
}
#endif

double read_temp_f(void)
{
    struct sensor_value temp_sample;

    sensor_sample_fetch(max30208_dev);
    sensor_channel_get(max30208_dev, SENSOR_CHAN_AMBIENT_TEMP, &temp_sample);
    // last_read_temp_value = temp_sample.val1;
    double temp_c = (double)temp_sample.val1 * 0.005;
    double temp_f = (temp_c * 1.8) + 32.0;
    // printk("Temp: %.2f F\n", temp_f);
    return temp_f;
}

// Compatibility wrapper called by the app to set RTC time
void hw_rtc_set_time(uint8_t m_sec, uint8_t m_min, uint8_t m_hour, uint8_t m_day, uint8_t m_month, uint8_t m_year)
{
    struct tm time_to_set = {
        .tm_sec = m_sec,
        .tm_min = m_min,
        .tm_hour = m_hour,
        .tm_mday = m_day,
        .tm_mon = m_month - 1,           // Convert 1-based month to 0-based (Jan=0, Dec=11)
        .tm_year = m_year + 2000 - 1900, // Convert full year to years since 1900, app send only YY
        .tm_wday = 0,
        .tm_yday = 0};

    hpi_sys_set_rtc_time(&time_to_set);
}

void hw_pwr_display_enable(bool enable)
{
    if (enable)
    {
        regulator_enable(ldsw_disp_unit);
        k_msleep(10);
        LOG_DBG("Display LDO enabled");
    }
    else
    {
        regulator_disable(ldsw_disp_unit);
        k_msleep(10);
        LOG_DBG("Display LDO disabled");
    }
}

K_MUTEX_DEFINE(mutex_batt_level);

bool hw_is_max32664c_present(void)
{
    return max32664c_device_present;
}

int hw_max32664c_set_op_mode(uint8_t op_mode, uint8_t algo_mode)
{
    LOG_DBG("Setting op mode: %d, algo mode: %d", op_mode, algo_mode);
    struct sensor_value mode_set;
    mode_set.val1 = op_mode;
    mode_set.val2 = algo_mode;
    return sensor_attr_set(max32664c_dev, SENSOR_CHAN_ALL, MAX32664C_ATTR_OP_MODE, &mode_set);
}

int hw_max32664c_stop_algo(void)
{
    struct sensor_value mode_set;
    mode_set.val1 = MAX32664C_OP_MODE_STOP_ALGO;
    mode_set.val2 = 0;
    return sensor_attr_set(max32664c_dev, SENSOR_CHAN_ALL, MAX32664C_ATTR_OP_MODE, &mode_set);
}

static void pmic_event_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    if (pins & BIT(NPM1300_EVENT_VBUS_DETECTED))
    {
        printk("Vbus connected\n");
        vbus_connected = true;
    }

    if (pins & BIT(NPM1300_EVENT_VBUS_REMOVED))
    {
        printk("Vbus removed\n");
        vbus_connected = false;
    }
}

static void hw_add_boot_msg(char *msg, bool status, bool show_status, bool show_progress, int progress)
{
    struct hpi_boot_msg_t boot_msg = {
        .show_status = show_status,
        .status = status,
        .show_progress = show_progress,
        .progress = progress,
    };
    strcpy(boot_msg.msg, msg);

    k_msgq_put(&q_disp_boot_msg, &boot_msg, K_NO_WAIT);
}

static int hw_enable_pmic_callback(void)
{
    static struct gpio_callback event_cb;
    int ret = 0;

    gpio_init_callback(&event_cb, pmic_event_callback,
                       BIT(NPM1300_EVENT_VBUS_DETECTED) | BIT(NPM1300_EVENT_VBUS_REMOVED));

    ret = mfd_npm1300_add_callback(pmic, &event_cb);
    if (ret)
    {
        LOG_ERR("Failed to add pmic callback");
        return ret;
    }

    /* Initialise vbus detection status. */
    struct sensor_value val;
    ret = sensor_attr_get(charger, SENSOR_CHAN_CURRENT, SENSOR_ATTR_UPPER_THRESH, &val);

    if (ret < 0)
    {
        return ret;
    }

    vbus_connected = (val.val1 != 0) || (val.val2 != 0);
    return 0;
}

void hpi_hw_ldsw2_on(void)
{
    regulator_enable(ldsw_sens_1_8);
}

void hpi_hw_ldsw2_off(void)
{
    regulator_disable(ldsw_sens_1_8);
}

void hw_module_init(void)
{
    int ret = 0;
    static struct rtc_time curr_time;

    // To fix nRF5340 Anomaly 47 (https://docs.nordicsemi.com/bundle/errata_nRF5340_EngD/page/ERR/nRF5340/EngineeringD/latest/anomaly_340_47.html)
    NRF_TWIM2->FREQUENCY = 0x06200000;
    NRF_TWIM1->FREQUENCY = 0x06200000;

    if (!device_is_ready(pmic))
    {
        LOG_ERR("PMIC device not ready");
    }

    if (!device_is_ready(regulators))
    {
        LOG_ERR("Error: Regulator device is not ready\n");
    }

    if (!device_is_ready(charger))
    {
        LOG_ERR("Charger device not ready.\n");
    }

    // Power ON display
    regulator_disable(ldsw_disp_unit);
    k_msleep(100);
    regulator_enable(ldsw_disp_unit);
    k_msleep(500);

    // Reset all sensors before starting
    regulator_disable(ldsw_sens_1_8);
    k_msleep(100);
    regulator_enable(ldsw_sens_1_8);
    k_msleep(100);

    // Signal to start display state machine
    k_sem_give(&sem_disp_smf_start);

    // Wait for display system to be initialized and ready
    k_sem_take(&sem_disp_ready, K_FOREVER);

    hw_enable_pmic_callback();
    if (npm_fuel_gauge_init(charger) < 0)
    {
        LOG_ERR("Could not initialise fuel gauge.\n");
        hw_add_boot_msg("PMIC", true, true, false, 0);
    }
    else
    {
        hw_add_boot_msg("PMIC", true, true, false, 0);
        hw_enable_pmic_callback();
    }

    // Check battery voltage during boot
    uint8_t boot_batt_level = 0;
    bool boot_batt_charging = false;
    float boot_batt_voltage = 0.0f;

    if (npm_fuel_gauge_update(charger, vbus_connected, &boot_batt_level, &boot_batt_charging, &boot_batt_voltage) == 0)
    {
        char batt_msg[32];
        snprintf(batt_msg, sizeof(batt_msg), "Battery: %.2fV (%d%%)", (double)boot_batt_voltage, boot_batt_level);

        // Check if battery voltage is critically low
        if (boot_batt_voltage <= HPI_BATTERY_SHUTDOWN_VOLTAGE && !boot_batt_charging)
        {
            hw_add_boot_msg(batt_msg, false, true, false, 0);
            hw_add_boot_msg("CRITICAL LOW VOLTAGE", false, true, false, 0);
            hw_add_boot_msg("Connect charger to boot", false, false, false, 0);

            // Wait a bit to show the message, then shutdown
            k_msleep(3000);
            LOG_ERR("Boot aborted - critical battery voltage: %.2fV", (double)boot_batt_voltage);
            hpi_hw_pmic_off();
            return; // This should never be reached, but just in case
        }
        else if (boot_batt_voltage <= HPI_BATTERY_CRITICAL_VOLTAGE && !boot_batt_charging)
        {
            hw_add_boot_msg(batt_msg, false, true, false, 0);
            hw_add_boot_msg("LOW VOLTAGE WARNING", false, true, false, 0);
        }
        else
        {
            hw_add_boot_msg(batt_msg, true, true, false, 0);
            if (boot_batt_charging)
            {
                hw_add_boot_msg("Charging...", true, false, false, 0);
            }
        }
    }
    else
    {
        hw_add_boot_msg("Battery: ERROR", false, true, false, 0);
    }

    // Init IMU device
    ret = device_init(imu_dev);
    k_msleep(10);

    if (!device_is_ready(imu_dev))
    {
        LOG_ERR("Error: IMU device not ready");
        hw_add_boot_msg("BMI323", false, true, false, 0);
    }
    else
    {
        hw_add_boot_msg("BMI323", true, true, false, 0);
        // struct sensor_value set_val;
        // set_val.val1 = 1;

        // sensor_attr_set(imu_dev, SENSOR_CHAN_ACCEL_XYZ, BMI323_HPI_ATTR_EN_FEATURE_ENGINE, &set_val);
        // sensor_attr_set(imu_dev, SENSOR_CHAN_ACCEL_XYZ, BMI323_HPI_ATTR_EN_STEP_COUNTER, &set_val);
    }

    device_init(max30001_dev);
    k_sleep(K_MSEC(10));

    if (!device_is_ready(max30001_dev))
    {
        LOG_ERR("MAX30001 device not found!");
        max30001_device_present = false;
        hw_add_boot_msg("MAX30001", false, true, false, 0);
    }
    else
    {
        hw_add_boot_msg("MAX30001", true, true, false, 0);
        LOG_INF("MAX30001 device found!");
        max30001_device_present = true;

        k_sem_give(&sem_ecg_bioz_sm_start);
    }

    k_sleep(K_MSEC(100));

    ret = gpio_pin_configure_dt(&dcdc_5v_en, GPIO_OUTPUT);
    if (ret < 0)
    {
        LOG_ERR("Error: Could not configure GPIO pin DC/DC 5v EN\n");
    }

    gpio_pin_set_dt(&dcdc_5v_en, 1);
    k_sleep(K_MSEC(100));

    device_init(max32664c_dev);
    k_sleep(K_MSEC(100));

    if (!device_is_ready(max32664c_dev))
    {
        LOG_ERR("MAX32664C device not present!");
        max32664c_device_present = false;
        hw_add_boot_msg("MAX32664C", false, true, false, 0);
    }
    else
    {
        hw_add_boot_msg("MAX32664C", true, true, false, 0);
        LOG_INF("MAX32664C device present!");
        max32664c_device_present = true;

        struct sensor_value ver_get;
        sensor_attr_get(max32664c_dev, SENSOR_CHAN_ALL, MAX32664C_ATTR_APP_VER, &ver_get);
        LOG_INF("MAX32664C App Version: %d.%d", ver_get.val1, ver_get.val2);
        char ver_msg[10] = {0};
        snprintf(ver_msg, sizeof(ver_msg), "\t v%d.%d", ver_get.val1, ver_get.val2);
        hw_add_boot_msg(ver_msg, true, false, false, 0);

        struct sensor_value sensor_ids_get;
        sensor_attr_get(max32664c_dev, SENSOR_CHAN_ALL, MAX32664C_ATTR_SENSOR_IDS, &sensor_ids_get);

        if (sensor_ids_get.val1 != MAX32664C_AFE_ID)
        {
            LOG_ERR("MAX32664C AFE Not Present");
            hw_add_boot_msg("\t AFE", false, true, false, 0);
        }
        else
        {
            LOG_INF("MAX32664C AFE OK: %x", sensor_ids_get.val1);
            hw_add_boot_msg("\t AFE", true, true, false, 0);
        }

        if (sensor_ids_get.val2 != MAX32664C_ACC_ID)
        {
            LOG_ERR("MAX32664C Accel Not Present");
            hw_add_boot_msg("\t Acc", false, true, false, 0);
        }
        else
        {
            LOG_INF("MAX32664C Accel OK: %x", sensor_ids_get.val2);
            hw_add_boot_msg("\t Acc", true, true, false, 0);
        }

        if ((ver_get.val1 < hpi_max32664c_req_ver.major) || (ver_get.val2 < hpi_max32664c_req_ver.minor))
        {
            LOG_INF("MAX32664C App update required");
            hw_add_boot_msg("\tUpdate required", false, false, false, 0);
            k_sem_give(&sem_boot_update_req);
            // max32664_updater_start(max32664c_dev, MAX32664_UPDATER_DEV_TYPE_MAX32664C);
        }

        k_sem_give(&sem_ppg_wrist_sm_start);
    }

    device_init(max32664d_dev);
    k_sleep(K_MSEC(100));

    if (!device_is_ready(max32664d_dev))
    {
        LOG_ERR("MAX32664D device not present!");
        max32664d_device_present = false;
        hw_add_boot_msg("MAX32664D", false, true, false, 0);
    }
    else
    {
        LOG_INF("MAX32664D device present!");
        max32664d_device_present = true;
        hw_add_boot_msg("MAX32664D", true, true, false, 0);

        struct sensor_value ver_get;
        sensor_attr_get(max32664d_dev, SENSOR_CHAN_ALL, MAX32664D_ATTR_APP_VER, &ver_get);
        LOG_INF("MAX32664D App Version: %d.%d", ver_get.val1, ver_get.val2);

        char ver_msg[10] = {0};
        snprintf(ver_msg, sizeof(ver_msg), "\t v%d.%d", ver_get.val1, ver_get.val2);
        hw_add_boot_msg(ver_msg, true, false, false, 0);

        if ((ver_get.val1 < hpi_max32664d_req_ver.major) || (ver_get.val2 < hpi_max32664d_req_ver.minor))
        {
            LOG_INF("MAX32664D App update required");
            hw_add_boot_msg("\tUpdate required", false, false, false, 0);
            k_sem_give(&sem_boot_update_req);
            // max32664_updater_start(max32664d_dev, MAX32664_UPDATER_DEV_TYPE_MAX32664D);
        }

        k_sem_give(&sem_ppg_finger_sm_start);
    }

    // Confirm MCUBoot image if not already confirmed by app
    if (boot_is_img_confirmed())
    {
        LOG_INF("DFU Image confirmed");
    }
    else
    {
        LOG_INF("DFU Image not confirmed. Confirming...");
        if (boot_write_img_confirmed())
        {
            LOG_ERR("Failed to confirm image");
        }
        else
        {
            LOG_INF("Marked image as OK");
        }
    }

    // setup_pmic_callbacks();

    device_init(max30208_dev);
    k_sleep(K_MSEC(100));

    if (!device_is_ready(max30208_dev))
    {
        LOG_ERR("MAX30208 device not found!");
        hw_add_boot_msg("MAX30208", false, true, false, 0);
    }
    else
    {
        LOG_INF("MAX30208 device found!");
        hw_add_boot_msg("MAX30208", true, true, false, 0);
    }

    rtc_get_time(rtc_dev, &curr_time);
    LOG_INF("RTC time: %d:%d:%d %d/%d/%d", curr_time.tm_hour, curr_time.tm_min, curr_time.tm_sec, curr_time.tm_mon, curr_time.tm_mday, curr_time.tm_year);
    // Read and publish time during initialization
    struct rtc_time rtc_sys_time;
    ret = rtc_get_time(rtc_dev, &rtc_sys_time);
    if (ret < 0)
    {
        LOG_ERR("Failed to get RTC time");
    }
    struct tm m_tm_time = *rtc_time_to_tm(&rtc_sys_time);
    hpi_sys_set_sys_time(&m_tm_time);

    // Initialize time synchronization
    hpi_sys_force_time_sync(); // Force initial sync

    // npm_fuel_gauge_update(charger, vbus_connected);

    fs_module_init();

    // Initialize user settings (load from file)
    ret = hpi_user_settings_init();
    if (ret < 0)
    {
        LOG_ERR("Failed to initialize user settings: %d", ret);
        hw_add_boot_msg("User Settings", false, true, false, 0);
    }
    else
    {
        LOG_INF("User settings initialized successfully");
        hw_add_boot_msg("User Settings", true, true, false, 0);
    }

    ret = settings_subsys_init();
    if (ret)
    {
        LOG_ERR("settings subsys initialization: fail (err %d)", ret);
        return;
    }

    pm_device_runtime_get(gpio_keys_dev);

    INPUT_CALLBACK_DEFINE(gpio_keys_dev, gpio_keys_cb_handler, NULL);

    ble_module_init();
    k_sem_give(&sem_ble_thread_start);

    k_sem_give(&sem_hw_inited);

    LOG_INF("HW Init complete");

    k_sem_give(&sem_disp_boot_complete);
    k_sem_give(&sem_hw_thread_start);

    // init_settings();

    // usb_init();
}

static uint32_t acc_get_steps(void)
{
    struct sensor_value steps;
    sensor_sample_fetch(imu_dev);
    sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_X, &steps);
    return (uint32_t)steps.val1;
}

void hw_thread(void)
{
    uint32_t _steps = 0;
    double _temp_f = 0.0;

    uint8_t sys_batt_level = 0;
    bool sys_batt_charging = false;
    float sys_batt_voltage = 4.2f; // Add voltage tracking

    // Variables for tracking daily reset
    static int last_day = -1;

    k_sem_take(&sem_hw_thread_start, K_FOREVER);
    LOG_INF("HW Thread starting");

    k_sem_give(&sem_hpi_sys_thread_start);

    int sc_reset_counter = 0;
    for (;;)
    {
        // Read and publish battery level
        npm_fuel_gauge_update(charger, vbus_connected, &sys_batt_level, &sys_batt_charging, &sys_batt_voltage);
        last_battery_level = sys_batt_level;     // Store for external access
        last_battery_voltage = sys_batt_voltage; // Store voltage for external access

        struct hpi_batt_status_t batt_s = {
            .batt_level = (uint8_t)sys_batt_level,
            .batt_charging = sys_batt_charging,
        };
        zbus_chan_pub(&batt_chan, &batt_s, K_SECONDS(1));

        // Check for low battery conditions (voltage-based)
        if (!sys_batt_charging)
        { // Only check cutoff when not charging
            if (sys_batt_voltage <= HPI_BATTERY_SHUTDOWN_VOLTAGE)
            {
                // Critical battery voltage - immediately shutdown
                LOG_ERR("Critical battery voltage (%.2fV) - shutting down", (double)sys_batt_voltage);
                k_msleep(1000); // Give time for log message
                hpi_hw_pmic_off();
            }
            else if (sys_batt_voltage <= HPI_BATTERY_CRITICAL_VOLTAGE && !low_battery_screen_active)
            {
                // Show low battery warning screen
                LOG_WRN("Low battery voltage (%.2fV) - showing warning screen", (double)sys_batt_voltage);
                low_battery_screen_active = true;
                critical_battery_notified = true;

                // Load the low battery screen with battery level and voltage as arguments
                // Pass voltage as arg3 (multiply by 100 to preserve 2 decimal places in uint32_t)
                hpi_load_scr_spl(SCR_SPL_LOW_BATTERY, SCROLL_NONE, sys_batt_level, sys_batt_charging, (uint32_t)(sys_batt_voltage * 100), 0);
            }
        }
        else
        {
            // Reset flags when charging and voltage recovers
            if (sys_batt_voltage > HPI_BATTERY_RECOVERY_VOLTAGE)
            {
                if (low_battery_screen_active)
                {
                    LOG_INF("Battery voltage recovered (%.2fV) - dismissing low battery screen", (double)sys_batt_voltage);
                    // Return to home screen
                    hpi_load_screen(SCR_HOME, SCROLL_NONE);
                }
                low_battery_screen_active = false;
                critical_battery_notified = false;
            }
        }

        // Update low battery screen if it's currently active and status changed
        if (low_battery_screen_active)
        {
            // Only refresh every 5 seconds to avoid excessive updates
            if (k_uptime_get_32() - low_battery_last_update > 5000)
            {
                // Refresh the low battery screen to show updated charging status and voltage
                hpi_load_scr_spl(SCR_SPL_LOW_BATTERY, SCROLL_NONE, sys_batt_level, sys_batt_charging, (uint32_t)(sys_batt_voltage * 100), 0);
                low_battery_last_update = k_uptime_get_32();
            }
        }

        // Sync time with RTC if needed
        if (hpi_sys_sync_time_if_needed() < 0)
        {
            LOG_ERR("Failed to sync time with RTC");
        }

        // Get current synced time and publish
        struct tm m_tm_time = hpi_sys_get_current_time();
        zbus_chan_pub(&sys_time_chan, &m_tm_time, K_SECONDS(1));

        // Check if day has changed and reset step counter if needed
        if (last_day == -1)
        {
            // Initialize on first run
            last_day = m_tm_time.tm_mday;
        }
        else if (last_day != m_tm_time.tm_mday)
        {
            // Day has changed - reset daily step counter
            last_day = m_tm_time.tm_mday;
            today_reset_steps();
            LOG_INF("New day detected (%d), daily steps reset", m_tm_time.tm_mday);
        }

        // Read and publish steps
        _steps = acc_get_steps();
        // LOG_DBG("Inc. Steps: %d", _steps);

        struct hpi_steps_t steps_point = {
            .timestamp = hw_get_sys_time_ts(),
            .steps = today_get_steps(),
        };
        zbus_chan_pub(&steps_chan, &steps_point, K_SECONDS(4));

        struct sensor_value set_val;
        set_val.val1 = 1;

        // Write to file Reset step counter every 60 seconds
        if (sc_reset_counter >= 12)
        {
            today_add_steps(_steps);

            struct hpi_steps_t tr_steps_point = {
                .timestamp = hw_get_sys_time_ts(),
                .steps = _steps,
            };

            if (_steps > 0)
            {
                k_msgq_put(&q_steps_trend, &tr_steps_point, K_NO_WAIT);
            }

            LOG_DBG("Resetting step counter");
            sensor_attr_set(imu_dev, SENSOR_CHAN_ACCEL_XYZ, BMI323_HPI_ATTR_RESET_STEP_COUNTER, &set_val);
            sc_reset_counter = 0;
        }
        else
        {
            sc_reset_counter++;
        }

        // Read and publish temperature
        if (hpi_sys_get_device_on_skin() == true)
        {
            _temp_f = read_temp_f();
            struct hpi_temp_t temp = {
                .temp_f = _temp_f,
                .timestamp = hw_get_sys_time_ts(),
            };
            zbus_chan_pub(&temp_chan, &temp, K_SECONDS(1));
        }

        k_sleep(K_MSEC(5000));
    }
}

#define HW_THREAD_STACKSIZE 4096
#define HW_THREAD_PRIORITY 7

K_THREAD_DEFINE(hw_thread_id, HW_THREAD_STACKSIZE, hw_thread, NULL, NULL, NULL, HW_THREAD_PRIORITY, 0, 0);