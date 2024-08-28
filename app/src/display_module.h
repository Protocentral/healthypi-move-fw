#define pragma once

#include <lvgl.h>

#define SCREEN_TRANS_TIME 300
#define DISP_THREAD_REFRESH_INT_MS 2

#define DISP_SLEEP_TIME_MS 20000

#define DISPLAY_DEFAULT_BRIGHTNESS 175

#define PPG_DISP_WINDOW_SIZE 128 // SAMPLE_RATE * 4
#define HRV_DISP_WINDOW_SIZE 128 
enum hpi_disp_screens
{
    SCR_LIST_START,
    SCR_CLOCK_ANALOG,
    //SCR_CLOCK_SMALL,
    
    SCR_PLOT_PPG,
    SCR_BPT_HOME,
    SCR_PLOT_EDA,
    SCR_PLOT_ECG,
    SCR_PLOT_HRV,
    SCR_PLOT_HRV_SCATTER,
    
    SCR_LIST_END,
    // Should not go here
    SCR_CLOCK,
    SCR_VITALS,
    //SCR_BPT_HOME,
};

enum hpi_disp_subscreens
{
    SUBSCR_BPT_CALIBRATE,
    SUBSCR_BPT_MEASURE,
};

enum scroll_dir
{
    SCROLL_UP,
    SCROLL_DOWN,
    SCROLL_LEFT,
    SCROLL_RIGHT,
};

void draw_scr_ppg(enum scroll_dir m_scroll_dir);

void draw_scr_vitals_home(enum scroll_dir m_scroll_dir);

void hpi_disp_update_batt_level(int batt_level, bool charging);
void hpi_disp_update_temp(int temp);

void hpi_show_screen(lv_obj_t *parent, enum scroll_dir m_scroll_dir);
void disp_screen_event(lv_event_t *e);