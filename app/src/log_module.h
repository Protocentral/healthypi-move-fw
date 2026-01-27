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

#include <time.h>
#include "fs_module.h"

enum hpi_log_types
{
    HPI_LOG_TYPE_TREND_HR =0x01,
    HPI_LOG_TYPE_TREND_SPO2,
    HPI_LOG_TYPE_TREND_TEMP,
    HPI_LOG_TYPE_TREND_STEPS,
    HPI_LOG_TYPE_TREND_BPT,
    
    HPI_LOG_TYPE_ECG_RECORD = 0x10,
    HPI_LOG_TYPE_BIOZ_RECORD,
    HPI_LOG_TYPE_PPG_WRIST_RECORD,
    HPI_LOG_TYPE_PPG_FINGER_RECORD,
    HPI_LOG_TYPE_GSR_RECORD,
    HPI_LOG_TYPE_HRV_RECORD,
};

char* log_get_current_session_id_str(void);
void log_session_add_point(uint16_t time, int16_t current, uint16_t impedance);
//void log_write_to_file(struct tes_session_log_t *m_session_log);
void log_complete(void);
void log_wipe_trends(void);
void log_wipe_records(void);

void log_delete(uint16_t session_id);
void log_get(uint8_t log_type, int64_t file_id);
void log_delete_by_type(uint8_t log_type, uint16_t timestamp);
int log_get_index(uint8_t m_log_type);
void log_seq_init(void);
uint16_t log_get_count(uint8_t m_log_type);

void hpi_hr_trend_wr_point_to_file(struct hpi_hr_trend_point_t m_hr_trend_point, int64_t day_ts);
void hpi_spo2_trend_wr_point_to_file(struct hpi_spo2_point_t m_spo2_point, int64_t day_ts);
void hpi_temp_trend_wr_point_to_file(struct hpi_temp_trend_point_t m_temp_point, int64_t day_ts);
void hpi_steps_trend_wr_point_to_file(struct hpi_steps_t m_steps_point, int64_t day_ts);
void hpi_bpt_trend_wr_point_to_file(struct hpi_bpt_point_t m_bpt_point, int64_t day_ts);

void hpi_write_ecg_record_file(const int32_t *ecg_record_buffer, uint16_t ecg_record_length, int64_t start_ts);
void hpi_write_gsr_record_file(const int32_t *samples, uint16_t num_samples, int64_t timestamp);
void hpi_write_hrv_record_file(const uint16_t *hrv_record_buffer, uint16_t hrv_record_length, int64_t start_ts);

void log_wipe_folder(const char *folder_path);

