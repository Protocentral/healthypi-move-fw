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


#pragma once

void hw_module_init(void);
void hw_pwr_display_enable(bool enable);

void send_usb_cdc(const char *buf, size_t len);

void hw_rtc_set_time(uint8_t m_sec, uint8_t m_min, uint8_t m_hour, uint8_t m_day, uint8_t m_month, uint8_t m_year);

void hpi_bpt_pause_thread(void);
void hpi_bpt_abort(void);

void hpi_hw_pmic_off(void);

void hpi_hw_fi_sensor_off(void);
void hpi_hw_fi_sensor_on(void);

void hpi_pwr_display_sleep(void);
void hpi_pwr_display_wake(void);

bool hw_is_max32664c_present(void);
int hw_max32664c_set_op_mode(uint8_t op_mode, uint8_t algo_mode);
int hw_max32664c_stop_algo(void);

bool get_on_skin(void);
void set_on_skin(bool on_skin);

void today_init_steps(uint16_t steps);

// Low battery management functions
bool hw_is_low_battery(void);
bool hw_is_critical_battery(void);
void hw_reset_low_battery_state(void);
uint8_t hw_get_current_battery_level(void);
float hw_get_current_battery_voltage(void);