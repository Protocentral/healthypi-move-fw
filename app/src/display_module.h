#define pragma once

enum disp_screns {
    SCR_HOME,
    SCR_VITALS,
    SCR_CLOCK,
    SCR_PLOT_PPG,
    SCR_PLOT_EDA,
    SCR_PLOT_ECG,
    SCR_BPT_HOME,
    SCR_BPT_CALIBRATE,
    SCR_BPT_MEASURE,
};

enum scroll_dir {
    SCROLL_UP,
    SCROLL_DOWN,
    SCROLL_LEFT,
    SCROLL_RIGHT,
};

void draw_scr_home_menu(void);
void draw_scr_ppg(enum scroll_dir m_scroll_dir);
void draw_scr_menu(char* session_names);

void draw_plotppg(float data_ppg);
void draw_plotresp(float data_resp);
void draw_plotECG(float data_ecg);

void hpi_disp_update_batt_level(int batt_level);
void hpi_disp_update_temp(int temp);