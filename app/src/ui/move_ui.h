#include <lvgl.h>

#define SAMPLE_RATE 125
#define DISP_WINDOW_SIZE_EDA 250

//Clock Screen functions
void draw_scr_clockface(enum scroll_dir m_scroll_dir);
void draw_scr_clock_small(enum scroll_dir m_scroll_dir);
void hpi_disp_draw_plotEDA(float data_eda);


void hpi_scr_home_hr_display_update(uint8_t hr_bpm);
void ui_time_display_update(uint8_t hour, uint8_t min, bool small);
void ui_date_display_update(uint8_t day, uint8_t month, uint16_t year);
void ui_battery_update(uint8_t percent);


// ECG Screen functions
void draw_scr_ecg(enum scroll_dir m_scroll_dir);
void hpi_ecg_disp_draw_plotECG(float data_ecg, bool ecg_lead_off);
void hpi_ecg_disp_update_hr(int hr);

// PPG screen functions
void hpi_disp_ppg_draw_plotPPG(float data_ppg);
void hpi_ppg_disp_do_set_scale(int disp_window_size);
void hpi_ppg_disp_update_hr(int hr);
void hpi_ppg_disp_add_samples(int num_samples);
void hpi_ppg_disp_update_spo2(int spo2);

// EDA screen functions
void draw_scr_eda(enum scroll_dir m_scroll_dir);

// BPT screen functions
void draw_scr_bpt_calibrate(void);
void draw_scr_bpt_home(enum scroll_dir m_scroll_dir);
void draw_scr_bpt_measure(void);

// HRV screen functions
void draw_scr_hrv(enum scroll_dir m_scroll_dir);
void hpi_disp_hrv_draw_plot_rtor(float rtor);
void hpi_disp_hrv_update_rtor(int rtor);
void hpi_disp_hrv_update_sdnn(int sdnn); 

// HRV Scatter screen functions
void draw_scr_hrv_scatter(enum scroll_dir m_scroll_dir);
void hpi_disp_hrv_scatter_draw_plot_rtor(float rtor, float prev_rtor);
void hpi_disp_hrv_scatter_update_rtor(int rtor);
void hpi_disp_hrv_scatter_update_sdnn(int sdnn);

// Helper objects
void draw_header_minimal(lv_obj_t *parent);
void hpi_move_load_screen(enum hpi_disp_screens m_screen, enum scroll_dir m_scroll_dir);
lv_obj_t *create_ui_steps(lv_obj_t *comp_parent);
lv_obj_t *create_ui_hr_button(lv_obj_t *comp_parent);
lv_obj_t *create_ui_spo2_button(lv_obj_t *comp_parent);

lv_obj_t *ui_dailymissiongroup_create(lv_obj_t *comp_parent);

void draw_bg(lv_obj_t *parent);

LV_IMG_DECLARE(heart);
LV_IMG_DECLARE( ui_img_flash_png);
LV_IMG_DECLARE( ui_img_step_png);   // assets/step.png
LV_IMG_DECLARE( ui_img_heart_png);   // assets/heart2.png
LV_IMG_DECLARE( ui_img_heart2_png);   // assets/heart2.png
LV_IMG_DECLARE( ui_img_daily_mission_png);   // assets/daily_mission.png
LV_IMG_DECLARE( ui_img_step_png);   // assets/step.png
LV_IMG_DECLARE( o2);

LV_FONT_DECLARE( ui_font_H1);
LV_FONT_DECLARE( ui_font_Number_big);
LV_FONT_DECLARE( ui_font_Number_extra);
LV_FONT_DECLARE( ui_font_Subtitle);
LV_FONT_DECLARE( ui_font_Title);
