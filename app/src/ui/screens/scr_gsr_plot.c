/*
 * HealthyPi Move GSR Plot Screen
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include "ui/move_ui.h"
#include "hpi_sys.h"

LOG_MODULE_REGISTER(hpi_disp_scr_gsr_plot, LOG_LEVEL_DBG);

// Extern semaphore declarations for GSR control
extern struct k_sem sem_gsr_cancel;

// GUI
static lv_obj_t *scr_gsr_plot;
static lv_obj_t *chart_gsr_trend;
static lv_chart_series_t *ser_gsr_trend;
static lv_obj_t *btn_stop;

// Styles extern
extern lv_style_t style_scr_black;

// Local state
static bool plot_ready = false;

static void scr_gsr_stop_btn_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED)
    {
        // Stop GSR measurement using semaphore control (same pattern as ECG)
        k_sem_give(&sem_gsr_cancel);
        hpi_load_screen(SCR_GSR, SCROLL_DOWN);
    }
}

void draw_scr_gsr_plot(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    LV_UNUSED(arg1); LV_UNUSED(arg2); LV_UNUSED(arg3); LV_UNUSED(arg4);

    scr_gsr_plot = lv_obj_create(NULL);
    // AMOLED: solid black background for power savings
    lv_obj_set_style_bg_color(scr_gsr_plot, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_gsr_plot, LV_OBJ_FLAG_SCROLLABLE);

    // Container with column layout
    lv_obj_t *cont_col = lv_obj_create(scr_gsr_plot);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(cont_col, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_clear_flag(cont_col, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t *label_title = lv_label_create(cont_col);
    lv_label_set_text(label_title, "GSR Trend");
    lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // Chart - larger, cleaner trend view
    chart_gsr_trend = lv_chart_create(cont_col);
    lv_obj_set_size(chart_gsr_trend, 380, 160);
    lv_chart_set_point_count(chart_gsr_trend, 80);
    lv_chart_set_update_mode(chart_gsr_trend, LV_CHART_UPDATE_MODE_CIRCULAR);
    lv_chart_set_div_line_count(chart_gsr_trend, 0, 0);
    lv_obj_set_style_bg_opa(chart_gsr_trend, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(chart_gsr_trend, 0, LV_PART_MAIN);
    lv_chart_set_type(chart_gsr_trend, LV_CHART_TYPE_LINE);
    ser_gsr_trend = lv_chart_add_series(chart_gsr_trend, lv_color_hex(COLOR_PRIMARY_BLUE), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_line_width(chart_gsr_trend, 3, LV_PART_ITEMS);
    lv_chart_set_all_value(chart_gsr_trend, ser_gsr_trend, 0);

    // Stop button - primary, compact
    btn_stop = hpi_btn_create_primary(cont_col);
    lv_obj_set_size(btn_stop, 140, 48);
    lv_obj_add_event_cb(btn_stop, scr_gsr_stop_btn_event_handler, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_radius(btn_stop, 22, LV_PART_MAIN);
    lv_obj_t *label_btn = lv_label_create(btn_stop);
    lv_label_set_text(label_btn, "Stop");
    lv_obj_center(label_btn);
    lv_obj_set_style_text_color(label_btn, lv_color_hex(COLOR_PRIMARY_BLUE), LV_PART_MAIN);

    plot_ready = true;

    hpi_disp_set_curr_screen(SCR_SPL_PLOT_GSR);
    hpi_show_screen(scr_gsr_plot, m_scroll_dir);
}

void hpi_gsr_disp_plot_add_sample(uint16_t gsr_value_x100)
{
    LOG_DBG("GSR plot add sample: %u, plot_ready=%d, chart=%p, series=%p", 
            gsr_value_x100, plot_ready, chart_gsr_trend, ser_gsr_trend);
    
    if (!plot_ready || !chart_gsr_trend || !ser_gsr_trend) {
        LOG_WRN("GSR plot not ready or objects null");
        return;
    }
    
    // Light autoscale around incoming values
    static uint16_t min_v = 65535, max_v = 0;
    static bool first_sample = true;
    
    // Reset min/max on first sample to avoid stuck scaling
    if (first_sample) {
        min_v = gsr_value_x100;
        max_v = gsr_value_x100;
        first_sample = false;
        LOG_DBG("GSR plot: First sample, reset scaling");
    }
    
    if (gsr_value_x100 < min_v) min_v = gsr_value_x100;
    if (gsr_value_x100 > max_v) max_v = gsr_value_x100;
    int y_min = (min_v > 50) ? (min_v - 50) : 0;
    int y_max = max_v + 50;
    if (y_max - y_min < 200) { int c = (y_min + y_max)/2; y_min = c - 100; y_max = c + 100; }
    
    LOG_DBG("GSR plot scaling: min_v=%u, max_v=%u, y_min=%d, y_max=%d", 
            min_v, max_v, y_min, y_max);
    
    lv_chart_set_range(chart_gsr_trend, LV_CHART_AXIS_PRIMARY_Y, y_min, y_max);

    lv_chart_set_next_value(chart_gsr_trend, ser_gsr_trend, gsr_value_x100);
    lv_chart_refresh(chart_gsr_trend);
}

void gesture_down_scr_gsr_plot(void)
{
    printk("Cancel GSR\n");
    k_sem_give(&sem_gsr_cancel);
    hpi_load_screen(SCR_GSR, SCROLL_DOWN);
}
