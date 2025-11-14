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
 * @brief Header for ECG/BioZ state machine functions
 * 
 * This file contains public function declarations for the ECG/BioZ state machine
 * that can be used by other modules.
 */

#ifndef SMF_ECG_BIOZ_H_
#define SMF_ECG_BIOZ_H_

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

#ifdef __cplusplus
}
#endif

#endif /* SMF_ECG_BIOZ_H_ */
