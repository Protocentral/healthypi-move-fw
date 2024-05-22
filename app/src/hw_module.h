#ifndef hw_module_h
#define hw_module_h

uint32_t hw_keypad_get_key(void);

void set_current(uint16_t current_uA);
uint16_t read_voltage(int channel_no);

void send_usb_cdc(const char *buf, size_t len);

void hw_bpt_start_cal(void);
void hw_bpt_get_calib(void);
void hw_bpt_start_est(void);
void hw_bpt_stop(void);

int readADC(void);

enum gpio_keypad_key
{
    GPIO_KEYPAD_KEY_NONE = 0,
    GPIO_KEYPAD_KEY_OK,
    GPIO_KEYPAD_KEY_UP,
    GPIO_KEYPAD_KEY_DOWN,
    GPIO_KEYPAD_KEY_LEFT,
    GPIO_KEYPAD_KEY_RIGHT,
};

#endif