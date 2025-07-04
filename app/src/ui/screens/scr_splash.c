#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>
#include <stdio.h>
#include <app_version.h>

#include "ui/move_ui.h"

LOG_MODULE_REGISTER(screen_splash, LOG_LEVEL_WRN);

lv_obj_t *scr_splash;

// Externs
extern lv_style_t style_red_medium;
extern lv_style_t style_scr_black;

void draw_scr_splash(void)
{
    scr_splash = lv_obj_create(NULL);

    lv_obj_add_style(scr_splash, &style_scr_black, 0);

    LV_IMG_DECLARE(pc_logo_text_300);
    lv_obj_t *img1 = lv_img_create(scr_splash);
    //lv_img_set_src(img1, &pc_logo_text_300);
    lv_obj_align(img1, LV_ALIGN_CENTER, 0, 0);

    /*LV_IMG_DECLARE(pc_logo_round_120);
    lv_obj_t *img_logo = lv_img_create(scr_splash);
    lv_img_set_src(img_logo, &pc_logo_round_120);
    lv_obj_align_to(img_logo, NULL, LV_ALIGN_CENTER, 0, 0);*/

    lv_scr_load_anim(scr_splash, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, true);
}