#ifndef HPI_SETTINGS_PERSISTENCE_H
#define HPI_SETTINGS_PERSISTENCE_H

#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <stdint.h>
#include <stdbool.h>

// Settings keys - using hierarchical naming for organization
#define SETTINGS_USER_HEIGHT_KEY        "user/height"
#define SETTINGS_USER_WEIGHT_KEY        "user/weight"
#define SETTINGS_HAND_WORN_KEY          "user/hand_worn"
#define SETTINGS_TIME_FORMAT_KEY        "display/time_format"
#define SETTINGS_TEMP_UNIT_KEY          "display/temp_unit"
#define SETTINGS_AUTO_SLEEP_KEY         "power/auto_sleep"
#define SETTINGS_SLEEP_TIMEOUT_KEY      "power/sleep_timeout"
#define SETTINGS_BACKLIGHT_TIMEOUT_KEY  "display/backlight_timeout"
#define SETTINGS_RAISE_TO_WAKE_KEY      "power/raise_to_wake"
#define SETTINGS_BUTTON_SOUNDS_KEY      "audio/button_sounds"

// Settings structure for easy management
struct hpi_user_settings {
    uint16_t height;              // cm (100-250)
    uint16_t weight;              // kg (30-200)
    uint8_t hand_worn;            // 0=Left, 1=Right
    uint8_t time_format;          // 0=24h, 1=12h
    uint8_t temp_unit;            // 0=Celsius, 1=Fahrenheit
    bool auto_sleep_enabled;      // Auto sleep on/off
    uint8_t sleep_timeout;        // seconds (10-120)
    uint8_t backlight_timeout;    // seconds
    bool raise_to_wake;           // Raise to wake on/off
    bool button_sounds;           // Button sounds on/off
};

// Default settings values
#define DEFAULT_USER_HEIGHT         170
#define DEFAULT_USER_WEIGHT         70
#define DEFAULT_HAND_WORN           0
#define DEFAULT_TIME_FORMAT         0
#define DEFAULT_TEMP_UNIT           0
#define DEFAULT_AUTO_SLEEP          true
#define DEFAULT_SLEEP_TIMEOUT       30
#define DEFAULT_BACKLIGHT_TIMEOUT   15
#define DEFAULT_RAISE_TO_WAKE       true
#define DEFAULT_BUTTON_SOUNDS       true

/**
 * @brief Initialize the settings subsystem
 * @return 0 on success, negative error code on failure
 */
int hpi_settings_persistence_init(void);

/**
 * @brief Load all settings from persistent storage
 * @param settings Pointer to settings structure to populate
 * @return 0 on success, negative error code on failure
 */
int hpi_settings_load_all(struct hpi_user_settings *settings);

/**
 * @brief Save all settings to persistent storage
 * @param settings Pointer to settings structure to save
 * @return 0 on success, negative error code on failure
 */
int hpi_settings_save_all(const struct hpi_user_settings *settings);

/**
 * @brief Save a single setting value
 * @param key Setting key (use defined constants)
 * @param value Pointer to value to save
 * @param value_len Length of value in bytes
 * @return 0 on success, negative error code on failure
 */
int hpi_settings_save_single(const char *key, const void *value, size_t value_len);

/**
 * @brief Reset all settings to default values
 * @return 0 on success, negative error code on failure
 */
int hpi_settings_factory_reset(void);

/**
 * @brief Get current settings structure
 * @return Pointer to current settings (read-only)
 */
const struct hpi_user_settings *hpi_settings_get_current(void);

/**
 * @brief Update settings structure and save to persistent storage
 * @param settings New settings to apply and save
 * @return 0 on success, negative error code on failure
 */
int hpi_settings_update_and_save(const struct hpi_user_settings *settings);

#endif /* HPI_SETTINGS_PERSISTENCE_H */
