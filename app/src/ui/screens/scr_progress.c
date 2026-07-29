/*
 * HealthyPi Move
 * 
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Author: Ashwin Whitchurch, Protocentral Electronics
 * Contact: ashwin@protocentral.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <app_version.h>

#include "ui/move_ui.h"
#include "ui/hpi_r0_theme.h"

LOG_MODULE_REGISTER(scr_progress, LOG_LEVEL_WRN);

lv_obj_t *scr_progress;

lv_obj_t *label_title;
lv_obj_t *label_subtitle;

lv_obj_t *label_progress;
lv_obj_t *label_progress_status;
lv_obj_t *label_error_msg;
static lv_obj_t *label_error_icon;   /* status glyph beside label_error_msg */

lv_obj_t *bar_progress;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_white_large_numeric;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_tiny;

// Modern style system
extern lv_style_t style_body_medium;
extern lv_style_t style_numeric_large;
extern lv_style_t style_caption;

void draw_scr_progress(const char *title, const char *message)
{
    hpi_scr_release_current();   /* reclaim the outgoing screen before building */

    scr_progress = lv_obj_create(NULL);
    lv_obj_clear_flag(scr_progress, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    // AMOLED OPTIMIZATION: Pure black background for power efficiency
    lv_obj_set_style_bg_color(scr_progress, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *cont_col = lv_obj_create(scr_progress);
    lv_obj_set_size(cont_col, lv_pct(100), lv_pct(100));
    // lv_obj_set_width(cont_col, lv_pct(100));
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(cont_col, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(cont_col, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);

    label_title = lv_label_create(cont_col);
    lv_label_set_text(label_title, title ? title : "Progress");
    lv_obj_add_style(label_title, &style_body_medium, 0);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 20);

    label_subtitle = lv_label_create(cont_col);
    lv_label_set_text(label_subtitle, message ? message : "Please wait...");
    lv_obj_add_style(label_subtitle, &style_caption, 0);
    lv_obj_set_style_text_align(label_subtitle, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label_subtitle, lv_color_hex(V2_MUTED2), 0);
    lv_obj_align_to(label_subtitle, label_title, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);

    bar_progress = lv_bar_create(cont_col);
    lv_obj_set_size(bar_progress, 300, 40);
    // Set initial progress bar color
    lv_obj_set_style_bg_color(bar_progress, lv_color_hex(V2_ACCENT), LV_PART_INDICATOR);

    label_progress = lv_label_create(cont_col);
    lv_label_set_text(label_progress, "0%");
    lv_obj_add_style(label_progress, &style_white_medium, 0);
    lv_obj_set_style_text_align(label_progress, LV_TEXT_ALIGN_CENTER, 0);

    /* Status glyph, on its own label so the text beside it can stay on the design
     * system's Manrope bin.
     *
     * This used to be one label forced onto &lv_font_montserrat_24, because the
     * string carried LV_SYMBOL_OK / LV_SYMBOL_CLOSE (FontAwesome) which the baked
     * bins do not have. Montserrat is not in the handoff's 4-bin budget (it is a
     * web-property face, not a watch face) and pulls a whole extra LVGL built-in
     * into flash. The premise that "no matsym bin has a glyph to swap in" was
     * stale: matsym_24 already bakes 0xF083 warning and 0xF0BE check_circle. */
    label_error_icon = lv_label_create(cont_col);
    lv_label_set_text(label_error_icon, SYM_WARNING);
    lv_obj_set_style_text_font(label_error_icon, &HPI_FONT_ICON, 0);
    lv_obj_set_style_text_color(label_error_icon, lv_color_hex(R0_ERROR), 0);
    lv_obj_add_flag(label_error_icon, LV_OBJ_FLAG_HIDDEN);

    // Error message label (initially hidden)
    label_error_msg = lv_label_create(cont_col);
    lv_label_set_text(label_error_msg, "");
    lv_obj_add_style(label_error_msg, &style_red_medium, 0);
    lv_obj_set_style_text_color(label_error_msg, lv_color_hex(R0_ERROR), 0);
    lv_obj_set_style_text_align(label_error_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(label_error_msg, LV_OBJ_FLAG_HIDDEN);

    hpi_disp_set_curr_screen(SCR_SPL_PROGRESS);
    hpi_show_screen(scr_progress, SCROLL_NONE);
}

/* Show the terminal status line: matsym glyph + Manrope text, both in `rgb`.
 * Two labels because one LVGL label carries one font, and the design system's
 * icon and text bins are separate (matsym_24 / manrope_700_22). */
static void hpi_progress_set_status_glyph(const char *sym, const char *text, uint32_t rgb)
{
    if (label_error_msg == NULL) {
        return;
    }
    if (label_error_icon != NULL) {
        lv_label_set_text(label_error_icon, sym);
        lv_obj_set_style_text_color(label_error_icon, lv_color_hex(rgb), 0);
        lv_obj_clear_flag(label_error_icon, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(label_error_msg, text);
    lv_obj_set_style_text_color(label_error_msg, lv_color_hex(rgb), 0);
    lv_obj_clear_flag(label_error_msg, LV_OBJ_FLAG_HIDDEN);
}

void hpi_disp_scr_update_progress(int progress, const char *status)
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
            hpi_progress_set_status_glyph(SYM_WARNING, "Update Failed", R0_ERROR);

            // Change progress bar color to red for error indication
            lv_obj_set_style_bg_color(bar_progress, lv_color_hex(R0_ERROR), LV_PART_INDICATOR);
            
            // Hide the percentage label as it's not meaningful in error state
            lv_obj_add_flag(label_progress, LV_OBJ_FLAG_HIDDEN);
            
            LOG_ERR("Progress screen detected error status: %s", status);
        }
        else if (strstr(status, "Complete") || strstr(status, "Success"))
        {
            // Show success state
            hpi_progress_set_status_glyph(SYM_CHECK_CIRCLE, "Update Complete", V2_GREEN);

            // Change progress bar color to green for success
            lv_obj_set_style_bg_color(bar_progress, lv_color_hex(V2_GREEN), LV_PART_INDICATOR);
        }
        else
        {
            // Normal progress state
            lv_obj_add_flag(label_error_msg, LV_OBJ_FLAG_HIDDEN);
            if (label_error_icon != NULL) {
                lv_obj_add_flag(label_error_icon, LV_OBJ_FLAG_HIDDEN);
            }
            lv_obj_clear_flag(label_progress, LV_OBJ_FLAG_HIDDEN);

            // Reset progress bar to the accent color
            lv_obj_set_style_bg_color(bar_progress, lv_color_hex(V2_ACCENT), LV_PART_INDICATOR);
        }
    }
}

void hpi_disp_scr_show_error(const char *error_message)
{
    if (label_error_msg == NULL || label_subtitle == NULL)
        return;
    
    /* Show prominent error display. No remove_style_all() here any more: it used
     * to strip the font this screen set at build time, which is why the font had
     * to be re-applied right after. The glyph now lives on its own label, so the
     * text label keeps its build-time styling and only the color changes. */
    hpi_progress_set_status_glyph(SYM_WARNING, "Update Failed", R0_ERROR);
    lv_obj_set_style_text_align(label_error_msg, LV_TEXT_ALIGN_CENTER, 0);


    // Update subtitle with specific error message
    if (error_message != NULL) {
        lv_label_set_text(label_subtitle, error_message);
    }
    
    // Hide progress percentage and set bar to red
    lv_obj_add_flag(label_progress, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(bar_progress, lv_color_hex(R0_ERROR), LV_PART_INDICATOR);
    
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
