/*
 * HealthyPi Move - Battery Management Module
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 * Copyright (c) 2023 Nordic Semiconductor ASA
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

#include "battery_module.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/fuel_gauge.h>
#include <zephyr/drivers/sensor/npm13xx_charger.h>
#include <stdio.h>
#include "nrf_fuel_gauge.h"
#include "ui/move_ui.h"
#include "hw_module.h"

LOG_MODULE_REGISTER(battery_module, LOG_LEVEL_DBG);

// Battery model for fuel gauge
static const struct battery_model battery_model = {
#include "battery_profile_200.inc"
};

// Static variables for fuel gauge operation
static float max_charge_current;
static float term_charge_current;
static int64_t ref_time;

// Low battery state tracking
static bool low_battery_screen_active = false;
static bool critical_battery_notified = false;
static uint8_t last_battery_level = 100;  // Store last known battery level
static float last_battery_voltage = 4.2f; // Store last known battery voltage

/**
 * @brief Read sensors from the NPM13xx charger
 */
static int npm_read_sensors(const struct device *charger,
                            float *voltage, float *current, float *temp, int32_t *chg_status)
{
    struct sensor_value value;
    int ret;

    ret = sensor_sample_fetch(charger);
    if (ret < 0)
    {
        return ret;
    }

    sensor_channel_get(charger, SENSOR_CHAN_GAUGE_VOLTAGE, &value);
    *voltage = (float)value.val1 + ((float)value.val2 / 1000000);

    sensor_channel_get(charger, SENSOR_CHAN_GAUGE_TEMP, &value);
    *temp = (float)value.val1 + ((float)value.val2 / 1000000);

    sensor_channel_get(charger, SENSOR_CHAN_GAUGE_AVG_CURRENT, &value);
    *current = (float)value.val1 + ((float)value.val2 / 1000000);

    sensor_channel_get(charger, SENSOR_CHAN_NPM13XX_CHARGER_STATUS, &value);
    *chg_status = value.val1;

    return 0;
}

/**
 * @brief Inform fuel gauge about charge status changes
 */
static int charge_status_inform(int32_t chg_status)
{
    union nrf_fuel_gauge_ext_state_info_data state_info;

    if (chg_status & NPM1300_CHG_STATUS_COMPLETE_MASK)
    {
        LOG_DBG("Charge complete");
        state_info.charge_state = NRF_FUEL_GAUGE_CHARGE_STATE_COMPLETE;
    }
    else if (chg_status & NPM1300_CHG_STATUS_TRICKLE_MASK)
    {
        LOG_DBG("Trickle charging");
        state_info.charge_state = NRF_FUEL_GAUGE_CHARGE_STATE_TRICKLE;
    }
    else if (chg_status & NPM1300_CHG_STATUS_CC_MASK)
    {
        LOG_DBG("Constant current charging");
        state_info.charge_state = NRF_FUEL_GAUGE_CHARGE_STATE_CC;
    }
    else if (chg_status & NPM1300_CHG_STATUS_CV_MASK)
    {
        LOG_DBG("Constant voltage charging");
        state_info.charge_state = NRF_FUEL_GAUGE_CHARGE_STATE_CV;
    }
    else
    {
        LOG_DBG("Charger idle");
        state_info.charge_state = NRF_FUEL_GAUGE_CHARGE_STATE_IDLE;
    }

    return nrf_fuel_gauge_ext_state_update(NRF_FUEL_GAUGE_EXT_STATE_INFO_CHARGE_STATE_CHANGE,
                                           &state_info);
}

int battery_fuel_gauge_init(const struct device *charger)
{
    struct sensor_value value;
    struct nrf_fuel_gauge_init_parameters parameters = {
        .model = &battery_model,
        .opt_params = NULL,
        .state = NULL,
    };

    int32_t chg_status;
    int ret;
    float max_charge_current;
	float term_charge_current;

    LOG_DBG("Initializing nRF Fuel Gauge");

    ret = npm_read_sensors(charger, &parameters.v0, &parameters.i0, &parameters.t0, &chg_status);
    if (ret < 0)
    {
        return ret;
    }
    /* Zephyr sensor API convention for Gauge current is negative=discharging,
	 * while nrf_fuel_gauge lib expects the opposite negative=charging
	 */
	parameters.i0 = -parameters.i0;

    /* Store charge nominal and termination current, needed for ttf calculation */
    sensor_channel_get(charger, SENSOR_CHAN_GAUGE_DESIRED_CHARGING_CURRENT, &value);
    max_charge_current = (float)value.val1 + ((float)value.val2 / 1000000);
    term_charge_current = max_charge_current / 10.f;

    ret = nrf_fuel_gauge_init(&parameters, NULL);
    if (ret < 0)
    {
        LOG_DBG("Fuel gauge init error: %d", ret);
        return ret;
    }

    ret = nrf_fuel_gauge_ext_state_update(NRF_FUEL_GAUGE_EXT_STATE_INFO_CHARGE_CURRENT_LIMIT,
					      &(union nrf_fuel_gauge_ext_state_info_data){
						      .charge_current_limit = max_charge_current});
	if (ret < 0) {
		printk("Error: Could not set fuel gauge state\n");
		return ret;
	}

	ret = nrf_fuel_gauge_ext_state_update(NRF_FUEL_GAUGE_EXT_STATE_INFO_TERM_CURRENT,
					      &(union nrf_fuel_gauge_ext_state_info_data){
						      .charge_term_current = term_charge_current});
	if (ret < 0) {
		printk("Error: Could not set fuel gauge state\n");
		return ret;
	}

    ret = charge_status_inform(chg_status);
	if (ret < 0) {
		printk("Error: Could not set fuel gauge state\n");
		return ret;
	}

    ref_time = k_uptime_get();

    return 0;
}

