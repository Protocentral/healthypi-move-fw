#pragma once

uint32_t hw_keypad_get_key(void);

void hw_init(void);

void set_current(uint16_t current_uA);
uint16_t read_voltage(int channel_no);
void hw_pwr_display_enable(void);

void send_usb_cdc(const char *buf, size_t len);

void hw_rtc_set_time(uint8_t m_sec, uint8_t m_min, uint8_t m_hour, uint8_t m_day, uint8_t m_month, uint8_t m_year);

void hpi_bpt_pause_thread(void);
void hpi_bpt_abort(void);

void hpi_hw_pmic_off(void);

void hpi_pwr_display_sleep(void);
void hpi_pwr_display_wake(void);

struct rtc_time hw_get_current_time(void);

bool hw_is_max32664c_present(void);
int hw_max32664c_set_op_mode(uint8_t op_mode, uint8_t algo_mode);
int hw_max32664c_stop_algo(void);

enum gpio_keypad_key
{
    GPIO_KEYPAD_KEY_NONE = 0,
    GPIO_KEYPAD_KEY_OK,
    GPIO_KEYPAD_KEY_UP,
    GPIO_KEYPAD_KEY_DOWN,
    GPIO_KEYPAD_KEY_LEFT,
    GPIO_KEYPAD_KEY_RIGHT,
};

