#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
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

#include "max30001.h"
#include "max32664.h"

#include "sys_sm_module.h"
#include "hw_module.h"
#include "fs_module.h"
#include "display_module.h"

#include <zephyr/sys/reboot.h>

LOG_MODULE_REGISTER(hw_module);
char curr_string[40];

/*******EXTERNS******/
extern struct k_msgq q_session_cmd_msg;

/****END EXTERNS****/

#define HW_THREAD_STACKSIZE 4096
#define HW_THREAD_PRIORITY 7

// Peripheral Device Pointers
const struct device *fg_dev = DEVICE_DT_GET_ANY(maxim_max17048);
const struct device *max30205_dev = DEVICE_DT_GET_ANY(maxim_max30205);
const struct device *max32664_dev = DEVICE_DT_GET_ANY(maxim_max32664);
const struct device *acc_dev = DEVICE_DT_GET_ONE(st_lsm6dso);
const struct device *const max30001_dev = DEVICE_DT_GET(DT_ALIAS(max30001));
static const struct device *rtc_dev = DEVICE_DT_GET(DT_ALIAS(rtc));
// const struct device *usb_dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);

// GPIO Keys and LEDs Device
static const struct device *const gpio_keys_dev = DEVICE_DT_GET_ANY(gpio_keys);
static const struct pwm_dt_spec pwm_led0 = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led0));

// PMIC Device Pointers
static const struct device *regulators = DEVICE_DT_GET(DT_NODELABEL(npm_pmic_regulators));
static const struct device *charger = DEVICE_DT_GET(DT_NODELABEL(npm_pmic_charger));
static const struct device *leds = DEVICE_DT_GET(DT_NODELABEL(npm_pmic_leds));
static const struct device *pmic = DEVICE_DT_GET(DT_NODELABEL(npm_pmic));

uint8_t global_batt_level = 0;
int32_t global_temp;

// USB CDC UART
#define RING_BUF_SIZE 1024
uint8_t ring_buffer[RING_BUF_SIZE];
struct ring_buf ringbuf_usb_cdc;
static bool rx_throttled;

uint8_t m_key_pressed = GPIO_KEYPAD_KEY_NONE;

K_SEM_DEFINE(sem_hw_inited, 0, 1);
K_SEM_DEFINE(sem_start_cal, 0, 1);

#define MOVE_SAMPLING_DISABLED 0

static void gpio_keys_cb_handler(struct input_event *evt)
{
    printk("GPIO_KEY %s pressed, zephyr_code=%u, value=%d\n",
           evt->dev->name, evt->code, evt->value);
    if (evt->value == 1)
    {
        switch (evt->code)
        {
        case INPUT_KEY_BACK:
            // m_key_pressed = GPIO_KEYPAD_KEY_OK;
            LOG_INF("OK Key Pressed");
            // k_sem_give(&sem_ok_key_pressed);
            break;
        case INPUT_KEY_UP:
            // m_key_pressed = GPIO_KEYPAD_KEY_UP;
            LOG_INF("UP Key Pressed");
            sys_reboot(SYS_REBOOT_COLD);
            break;
        case INPUT_KEY_DOWN:
            // m_key_pressed = GPIO_KEYPAD_KEY_DOWN;
            LOG_INF("DOWN Key Pressed");
            // k_sem_give(&sem_start_cal);
            sys_reboot(SYS_REBOOT_COLD);
            break;
        default:
            break;
        }
    }
}
INPUT_CALLBACK_DEFINE(gpio_keys_dev, gpio_keys_cb_handler);

void send_usb_cdc(const char *buf, size_t len)
{
    int rb_len;
    // rb_len = ring_buf_put(&ringbuf_usb_cdc, buf, len);
    // uart_irq_tx_enable(usb_dev);
}

/*
static void interrupt_handler(const struct device *dev, void *user_data)
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
*/

uint8_t read_battery_level(void)
{
    int ret = 0;
    uint8_t batt_level = 0;

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
    }

    return batt_level;
}

static void usb_init()
{
    int ret = 0;

    /*if (!device_is_ready(usb_dev))
    {
        LOG_ERR("CDC ACM device not ready");
        // return;
    }

    ret = usb_enable(NULL);
    if (ret != 0)
    {
        LOG_ERR("Failed to enable USB");
        // return;
    }
    */

    /* Enabled USB CDC interrupts */

    ring_buf_init(&ringbuf_usb_cdc, sizeof(ring_buffer), ring_buffer);
    k_msleep(100);
    // uart_irq_callback_set(usb_dev, interrupt_handler);
    // uart_irq_rx_enable(usb_dev);

    printk("\nUSB Init complete\n\n");
}

