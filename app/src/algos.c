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


#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <stdio.h>
#include <math.h>
#include "algos.h"

int rear = -1;
int k = 0;
unsigned int array[HRV_LIMIT];
int min_hrv = 0;
int max_hrv = 0;
float mean_hrv;
float sdnn;
float rmssd;
float pnn;
int max_t = 0;
int min_t = 0;
bool ready_flag = false;

// hpi_computed_hrv_t hrv_calculated;

void calculate_hrv(int32_t heart_rate, int32_t *hrv_max, int32_t *hrv_min, float *mean, float *sdnn, float *pnn, float *rmssd, bool *hrv_ready_flag)
{
  k++;

  if (rear == HRV_LIMIT - 1)
  {
    for (int i = 0; i < (HRV_LIMIT - 1); i++)
    {
      array[i] = array[i + 1];
    }

    array[HRV_LIMIT - 1] = heart_rate;
  }
  else
  {
    rear++;
    array[rear] = heart_rate;
  }

  if (k >= HRV_LIMIT)
  {
    *hrv_max = calculate_hrvmax(array);
    *hrv_min = calculate_hrvmin(array);
    *mean = calculate_mean(array);
    *sdnn = calculate_sdnn(array);
    calculate_pnn_rmssd(array, pnn, rmssd);
    *hrv_ready_flag = true;
  }
}

int calculate_hrvmax(unsigned int array[])
{
  for (int i = 0; i < HRV_LIMIT; i++)
  {
    if (array[i] > max_t)
    {
      max_t = array[i];
    }
  }
  return max_t;
}

int calculate_hrvmin(unsigned int array[])
{
  min_t = max_hrv;
  for (int i = 0; i < HRV_LIMIT; i++)
  {
    if (array[i] < min_t)
    {
      min_t = array[i];
    }
  }
  return min_t;
}

float calculate_mean(unsigned int array[])
{
  int sum = 0;
  for (int i = 0; i < (HRV_LIMIT); i++)
  {
    sum = sum + array[i];
  }
  return ((float)sum) / HRV_LIMIT;
}

float calculate_sdnn(unsigned int array[])
{
  int sumsdnn = 0;
  int diff;

  for (int i = 0; i < (HRV_LIMIT); i++)
  {
    diff = (array[i] - (mean_hrv)) * (array[i] - (mean_hrv));
    sumsdnn = sumsdnn + diff;
  }
  return sqrt(sumsdnn / (HRV_LIMIT));
}

void calculate_pnn_rmssd(unsigned int array[], float *pnn50, float *rmssd)
{
  unsigned int pnn_rmssd[HRV_LIMIT];
  int count = 0;
  int sqsum = 0;

  for (int i = 0; i < (HRV_LIMIT - 2); i++)
  {
    pnn_rmssd[i] = abs(array[i + 1] - array[i]);
    sqsum = sqsum + (pnn_rmssd[i] * pnn_rmssd[i]);

    if (pnn_rmssd[i] > 50)
    {
      count = count + 1;
    }
  }
  *pnn50 = ((float)count / HRV_LIMIT);
  *rmssd = sqrt(sqsum / (HRV_LIMIT - 1));
}

// GSR Stress Index Calculation (conditionally compiled)
#if defined(CONFIG_HPI_GSR_STRESS_INDEX)

#define GSR_HISTORY_SIZE 60           // 60 samples for peak detection
#define GSR_PEAK_THRESHOLD_X100 30    // 0.3 μS minimum rise to count as peak
#define GSR_PEAK_MIN_INTERVAL_MS 500  // Minimum 500ms between peaks
#define GSR_BASELINE_ALPHA 0.95f      // Exponential moving average alpha for baseline

static uint16_t gsr_history[GSR_HISTORY_SIZE];
static uint8_t gsr_history_index = 0;
static uint8_t gsr_history_count = 0;
static uint16_t gsr_baseline_ema_x100 = 0;
static bool gsr_baseline_initialized = false;

// Peak detection state
static uint8_t recent_peaks[60];      // Track peaks in last 60 seconds
static uint8_t peak_write_index = 0;
static uint16_t peak_amplitudes[10];  // Track last 10 peak amplitudes
static uint8_t peak_amp_index = 0;
static int64_t last_peak_time = 0;

void calculate_gsr_stress_index(uint16_t gsr_value_x100,
                                 struct hpi_gsr_stress_index_t *stress_index)
{
    if (!stress_index) {
        return;
    }

    // Initialize baseline on first call
    if (!gsr_baseline_initialized) {
        gsr_baseline_ema_x100 = gsr_value_x100;
        gsr_baseline_initialized = true;
        stress_index->stress_data_ready = false;
        return;
    }

