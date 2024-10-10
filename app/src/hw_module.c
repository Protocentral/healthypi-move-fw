#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/rtc.h>

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

#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/zbus/zbus.h>

#include <time.h>
#include <zephyr/posix/time.h>

#include <nrfx_clock.h>

#include <nrfx_spim.h>

#include "max30001.h"
#include "max32664.h"

#ifdef CONFIG_SENSOR_MAXM86146
#include "maxm86146.h"
#endif

#include "hw_module.h"
#include "fs_module.h"
#include "ui/move_ui.h"

// #include "max32664c_msbl.h"

#include <zephyr/sys/reboot.h>
#include <zephyr/drivers/mfd/npm1300.h>
#include <zephyr/drivers/regulator.h>

#include <zephyr/pm/device.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/device_runtime.h>

#include "nrf_fuel_gauge.h"

LOG_MODULE_REGISTER(hw_module);
char curr_string[40];

/*******EXTERNS******/
extern struct k_msgq q_session_cmd_msg;
ZBUS_CHAN_DECLARE(sys_time_chan, batt_chan);

/****END EXTERNS****/

#define HW_THREAD_STACKSIZE 4096
#define HW_THREAD_PRIORITY 7

// Peripheral Device Pointers
const struct device *max30205_dev = DEVICE_DT_GET_ANY(maxim_max30205);
const struct device *max32664d_dev = DEVICE_DT_GET_ANY(maxim_max32664);
const struct device *maxm86146_dev = DEVICE_DT_GET_ANY(maxim_maxm86146);

const struct device *acc_dev = DEVICE_DT_GET_ONE(st_lsm6dso);
const struct device *const max30001_dev = DEVICE_DT_GET(DT_ALIAS(max30001));
static const struct device *rtc_dev = DEVICE_DT_GET(DT_ALIAS(rtc));
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
volatile bool maxm86146_device_present = false;
volatile bool max32664d_device_present = false;

// static const struct device npm_gpio_keys = DEVICE_DT_GET(DT_NODELABEL(npm_pmic_buttons));
// static const struct gpio_dt_spec button1 = GPIO_DT_SPEC_GET(DT_ALIAS(gpio_button0), gpios);

// uint8_t global_batt_level = 0;
// int32_t global_temp;
// bool global_batt_charging = false;
// struct rtc_time global_system_time;

// USB CDC UART
#define RING_BUF_SIZE 1024
uint8_t ring_buffer[RING_BUF_SIZE];
struct ring_buf ringbuf_usb_cdc;
static bool rx_throttled;

uint8_t m_key_pressed = GPIO_KEYPAD_KEY_NONE;

K_SEM_DEFINE(sem_hw_inited, 0, 1);
K_SEM_DEFINE(sem_start_cal, 0, 1);

K_SEM_DEFINE(sem_ecg_intb_recd, 0, 1);

K_SEM_DEFINE(sem_ppg_finger_thread_start, 0, 1);
K_SEM_DEFINE(sem_ppg_wrist_thread_start, 0, 1);
K_SEM_DEFINE(sem_ecg_bioz_thread_start, 0, 1);

#define MOVE_SAMPLING_DISABLED 0

static float max_charge_current;
static float term_charge_current;
static int64_t ref_time;

void ecg_sampling_trigger(void);

static const struct battery_model battery_model = {
#include "battery_profile_200.inc"
};

/* nPM1300 CHARGER.BCHGCHARGESTATUS.CONSTANTCURRENT register bitmask */
#define NPM1300_CHG_STATUS_CC_MASK BIT_MASK(3)

