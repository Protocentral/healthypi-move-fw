/*
 * HealthyPi Move
 * 
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Author: Ashwin Whitchurch, Protocentral Electronics
 * Contact: ashwin@protocentral.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


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

#include <zephyr/drivers/sensor/npm13xx_charger.h>
#include <zephyr/dt-bindings/regulator/npm13xx.h>
#include <zephyr/drivers/mfd/npm13xx.h>

#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/zbus/zbus.h>

#include <time.h>

#include <nrfx_clock.h>
#include <nrfx_spim.h>

LOG_MODULE_REGISTER(hw_helpers);

/* ============================ DO NOT DELETE ============================
 * An audit (2026-07-16) found this file orphaned -- both functions have zero
 * callers and (until now) no prototype in hw_helpers.h. That is accurate but
 * misleading: this is working, measurement-gated P4 scaffolding, not rot.
 *
 * Halving the application core (128 -> 64 MHz) is a real power lever, and P4's
 * remaining levers are all deferred to the PPK/measurement session (see
 * docs/ARCHITECTURE_REWRITE_PLAN.md §0 P4) precisely because there is no way to
 * judge them without current measurements. Same call as scr_aod.c: built,
 * proposed, waiting on the rig -- keep it.
 *
 * When P4 picks this up: 64 MHz halves HFCLK, so anything deriving timing from it
 * needs re-checking, notably the SPIM4 display link (see the commented
 * nrf_spim_frequency_set/iftiming_set below -- they are the known follow-on, not
 * decoration) and any peripheral running near its clock ceiling.
 * =======================================================================
 */

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