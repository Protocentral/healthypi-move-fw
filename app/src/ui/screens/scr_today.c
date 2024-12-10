#include <zephyr/kernel.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <stdio.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/rtc.h>

#include "ui/move_ui.h"
#include "hw_module.h"

lv_obj_t *scr_today;

lv_obj_t *today_arcs;

void draw_scr_today(enum scroll_dir m_scroll_dir)
{
    scr_today = lv_obj_create(NULL);

    lv_obj_clear_flag(scr_today, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    //draw_bg(scr_today);
    draw_header_minimal(scr_today, 320);

    today_arcs = ui_dailymissiongroup_create(scr_today);


    hpi_disp_set_curr_screen(SCR_TODAY);
    hpi_show_screen(scr_today, m_scroll_dir);
}