    // Update exponential moving average baseline (tonic level)
    gsr_baseline_ema_x100 = (uint16_t)(GSR_BASELINE_ALPHA * gsr_baseline_ema_x100 +
                                       (1.0f - GSR_BASELINE_ALPHA) * gsr_value_x100);

    // Store in circular buffer
    gsr_history[gsr_history_index] = gsr_value_x100;
    gsr_history_index = (gsr_history_index + 1) % GSR_HISTORY_SIZE;
    if (gsr_history_count < GSR_HISTORY_SIZE) {
        gsr_history_count++;
    }

    // Calculate phasic component (deviation from baseline)
    int16_t phasic = gsr_value_x100 - gsr_baseline_ema_x100;
    if (phasic < 0) phasic = 0;

    // Peak detection: Look for significant rises above baseline
    int64_t current_time = k_uptime_get();
    bool is_peak = false;

    if (phasic > GSR_PEAK_THRESHOLD_X100 &&
        (current_time - last_peak_time) > GSR_PEAK_MIN_INTERVAL_MS) {

        // Check if this is a local maximum by comparing with recent samples
        bool is_local_max = true;
        uint8_t lookback = (gsr_history_count < 5) ? gsr_history_count : 5;

        for (uint8_t i = 1; i <= lookback && i < gsr_history_count; i++) {
            uint8_t prev_idx = (gsr_history_index + GSR_HISTORY_SIZE - i) % GSR_HISTORY_SIZE;
            if (gsr_value_x100 <= gsr_history[prev_idx]) {
                is_local_max = false;
                break;
            }
        }

        if (is_local_max) {
            is_peak = true;
            last_peak_time = current_time;
            stress_index->last_peak_timestamp = current_time;

            // Record peak amplitude
            peak_amplitudes[peak_amp_index] = phasic;
            peak_amp_index = (peak_amp_index + 1) % 10;

            // Track peak in time window (shift left and add new)
            for (int i = 0; i < 59; i++) {
                recent_peaks[i] = recent_peaks[i + 1];
            }
            recent_peaks[59] = 1;
        }
    }

    // If not a peak, shift peak history
    if (!is_peak && gsr_history_count > 0) {
        for (int i = 0; i < 59; i++) {
            recent_peaks[i] = recent_peaks[i + 1];
        }
        recent_peaks[59] = 0;
    }

    // Calculate peaks per minute
    uint8_t peak_count = 0;
    for (int i = 0; i < 60; i++) {
        peak_count += recent_peaks[i];
    }

    // Calculate mean peak amplitude from last 10 peaks
    uint32_t peak_sum = 0;
    uint8_t valid_peaks = 0;
    for (int i = 0; i < 10; i++) {
        if (peak_amplitudes[i] > 0) {
            peak_sum += peak_amplitudes[i];
            valid_peaks++;
        }
    }
    uint16_t mean_peak_amp = (valid_peaks > 0) ? (peak_sum / valid_peaks) : 0;

    // Calculate stress level (0-100)
    // Factors: peak frequency (60%), phasic amplitude (30%), baseline level (10%)
    uint8_t freq_score = (peak_count > 10) ? 100 : (peak_count * 10);
    uint8_t amp_score = (phasic > 500) ? 100 : ((phasic * 100) / 500);

    // Higher baseline indicates higher arousal state
    uint8_t baseline_score = 0;
    if (gsr_baseline_ema_x100 > 2000) {  // Above 20 μS
        baseline_score = 100;
    } else if (gsr_baseline_ema_x100 > 1000) {  // 10-20 μS
        baseline_score = (gsr_baseline_ema_x100 - 1000) / 10;
    }

    uint32_t weighted_stress = (freq_score * 60 + amp_score * 30 + baseline_score * 10) / 100;

    // Populate output structure
    stress_index->stress_level = (weighted_stress > 100) ? 100 : (uint8_t)weighted_stress;
    stress_index->tonic_level_x100 = gsr_baseline_ema_x100;
    stress_index->phasic_amplitude_x100 = phasic;
    stress_index->peaks_per_minute = peak_count;
    stress_index->mean_peak_amplitude_x100 = mean_peak_amp;
    stress_index->stress_data_ready = (gsr_history_count >= 10);  // Need at least 10 samples
}

void reset_gsr_stress_index(void)
{
    gsr_history_index = 0;
    gsr_history_count = 0;
    gsr_baseline_initialized = false;
    peak_write_index = 0;
    peak_amp_index = 0;
    last_peak_time = 0;

    for (int i = 0; i < 60; i++) {
        recent_peaks[i] = 0;
    }
    for (int i = 0; i < 10; i++) {
        peak_amplitudes[i] = 0;
    }
}

#endif // CONFIG_HPI_GSR_STRESS_INDEX
