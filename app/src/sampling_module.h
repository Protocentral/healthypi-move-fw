#pragma once

void ppg_thread_create(void);
void ppg_data_start(void);
void ppg_data_stop(void);

struct hpi_ecg_bioz_sensor_data_t
{
    int32_t ecg_sample;
    int32_t bioz_sample;
    uint16_t rtor_sample;
    uint16_t hr_sample;
    uint8_t ecg_lead_off;
    uint8_t bioz_lead_off;
    bool _bioZSkipSample;
};

struct hpi_ppg_sensor_data_t
{
    uint32_t raw_red;
    uint32_t raw_ir;
    uint32_t raw_green;
    
    uint8_t hr;
    uint8_t spo2;
    
    uint8_t bp_sys;
    uint8_t bp_dia;
    uint8_t bpt_status;
    uint8_t bpt_progress;

    uint16_t rtor;
    uint8_t scd_state;

    uint32_t steps_run;
    uint32_t steps_walk;
};

struct hpi_computed_hrv_t {
    int32_t hrv_max;
    int32_t hrv_min;
    float mean;
    float sdnn;
    float pnn;
    float rmssd; 
    bool hrv_ready_flag;
};

