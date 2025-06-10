#include <lvgl.h>
#include "ui/move_ui.h"

static lv_obj_t *scr_settings;

extern lv_style_t style_white_medium;

// Enum for menu indices
typedef enum
{
    SETTINGS_MENU_DISPLAY = 0,
    SETTINGS_MENU_ECG,
    SETTINGS_MENU_PPG,
    SETTINGS_MENU_BLUETOOTH,
    SETTINGS_MENU_ABOUT,
    SETTINGS_MENU_COUNT
} settings_menu_index_t;

// Titles for menu items
static const char *settings_menu_titles[SETTINGS_MENU_COUNT] = {
    "Display",
    "ECG Settings",
    "PPG Settings",
    "Bluetooth",
    "About"};

// Icons for menu items (LVGL built-in symbols)
static const char *settings_menu_icons[SETTINGS_MENU_COUNT] = {
    LV_SYMBOL_EYE_OPEN,      // Display
    LV_SYMBOL_EDIT,      // ECG (using eye as example, replace with custom if needed)
    LV_SYMBOL_EDIT,       // PPG (using refresh as example)
    LV_SYMBOL_BLUETOOTH,     // Bluetooth
    LV_SYMBOL_LIST           // About
};

// Forward declarations for submenu draw functions
static void draw_subscr_display(void);
static void draw_subscr_ecg(void);
static void draw_subscr_ppg(void);
static void draw_subscr_bluetooth(void);
static void draw_subscr_about(void);

// Table of submenu draw functions
typedef void (*settings_menu_func_t)(void);
static settings_menu_func_t settings_menu_funcs[SETTINGS_MENU_COUNT] = {
    draw_subscr_display,
    draw_subscr_ecg,
    draw_subscr_ppg,
    draw_subscr_bluetooth,
    draw_subscr_about
};

// Gesture event callback for closing submenu and returning to settings screen
static void submenu_gesture_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_GESTURE)
    {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_RIGHT)
        {
            // Return to settings menu screen with right scroll animation
            draw_scr_settings(SCROLL_RIGHT);
        }
    }
}

