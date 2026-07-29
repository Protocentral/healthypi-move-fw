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

/* BMI323 any-motion (CONFIG_HPI_IMU_MOTION_WAKE). `last_motion_s` is the uptime, in
 * seconds, of the most recent physical motion assertion -- the stillness signal the
 * HRV gate needs (motion destroys pulse-rate variability). Defined in hw_module.c;
 * these were previously not declared anywhere, so nothing outside that file could use
 * them. */
uint32_t hpi_hw_get_motion_count(void);
int64_t  hpi_hw_get_last_motion_s(void);

void hpi_bpt_pause_thread(void);
void hpi_bpt_abort(void);

/* BPT calibration control (HPI_HS group v2, cmds 8-11). Non-blocking — safe to
 * call from the SMP/mgmt thread. enter/point/end return 0 or a negative errno
 * (-EINVAL bad idx, -EBUSY a point already running); status fills a snapshot for
 * the polled BPT_CAL_STATUS command. Cal is a fixed 3 points (idx 0..2). */
int  hpi_bpt_cal_enter(void);
int  hpi_bpt_cal_point(uint8_t sys, uint8_t dia, uint8_t idx);
int  hpi_bpt_cal_end(void);
/* `idx` is the point the DEVICE is measuring (it used to echo back the index the
 * client last requested, which told the client nothing it did not already know). */
void hpi_bpt_cal_status(uint8_t *st, uint8_t *prog, uint8_t *idx, bool *run);

/* Points completed by the device in this calibration session, 0..3. Monotonic
 * within a session. Clients should advance their UI on THIS, not on the
 * (prog == 100 && !run) pair: prog latches at 100 when a point finishes and
 * stays there until the next CAL_POINT, so consecutive polls in that window are
 * indistinguishable and a client that advances per-poll skips a point. */
uint8_t hpi_bpt_cal_points_done(void);

/* Bitmask of calibration vectors stored in /lfs/sys (bit n = point n); 0x7 means
 * fully calibrated. Survives reboot, RAM-cached so it is safe on the SMP thread.
 * This is what tells the phone whether BP is set up on this watch — previously
 * nothing reported that at all, so the app could only ever show "BP Not Set". */
uint8_t hpi_bpt_cal_vectors(void);

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

void today_init_steps(uint32_t steps);

/* Recovered-fault breadcrumb (P0 safety net, main.c) — for surfacing on the
 * display when the console is unavailable. false if the last boot was clean. */
bool hpi_crash_get_last(uint32_t *reason, char *thread, size_t thread_sz, uint32_t *count);

// Low battery management functions
bool hw_is_low_battery(void);
uint8_t hw_get_current_battery_level(void);
float hw_get_current_battery_voltage(void);
bool hw_is_vbus_connected(void);   /* true when USB/charger power is present */