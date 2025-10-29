#include <zephyr/kernel.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <stdio.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/rtc.h>
#include <time.h>

#include "ui/move_ui.h"
#include "hw_module.h"
#include "hpi_user_settings_api.h"

lv_obj_t *scr_home;

static lv_obj_t *meter_clock;
static lv_obj_t *home_step_disp;
static lv_obj_t *home_hr_disp;

static lv_obj_t *ui_home_label_hour;
static lv_obj_t *ui_home_label_min;
static lv_obj_t *ui_home_label_date;
static lv_obj_t *ui_home_label_ampm;

/**
 * Format time according to user's 12/24 hour preference
 * @param in_time Input time structure
 * @param hour_buf Buffer for hour string (minimum 4 characters)
 * @param min_buf Buffer for minute string (minimum 3 characters)
 * @param ampm_buf Buffer for AM/PM string (minimum 3 characters), can be NULL for 24-hour format
 */
static void format_time_for_display(struct tm in_time, char *hour_buf, char *min_buf, char *ampm_buf)
{
    uint8_t time_format = hpi_user_settings_get_time_format();
    
    if (time_format == 0) {
        // 24-hour format
        sprintf(hour_buf, "%02d:", in_time.tm_hour);
        sprintf(min_buf, "%02d", in_time.tm_min);
        if (ampm_buf) {
            ampm_buf[0] = '\0'; // Empty string for 24-hour format
        }
    } else {
        // 12-hour format
        int hour_12 = in_time.tm_hour;
        bool is_pm = false;
        
        if (hour_12 == 0) {
            hour_12 = 12; // 12 AM
        } else if (hour_12 > 12) {
            hour_12 -= 12; // Convert to 12-hour format
            is_pm = true;
        } else if (hour_12 == 12) {
            is_pm = true; // 12 PM
        }
        
        sprintf(hour_buf, "%2d:", hour_12);
        sprintf(min_buf, "%02d", in_time.tm_min);
        if (ampm_buf) {
            sprintf(ampm_buf, "%s", is_pm ? "PM" : "AM");
        }
    }
}

static lv_obj_t *label_batt_level_val;

// Externs
extern lv_style_t style_lbl_white_14;

