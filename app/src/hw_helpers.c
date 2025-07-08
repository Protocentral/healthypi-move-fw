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