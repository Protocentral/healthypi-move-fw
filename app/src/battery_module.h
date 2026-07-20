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

/*
 * Battery thresholds — single source of truth for both the boot check and the
 * runtime monitor.
 *
 * The user-facing "battery low" warning is driven by fuel-gauge STATE OF CHARGE
 * (SoC), so what the warning trips on matches the "%" the screen shows. VOLTAGE
 * is used ONLY as an over-discharge hard floor: below it, when not charging, we
 * ship-mode the PMIC to protect the cell regardless of the SoC estimate.
 */
#define HPI_BATTERY_SHUTDOWN_VOLTAGE 3.0f // Over-discharge hard floor (V) - ship mode below this when not charging

#define HPI_BATTERY_LOW_SOC_PCT     10 // Enter low-battery warning at/below this SoC (%)
#define HPI_BATTERY_RECOVER_SOC_PCT 15 // Exit the warning at/above this SoC (%) - hysteresis; NOT gated on charging

// Debounce: consecutive hw_thread samples (~5 s each) a condition must hold before
// the state flips, so a transient under-load voltage sag / noisy SoC read can't trip it.
#define HPI_BATTERY_DEBOUNCE_SAMPLES 3

/**
 * @brief Latched battery state produced by battery_evaluate().
 */
enum hpi_batt_state {
    HPI_BATT_NORMAL = 0, // SoC above the recover threshold
    HPI_BATT_LOW,        // SoC-based low-battery warning latched (hysteresis)
    HPI_BATT_SHUTDOWN,   // voltage hard floor breached while not charging - powering off
};

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
 * @return true if the low-battery warning is latched, false otherwise
 */
bool battery_is_low(void);

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
 * @brief Evaluate battery state (SoC warning + voltage hard floor).
 *
 * Pure decision with hysteresis + debounce; no UI side effects. Both the boot
 * check and the runtime monitor share the same thresholds via this module.
 *
 * @param soc      Fuel-gauge state of charge (0-100%)
 * @param charging True if the charger is supplying current
 * @param voltage  Battery terminal voltage (V)
 * @return latched ::hpi_batt_state
 */
enum hpi_batt_state battery_evaluate(uint8_t soc, bool charging, float voltage);

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
