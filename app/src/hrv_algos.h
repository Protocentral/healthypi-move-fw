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

#define HRV_LIMIT 120
#define MAX_RR_INTERVALS 30        // Maximum RR intervals to process
#define INTERP_FS 4.0f             // Interpolation sampling frequency (Hz)
#define FFT_SIZE 64                // Must be power of 2
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

//HRV calculation state
typedef struct {
    uint16_t rr_intervals[HRV_LIMIT];
    int sample_count;
    int buffer_index;
    bool buffer_full; 
    // Cached values to avoid recalculation
    float cached_mean;
    bool mean_valid;
} hrv_state_t;

// Required buffer sizes to process LF and HF power
float32_t rr_time[MAX_RR_INTERVALS + 1]; // Time taken to collect samples (cumulative time processed from RR intervals)
float32_t rr_values[MAX_RR_INTERVALS + 1]; // RR intervals in seconds
float32_t interp_signal[FFT_SIZE * 4];  // Larger buffer for interpolated signal
float32_t fft_input[FFT_SIZE * 2];      // Complex FFT input
float32_t fft_output[FFT_SIZE * 2];     // Complex FFT output
float32_t psd[FFT_SIZE];                // Power spectral density
float32_t window[FFT_SIZE];             // Hanning window


// Static variables for HRV frequency analysis
float lf_power_compact = 0.0f;
float hf_power_compact = 0.0f;
float stress_score_compact = 0.0f;
float sdnn_val = 0.0f;
float rmssd_val = 0.0f;

int interval_counter = 0;

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
void hpi_hrv_frequency_compact_update_spectrum(uint16_t *rr_intervals, int num_intervals);