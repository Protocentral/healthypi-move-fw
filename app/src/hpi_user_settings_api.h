#ifndef HPI_USER_SETTINGS_API_H
#define HPI_USER_SETTINGS_API_H

#include "hpi_settings_persistence.h"

/**
 * @brief Public API for accessing user settings from other modules
 * This provides a clean interface for other parts of the application
 * to access user settings without directly dealing with persistence
 */

/**
 * @brief Initialize user settings system
 * @return 0 on success, negative error code on failure
 */
int hpi_user_settings_init(void);

/**
 * @brief Get user's height setting
 * @return Height in cm (100-250)
 */
uint16_t hpi_user_settings_get_height(void);

/**
 * @brief Get user's weight setting  
 * @return Weight in kg (30-200)
 */
uint16_t hpi_user_settings_get_weight(void);

/**
 * @brief Get hand worn setting
 * @return 0 for left hand, 1 for right hand
 */
uint8_t hpi_user_settings_get_hand_worn(void);

/**
 * @brief Get time format setting
 * @return 0 for 24H, 1 for 12H
 */
uint8_t hpi_user_settings_get_time_format(void);

/**
 * @brief Get temperature unit setting
 * @return 0 for Celsius, 1 for Fahrenheit
 */
uint8_t hpi_user_settings_get_temp_unit(void);

/**
 * @brief Get auto sleep enabled setting
 * @return true if auto sleep is enabled
 */
bool hpi_user_settings_get_auto_sleep_enabled(void);

/**
 * @brief Get sleep timeout setting
 * @return Sleep timeout in seconds (10-120)
 */
uint8_t hpi_user_settings_get_sleep_timeout(void);

/**
 * @brief Set user's height and save
 * @param height Height in cm (100-250)
 * @return 0 on success, negative error code on failure
 */
int hpi_user_settings_set_height(uint16_t height);

/**
 * @brief Set user's weight and save
 * @param weight Weight in kg (30-200)  
 * @return 0 on success, negative error code on failure
 */
int hpi_user_settings_set_weight(uint16_t weight);

/**
 * @brief Set hand worn and save
 * @param hand_worn 0 for left hand, 1 for right hand
 * @return 0 on success, negative error code on failure
 */
int hpi_user_settings_set_hand_worn(uint8_t hand_worn);

/**
 * @brief Set time format and save
 * @param time_format 0 for 24H, 1 for 12H
 * @return 0 on success, negative error code on failure
 */
int hpi_user_settings_set_time_format(uint8_t time_format);

/**
 * @brief Set temperature unit and save
 * @param temp_unit 0 for Celsius, 1 for Fahrenheit
 * @return 0 on success, negative error code on failure
 */
int hpi_user_settings_set_temp_unit(uint8_t temp_unit);

/**
 * @brief Get a copy of all current settings
 * @param settings Pointer to settings structure to populate
 * @return 0 on success, negative error code on failure
 */
int hpi_user_settings_get_all(struct hpi_user_settings *settings);

#endif /* HPI_USER_SETTINGS_API_H */
