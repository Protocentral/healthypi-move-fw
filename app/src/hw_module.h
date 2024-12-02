#pragma once

uint32_t hw_keypad_get_key(void);

void hw_init(void);

void set_current(uint16_t current_uA);
uint16_t read_voltage(int channel_no);
void hw_pwr_display_enable(void);

void send_usb_cdc(const char *buf, size_t len);

void hw_bpt_start_cal(void);
void hw_bpt_get_calib(void);
void hw_bpt_start_est(void);
void hw_bpt_stop(void);
void hw_rtc_set_time(uint8_t m_sec, uint8_t m_min, uint8_t m_hour, uint8_t m_day, uint8_t m_month, uint8_t m_year);

void hpi_pwr_display_sleep(void);
void hpi_pwr_display_wake(void);

struct rtc_time hw_get_current_time(void);

uint8_t hw_get_battery_level(void);
void hw_set_battery_level(uint8_t batt_level);

bool hw_is_maxm86146_present(void);
int hw_maxm86146_set_op_mode(uint8_t mode);

int hw_max30001_ecg_enable(bool enable);
int hw_max30001_bioz_enable(bool enable);

enum gpio_keypad_key
{
    GPIO_KEYPAD_KEY_NONE = 0,
    GPIO_KEYPAD_KEY_OK,
    GPIO_KEYPAD_KEY_UP,
    GPIO_KEYPAD_KEY_DOWN,
    GPIO_KEYPAD_KEY_LEFT,
    GPIO_KEYPAD_KEY_RIGHT,
};

struct batt_status
{
    uint8_t batt_level;
    bool batt_charging;
};