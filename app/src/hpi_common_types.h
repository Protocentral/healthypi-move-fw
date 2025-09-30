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


/*
HealthyPi specific common data types
*/

#pragma once

#include <time.h>

#define ECG_POINTS_PER_SAMPLE 16
#define BIOZ_POINTS_PER_SAMPLE 8
#define PPG_POINTS_PER_SAMPLE 8
#define BPT_PPG_POINTS_PER_SAMPLE 32

#define ECG_RECORD_BUFFER_SAMPLES 1920 // Reduced from 3840 to 1920 (128*15 instead of 128*30) - saves 7.68KB 

enum hpi_ppg_status 
{
    HPI_PPG_SCD_STATUS_UNKNOWN,
    HPI_PPG_SCD_OFF_SKIN,
    HPI_PPG_SCD_ON_OBJ,
    HPI_PPG_SCD_ON_SKIN,
};

struct hpi_ecg_bioz_sensor_data_t
{
    int32_t ecg_samples[ECG_POINTS_PER_SAMPLE];
    int32_t bioz_sample[BIOZ_POINTS_PER_SAMPLE];

    uint8_t ecg_num_samples;
    uint8_t bioz_num_samples;

    uint16_t rtor;
    uint16_t hr;
    uint8_t ecg_lead_off;
    uint8_t bioz_lead_off;
    bool _bioZSkipSample;

    uint8_t rrint;
};

struct hpi_gsr_sensor_data_t
{
    int32_t bioz_samples[BIOZ_POINTS_PER_SAMPLE];  // Raw BioZ samples from MAX30001
    uint8_t bioz_num_samples;                      // Number of valid samples in this batch
    uint8_t bioz_lead_off;                         // Lead-off detection status
};

/*
 * Lightweight BioZ-only sample used for internal producer/consumer queues
 * when ECG decoding is not required. Keeps the copy footprint small.
 */
struct hpi_bioz_sample_t
{
    int32_t bioz_samples[BIOZ_POINTS_PER_SAMPLE];
    uint8_t bioz_num_samples;
    uint8_t bioz_lead_off;
    int64_t timestamp;
};

struct hpi_ppg_wr_data_t
{
    uint32_t raw_red[PPG_POINTS_PER_SAMPLE];
    uint32_t raw_ir[PPG_POINTS_PER_SAMPLE];
    uint32_t raw_green[PPG_POINTS_PER_SAMPLE];

    uint8_t ppg_num_samples;

    uint16_t hr;
    uint8_t hr_confidence;

    uint8_t spo2;
    uint8_t spo2_confidence;
    uint8_t spo2_valid_percent_complete;
	uint8_t spo2_state;
	uint8_t spo2_excessive_motion;
	uint8_t spo2_low_pi;

    uint8_t bp_sys;
    uint8_t bp_dia;
    uint8_t bpt_status;
    uint8_t bpt_progress;

    uint16_t rtor;
    uint8_t rtor_confidence;
    
    uint8_t scd_state;
};

struct hpi_ppg_fi_data_t
{
    uint32_t raw_red[BPT_PPG_POINTS_PER_SAMPLE];
    uint32_t raw_ir[BPT_PPG_POINTS_PER_SAMPLE];

    uint8_t ppg_num_samples;

    uint16_t hr;
    uint8_t hr_confidence;

    uint8_t spo2;
    uint8_t spo2_confidence;
    uint8_t spo2_valid_percent_complete;
    uint8_t spo2_state;

    uint8_t bp_sys;
    uint8_t bp_dia;
    uint8_t bpt_status;
    uint8_t bpt_progress;

    uint16_t rtor;
    uint8_t rtor_confidence;
    
    uint8_t scd_state;
};

/*struct hpi_ppg_fi_bpt_cal_t
{
    uint32_t 
}*/

struct hpi_computed_hrv_t
{
    int32_t hrv_max;
    int32_t hrv_min;
    float mean;
    float sdnn;
    float pnn;
    float rmssd;
    bool hrv_ready_flag;
};

