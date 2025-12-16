#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <lvgl.h>
#include <stdio.h>

#include "hpi_common_types.h"
#include "hw_module.h"
#include "ui/move_ui.h"

LOG_MODULE_REGISTER(scr_timeout, LOG_LEVEL_DBG);

lv_obj_t *scr_finger_timeout;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_red_large;
extern lv_style_t style_white_medium;
extern lv_style_t style_scr_black;
extern lv_style_t style_white_large_numeric;
static int source = 0;

void draw_scr_timeout(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    scr_finger_timeout = lv_obj_create(NULL);
    source = arg1;
    lv_obj_add_style(scr_finger_timeout, &style_scr_black, 0);
    lv_obj_add_flag(scr_finger_timeout, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    /*Create a container with COLUMN flex direction optimized for 390x390 circular display*/
    lv_obj_t *cont_col = lv_obj_create(scr_finger_timeout);
    lv_obj_set_size(cont_col, 350, 350); /* Fit within circular bounds */
    lv_obj_align_to(cont_col, NULL, LV_ALIGN_CENTER, 0, 0); /* Center the container */
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(cont_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(cont_col, &style_scr_black, 0);
    lv_obj_set_style_pad_all(cont_col, 20, LV_PART_MAIN); /* Add padding for circular bounds */
    lv_obj_set_style_pad_row(cont_col, 0, LV_PART_MAIN);  


    lv_obj_t *label_signal = lv_label_create(cont_col);
    lv_label_set_text(label_signal, "Timeout occured");
    lv_obj_add_style(label_signal, &style_red_large, 0); 
    lv_obj_set_style_text_color(label_signal, lv_color_hex(0x8B0000), LV_PART_MAIN);

    lv_obj_t *label_info = lv_label_create(cont_col);
    lv_label_set_long_mode(label_info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_info, 280); /* Reduced width for circular display */
    lv_label_set_text(label_info, "Finger sensor not connected\nPlease ensure proper connection and try again.");
    lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_style(label_info, &style_white_medium, 0); /* Consistent text styling */

    hpi_disp_set_curr_screen(SCR_SPL_SPO2_BPT_TIMEOUT);
    hpi_show_screen(scr_finger_timeout, m_scroll_dir);
}

void gesture_down_scr_timeout(void)
{
    if(source == SCR_SPO2)
    {
        LOG_INF("Gesture Down on SpO2 Timeout Screen - Cancelling SpO2 Measurement");
        hpi_load_screen(SCR_SPO2, SCROLL_DOWN);
    }
    else
    {
        LOG_INF("Gesture Down on Finger Timeout Screen - Cancelling BPT Measurement");
        hpi_load_screen(SCR_BPT, SCROLL_DOWN);
    }
}