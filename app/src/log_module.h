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
    
    HPI_LOG_TYPE_ECG_RECORD,
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
void log_get(uint16_t session_id);
int log_get_index(uint8_t m_log_type);
void log_seq_init(void);
uint16_t log_get_count(uint8_t m_log_type);