static inline float out_ev(struct sensor_value *val)
{
    return (val->val1 + (float)val->val2 / 1000000);
}

static void fetch_and_display(const struct device *dev)
{
    struct sensor_value x, y, z;
    // static int trig_cnt;

    // trig_cnt++;

    /* lsm6dso accel */
    sensor_sample_fetch_chan(dev, SENSOR_CHAN_ACCEL_XYZ);
    sensor_channel_get(dev, SENSOR_CHAN_ACCEL_X, &x);
    sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Y, &y);
    sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Z, &z);

    printf("accel x:%f ms/2 y:%f ms/2 z:%f ms/2\n",
           (double)out_ev(&x), (double)out_ev(&y), (double)out_ev(&z));

    /* lsm6dso gyro */
    sensor_sample_fetch_chan(dev, SENSOR_CHAN_GYRO_XYZ);
    sensor_channel_get(dev, SENSOR_CHAN_GYRO_X, &x);
    sensor_channel_get(dev, SENSOR_CHAN_GYRO_Y, &y);
    sensor_channel_get(dev, SENSOR_CHAN_GYRO_Z, &z);

    printf("gyro x:%f rad/s y:%f rad/s z:%f rad/s\n",
           (double)out_ev(&x), (double)out_ev(&y), (double)out_ev(&z));

    // printf("trig_cnt:%d\n\n", trig_cnt);
}

static int set_sampling_freq(const struct device *dev)
{
    int ret = 0;
    struct sensor_value odr_attr;

    /* set accel/gyro sampling frequency to 12.5 Hz */
    odr_attr.val1 = 12.5;
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
    sensor_attr_set(max32664_dev, SENSOR_CHAN_ALL, MAX32664_ATTR_DATE_TIME, &data_time_val);
    k_sleep(K_MSEC(100));

    struct sensor_value bp_cal_val;
    bp_cal_val.val1 = 0x00787A7D; // Sys vals
    bp_cal_val.val2 = 0x00505152; // Dia vals
    sensor_attr_set(max32664_dev, SENSOR_CHAN_ALL, MAX32664_ATTR_BP_CAL, &bp_cal_val);
    k_sleep(K_MSEC(100));

    // Start BPT Calibration
    struct sensor_value mode_val;
    mode_val.val1 = MAX32664_OP_MODE_BPT_CAL_START;
    sensor_attr_set(max32664_dev, SENSOR_CHAN_ALL, MAX32664_ATTR_OP_MODE, &mode_val);
    k_sleep(K_MSEC(100));

    ppg_data_start();
}

void hw_bpt_start_est(void)
{
    struct sensor_value load_cal;
    load_cal.val1 = 0x00000000;
    // sensor_attr_set(max32664_dev, SENSOR_CHAN_ALL, MAX32664_ATTR_LOAD_CALIB, &load_cal);

    struct sensor_value data_time_val;
    data_time_val.val1 = DEFAULT_FUTURE_DATE; // Date // TODO: Update to local time
    data_time_val.val2 = DEFAULT_FUTURE_TIME; // Time
    sensor_attr_set(max32664_dev, SENSOR_CHAN_ALL, MAX32664_ATTR_DATE_TIME, &data_time_val);
    k_sleep(K_MSEC(100));

    struct sensor_value mode_set;
    mode_set.val1 = MAX32664_OP_MODE_BPT;
    sensor_attr_set(max32664_dev, SENSOR_CHAN_ALL, MAX32664_ATTR_OP_MODE, &mode_set);

    k_sleep(K_MSEC(1000));
    ppg_data_start();
}

void hw_bpt_stop(void)
{
    struct sensor_value mode_val;
    mode_val.val1 = MAX32664_ATTR_STOP_EST;
    sensor_attr_set(max32664_dev, SENSOR_CHAN_ALL, MAX32664_ATTR_STOP_EST, &mode_val);
}

void hw_bpt_get_calib(void)
{
    struct sensor_value mode_val;
    mode_val.val1 = MAX32664_OP_MODE_BPT_CAL_GET_VECTOR;
    sensor_attr_set(max32664_dev, SENSOR_CHAN_ALL, MAX32664_ATTR_OP_MODE, &mode_val);
}

