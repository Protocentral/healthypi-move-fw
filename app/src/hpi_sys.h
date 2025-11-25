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

void hpi_sys_set_last_hr_update(uint16_t hr_last_value, int64_t hr_last_update_ts);
void hpi_sys_set_last_spo2_update(uint8_t spo2_last_value, int64_t spo2_last_update_ts);
void hpi_sys_set_last_bp_update(uint16_t bp_sys_last_value, uint16_t bp_dia_last_value, int64_t bp_last_update_ts);
void hpi_sys_set_last_ecg_update(int64_t ecg_last_update_ts);
void hpi_sys_set_last_gsr_update(uint16_t gsr_last_value, int64_t gsr_last_update_ts);

int hpi_sys_get_last_hr_update(uint16_t *hr_last_value, int64_t *hr_last_update_ts);
int hpi_sys_get_last_spo2_update(uint8_t *spo2_last_value, int64_t *spo2_last_update_ts);
int hpi_sys_get_last_bp_update(uint8_t *bp_sys_last_value, uint8_t *bp_dia_last_value, int64_t *bp_last_update_ts);
int hpi_sys_get_last_ecg_update(uint8_t *ecg_hr, int64_t *ecg_last_update_ts);
int hpi_sys_get_last_steps_update(uint16_t *steps_last_value, int64_t *steps_last_update_ts);
int hpi_sys_get_last_temp_update(uint16_t *temp_last_value_x100, int64_t *temp_last_update_ts);
int hpi_sys_get_last_gsr_update(uint16_t *gsr_last_value, int64_t *gsr_last_update_ts);

void hpi_sys_set_device_on_skin(bool on_skin);
bool hpi_sys_get_device_on_skin(void);

void hpi_display_signal_touch_wakeup(void);

int hpi_helper_get_relative_time_str(int64_t in_ts, char *out_str, size_t out_str_size);
int hpi_sys_set_sys_time(struct tm *tm);

struct tm hpi_sys_get_sys_time(void);
int64_t hw_get_sys_time_ts(void);

// Time synchronization functions
int64_t hw_get_synced_system_time(void);
void hpi_sys_set_rtc_time(const struct tm *time_to_set);
int hpi_sys_sync_time_if_needed(void);
int hpi_sys_force_time_sync(void);
struct tm hpi_sys_get_current_time(void);

void hpi_data_set_ecg_record_active(bool active);
void hpi_data_reset_ecg_record_buffer(void);
bool hpi_data_is_ecg_record_active(void);

void hpi_data_set_gsr_record_active(bool active);
bool hpi_data_is_gsr_record_active(void);


void hpi_data_set_gsr_measurement_active(bool active);
bool hpi_data_is_gsr_measurement_active(void);
