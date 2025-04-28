#pragma once

void hw_module_init(void);
void hw_pwr_display_enable(void);

void send_usb_cdc(const char *buf, size_t len);

void hw_rtc_set_time(uint8_t m_sec, uint8_t m_min, uint8_t m_hour, uint8_t m_day, uint8_t m_month, uint8_t m_year);

void hpi_bpt_pause_thread(void);
void hpi_bpt_abort(void);

void hpi_hw_pmic_off(void);

void hpi_pwr_display_sleep(void);
void hpi_pwr_display_wake(void);

struct tm hw_get_sys_time(void);
int64_t hw_get_sys_time_ts(void);

bool hw_is_max32664c_present(void);
int hw_max32664c_set_op_mode(uint8_t op_mode, uint8_t algo_mode);
int hw_max32664c_stop_algo(void);

int hpi_smf_ppg_get_last_spo2(uint16_t *spo2_value, int64_t *timestamp);