void hw_pmic_read_sensors(void)
{
	struct sensor_value volt;
	struct sensor_value current;
	struct sensor_value temp;
	struct sensor_value error;
	struct sensor_value status;

	sensor_sample_fetch(charger);
	sensor_channel_get(charger, SENSOR_CHAN_GAUGE_VOLTAGE, &volt);
	sensor_channel_get(charger, SENSOR_CHAN_GAUGE_AVG_CURRENT, &current);
	sensor_channel_get(charger, SENSOR_CHAN_GAUGE_TEMP, &temp);
	sensor_channel_get(charger, SENSOR_CHAN_NPM1300_CHARGER_STATUS, &status);
	sensor_channel_get(charger, SENSOR_CHAN_NPM1300_CHARGER_ERROR, &error);

	printk("V: %d.%03d ", volt.val1, volt.val2 / 1000);

	printk("I: %s%d.%04d ", ((current.val1 < 0) || (current.val2 < 0)) ? "-" : "",
	       abs(current.val1), abs(current.val2) / 100);

	printk("T: %d.%02d\n", temp.val1, temp.val2 / 10000);

	printk("Charger Status: %d, Error: %d\n", status.val1, error.val1);
}

void hw_thread(void)
{
    // int ret = 0;
    static struct rtc_time curr_time;

#ifdef CONFIG_SENSOR_MAX30001
    if (!device_is_ready(max30001_dev))
    {
        printk("MAX30001 device not found!");
        struct sensor_value ecg_mode_set;
        // ecg_mode_set.val1 = 1;
        // sensor_attr_set(max30001_dev, SENSOR_CHAN_ALL, MAX30001_ATTR_ECG_ENABLED, &ecg_mode_set);
        // sensor_attr_set(max30001_dev, SENSOR_CHAN_ALL, MAX30001_ATTR_BIOZ_ENABLED, &ecg_mode_set);

        // return;
    }
#endif

    if (!device_is_ready(max32664_dev))
    {
        printk("MAX32664 device not found!\n");
        // return;
    }
    else
    {
        struct sensor_value mode_set;
        mode_set.val1 = MAX32664_OP_MODE_BPT;
        sensor_attr_set(max32664_dev, SENSOR_CHAN_ALL, MAX32664_ATTR_OP_MODE, &mode_set);
    }

    if (!device_is_ready(fg_dev))
    {
        printk("No device found...\n");
    }
    if (!device_is_ready(max30205_dev))
    {
        LOG_ERR("MAX30205 device not found!\n");
        // return;
    }

    if (!device_is_ready(acc_dev))
    {
        printk("LSM6DSO device not ready!\n");
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

    if (!pwm_is_ready_dt(&pwm_led0))
    {
        printk("Error: PWM device %s is not ready\n",
               pwm_led0.dev->name);
        // return 0;
    }

    if (!device_is_ready(regulators)) {
		printk("Error: Regulator device is not ready\n");
		return 0;
	}

	if (!device_is_ready(charger)) {
		printk("Charger device not ready.\n");
		return 0;
	}

    rtc_get_time(rtc_dev, &curr_time);

    printk("Current time: %d:%d:%d\n", curr_time.tm_hour, curr_time.tm_min, curr_time.tm_sec);
    printk("Current date: %d/%d/%d\n", curr_time.tm_mon, curr_time.tm_mday, curr_time.tm_year);

    // fs_module_init();

    // init_settings();

    printk("HW Thread started\n");
    // printk("Initing...\n");

    k_sem_give(&sem_hw_inited);

    ppg_thread_create();

    

    // usb_init();

    // ble_module_init();

    // read_temp();

    for (;;)
    {
        /*if (k_sem_take(&sem_start_cal, K_NO_WAIT) == 0)
        {
            hw_bpt_start_cal();
        }*/

        // ifndef MOVE_SAMPLING_DISABLED
        // fetch_and_display(acc_dev);
        // k_sleep(K_MSEC(3000));
        k_sleep(K_MSEC(3000));

        hw_pmic_read_sensors();
    }
}

K_THREAD_DEFINE(hw_thread_id, HW_THREAD_STACKSIZE, hw_thread, NULL, NULL, NULL, HW_THREAD_PRIORITY, 0, 0);
