#pragma once

void ble_module_init();
void ble_bas_notify(uint8_t batt_level);
void ble_bpt_cal_progress_notify(uint8_t bpt_status, uint8_t bpt_progress);
void hpi_ble_send_data(const uint8_t *data, uint16_t len);