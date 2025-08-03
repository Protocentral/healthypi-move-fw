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


/**
 * @file smf_ecg_bioz.h
 * @brief Header for ECG/BioZ/GSR state machine functions
 * 
 * This file contains public function declarations for the ECG/BioZ state machine
 * that can be used by other modules. GSR functionality is integrated into the
 * BioZ channel processing.
 */

#ifndef SMF_ECG_BIOZ_H_
#define SMF_ECG_BIOZ_H_

#include <stdbool.h>
#include "hpi_common_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reconfigure ECG leads when hand worn setting changes
 * 
 * This function reconfigures the ECG lead polarity based on the current
 * hand worn setting. It only applies the configuration if ECG is currently active.
 * 
 * @note This function is thread-safe and can be called from UI callbacks.
 */
void reconfigure_ecg_leads_for_hand_worn(void);

/**
 * @brief GSR measurement API (integrated with BioZ channel)
 * 
 * GSR functionality uses the existing MAX30001 BioZ channel to measure
 * galvanic skin response for stress monitoring.
 */

// External message queue declarations
extern struct k_msgq q_gsr_sample;

/**
 * @brief Start GSR measurement
 * 
 * This function enables GSR processing from the BioZ channel.
 * The ECG/BioZ state machine must already be running.
 * 
 * @return 0 on success, negative error code on failure
 */
int hpi_gsr_start_measurement(void);

/**
 * @brief Stop GSR measurement
 * 
 * This function disables GSR processing while keeping the
 * underlying BioZ channel active for other measurements.
 * 
 * @return 0 on success, negative error code on failure
 */
int hpi_gsr_stop_measurement(void);

/**
 * @brief Get current GSR conductance value
 * 
 * @return Current GSR conductance in microsiemens (µS)
 */
float hpi_gsr_get_current_value(void);

/**
 * @brief Get current stress level
 * 
 * Stress levels:
 * - 0: Very Low (minimal GSR change)
 * - 1: Low (slight GSR change)
 * - 2: Moderate (noticeable GSR change)
 * - 3: High (significant GSR change)
 * - 4: Very High (extreme GSR change)
 * 
 * @return Current stress level (0-4)
 */
uint8_t hpi_gsr_get_stress_level(void);

/**
 * @brief Get GSR baseline value
 * 
 * The baseline is calculated as a moving average over a window
 * of recent measurements and is used for stress level calculation.
 * 
 * @return GSR baseline in microsiemens (µS)
 */
float hpi_gsr_get_baseline(void);

/**
 * @brief Check if GSR measurement is active
 * 
 * @return true if GSR measurement is active, false otherwise
 */
bool hpi_gsr_is_active(void);

/**
 * @brief GSR measurement constants
 */
#define GSR_MIN_CONDUCTANCE_US    0.1f    // Minimum GSR value in µS
#define GSR_MAX_CONDUCTANCE_US    100.0f  // Maximum GSR value in µS
#define GSR_NORMAL_CONDUCTANCE_US 10.0f   // Typical resting GSR value in µS

/**
 * @brief GSR quality thresholds
 */
#define GSR_QUALITY_EXCELLENT 90
#define GSR_QUALITY_GOOD      70
#define GSR_QUALITY_FAIR      50
#define GSR_QUALITY_POOR      30

#ifdef __cplusplus
}
#endif

#endif /* SMF_ECG_BIOZ_H_ */
