#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <lvgl.h>
#include <stdio.h>

#include "hpi_common_types.h"
#include "ui/move_ui.h"
#include "hw_module.h"

lv_obj_t *scr_low_battery;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_white_large;

static void btn_power_off_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED)
    {
        hpi_hw_pmic_off();
    }
}

void draw_scr_spl_low_battery(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    uint8_t battery_level = (uint8_t)arg1; // Battery level passed as argument
    bool is_charging = (bool)arg2; // Charging status passed as arg2
    float battery_voltage = (float)arg3 / 100.0f; // Voltage passed as arg3 (multiplied by 100)

    scr_low_battery = lv_obj_create(NULL);
    lv_obj_add_style(scr_low_battery, &style_scr_black, 0);
    lv_obj_add_flag(scr_low_battery, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    /*Create a container with COLUMN flex direction*/
    lv_obj_t *cont_col = lv_obj_create(scr_low_battery);
    lv_obj_set_size(cont_col, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(cont_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(cont_col, &style_scr_black, 0);

    lv_obj_t *label_title = lv_label_create(cont_col);
    lv_label_set_text(label_title, "Low Battery");
    lv_obj_add_style(label_title, &style_red_medium, 0);

    // Battery icon using LVGL symbols
    lv_obj_t *label_battery_icon = lv_label_create(cont_col);
    lv_label_set_text(label_battery_icon, LV_SYMBOL_BATTERY_EMPTY);
    lv_obj_add_style(label_battery_icon, &style_white_large, 0);

    // Display battery percentage and voltage
    lv_obj_t *label_battery_info = lv_label_create(cont_col);
    lv_label_set_text_fmt(label_battery_info, "%d%% (%.2fV)", battery_level, battery_voltage);
    lv_obj_add_style(label_battery_info, &style_white_large, 0);

    lv_obj_t *label_info = lv_label_create(cont_col);
    lv_label_set_long_mode(label_info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_info, 330);
    lv_label_set_text(label_info, "Battery voltage is critically low.\n\nPlease connect charger immediately to prevent shutdown.");
    lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_style(label_info, &style_white_medium, 0);

    // Add charging status info if available
    if (is_charging) {
        lv_obj_t *label_charging = lv_label_create(cont_col);
        lv_label_set_text(label_charging, LV_SYMBOL_CHARGE " Charging...");
        lv_obj_add_style(label_charging, &style_white_medium, 0);
        lv_obj_set_style_text_color(label_charging, lv_color_hex(0x00FF00), 0); // Green color for charging
    }

    // Add power off button
    lv_obj_t *btn_power_off = lv_btn_create(cont_col);
    lv_obj_set_size(btn_power_off, LV_PCT(80), 60);

    lv_obj_t *label_btn = lv_label_create(btn_power_off);
    lv_label_set_text(label_btn, LV_SYMBOL_POWER " Power Off");
    lv_obj_center(label_btn);

    // Add event callback for power off button
    lv_obj_add_event_cb(btn_power_off, btn_power_off_event_cb, LV_EVENT_ALL, NULL);

    hpi_disp_set_curr_screen(SCR_SPL_LOW_BATTERY);
    hpi_show_screen(scr_low_battery, m_scroll_dir);
}

void gesture_down_scr_spl_low_battery(void)
{
    // No action on gesture down
}
