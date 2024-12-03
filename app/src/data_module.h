#pragma once

#define ECG_DATA 0
#define PPG_DATA 1
#define RESP_DATA 2
#define ALL_DATA 3

void flush_current_session_logs(void);
void record_session_add_ppg_point(int16_t ppg_sample);
void record_session_add_ecg_point(int32_t *ecg_samples, uint8_t ecg_len, int32_t *bioz_samples, uint8_t bioz_len);





