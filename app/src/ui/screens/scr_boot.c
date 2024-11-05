#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>
#include <stdio.h>
#include <app_version.h>

#include "ui/move_ui.h"

LOG_MODULE_REGISTER(boot_module, LOG_LEVEL_WRN);

lv_obj_t *scr_boot;

// Externs
extern lv_style_t style_lbl_orange;
extern lv_style_t style_lbl_white;
extern lv_style_t style_lbl_red;
extern lv_style_t style_lbl_white_small;

void draw_scr_boot(void)
{
    scr_boot = lv_obj_create(NULL);
    lv_obj_clear_flag(scr_boot, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    //lv_obj_set_style_bg_color(scr_boot, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    //lv_obj_set_style_bg_opa(scr_boot, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *label_boot = lv_label_create(scr_boot);
    lv_label_set_text(label_boot, "Booting...");
    lv_obj_align(label_boot, LV_ALIGN_CENTER, 0, 0);
    //lv_obj_add_style(label_boot, &style_lbl_white, 0);

    hpi_disp_set_curr_screen(SCR_SPL_BOOT);

    hpi_show_screen(scr_boot, SCROLL_RIGHT);
}