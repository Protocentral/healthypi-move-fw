#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <lvgl.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "hpi_common_types.h"
#include "ui/move_ui.h"
#include "arm_math.h"
#include "arm_const_structs.h"
#include <string.h>
#include <zephyr/sys/util.h>
#include "hrv_algos.h"

LOG_MODULE_REGISTER(hpi_disp_scr_hrv_frequency_compact, LOG_LEVEL_DBG);

lv_obj_t *scr_hrv_frequency_compact;

static lv_obj_t *label_title;          // "HRV Complete"
static lv_obj_t *label_stress_text;    // "Stress"
static lv_obj_t *bar_stress;           // progress bar
static lv_obj_t *label_stress_pct;     // "xx%"
static lv_obj_t *label_sdnn;
static lv_obj_t *label_rmssd;
static lv_obj_t *label_lf_hf_ratio_compact;
static lv_obj_t *label_stress_level_compact;

// Externs
extern lv_style_t style_white_large;
extern lv_style_t style_white_medium;
extern lv_style_t style_white_small;
extern lv_style_t style_scr_black;

extern float lf_power_compact;
extern float hf_power_compact ;
extern float stress_score_compact;
extern float sdnn_val;
extern float rmssd_val;

extern struct k_sem hrv_state_set_mutex;

static void lvgl_update_cb(void *user_data)
{
    hpi_hrv_frequency_compact_update_display();
}

// Simplified stress assessment for compact display
// LF/HF ratio interpretation: higher ratio = more sympathetic (stress) activity
int get_stress_percentage(float lf, float hf) {
    
    if (hf <= 0.0f) return 100;  // Invalid data = max stress indicator
    float lf_hf = lf / hf;
    
    // Monotonically increasing stress levels based on LF/HF ratio
    if (lf_hf < 0.5f)       return 15;  // Very relaxed (parasympathetic dominant)
    else if (lf_hf < 1.0f)  return 30;  // Relaxed
    else if (lf_hf < 1.5f)  return 45;  // Normal/balanced
    else if (lf_hf < 2.0f)  return 55;  // Slightly elevated
    else if (lf_hf < 3.0f)  return 70;  // Moderately stressed
    else if (lf_hf < 5.0f)  return 80;  // Stressed
    else if (lf_hf < 8.0f)  return 90;  // Highly stressed
    else                    return 95;  // Very stressed (sympathetic dominant)
}

static lv_color_t get_stress_arc_color(int stress_percentage) {
    if (stress_percentage < 25) {
        return lv_palette_main(LV_PALETTE_GREEN);
    } else if (stress_percentage < 50) {
        return lv_palette_main(LV_PALETTE_YELLOW);
    } else if (stress_percentage < 75) {
        return lv_palette_main(LV_PALETTE_ORANGE);
    } else {
        return lv_palette_main(LV_PALETTE_RED);
    }
}

void gesture_handler(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_BOTTOM) {
        gesture_down_scr_spl_hrv_complete();
    }
}

 void gesture_down_scr_spl_hrv_complete(void)
 {
     // Handle gesture down event - return to HRV home screen
          hpi_load_screen(SCR_HRV, SCROLL_DOWN);
 }

