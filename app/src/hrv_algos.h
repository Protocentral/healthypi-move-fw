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


#pragma once

#define SF_spo2 25 // sampling frequency
#define BUFFER_SIZE (SF_spo2 * 4)
#define MA4_SIZE 4 // DONOT CHANGE
#define min(x, y) ((x) < (y) ? (x) : (y))

#define FILTERORDER 161 /* DC Removal Numerator Coeff*/
#define NRCOEFF (0.992)
#define HRV_LIMIT 30

void hrv_reset(void);
int hrv_get_sample_count(void);
bool hrv_is_ready(void);
static void hrv_add_sample(double rr_interval);
void start_hrv_collection(void);
void stop_hrv_collection(void);
void on_new_rr_interval_detected(uint16_t rr_interval);
float hrv_calculate_mean(void);
float hrv_calculate_sdnn(void);
float hrv_calculate_rmssd(void);
float hrv_calculate_pnn50(void);
uint32_t hrv_calculate_min(void);
uint32_t hrv_calculate_max(void);
int hrv_get_rr_intervals(uint16_t *buffer, int buffer_size);