int battery_fuel_gauge_update(const struct device *charger, bool vbus_connected,
                              uint8_t *batt_level, bool *batt_charging, float *batt_voltage)
{
    static int32_t chg_status_prev;
    
    float voltage;
    float current;
    float temp;
    float soc;
    float tte;
    float ttf;
    float delta;
    int32_t chg_status;
    int ret;

    ret = npm_read_sensors(charger, &voltage, &current, &temp, &chg_status);
    if (ret < 0)
    {
        printk("Error: Could not read from charger device\n");
        return ret;
    }

    ret = nrf_fuel_gauge_ext_state_update(
        vbus_connected ? NRF_FUEL_GAUGE_EXT_STATE_INFO_VBUS_CONNECTED
                       : NRF_FUEL_GAUGE_EXT_STATE_INFO_VBUS_DISCONNECTED,
        NULL);
    if (ret < 0)
    {
        printk("Error: Could not inform of state\n");
        return ret;
    }

    if (chg_status != chg_status_prev)
    {
        chg_status_prev = chg_status;

        ret = charge_status_inform(chg_status);
        if (ret < 0)
        {
            printk("Error: Could not inform of charge status\n");
            return ret;
        }
    }

    delta = (float)k_uptime_delta(&ref_time) / 1000.f;

    /* Zephyr sensor API convention for Gauge current is negative=discharging,
     * while nrf_fuel_gauge lib expects the opposite negative=charging
     */
    current = -current;

    /* Process fuel gauge data with nRF Connect SDK 3.0.2 API */
    soc = nrf_fuel_gauge_process(voltage, current, temp, delta, NULL);
    tte = nrf_fuel_gauge_tte_get();
    ttf = nrf_fuel_gauge_ttf_get();

    // LOG_DBG("V: %.3f, I: %.3f, T: %.2f, SoC: %.2f, TTE: %.0f, TTF: %.0f, Charge status: %d",
    //          (double)voltage, (double)current, (double)temp, (double)soc, (double)tte, (double)ttf, chg_status);

    // Update return values
    *batt_level = (uint8_t)soc;
    *batt_charging = chg_status;
    *batt_voltage = voltage; // Return the battery voltage

    // Update internal state for external access
    last_battery_level = *batt_level;
    last_battery_voltage = *batt_voltage;

    return 0;
}

bool battery_is_low(void)
{
    return low_battery_screen_active;
}

bool battery_is_critical(void)
{
    return (last_battery_voltage <= HPI_BATTERY_CRITICAL_VOLTAGE);
}

void battery_reset_low_state(void)
{
    low_battery_screen_active = false;
    critical_battery_notified = false;
}

uint8_t battery_get_level(void)
{
    return last_battery_level;
}

float battery_get_voltage(void)
{
    return last_battery_voltage;
}

void battery_monitor_conditions(uint8_t sys_batt_level, bool sys_batt_charging, float sys_batt_voltage)
{
    // Update internal state
    last_battery_level = sys_batt_level;
    last_battery_voltage = sys_batt_voltage;

    // Check for low battery conditions (voltage-based)
    if (!sys_batt_charging)
    { // Only check cutoff when not charging
        if (sys_batt_voltage <= HPI_BATTERY_SHUTDOWN_VOLTAGE)
        {
            // Critical battery voltage - immediately shutdown
            LOG_ERR("Critical battery voltage (%.2f V) - shutting down", (double)sys_batt_voltage);
            k_msleep(1000); // Give time for log message
            hpi_hw_pmic_off();
        }
        else if (sys_batt_voltage <= HPI_BATTERY_CRITICAL_VOLTAGE && !low_battery_screen_active)
        {
            // Show low battery warning screen
            LOG_WRN("Low battery voltage (%.2f V) - showing warning screen", (double)sys_batt_voltage);
            low_battery_screen_active = true;
            critical_battery_notified = true;

            // Load the low battery screen with battery level and voltage as arguments
            // Pass voltage as arg3 (multiply by 100 to preserve 2 decimal places in uint32_t)
            hpi_load_scr_spl(SCR_SPL_LOW_BATTERY, SCROLL_NONE, sys_batt_level, sys_batt_charging, (uint32_t)(sys_batt_voltage * 100), 0);
        }
    }
    else
    {
        // Reset flags when charging and voltage recovers
        if (sys_batt_voltage > HPI_BATTERY_RECOVERY_VOLTAGE)
        {
            if (low_battery_screen_active)
            {
                LOG_INF("Battery voltage recovered (%.2f V) - dismissing low battery screen", (double)sys_batt_voltage);
                // Cleanup low battery screen UI references
                hpi_disp_low_battery_cleanup();
                // Return to home screen and reset low battery screen state
                hpi_load_screen(SCR_HOME, SCROLL_NONE);
            }
            low_battery_screen_active = false;
            critical_battery_notified = false;
        }
    }
}

void battery_update_low_battery_screen(uint8_t sys_batt_level, bool sys_batt_charging, float sys_batt_voltage)
{
    // The display state machine now handles updates via hpi_disp_low_battery_update()
    // This function is kept for backward compatibility but no longer does screen recreation
    (void)sys_batt_level;
    (void)sys_batt_charging;
    (void)sys_batt_voltage;
}