// Example submenu draw implementations
static void draw_subscr_display(void)
{
    lv_obj_t *submenu = lv_obj_create(NULL);
    lv_obj_set_size(submenu, 320, 240);
    lv_obj_align(submenu, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(submenu, submenu_gesture_event_cb, LV_EVENT_GESTURE, NULL);

    lv_obj_t *cont_col = lv_obj_create(submenu);
    lv_obj_set_size(cont_col, 300, 220);
    lv_obj_align(cont_col, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(cont_col, lv_color_black(), 0);
    lv_obj_set_style_border_width(cont_col, 0, 0); // Remove border

    lv_obj_t *lbl = lv_label_create(cont_col);
    lv_label_set_text(lbl, "Display Settings");
    lv_obj_center(lbl);

    lv_scr_load_anim(submenu, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}

// ECG submenu toggle state (0 = Left Hand, 1 = Right Hand)
static int ecg_watch_hand = 0;

// ECG submenu event callback
static void ecg_hand_toggle_event_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    ecg_watch_hand = lv_dropdown_get_selected(dd);
    // Optionally, save this setting or trigger an action here
}

// ECG submenu implementation
static void draw_subscr_ecg(void)
{
    lv_obj_t *submenu = lv_obj_create(NULL);
    lv_obj_set_size(submenu, 320, 240);
    lv_obj_align(submenu, LV_ALIGN_CENTER, 0, 0);

    // Add gesture event for swipe right
    lv_obj_add_event_cb(submenu, submenu_gesture_event_cb, LV_EVENT_GESTURE, NULL);

    // Column container for content
    lv_obj_t *cont_col = lv_obj_create(submenu);
    lv_obj_set_size(cont_col, 300, 220);
    lv_obj_align(cont_col, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(cont_col, lv_color_black(), 0);
    lv_obj_set_style_border_width(cont_col, 0, 0); // Remove border

    lv_obj_t *lbl = lv_label_create(cont_col);
    lv_label_set_text(lbl, "ECG Options");
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 0);

    // "Watch worn on" label
    lv_obj_t *lbl_hand = lv_label_create(cont_col);
    lv_label_set_text(lbl_hand, "Watch worn on:");

    // Dropdown for Left/Right hand
    lv_obj_t *dd_hand = lv_dropdown_create(cont_col);
    lv_obj_set_width(dd_hand, 150);
    lv_dropdown_set_options(dd_hand, "Left Hand\nRight Hand");
    lv_dropdown_set_selected(dd_hand, ecg_watch_hand);
    lv_obj_add_event_cb(dd_hand, ecg_hand_toggle_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_scr_load_anim(submenu, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}

static void draw_subscr_ppg(void)
{
    lv_obj_t *submenu = lv_obj_create(NULL);
    lv_obj_set_size(submenu, 320, 240);
    lv_obj_align(submenu, LV_ALIGN_CENTER, 0, 0);

    // Add gesture event for swipe right
    lv_obj_add_event_cb(submenu, submenu_gesture_event_cb, LV_EVENT_GESTURE, NULL);

    // Column container for content
    lv_obj_t *cont_col = lv_obj_create(submenu);
    lv_obj_set_size(cont_col, 300, 220);
    lv_obj_align(cont_col, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(cont_col, lv_color_black(), 0);
    lv_obj_set_style_border_width(cont_col, 0, 0); // Remove border

    lv_obj_t *lbl = lv_label_create(cont_col);
    lv_label_set_text(lbl, "PPG Settings");
    lv_obj_center(lbl);

    lv_scr_load_anim(submenu, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}

static void draw_subscr_bluetooth(void)
{
    lv_obj_t *submenu = lv_obj_create(NULL);
    lv_obj_set_size(submenu, 320, 240);
    lv_obj_align(submenu, LV_ALIGN_CENTER, 0, 0);

    // Add gesture event for swipe right
    lv_obj_add_event_cb(submenu, submenu_gesture_event_cb, LV_EVENT_GESTURE, NULL);

    // Column container for content
    lv_obj_t *cont_col = lv_obj_create(submenu);
    lv_obj_set_size(cont_col, 300, 220);
    lv_obj_align(cont_col, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(cont_col, lv_color_black(), 0);
    lv_obj_set_style_border_width(cont_col, 0, 0); // Remove border

    lv_obj_t *lbl = lv_label_create(cont_col);
    lv_label_set_text(lbl, "Bluetooth Settings");
    lv_obj_center(lbl);

    lv_scr_load_anim(submenu, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}

static void draw_subscr_about(void)
{
    lv_obj_t *submenu = lv_obj_create(NULL);
    lv_obj_set_size(submenu, 320, 240);
    lv_obj_align(submenu, LV_ALIGN_CENTER, 0, 0);

    // Add gesture event for swipe right
    lv_obj_add_event_cb(submenu, submenu_gesture_event_cb, LV_EVENT_GESTURE, NULL);

    // Column container for content
    lv_obj_t *cont_col = lv_obj_create(submenu);
    lv_obj_set_size(cont_col, 300, 220);
    lv_obj_align(cont_col, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(cont_col, lv_color_black(), 0);
    lv_obj_set_style_border_width(cont_col, 0, 0); // Remove border

    lv_obj_t *lbl = lv_label_create(cont_col);
    lv_label_set_text(lbl, "About");
    lv_obj_center(lbl);

    lv_scr_load_anim(submenu, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}

// Event callback for menu item button
static void settings_menu_item_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED)
    {
        lv_obj_t *btn = lv_event_get_target(e);
        uintptr_t idx = (uintptr_t)lv_obj_get_user_data(btn);
        if (idx < SETTINGS_MENU_COUNT && settings_menu_funcs[idx])
        {
            settings_menu_funcs[idx]();
        }
    }
}

// Add this gesture event callback near the other static functions
static void settings_gesture_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_GESTURE)
    {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_LEFT)
        {
            // Return to home screen with left scroll animation
            hpi_load_screen(SCR_HOME, SCROLL_LEFT);
        }
    }
}

void draw_scr_settings(enum scroll_dir m_scroll_dir)
{
    scr_settings = lv_obj_create(NULL);

    draw_scr_common(scr_settings);

    // Add gesture event for swipe left to close settings and return home
    lv_obj_add_event_cb(scr_settings, settings_gesture_event_cb, LV_EVENT_GESTURE, NULL);

    // Create a scrollable column container for menu items
    lv_obj_t *cont_col = lv_obj_create(scr_settings);
    lv_obj_set_size(cont_col, 300, 350);
    lv_obj_align(cont_col, LV_ALIGN_TOP_MID, 0, 45);
    lv_obj_set_flex_flow(cont_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(cont_col, LV_DIR_VER);

    // Set background color to black and remove border
    lv_obj_set_style_bg_color(cont_col, lv_color_black(), 0);
    lv_obj_set_style_border_width(cont_col, 0, 0);

    // Add a settings header
    lv_obj_t *header = lv_label_create(cont_col);
    lv_label_set_text(header, "Settings");
    lv_obj_set_style_text_font(header, &lv_font_montserrat_20, 0); // Use a larger font if available
    lv_obj_set_style_text_color(header, lv_color_white(), 0);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);

    // Create menu item buttons with icons
    for (uintptr_t i = 0; i < SETTINGS_MENU_COUNT; i++)
    {
        lv_obj_t *btn = lv_btn_create(cont_col);
        lv_obj_set_width(btn, lv_pct(100));
        lv_obj_set_height(btn, 65); // Increased height by 10 pixels (default is usually 40)
        lv_obj_set_user_data(btn, (void *)i);
        lv_obj_add_event_cb(btn, settings_menu_item_event_cb, LV_EVENT_CLICKED, NULL);

        // Create a horizontal flex container for icon + label
        lv_obj_t *row = lv_obj_create(btn);
        lv_obj_remove_style_all(row); // Remove default background
        lv_obj_set_size(row, lv_pct(100), lv_pct(100));
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_border_width(row, 0, 0); // Remove border


        // Icon
        lv_obj_t *icon = lv_label_create(row);
        lv_label_set_text(icon, settings_menu_icons[i]);
        lv_obj_set_style_text_font(icon, LV_FONT_DEFAULT, 0);
        lv_obj_set_style_pad_right(icon, 12, 0);

        // Label
        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, settings_menu_titles[i]);
        lv_obj_set_style_text_font(lbl, LV_FONT_DEFAULT, 0);

        lv_obj_center(row);
    }

    hpi_disp_set_curr_screen(SCR_SPL_SETTINGS);
    hpi_show_screen(scr_settings, m_scroll_dir);
}