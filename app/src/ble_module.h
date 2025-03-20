#pragma once

void ble_module_init();

void ble_bas_notify(uint8_t batt_level);
void ble_spo2_notify(uint16_t spo2_val);
void ble_temp_notify(uint16_t temp_val);
void ble_hrs_notify(uint16_t hr_val);

void ble_ecg_notify(int32_t *ecg_data, uint8_t len);
void ble_ppg_notify(int16_t *ppg_data, uint8_t len);
void ble_bioz_notify(int32_t *resp_data, uint8_t len);

void hpi_ble_send_data(const uint8_t *data, uint16_t len);