#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <lvgl.h>
#include <stdio.h>

#include <zephyr/logging/log.h>

#include "hpi_common_types.h"
#include "ui/move_ui.h"
#include "trends.h"

LOG_MODULE_REGISTER(hpi_disp_scr_spo2_scr2, LOG_LEVEL_DBG);

lv_obj_t *scr_spo2_scr2;

// GUI Labels
static lv_obj_t *label_spo2_percent;
static lv_obj_t *label_spo2_last_update_time;
// static lv_obj_t *label_spo2_status;

// static lv_obj_t *label_min_max;
static lv_obj_t *btn_spo2_proceed;


// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_large;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_tiny;

extern lv_style_t style_bg_blue;
extern lv_style_t style_bg_red;

static void scr_spo2_btn_proceed_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        hpi_move_load_scr_spl(SCR_SPL_SPO2_SCR3, SCROLL_UP, (uint8_t)SCR_SPO2);
    }
}

void draw_scr_spo2_scr2(enum scroll_dir m_scroll_dir)
{
    scr_spo2_scr2 = lv_obj_create(NULL);
    lv_obj_add_style(scr_spo2_scr2, &style_scr_black, 0);
    // lv_obj_set_flag(scr_spo2, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    //draw_scr_common(scr_spo2_scr2);

    lv_obj_set_scrollbar_mode(scr_spo2_scr2, LV_SCROLLBAR_MODE_ON);

    /*Create a container with COLUMN flex direction*/
    lv_obj_t *cont_col = lv_obj_create(scr_spo2_scr2);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 25);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(cont_col, -1, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_top(cont_col, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(cont_col, 1, LV_PART_MAIN);
    lv_obj_add_style(cont_col, &style_scr_black, 0);
    lv_obj_add_style(cont_col, &style_bg_red, 0);

    lv_obj_t *label_info = lv_label_create(cont_col);
    lv_label_set_long_mode(label_info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_info, 300);
    lv_label_set_text(label_info, "Ensure that your Move is worn on the wrist as shown, not too tight nor too loose, away from the wrist bone.");
    lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *img_spo2 = lv_img_create(cont_col);
    lv_img_set_src(img_spo2, &img_spo2_hand);

    btn_spo2_proceed = lv_btn_create(cont_col);
    lv_obj_add_event_cb(btn_spo2_proceed, scr_spo2_btn_proceed_handler, LV_EVENT_ALL, NULL);
    lv_obj_set_height(btn_spo2_proceed, 85);

    lv_obj_t *label_btn = lv_label_create(btn_spo2_proceed);
    lv_label_set_text(label_btn, LV_SYMBOL_PLAY " Proceed");
    lv_obj_center(label_btn);

    hpi_disp_set_curr_screen(SCR_SPL_SPO2_SCR2);
    hpi_show_screen(scr_spo2_scr2, m_scroll_dir);
}

void hpi_disp_update_spo2(uint8_t spo2,struct tm tm_last_update)
{
    if (label_spo2_percent == NULL)
        return;

    if (spo2 == 0)
    {
        lv_label_set_text(label_spo2_percent, "-- %");
    }
    else
    {
        lv_label_set_text_fmt(label_spo2_percent, "%d %", spo2);
    }
    lv_label_set_text_fmt(label_spo2_last_update_time, "Last updated: %02d:%02d", tm_last_update.tm_hour, tm_last_update.tm_min);
}
    
/*void hpi_disp_spo2_load_trend(void)
{
    struct hpi_hourly_trend_point_t spo2_hourly_trend_points[SPO2_SCR_TREND_MAX_POINTS];
    if (chart_spo2_trend == NULL)
        return;

    int m_num_points = 0;

    //if(0)
    if(hpi_trend_load_day_trend(spo2_hourly_trend_points, &m_num_points, TREND_SPO2) == 0)
    {
        int y_max = -1;
        int y_min = 999;

        for (int i = 0; i < SPO2_SCR_TREND_MAX_POINTS; i++)
        {
            if(spo2_hourly_trend_points[i].max > y_max)
            {
                y_max = spo2_hourly_trend_points[i].max;
            }
            if((spo2_hourly_trend_points[i].min < y_min)&&(spo2_hourly_trend_points[i].min != 0))
            {
                y_min = spo2_hourly_trend_points[i].min;
            }

            ser_max_trend->y_points[i] = spo2_hourly_trend_points[i].max;
            ser_min_trend->y_points[i] = spo2_hourly_trend_points[i].min;

           // LOG_DBG("SpO2 Point: %d | %d | %d | %d", spo2_hourly_trend_points[i].hour_no, spo2_hourly_trend_points[i].max, spo2_hourly_trend_points[i].min, spo2_hourly_trend_points[i].avg);

            lv_chart_set_range(chart_spo2_trend, LV_CHART_AXIS_PRIMARY_Y, y_min, y_max);
            lv_chart_refresh(chart_spo2_trend);   
        }
    } else
    {
        LOG_ERR("No SpO2 data to load");
    }
}*/