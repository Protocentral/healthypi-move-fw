/*
HealthyPi specific common data types
*/

#pragma once

#include <time.h>

#define ECG_POINTS_PER_SAMPLE 16
#define BIOZ_POINTS_PER_SAMPLE 8
#define PPG_POINTS_PER_SAMPLE 8
#define BPT_PPG_POINTS_PER_SAMPLE 32

#define ECG_RECORD_BUFFER_SAMPLES 3040 //128 *30 

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
};