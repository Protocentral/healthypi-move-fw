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
#include "arm_math.h"


#define MAX_RR_INTERVALS 300       // Maximum RR intervals to process
#define INTERP_FS 4.0f             // Interpolation sampling frequency (Hz)

// FFT size must be power of 2. Larger = better frequency resolution.
// With INTERP_FS=4Hz: FFT_SIZE=64 gives 0.0625 Hz resolution
// LF band (0.04-0.15 Hz) will have ~2 bins, HF band (0.15-0.4 Hz) will have ~4 bins
// Note: 64 chosen to work with shorter recordings (30-45 RR intervals = ~120-180 samples)
#define FFT_SIZE 64               // 64-point FFT for reliable LF/HF with shorter recordings

#define WELCH_OVERLAP 0.5f         // 50% overlap for Welch method

// Frequency band definitions (Hz)
#define LF_LOW   0.04f
#define LF_HIGH  0.15f
#define HF_LOW   0.15f
#define HF_HIGH  0.4f

typedef struct 
{
  float mean;
  float sdnn;
  float rmssd;
  float pnn50;
  float hrv_min;
  float hrv_max;

}time_domain;

float hrv_calculate_mean(uint16_t * rr_buffer, int count);
float hrv_calculate_sdnn(uint16_t * rr_buffer, int count);
float hrv_calculate_pnn50(uint16_t * rr_buffer, int count);
uint32_t hrv_calculate_min(uint16_t * rr_buffer, int count);
uint32_t hrv_calculate_max(uint16_t * rr_buffer, int count);
void hpi_hrv_frequency_compact_update_spectrum(uint16_t *rr_intervals, int num_intervals);
float hpi_get_lf_hf_ratio(void);