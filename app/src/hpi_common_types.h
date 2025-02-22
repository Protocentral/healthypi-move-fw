/*
HealthyPi specific common data types
*/

#pragma once

#define ECG_POINTS_PER_SAMPLE 8
#define BIOZ_POINTS_PER_SAMPLE 8
#define PPG_POINTS_PER_SAMPLE 8
#define BPT_PPG_POINTS_PER_SAMPLE 32

enum hpi_ppg_status 
{
    HPI_PPG_STATUS_UNKNOWN,
    HPI_PPG_STATUS_OFF_SKIN,
    HPI_PPG_STATUS_ON_OBJ,
    HPI_PPG_STATUS_ON_SKIN,
};

struct hpi_hr_trend_point_t
{
    uint32_t timestamp;
    uint16_t hr;
};

struct hpi_hr_trend_day_t
{
    struct hpi_hr_trend_point_t hr_points[1440];
    uint32_t time_last_update;
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
    uint16_t hr;
    bool hr_ready_flag;
};

struct hpi_steps_t
{
    uint32_t steps_run;
    uint32_t steps_walk;
};

struct hpi_temp_t
{
    double temp_f;
    double temp_c;
};

struct hpi_bpt_t
{
    uint32_t timestamp;

    uint16_t sys;
    uint16_t dia;
    uint16_t hr;

    uint8_t status;
    uint8_t progress;
};

struct hpi_spo2_t
{
    uint32_t timestamp;

    uint16_t spo2;
    uint16_t hr;
};

struct hpi_batt_status_t
{
    uint8_t batt_level;
    bool batt_charging;
};
