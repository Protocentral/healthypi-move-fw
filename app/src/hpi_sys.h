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

/* Last-value get/set API removed — the health store (app/src/health/) owns
 * per-metric last values + persistence now. */

void hpi_sys_set_device_on_skin(bool on_skin);
bool hpi_sys_get_device_on_skin(void);

void hpi_display_signal_touch_wakeup(void);

int hpi_helper_get_relative_time_str(int64_t in_ts, char *out_str, size_t out_str_size);
int hpi_sys_set_sys_time(struct tm *tm);
int64_t hw_get_sys_time_ts(void);

// Time validation - checks if device time has been properly set
// Returns true if time is valid (year >= 2020), false if time needs to be set
bool hpi_sys_is_time_valid(void);

// True if a stored UTC timestamp falls on the current local calendar day.
bool hpi_sys_ts_is_today(int64_t ts_utc);

// Time synchronization functions
int64_t hw_get_synced_system_time(void);
void hpi_sys_set_rtc_time(const struct tm *time_to_set);
int hpi_sys_force_time_sync(void);
struct tm hpi_sys_get_current_time(void);

/* UTC offset (seconds east of UTC; local = UTC + offset). Set + persisted via the
 * HPI_HS SET_TZ command; applied only at the display / local-calendar edge. */
void hpi_sys_set_utc_offset(int32_t offset_sec);
int32_t hpi_sys_get_utc_offset(void);

void hpi_data_set_ecg_record_active(bool active);
void hpi_data_reset_ecg_record_buffer(void);
bool hpi_data_is_ecg_record_active(void);

void hpi_data_set_gsr_record_active(bool active);
bool hpi_data_is_gsr_record_active(void);
void hpi_data_reset_gsr_record_buffer(void);

/* H-REC: episodic wrist/finger PPG capture. Mirror the ECG/GSR bracket — the
 * owning SMF calls set(true) at measurement start and set(false) on every exit
 * (complete / timeout / cancel). Raw FIFO batches stream into the open record
 * from the data_thread drain while active. */
void hpi_data_set_ppg_wrist_record_active(bool active);
bool hpi_data_is_ppg_wrist_record_active(void);

void hpi_data_set_ppg_finger_record_active(bool active);
bool hpi_data_is_ppg_finger_record_active(void);
/* Mark the in-flight GSR capture as user-cancelled, so the H-REC session is
 * dropped rather than stored when it stops. Must be called BEFORE
 * hpi_data_set_gsr_record_active(false). The SMF's `ecg_cancellation` flag
 * cannot be reused: it is also set true on a *successful* GSR completion. */
void hpi_data_set_gsr_cancelled(bool cancelled);
int hpi_data_get_last_scr_count(void);

void hpi_data_set_gsr_measurement_active(bool active);
bool hpi_data_is_gsr_measurement_active(void);
float hpi_data_get_last_converted_us(void);
void hpi_data_set_hrv_record_active(bool active);
void hpi_data_reset_hrv_record_buffer(void);
bool hpi_data_is_hrv_record_active(void);

void hpi_data_set_gsr_measurement_active(bool active);
bool hpi_data_is_gsr_measurement_active(void);

void hpi_data_set_hrv_eval_active(bool active);
bool hpi_data_is_hrv_eval_active(void);
struct hpi_hrv_eval_result_t *hpi_data_get_hrv_eval_result(void);
void hpi_data_add_hrv_interval(uint16_t rtor_ms);
void hpi_data_hrv_record_to_file(bool active);
struct hpi_hrv_eval_result_t hpi_data_get_hrv_result(void);
void hpi_data_reset_hrv_record_buffer(void);

void gsr_background_start(void);
void gsr_background_stop(void);
