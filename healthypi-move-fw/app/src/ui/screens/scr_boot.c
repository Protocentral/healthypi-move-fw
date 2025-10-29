#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <app_version.h>

#include "ui/move_ui.h"

LOG_MODULE_REGISTER(boot_module, LOG_LEVEL_WRN);

lv_obj_t *scr_boot;
static lv_obj_t *label_boot_messages;
static lv_obj_t *scroll_container;

void scr_boot_add_final(bool status);

// Externs
extern lv_style_t style_red_medium;

void draw_scr_boot(void)
{
    // Create main screen
    scr_boot = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_boot, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr_boot, LV_OPA_COVER, LV_PART_MAIN);

    // Main container with flex layout
    lv_obj_t *main_container = lv_obj_create(scr_boot);
    lv_obj_set_size(main_container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(main_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(main_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(main_container, 20, LV_PART_MAIN);
    lv_obj_set_flex_flow(main_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(main_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Title label
    lv_obj_t *label_hpi = lv_label_create(main_container);
    lv_label_set_text(label_hpi, "HealthyPi Move");
    lv_obj_set_style_text_color(label_hpi, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_margin_bottom(label_hpi, 10, LV_PART_MAIN);

    // Version label
    lv_obj_t *label_boot = lv_label_create(main_container);
    lv_label_set_text(label_boot, "Booting v" APP_VERSION_STRING);
    lv_obj_set_style_text_color(label_boot, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_margin_bottom(label_boot, 20, LV_PART_MAIN);

    // Scrollable container for boot messages
    scroll_container = lv_obj_create(main_container);
    lv_obj_set_size(scroll_container, LV_PCT(80), 250);
    lv_obj_set_style_bg_color(scroll_container, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scroll_container, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_border_color(scroll_container, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(scroll_container, 1, LV_PART_MAIN);
    lv_obj_set_style_border_opa(scroll_container, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(scroll_container, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scroll_container, 10, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(scroll_container, LV_SCROLLBAR_MODE_AUTO);

    // Boot messages label inside scroll container
    label_boot_messages = lv_label_create(scroll_container);
    lv_label_set_text(label_boot_messages, "");
    lv_obj_set_width(label_boot_messages, LV_PCT(100));
    lv_label_set_long_mode(label_boot_messages, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(label_boot_messages, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(label_boot_messages, LV_ALIGN_TOP_LEFT, 0, 0);

    hpi_disp_set_curr_screen(SCR_SPL_BOOT);
    hpi_show_screen(scr_boot, SCROLL_RIGHT);
}

void scr_boot_add_status(char *dev_label, bool status, bool show_status)
{
    char buf[64];
    if (show_status)
    {
        sprintf(buf, "%s: %s\n", dev_label, status ? "OK" : "FAIL");
    }
    else
    {
        sprintf(buf, "%s\n", dev_label);
    }

    // Get current text and append new message using static buffer
    const char *current_text = lv_label_get_text(label_boot_messages);
    static char full_text[2048]; // Static buffer to avoid malloc/free
    
    // Safely copy and concatenate
    strncpy(full_text, current_text, sizeof(full_text) - 1);
    full_text[sizeof(full_text) - 1] = '\0';
    
    size_t current_len = strlen(full_text);
    size_t remaining = sizeof(full_text) - current_len - 1;
    
    if (remaining > 0) {
        strncat(full_text, buf, remaining);
    }
    
    lv_label_set_text(label_boot_messages, full_text);
    
    // Auto-scroll to bottom to show latest message
    lv_obj_scroll_to_y(scroll_container, LV_COORD_MAX, LV_ANIM_ON);
}

void scr_boot_add_final(bool status)
{
    char buf[64];
    sprintf(buf, "\nCOMPLETE: %s\n", status ? "OK" : "FAIL");
    
    // Get current text and append final message using static buffer
    const char *current_text = lv_label_get_text(label_boot_messages);
    static char full_text[2048]; // Static buffer to avoid malloc/free
    
    // Safely copy and concatenate
    strncpy(full_text, current_text, sizeof(full_text) - 1);
    full_text[sizeof(full_text) - 1] = '\0';
    
    size_t current_len = strlen(full_text);
    size_t remaining = sizeof(full_text) - current_len - 1;
    
    if (remaining > 0) {
        strncat(full_text, buf, remaining);
    }
    
    lv_label_set_text(label_boot_messages, full_text);
    
    // Auto-scroll to bottom to show final message
    lv_obj_scroll_to_y(scroll_container, LV_COORD_MAX, LV_ANIM_ON);
}
