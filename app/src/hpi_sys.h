#pragma once

void hpi_sys_set_last_hr_update(uint16_t hr_last_value, int64_t hr_last_update_ts);
void hpi_sys_set_last_spo2_update(uint8_t spo2_last_value, int64_t spo2_last_update_ts);
void hpi_sys_set_last_bp_update(uint16_t bp_sys_last_value, uint16_t bp_dia_last_value, int64_t bp_last_update_ts);
void hpi_sys_set_last_ecg_update(int64_t ecg_last_update_ts);

int hpi_sys_get_last_hr_update(uint16_t *hr_last_value, int64_t *hr_last_update_ts);
int hpi_sys_get_last_spo2_update(uint8_t *spo2_last_value, int64_t *spo2_last_update_ts);
int hpi_sys_get_last_bp_update(uint8_t *bp_sys_last_value, uint8_t *bp_dia_last_value, int64_t *bp_last_update_ts);
int hpi_sys_get_last_ecg_update(uint8_t *ecg_hr, int64_t *ecg_last_update_ts);
int hpi_sys_get_last_steps_update(uint16_t *steps_last_value, int64_t *steps_last_update_ts);
int hpi_sys_get_last_temp_update(uint16_t *temp_last_value_x100, int64_t *temp_last_update_ts);

void hpi_sys_set_device_on_skin(bool on_skin);
bool hpi_sys_get_device_on_skin(void);

int hpi_helper_get_relative_time_str(int64_t in_ts, char *out_str, size_t out_str_size);
int hpi_sys_set_sys_time(struct tm *tm);

struct tm hpi_sys_get_sys_time(void);
int64_t hw_get_sys_time_ts(void);

void hpi_data_set_ecg_record_active(bool active);
bool hpi_data_is_ecg_record_active(void);
