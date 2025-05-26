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
};

char* log_get_current_session_id_str(void);
void log_session_add_point(uint16_t time, int16_t current, uint16_t impedance);
//void log_write_to_file(struct tes_session_log_t *m_session_log);
void log_complete(void);
void log_wipe_all(void);

void log_delete(uint16_t session_id);
void log_get(uint8_t log_type, int64_t file_id);
int log_get_index(uint8_t m_log_type);
void log_seq_init(void);
uint16_t log_get_count(uint8_t m_log_type);

void hpi_hr_trend_wr_point_to_file(struct hpi_hr_trend_point_t m_hr_trend_point, int64_t day_ts);
void hpi_spo2_trend_wr_point_to_file(struct hpi_spo2_point_t m_spo2_point, int64_t day_ts);
void hpi_temp_trend_wr_point_to_file(struct hpi_temp_trend_point_t m_temp_point, int64_t day_ts);
void hpi_steps_trend_wr_point_to_file(struct hpi_steps_t m_steps_point, int64_t day_ts);

void hpi_write_ecg_record_file(int16_t *ecg_record_buffer, uint16_t ecg_record_length, int64_t start_ts);