void draw_scr_home(enum scroll_dir m_scroll_dir)
{
    scr_home = lv_obj_create(NULL);

    lv_obj_set_style_bg_color(scr_home, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr_home, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    draw_bg(scr_home);
    // draw_header_minimal(scr_home, 320);

    home_step_disp = ui_steps_button_create(scr_home);
    lv_obj_align_to(home_step_disp, NULL, LV_ALIGN_TOP_MID, -80, 210);
    lv_obj_set_style_border_opa(home_step_disp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    home_hr_disp = ui_hr_button_create(scr_home);
    lv_obj_align_to(home_hr_disp, NULL, LV_ALIGN_TOP_MID, 80, 210);
    lv_obj_set_style_border_opa(home_hr_disp, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_home_label_hour = lv_label_create(scr_home);
    // lv_obj_set_width(ui_label_hour, LV_SIZE_CONTENT);  /// 1
    // lv_obj_set_height(ui_label_hour, LV_SIZE_CONTENT); /// 1
    lv_obj_align_to(ui_home_label_hour, NULL, LV_ALIGN_TOP_MID, -90, 70);
    lv_label_set_text(ui_home_label_hour, "00:");
    lv_obj_set_style_text_color(ui_home_label_hour, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_home_label_hour, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_home_label_hour, &ui_font_number_big, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_home_label_min = lv_label_create(scr_home);
    // lv_obj_set_width(ui_label_min, LV_SIZE_CONTENT);  /// 1
    // lv_obj_set_height(ui_label_min, LV_SIZE_CONTENT); /// 1
    lv_obj_align_to(ui_home_label_min, ui_home_label_hour, LV_ALIGN_OUT_RIGHT_TOP, 0, 0);
    lv_label_set_text(ui_home_label_min, "00");
    lv_obj_set_style_text_color(ui_home_label_min, lv_color_hex(0xEE1E1E), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_home_label_min, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_home_label_min, &ui_font_number_big, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_home_label_ampm = lv_label_create(scr_home);
    lv_obj_align_to(ui_home_label_ampm, ui_home_label_min, LV_ALIGN_OUT_RIGHT_BOTTOM, 2, 10);
    lv_label_set_text(ui_home_label_ampm, "");
    lv_obj_set_style_text_color(ui_home_label_ampm, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_home_label_ampm, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_home_label_ampm, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_home_label_date = lv_label_create(scr_home);
    lv_obj_set_width(ui_home_label_date, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_home_label_date, LV_SIZE_CONTENT); /// 1
    lv_label_set_text(ui_home_label_date, "-- --- ----");
    lv_obj_align_to(ui_home_label_date, NULL, LV_ALIGN_TOP_MID, -20, 150);
    lv_obj_set_style_text_color(ui_home_label_date, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_home_label_date, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_home_label_date, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    label_batt_level_val = lv_label_create(scr_home);
    lv_label_set_text(label_batt_level_val, LV_SYMBOL_BATTERY_FULL "  --");
    lv_obj_add_style(label_batt_level_val, &style_lbl_white_14, LV_STATE_DEFAULT);
    lv_obj_align_to(label_batt_level_val, NULL, LV_ALIGN_TOP_MID, 0, 25);

    // ui_home_time_display_update(hw_get_current_time());

    hpi_disp_set_curr_screen(SCR_HOME);
    hpi_show_screen(scr_home, m_scroll_dir);
}



/*void draw_scr_home(enum scroll_dir m_scroll_dir)
{
    draw_scr_home_digital(m_scroll_dir);
}*/

void hpi_scr_home_update_time_date(struct tm in_time)
{
    if (ui_home_label_hour == NULL || ui_home_label_min == NULL || ui_home_label_date == NULL)
        return;

    char hour_buf[5];
    char min_buf[3];
    char ampm_buf[3];
    char date_buf[20];

    char mon_strs[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    // Format time according to user's 12/24 hour preference
    format_time_for_display(in_time, hour_buf, min_buf, ampm_buf);
    
    lv_label_set_text(ui_home_label_hour, hour_buf);
    lv_label_set_text(ui_home_label_min, min_buf);
    
    // Update AM/PM label if it exists
    if (ui_home_label_ampm != NULL) {
        lv_label_set_text(ui_home_label_ampm, ampm_buf);
    }

    sprintf(date_buf, "%02d %s %04d", in_time.tm_mday, mon_strs[in_time.tm_mon], in_time.tm_year + 1900);
    lv_label_set_text(ui_home_label_date, date_buf);
}

void hpi_home_hr_update(int hr)
{
    // printk("HR Update : %d\n", hr);
    if (home_hr_disp == NULL)
        return;

    char buf[5];
    sprintf(buf, "%d", hr);
    lv_label_set_text(home_hr_disp, buf);
}

void hpi_home_steps_update(int steps)
{
    // printk("Steps Update : %d\n", steps);
    if (home_step_disp == NULL)
        return;

    char buf[5];
    sprintf(buf, "%d", steps);
    lv_label_set_text(home_step_disp, buf);
}

void hpi_disp_home_update_batt_level(int batt_level, bool charging)
{
    if (label_batt_level_val == NULL)
    {
        return;
    }

    if (batt_level < 0)
    {
        batt_level = 0;
    }

    if (batt_level > 75)
    {
        if (charging)
            lv_label_set_text_fmt(label_batt_level_val, LV_SYMBOL_CHARGE " " LV_SYMBOL_BATTERY_FULL " %d %%", batt_level);
        else
            lv_label_set_text_fmt(label_batt_level_val, LV_SYMBOL_BATTERY_FULL " %d %%", batt_level);
    }

    else if (batt_level > 50)
    {
        if (charging)
            lv_label_set_text_fmt(label_batt_level_val, LV_SYMBOL_CHARGE " " LV_SYMBOL_BATTERY_3 " %d %%", batt_level);
        else
            lv_label_set_text_fmt(label_batt_level_val, LV_SYMBOL_BATTERY_3 " %d %%", batt_level);
    }
    else if (batt_level > 25)
    {
        if (charging)
            lv_label_set_text_fmt(label_batt_level_val, LV_SYMBOL_CHARGE " " LV_SYMBOL_BATTERY_2 " %d %%", batt_level);
        else
            lv_label_set_text_fmt(label_batt_level_val, LV_SYMBOL_BATTERY_2 " %d %%", batt_level);
    }
    else if (batt_level > 10)
    {
        if (charging)
            lv_label_set_text_fmt(label_batt_level_val, LV_SYMBOL_CHARGE " " LV_SYMBOL_BATTERY_1 " %d %%", batt_level);
        else
            lv_label_set_text_fmt(label_batt_level_val, LV_SYMBOL_BATTERY_1 " %d %%", batt_level);
    }
    else
    {
        if (charging)
            lv_label_set_text_fmt(label_batt_level_val, LV_SYMBOL_CHARGE " " LV_SYMBOL_BATTERY_EMPTY " %d %%", batt_level);
        else
            lv_label_set_text_fmt(label_batt_level_val, LV_SYMBOL_BATTERY_EMPTY " %d %%", batt_level);
    }
}