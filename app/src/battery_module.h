/*
 * HealthyPi Move - Battery Management Module
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

#pragma once

#include <zephyr/device.h>
#include <stdbool.h>
#include <stdint.h>

/* nPM1300 CHARGER.BCHGCHARGESTATUS register bitmasks */
#define NPM1300_CHG_STATUS_COMPLETE_MASK BIT(1)
#define NPM1300_CHG_STATUS_TRICKLE_MASK	 BIT(2)
#define NPM1300_CHG_STATUS_CC_MASK	 BIT(3)
#define NPM1300_CHG_STATUS_CV_MASK	 BIT(4)

// Battery cutoff thresholds - voltage based (typical Li-ion voltages)
// These values can be adjusted based on the specific battery characteristics
#define HPI_BATTERY_CRITICAL_VOLTAGE 3.3f // Show critical low battery screen (V)
#define HPI_BATTERY_SHUTDOWN_VOLTAGE 3.0f // Auto shutdown level (V) - prevents over-discharge
#define HPI_BATTERY_RECOVERY_VOLTAGE 3.5f // Recovery threshold when charging (V) - allows hysteresis

/**
 * @brief Initialize the fuel gauge system
 * 
 * @param charger Pointer to the charger device
 * @return 0 on success, negative error code on failure
 */
int battery_fuel_gauge_init(const struct device *charger);

/**
 * @brief Update fuel gauge data and get current battery status
 * 
 * @param charger Pointer to the charger device
 * @param vbus_connected True if VBUS/charger is connected
 * @param batt_level Pointer to store battery level (0-100%)
 * @param batt_charging Pointer to store charging status
 * @param batt_voltage Pointer to store battery voltage
 * @return 0 on success, negative error code on failure
 */
int battery_fuel_gauge_update(const struct device *charger, bool vbus_connected, 
                              uint8_t *batt_level, bool *batt_charging, float *batt_voltage);

/**
 * @brief Check if the device is currently in low battery condition
 * 
 * @return true if low battery screen is active, false otherwise
 */
bool battery_is_low(void);

/**
 * @brief Check if the battery is in critical voltage range
 * 
 * @return true if battery voltage is critically low, false otherwise
 */
bool battery_is_critical(void);

/**
 * @brief Reset low battery state flags
 * 
 * Used when charging resumes or battery voltage recovers
 */
void battery_reset_low_state(void);

/**
 * @brief Get the last known battery level
 * 
 * @return Battery level as percentage (0-100)
 */
uint8_t battery_get_level(void);

/**
 * @brief Get the last known battery voltage
 * 
 * @return Battery voltage in volts
 */
float battery_get_voltage(void);

/**
 * @brief Check for battery conditions and handle low battery scenarios
 * 
 * This function should be called periodically from the main system thread
 * to monitor battery status and take appropriate actions.
 * 
 * @param sys_batt_level Current battery level (0-100%)
 * @param sys_batt_charging Current charging status
 * @param sys_batt_voltage Current battery voltage
 */
void battery_monitor_conditions(uint8_t sys_batt_level, bool sys_batt_charging, float sys_batt_voltage);

/**
 * @brief Update low battery screen if currently active
 * 
 * Refreshes the low battery warning screen with current status
 * 
 * @param sys_batt_level Current battery level (0-100%)
 * @param sys_batt_charging Current charging status  
 * @param sys_batt_voltage Current battery voltage
 */
void battery_update_low_battery_screen(uint8_t sys_batt_level, bool sys_batt_charging, float sys_batt_voltage);
