#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <app_version.h>

#include "ui/move_ui.h"

LOG_MODULE_REGISTER(scr_progress, LOG_LEVEL_WRN);

lv_obj_t *scr_progress;

lv_obj_t *label_title;
lv_obj_t *label_subtitle;

lv_obj_t *label_progress;
lv_obj_t *label_progress_status;
lv_obj_t *label_error_msg;

lv_obj_t *bar_progress;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_large;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_tiny;

void draw_scr_progress(char *title, char *message)
{
    scr_progress = lv_obj_create(NULL);
    lv_obj_clear_flag(scr_progress, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    lv_obj_add_style(scr_progress, &style_scr_black, 0);

    lv_obj_t *cont_col = lv_obj_create(scr_progress);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    // lv_obj_set_width(cont_col, lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(cont_col, &style_scr_black, 0);

    lv_obj_set_style_bg_color(cont_col, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(cont_col, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    label_title = lv_label_create(cont_col);
    lv_label_set_text(label_title, title ? title : "Progress");
    lv_obj_add_style(label_title, &style_white_medium, 0);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 20);

    label_subtitle = lv_label_create(cont_col);
    lv_label_set_text(label_subtitle, message ? message : "Please wait...");
    lv_obj_add_style(label_subtitle, &style_white_medium, 0);
    lv_obj_set_style_text_align(label_subtitle, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(label_subtitle, label_title, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);

    bar_progress = lv_bar_create(cont_col);
    lv_obj_set_size(bar_progress, 300, 40);
    // Set initial progress bar color
    lv_obj_set_style_bg_color(bar_progress, lv_palette_main(LV_PALETTE_BLUE), LV_PART_INDICATOR);

    label_progress = lv_label_create(cont_col);
    lv_label_set_text(label_progress, "0%");
    lv_obj_add_style(label_progress, &style_white_medium, 0);
    lv_obj_set_style_text_align(label_progress, LV_TEXT_ALIGN_CENTER, 0);

    // Error message label (initially hidden)
    label_error_msg = lv_label_create(cont_col);
    lv_label_set_text(label_error_msg, "");
    lv_obj_add_style(label_error_msg, &style_red_medium, 0);
    lv_obj_set_style_text_align(label_error_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(label_error_msg, LV_OBJ_FLAG_HIDDEN);

    hpi_disp_set_curr_screen(SCR_SPL_PROGRESS);
    hpi_show_screen(scr_progress, SCROLL_NONE);
}

void hpi_disp_scr_update_progress(int progress, char *status)
{
    if (label_progress == NULL)
        return;

    lv_label_set_text_fmt(label_progress, "%d %%", progress);
    lv_bar_set_value(bar_progress, progress, LV_ANIM_ON);

    if (status != NULL)
    {
        lv_label_set_text(label_subtitle, status);
        
        // Check if this is an error status and show error styling
        if (strstr(status, "Failed") || strstr(status, "Error") || 
            strstr(status, "Not Found") || strstr(status, "Missing") ||
            strstr(status, "No Firmware Files"))
        {
            // Show error state
            lv_obj_clear_flag(label_error_msg, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(label_error_msg, "✗ Update Failed");
            
            // Change progress bar color to red for error indication
            lv_obj_set_style_bg_color(bar_progress, lv_palette_main(LV_PALETTE_RED), LV_PART_INDICATOR);
            
            // Hide the percentage label as it's not meaningful in error state
            lv_obj_add_flag(label_progress, LV_OBJ_FLAG_HIDDEN);
            
            LOG_ERR("Progress screen detected error status: %s", status);
        }
        else if (strstr(status, "Complete") || strstr(status, "Success"))
        {
            // Show success state
            lv_obj_clear_flag(label_error_msg, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(label_error_msg, "✓ Update Complete");
            lv_obj_set_style_text_color(label_error_msg, lv_palette_main(LV_PALETTE_GREEN), 0);
            
            // Change progress bar color to green for success
            lv_obj_set_style_bg_color(bar_progress, lv_palette_main(LV_PALETTE_GREEN), LV_PART_INDICATOR);
        }
        else
        {
            // Normal progress state
            lv_obj_add_flag(label_error_msg, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(label_progress, LV_OBJ_FLAG_HIDDEN);
            
            // Reset progress bar to default color
            lv_obj_set_style_bg_color(bar_progress, lv_palette_main(LV_PALETTE_BLUE), LV_PART_INDICATOR);
        }
    }
}

void hpi_disp_scr_show_error(char *error_message)
{
    if (label_error_msg == NULL || label_subtitle == NULL)
        return;
    
    // Show prominent error display
    lv_obj_clear_flag(label_error_msg, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(label_error_msg, "✗ Update Failed");
    
    // Ensure the error message has red color styling
    lv_obj_remove_style_all(label_error_msg);
    lv_obj_add_style(label_error_msg, &style_red_medium, 0);
    lv_obj_set_style_text_color(label_error_msg, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_text_align(label_error_msg, LV_TEXT_ALIGN_CENTER, 0);
    
    // Update subtitle with specific error message
    if (error_message != NULL) {
        lv_label_set_text(label_subtitle, error_message);
    }
    
    // Hide progress percentage and set bar to red
    lv_obj_add_flag(label_progress, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(bar_progress, lv_palette_main(LV_PALETTE_RED), LV_PART_INDICATOR);
    
    // Force a refresh of the display
    lv_obj_invalidate(scr_progress);
    
    LOG_ERR("Progress screen showing error: %s", error_message ? error_message : "Unknown error");
}

void hpi_disp_scr_reset_progress(void)
{
    if (label_error_msg == NULL || label_progress == NULL || bar_progress == NULL)
        return;
    
    // Reset to normal progress state
    lv_obj_add_flag(label_error_msg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(label_progress, LV_OBJ_FLAG_HIDDEN);
    
    // Reset progress bar to default color and value
    lv_obj_set_style_bg_color(bar_progress, lv_palette_main(LV_PALETTE_BLUE), LV_PART_INDICATOR);
    lv_bar_set_value(bar_progress, 0, LV_ANIM_OFF);
    lv_label_set_text(label_progress, "0%");
    
    LOG_DBG("Progress screen reset to normal state");
}

void hpi_disp_scr_debug_status(void)
{
    LOG_DBG("Progress screen debug status:");
    LOG_DBG("  scr_progress: %s", scr_progress ? "initialized" : "null");
    LOG_DBG("  label_title: %s", label_title ? "initialized" : "null");
    LOG_DBG("  label_subtitle: %s", label_subtitle ? "initialized" : "null");
    LOG_DBG("  label_progress: %s", label_progress ? "initialized" : "null");
    LOG_DBG("  label_error_msg: %s", label_error_msg ? "initialized" : "null");
    LOG_DBG("  bar_progress: %s", bar_progress ? "initialized" : "null");
}