static void gpio_keys_cb_handler(struct input_event *evt)
{
    // printk("GPIO_KEY %s pressed, zephyr_code=%u, value=%d type=%d\n",
    //        evt->dev->name, evt->code, evt->value, evt->type);

    if (evt->value == 1)
    {
        switch (evt->code)
        {
        case INPUT_KEY_UP:
            LOG_INF("Crown Key Pressed");
            lv_disp_trig_activity(NULL);
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

void send_usb_cdc(const char *buf, size_t len)
{
    int rb_len;
    rb_len = ring_buf_put(&ringbuf_usb_cdc, buf, len);
    uart_irq_tx_enable(usb_cdc_uart_dev);
}

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

uint8_t read_battery_level(void)
{
    uint8_t batt_level = 0;

    /*int ret = 0;

    fuel_gauge_prop_t props[] = {
        FUEL_GAUGE_RUNTIME_TO_EMPTY,
        FUEL_GAUGE_RUNTIME_TO_FULL,
        FUEL_GAUGE_RELATIVE_STATE_OF_CHARGE,
        FUEL_GAUGE_VOLTAGE,
    };

    union fuel_gauge_prop_val vals[ARRAY_SIZE(props)];

    ret = fuel_gauge_get_props(fg_dev, props, vals, ARRAY_SIZE(props));
    if (ret < 0)
    {
        printk("Error: cannot get properties\n");
    }
    else
    {
        // printk("Time to empty %d\n", vals[0].runtime_to_empty);
        // printk("Time to full %d\n", vals[1].runtime_to_full);
        // printk("Charge %d%%\n", vals[2].relative_state_of_charge);
        // printk("Voltage %d\n", vals[3].voltage);

        batt_level = vals[2].relative_state_of_charge;
    }*/

    return batt_level;
}

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

    printk("nRF Fuel Gauge version: %s\n", nrf_fuel_gauge_version);

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

int npm_fuel_gauge_update(const struct device *charger)
{
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

    soc = nrf_fuel_gauge_process(voltage, current, temp, delta, NULL);
    tte = nrf_fuel_gauge_tte_get();
    ttf = nrf_fuel_gauge_ttf_get(cc_charging, -term_charge_current);

    // printk("V: %.3f, I: %.3f, T: %.2f, ", voltage, current, temp);
    // printk("SoC: %.2f, TTE: %.0f, TTF: %.0f, ", soc, tte, ttf);
    // printk("Charge status: %d\n", chg_status);
    struct batt_status batt_s = {
        .batt_level = (uint8_t)soc,
        .batt_charging = (chg_status & NPM1300_CHG_STATUS_CC_MASK) != 0,
    };

    zbus_chan_pub(&batt_chan, &batt_s, K_SECONDS(1));
    // global_batt_level = (int)soc;

    return 0;
}

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

static inline float out_ev(struct sensor_value *val)
{
    return (val->val1 + (float)val->val2 / 1000000);
}

/*
static void fetch_and_display(const struct device *dev)
{
    struct sensor_value x, y, z;
    // static int trig_cnt;

    // trig_cnt++;

    sensor_sample_fetch_chan(dev, SENSOR_CHAN_ACCEL_XYZ);
    sensor_channel_get(dev, SENSOR_CHAN_ACCEL_X, &x);
    sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Y, &y);
    sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Z, &z);

    printf("accel x:%f ms/2 y:%f ms/2 z:%f ms/2\n",
           (double)out_ev(&x), (double)out_ev(&y), (double)out_ev(&z));

    sensor_sample_fetch_chan(dev, SENSOR_CHAN_GYRO_XYZ);
    sensor_channel_get(dev, SENSOR_CHAN_GYRO_X, &x);
    sensor_channel_get(dev, SENSOR_CHAN_GYRO_Y, &y);
    sensor_channel_get(dev, SENSOR_CHAN_GYRO_Z, &z);

    printf("gyro x:%f rad/s y:%f rad/s z:%f rad/s\n",
           (double)out_ev(&x), (double)out_ev(&y), (double)out_ev(&z));

    // printf("trig_cnt:%d\n\n", trig_cnt);
}
*/

static int set_sampling_freq(const struct device *dev)
{
    int ret = 0;
    struct sensor_value odr_attr;

    /* set accel/gyro sampling frequency to 12.5 Hz */
    odr_attr.val1 = 0;
    odr_attr.val2 = 0;

    ret = sensor_attr_set(dev, SENSOR_CHAN_ACCEL_XYZ,
                          SENSOR_ATTR_SAMPLING_FREQUENCY, &odr_attr);
    if (ret != 0)
    {
        printf("Cannot set sampling frequency for accelerometer.\n");
        return ret;
    }

    ret = sensor_attr_set(dev, SENSOR_CHAN_GYRO_XYZ,
                          SENSOR_ATTR_SAMPLING_FREQUENCY, &odr_attr);
    if (ret != 0)
    {
        printf("Cannot set sampling frequency for gyro.\n");
        return ret;
    }

    return 0;
}

int32_t read_temp(void)
{
    sensor_sample_fetch(max30205_dev);
    struct sensor_value temp_sample;
    sensor_channel_get(max30205_dev, SENSOR_CHAN_AMBIENT_TEMP, &temp_sample);
    // last_read_temp_value = temp_sample.val1;
    printk("Temp: %d\n", temp_sample.val1);
    return temp_sample.val1;
}

void hw_rtc_set_device_time(uint8_t m_sec, uint8_t m_min, uint8_t m_hour, uint8_t m_day, uint8_t m_month, uint8_t m_year)
{
    struct rtc_time time_set;

    time_set.tm_sec = m_sec;
    time_set.tm_min = m_min;
    time_set.tm_hour = m_hour;
    time_set.tm_mday = m_day;
    time_set.tm_mon = m_month;
    time_set.tm_year = m_year;

    int ret = rtc_set_time(rtc_dev, &time_set);
    printk("RTC Set Time: %d\n", ret);
}

#define DEFAULT_DATE 240428 // YYMMDD 28th April 2024
#define DEFAULT_TIME 121212 // HHMMSS 12:12:12

#define DEFAULT_FUTURE_DATE 240429 // YYMMDD 29th April 2024
#define DEFAULT_FUTURE_TIME 121213 // HHMMSS 12:12:13

void hw_bpt_start_cal(void)
{
    printk("Starting BPT Calibration\n");

    struct sensor_value data_time_val;
    data_time_val.val1 = DEFAULT_DATE; // Date
    data_time_val.val2 = DEFAULT_TIME; // Time
    sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664_ATTR_DATE_TIME, &data_time_val);
    k_sleep(K_MSEC(100));

    struct sensor_value bp_cal_val;
    bp_cal_val.val1 = 0x00787A7D; // Sys vals
    bp_cal_val.val2 = 0x00505152; // Dia vals
    sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664_ATTR_BP_CAL, &bp_cal_val);
    k_sleep(K_MSEC(100));

    // Start BPT Calibration
    struct sensor_value mode_val;
    mode_val.val1 = MAX32664_OP_MODE_BPT_CAL_START;
    sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664_ATTR_OP_MODE, &mode_val);
    k_sleep(K_MSEC(100));

    // ppg_data_start();
}

