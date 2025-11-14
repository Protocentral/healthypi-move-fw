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


#ifndef HPI_USER_SETTINGS_API_H
#define HPI_USER_SETTINGS_API_H

#include "hpi_settings_store.h"

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
