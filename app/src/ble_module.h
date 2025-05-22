#pragma once

void ble_module_init();
void ble_bas_notify(uint8_t batt_level);
void ble_bpt_cal_progress_notify(uint8_t bpt_status, uint8_t bpt_progress);
void hpi_ble_send_data(const uint8_t *data, uint16_t len);

void ble_ppg_notify_wr(uint32_t *ppg_data, uint8_t len);
void ble_ppg_notify_fi(uint32_t *ppg_data, uint8_t len);
void ble_ecg_notify(int32_t *ecg_data, uint8_t len);
void ble_gsr_notify(int32_t *gsr_data, uint8_t len);