void hw_bpt_start_est(void)
{
    struct sensor_value load_cal;
    load_cal.val1 = 0x00000000;
    // sensor_attr_set(max32664_dev, SENSOR_CHAN_ALL, MAX32664_ATTR_LOAD_CALIB, &load_cal);

    struct sensor_value data_time_val;
    data_time_val.val1 = DEFAULT_FUTURE_DATE; // Date // TODO: Update to local time
    data_time_val.val2 = DEFAULT_FUTURE_TIME; // Time
    sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664_ATTR_DATE_TIME, &data_time_val);
    k_sleep(K_MSEC(100));

    struct sensor_value mode_set;
    mode_set.val1 = MAX32664_OP_MODE_BPT;
    sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664_ATTR_OP_MODE, &mode_set);

    k_sleep(K_MSEC(1000));
    // ppg_data_start();
}

void hw_bpt_stop(void)
{
    struct sensor_value mode_val;
    mode_val.val1 = MAX32664_ATTR_STOP_EST;
    sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664_ATTR_STOP_EST, &mode_val);
}

void hw_bpt_get_calib(void)
{
    struct sensor_value mode_val;
    mode_val.val1 = MAX32664_OP_MODE_BPT_CAL_GET_VECTOR;
    sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664_ATTR_OP_MODE, &mode_val);
}

