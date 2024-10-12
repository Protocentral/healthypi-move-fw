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

LOG_MODULE_REGISTER(hw_helpers);

static const struct device *rtc_dev = DEVICE_DT_GET(DT_ALIAS(rtc));

void hpi_switch_cpu_64mhz(void)
{
    LOG_DBG("Switching application core from 128 MHz and 64 MHz.");
    nrfx_clock_divider_set(NRF_CLOCK_DOMAIN_HFCLK, NRF_CLOCK_HFCLK_DIV_2);
    LOG_DBG("NRF_CLOCK_S.HFCLKCTRL:%d\n", NRF_CLOCK_S->HFCLKCTRL);

    // nrf_spim_frequency_set(NRF_SPIM_INST_GET(4), NRF_SPIM_FREQ_32M);
    // nrf_spim_iftiming_set(NRF_SPIM_INST_GET(4), 0);
}

void hpi_switch_cpu_128mhz(void)
{
    LOG_DBG("Switching application core from 64 MHz and 128 MHz.");
    nrfx_clock_divider_set(NRF_CLOCK_DOMAIN_HFCLK, NRF_CLOCK_HFCLK_DIV_1);
    LOG_DBG("NRF_CLOCK_S.HFCLKCTRL:%d\n", NRF_CLOCK_S->HFCLKCTRL);

    // nrf_spim_frequency_set(NRF_SPIM_INST_GET(4), NRF_SPIM_FREQ_32M);
    // nrf_spim_iftiming_set(NRF_SPIM_INST_GET(4), 0);
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