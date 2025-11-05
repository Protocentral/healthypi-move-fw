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
#define HRV_LIMIT 256

void calculate_pnn_rmssd(unsigned int array[], float *pnn50, float *rmssd);
float calculate_sdnn(unsigned int array[]);
float calculate_mean(unsigned int array[]);
int calculate_hrvmin(unsigned int array[]);
int calculate_hrvmax(unsigned int array[]);
//struct calculate_hrv(uint8_t heart_rate);

void calculate_hrv (int32_t heart_rate, int32_t *hrv_max, int32_t *hrv_min, float *mean, float *sdnn, float *pnn, float *rmssd, bool *hrv_ready_flag);