struct hpi_hr_t
{
    int64_t timestamp;
    uint16_t hr;
    bool hr_ready_flag;
};

struct hpi_steps_t
{
    int64_t timestamp;
    uint16_t steps;
};

struct hpi_temp_t
{
    int64_t timestamp;
    double temp_f;
    double temp_c;
};

struct hpi_bpt_t
{
    int64_t timestamp;

    uint16_t sys;
    uint16_t dia;
    uint16_t hr;

    uint8_t spo2;
    uint8_t spo2_conf;
    uint8_t spo2_report;

    uint8_t status;
    uint8_t progress;
};

struct hpi_bpt_point_t
{ 
    int64_t timestamp;

    uint16_t sys;
    uint16_t dia;
    uint16_t hr;
};

struct hpi_spo2_point_t
{
    int64_t timestamp;
    uint16_t spo2;
};

struct hpi_batt_status_t
{
    uint8_t batt_level;
    bool batt_charging;
};

enum hpi_ecg_status
{
    HPI_ECG_STATUS_IDLE = 0x00,
    HPI_ECG_STATUS_STREAMING,
    HPI_ECG_STATUS_COMPLETE,
    HPI_ECG_STATUS_ERROR,
};

enum hpi_ble_event
{
    HPI_BLE_EVENT_PAIR_REQUEST =0x01,
    HPI_BLE_EVENT_PAIR_SUCCESS,
    HPI_BLE_EVENT_PAIR_FAILED,
    HPI_BLE_EVENT_PAIR_CANCELLED,
};

enum hpi_bpt_status
{
    HPI_BPT_STATUS_IDLE = 0x00,
    HPI_BPT_STATUS_MEASURING,
    HPI_BPT_STATUS_COMPLETE,
    HPI_BPT_STATUS_ERROR,
    HPI_BPT_STATUS_CANCELLED,
    HPI_BPT_STATUS_TIMEOUT,
};

struct hpi_ecg_status_t
{
    int64_t ts_complete;
    uint8_t status;
    uint16_t progress_timer;
    uint8_t hr;
};

struct hpi_ecg_lead_on_off_t
{
    bool lead_on_off;
};

enum spo2_meas_state
{
    SPO2_MEAS_LED_ADJ = 0x00,
    SPO2_MEAS_COMPUTATION,
    SPO2_MEAS_SUCCESS,
    SPO2_MEAS_TIMEOUT,
    SPO2_MEAS_UNK,
};

enum spo2_source
{
    SPO2_SOURCE_PPG_WR = 0x00,
    SPO2_SOURCE_PPG_FI,
};

struct hpi_version_desc_t
{
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
};

struct hpi_last_update_time_t
{
    uint16_t hr_last_value;
    int64_t hr_last_update_ts;

    uint8_t spo2_last_value;
    int64_t spo2_last_update_ts;
    
    uint8_t bp_sys_last_value;
    uint8_t bp_dia_last_value;
    int64_t bp_last_update_ts;

    uint8_t ecg_last_hr;
    int64_t ecg_last_update_ts;

    uint16_t steps_last_value;
    int64_t steps_last_update_ts;

    uint16_t temp_last_value;
    int64_t temp_last_update_ts;

    uint16_t gsr_last_value; // GSR value * 100 (microsiemens)
    int64_t gsr_last_update_ts;
};

struct hpi_gsr_stress_index_t
{
    uint8_t stress_level;              // 0-100 stress score
    uint16_t tonic_level_x100;         // Baseline GSR (SCL) in μS * 100
    uint16_t phasic_amplitude_x100;    // Current phasic response (SCR) in μS * 100
    uint8_t peaks_per_minute;          // Number of SCR peaks detected per minute
    uint16_t mean_peak_amplitude_x100; // Average peak amplitude in μS * 100
    int64_t last_peak_timestamp;       // Timestamp of last detected peak
    bool stress_data_ready;            // Flag indicating valid stress data
};