static volatile bool vbus_connected;

static void pmic_event_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    printk("Event detected\n");

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

    vbus_connected = (val.val1 != 0) || (val.val2 != 0);
}

void hw_rtc_set_time(uint8_t m_sec, uint8_t m_min, uint8_t m_hour, uint8_t m_day, uint8_t m_month, uint8_t m_year)
{
    struct rtc_time time_set;

    time_set.tm_sec = m_sec;
    time_set.tm_min = m_min;
    time_set.tm_hour = m_hour;
    time_set.tm_mday = m_day;
    time_set.tm_mon = (m_month - 1);
    time_set.tm_year = (m_year + 100);

    int ret = rtc_set_time(rtc_dev, &time_set);
    printk("RTC Set Time: %d\n", ret);
}

void hw_init(void)
{
    int ret = 0;
    static struct rtc_time curr_time;

    if (!device_is_ready(regulators))
    {
        LOG_ERR("Error: Regulator device is not ready\n");
        // return 0;
    }

    if (!device_is_ready(charger))
    {
        LOG_ERR("Charger device not ready.\n");
        // return 0;
    }
    if (npm_fuel_gauge_init(charger) < 0)
    {
        LOG_ERR("Could not initialise fuel gauge.\n");
        // return 0;
    }

    //regulator_disable(ldsw_disp_unit);
    //k_sleep(K_MSEC(100));

    regulator_enable(ldsw_disp_unit);
    k_sleep(K_MSEC(1000));

    // device_init(display_dev);
    // k_sleep(K_MSEC(1000));

    // device_init(touch_dev);

    regulator_enable(ldsw_sens_1_8);
    k_sleep(K_MSEC(100));

    // regulator_disable(ldsw_sens_1_8);

    ret = gpio_pin_configure_dt(&dcdc_5v_en, GPIO_OUTPUT_ACTIVE);
    if (ret < 0)
    {
        // return;
        LOG_ERR("Error: Could not configure GPIO pin DC/DC 5v EN\n");
    }

    gpio_pin_set_dt(&dcdc_5v_en, 1);

#ifdef CONFIG_SENSOR_MAX30001
    if (!device_is_ready(max30001_dev))
    {
        printk("MAX30001 device not found!"); // Rebooting !");
        // sys_reboot(SYS_REBOOT_COLD);
    }
    else
    {
        struct sensor_value ecg_mode_set;

        // ecg_mode_set.val1 = 1;
        // sensor_attr_set(max30001_dev, SENSOR_CHAN_ALL, MAX30001_ATTR_ECG_ENABLED, &ecg_mode_set);
        // sensor_attr_set(max30001_dev, SENSOR_CHAN_ALL, MAX30001_ATTR_BIOZ_ENABLED, &ecg_mode_set);
    }
#endif

    if (!device_is_ready(maxm86146_dev))
    {
        LOG_ERR("MAXM86146 device not present!");
    }
    else
    {
        LOG_INF("MAXM86146 device present!");
        maxm86146_device_present = true;

        struct sensor_value mode_set;
        mode_set.val1 = MAXM86146_OP_MODE_ALGO;
        sensor_attr_set(maxm86146_dev, SENSOR_CHAN_ALL, MAXM86146_ATTR_OP_MODE, &mode_set);
    }

    struct sensor_value mode_set;
    mode_set.val1 = 1;
    //sensor_attr_set(maxm86146_dev, SENSOR_CHAN_ALL, MAXM86146_ATTR_ENTER_BOOTLOADER, &mode_set);

    if (!device_is_ready(max32664d_dev))
    {
        LOG_ERR("MAX32664D device not present!");
        max32664d_device_present = false;
    }
    else
    {
        LOG_INF("MAX32664D device present!");
        max32664d_device_present = true;
        struct sensor_value mode_set;
        mode_set.val1 = MAX32664_OP_MODE_BPT;
        // sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664_ATTR_OP_MODE, &mode_set);
    }

    // printk("Switching application core from 64 MHz and 128 MHz. \n");
    // nrfx_clock_divider_set(NRF_CLOCK_DOMAIN_HFCLK, NRF_CLOCK_HFCLK_DIV_1);
    // printk("NRF_CLOCK_S.HFCLKCTRL:%d\n", NRF_CLOCK_S->HFCLKCTRL);

    // printk("Switching application core from 128 MHz and 64 MHz. \n");
    //  nrfx_clock_divider_set(NRF_CLOCK_DOMAIN_HFCLK, NRF_CLOCK_HFCLK_DIV_2);
    //  printk("NRF_CLOCK_S.HFCLKCTRL:%d\n", NRF_CLOCK_S->HFCLKCTRL);

    // nrfx_clock_divider_set(NRF_CLOCK_DOMAIN_HFCLK, NRF_CLOCK_HFCLK_DIV_1);

    // nrf_spim_frequency_set(NRF_SPIM_INST_GET(4), NRF_SPIM_FREQ_32M);
    // nrf_spim_iftiming_set(NRF_SPIM_INST_GET(4), 0);

#ifdef NRF_SPIM_HAS_32_MHZ_FREQ
    LOG_INF("SPIM runs at 32MHz !\n");
#endif

    // setup_pmic_callbacks();

    if (!device_is_ready(max30205_dev))
    {
        LOG_ERR("MAX30205 device not found!\n");
        // return;
    }

    if (!device_is_ready(acc_dev))
    {
        LOG_ERR("LSM6DSO device not ready!\n");
        // return 0;
    }
    else
    {
        if (set_sampling_freq(acc_dev) != 0)
        {
            // return;
            printk("Error setting sampling frequency\n");
        }
    }
    // pm_device_runtime_put(acc_dev);

    rtc_get_time(rtc_dev, &curr_time);
    LOG_INF("Current time: %d:%d:%d %d/%d/%d", curr_time.tm_hour, curr_time.tm_min, curr_time.tm_sec, curr_time.tm_mon, curr_time.tm_mday, curr_time.tm_year);

    fs_module_init();

    // TODO: If MAXM86146 is present without application firmware, enter bootloader mode

    pm_device_runtime_get(gpio_keys_dev);

    INPUT_CALLBACK_DEFINE(gpio_keys_dev, gpio_keys_cb_handler);

    // pm_device_runtime_put(w25_flash_dev);

    k_sem_give(&sem_hw_inited);

    // Start sampling if devices are present

    if (max30001_device_present)
    {
        k_sem_give(&sem_ecg_bioz_thread_start);
    }

    if (maxm86146_device_present)
    {
        k_sem_give(&sem_ppg_wrist_thread_start);
    }

    if (max32664d_device_present)
    {
        k_sem_give(&sem_ppg_finger_thread_start);
    }

    // init_settings();

    // usb_init();
}

void hw_thread(void)
{
    LOG_INF("HW Thread started\n");

    // hw_msbl_load();
    //  printk("Initing...\n");

    // ecg_sampling_timer_start();

    struct rtc_time sys_time;

    for (;;)
    {
        /*if (k_sem_take(&sem_start_cal, K_NO_WAIT) == 0)
        {
            hw_bpt_start_cal();
        }*/

        // fetch_and_display(acc_dev);

        npm_fuel_gauge_update(charger);

        rtc_get_time(rtc_dev, &sys_time);
        zbus_chan_pub(&sys_time_chan, &sys_time, K_SECONDS(1));

        //  send_usb_cdc("H ", 1);
        //  printk("H ");

        k_sleep(K_MSEC(6000));
    }
}

K_THREAD_DEFINE(hw_thread_id, HW_THREAD_STACKSIZE, hw_thread, NULL, NULL, NULL, HW_THREAD_PRIORITY, 0, 0);