void draw_scr_spl_hrv_complete(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{

    scr_hrv_frequency_compact = lv_obj_create(NULL);
    lv_obj_clear_flag(scr_hrv_frequency_compact, LV_OBJ_FLAG_SCROLLABLE);
    draw_scr_common(scr_hrv_frequency_compact);

    lv_obj_t *cont_main = lv_obj_create(scr_hrv_frequency_compact);
    lv_obj_set_size(cont_main, lv_pct(100), lv_pct(100));
    lv_obj_align_to(cont_main, NULL, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(cont_main, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_main, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(cont_main, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(cont_main, &style_scr_black, 0);

    // 1) Title: HRV Complete
    label_title = lv_label_create(cont_main);
    lv_label_set_text(label_title, "HRV Complete");
    lv_obj_add_style(label_title, &style_white_medium, 0);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 5);

    // 2) "Stress" text
    label_stress_text = lv_label_create(cont_main);
    lv_label_set_text(label_stress_text, "Stress");
    lv_obj_add_style(label_stress_text, &style_white_small, 0);
    lv_obj_set_style_text_align(label_stress_text, LV_TEXT_ALIGN_CENTER, 0);


    // 3) Stress progress bar
    bar_stress = lv_bar_create(cont_main);
    lv_obj_set_size(bar_stress, 240, 12);
    lv_bar_set_range(bar_stress, 0, 100);  // 0‑100% stress
    lv_bar_set_value(bar_stress, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_stress, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar_stress, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_stress, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_stress, lv_palette_main(LV_PALETTE_GREEN), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_stress, 6, LV_PART_INDICATOR);

    // 4) Stress percentage label ("xx%")
    label_stress_level_compact = lv_label_create(cont_main);
    lv_label_set_text(label_stress_level_compact, "0%");
    lv_obj_add_style(label_stress_level_compact, &style_white_medium, 0);
    lv_obj_set_style_text_align(label_stress_level_compact, LV_TEXT_ALIGN_CENTER, 0);

    // 5) SDNN
    label_sdnn = lv_label_create(cont_main);
    lv_label_set_text(label_sdnn, "SDNN: --");
    lv_obj_set_style_text_color(label_sdnn, lv_color_hex(0xFF7070), 0);
    lv_obj_add_style(label_sdnn, &style_white_small, 0);

    // 6) RMSSD
    label_rmssd = lv_label_create(cont_main);
    lv_label_set_text(label_rmssd, "RMSSD: --");
    lv_obj_set_style_text_color(label_rmssd, lv_color_hex(0x70A0FF), 0);
    lv_obj_add_style(label_rmssd, &style_white_small, 0);

    // 7) LF/HF
    label_lf_hf_ratio_compact = lv_label_create(cont_main);
    lv_label_set_text(label_lf_hf_ratio_compact, "LF/HF: --");
    lv_obj_set_style_text_color(label_lf_hf_ratio_compact, lv_color_hex(0xC080FF), 0);
    lv_obj_add_style(label_lf_hf_ratio_compact, &style_white_small, 0);

    // Gesture handler
    lv_obj_add_event_cb(scr_hrv_frequency_compact, gesture_handler, LV_EVENT_GESTURE, NULL);
    hpi_disp_set_curr_screen(SCR_SPL_HRV_FREQUENCY);
    hpi_show_screen(scr_hrv_frequency_compact, m_scroll_dir);
    lv_async_call(lvgl_update_cb, NULL);
}
void hpi_hrv_frequency_compact_update_display(void)
{
    float ratio = 0.0f;

    int stress_pct = get_stress_percentage(lf_power_compact, hf_power_compact);
    ratio = (hf_power_compact > 0.0f) ? (lf_power_compact / hf_power_compact) : 0.0f;

    int ratio_int  = (int)ratio;
    int ratio_dec  = (int)((ratio - ratio_int) * 100);

    int sdnn_int   = (int)sdnn_val;
    int sdnn_dec   = (int)((sdnn_val - sdnn_int) * 100);

    int rmssd_int  = (int)rmssd_val;
    int rmssd_dec  = (int)((rmssd_val - rmssd_int) * 100);

    // SDNN and RMSSD
    if (label_sdnn)
        lv_label_set_text_fmt(label_sdnn, "SDNN: %d.%02d", sdnn_int, abs(sdnn_dec));

    if (label_rmssd)
        lv_label_set_text_fmt(label_rmssd, "RMSSD: %d.%02d", rmssd_int, abs(rmssd_dec));

    // LF/HF ratio
    if (label_lf_hf_ratio_compact) {
        if (hf_power_compact > 0.0f)
            lv_label_set_text_fmt(label_lf_hf_ratio_compact, "LF/HF: %d.%02d", ratio_int, abs(ratio_dec));
        else
            lv_label_set_text(label_lf_hf_ratio_compact, "LF/HF: --");
    }

    // Stress bar and percentage
    if (bar_stress) {
        lv_bar_set_value(bar_stress, stress_pct, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar_stress,get_stress_arc_color(stress_pct), LV_PART_INDICATOR);
    }

    if (label_stress_level_compact) {
        lv_label_set_text_fmt(label_stress_level_compact, "%d%%", stress_pct);
        lv_obj_set_style_text_color(label_stress_level_compact,get_stress_arc_color(stress_pct), 0);
    }
}

