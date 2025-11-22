#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "gsr_algo.h"


#define SCR_THRESHOLD    0.03f   // Threshold in µS (typical 0.03 µS)
#define SCR_MIN_INTERVAL 32      // Minimum interval between SCRs (1 s at 32 Hz)
#define GSR_MAX_SAMPLES  1024    // Maximum expected GSR samples

// Static buffers
static float gsr_uS[GSR_MAX_SAMPLES];
static float smooth_temp[GSR_MAX_SAMPLES];
static float baseline_temp[GSR_MAX_SAMPLES];

// Convert raw 24-bit MAX30001 BIOZ counts to µS
void convert_raw_to_uS(int32_t *raw_data, float *gsr_data, int length)
{
    for (int i = 0; i < length; i++)
    {
        // 24-bit signed ADC: counts range [-8388608, +8388607]
        // Vref = 2400 mV, series resistor = 1 MOhm, convert to µS
        gsr_data[i] = ((float)raw_data[i] * 2400.0f) / 8388607.0f / 1000.0f; 
    }
}

// Simple moving average for smoothing
void smooth_gsr(float *data, int length, int window)
{
    for (int i = 0; i < length; i++) {
        float sum = 0;
        int count = 0;
        for (int j = i - window/2; j <= i + window/2; j++) {
            if (j >= 0 && j < length) {
                sum += data[j];
                count++;
            }
        }
        smooth_temp[i] = sum / count;
    }
    // Copy back
    for (int i = 0; i < length; i++)
        data[i] = smooth_temp[i];
}

// Simple baseline removal
void remove_baseline(float *data, int length, int window)
{
    for (int i = 0; i < length; i++) {
        float sum = 0;
        int count = 0;
        for (int j = i - window/2; j <= i + window/2; j++) {
            if (j >= 0 && j < length) {
                sum += data[j];
                count++;
            }
        }
        baseline_temp[i] = sum / count;
    }
    // Subtract baseline
    for (int i = 0; i < length; i++)
        data[i] -= baseline_temp[i];
}

// Calculate SCR count from raw data
int calculate_scr_count(int32_t *raw_gsr_data, int length)
{
    int scr_count = 0;
    int last_peak_index = -SCR_MIN_INTERVAL;

    // 1. Convert raw counts to µS
    convert_raw_to_uS(raw_gsr_data, gsr_uS, length);

    // 2. Smooth the signal
    smooth_gsr(gsr_uS, length, 5);        // 5-sample moving average

    // 3. Remove baseline
    remove_baseline(gsr_uS, length, 128); // 128 samples → 4 sec at 32 Hz

    // 4. Detect SCR peaks
    for (int i = 1; i < length - 1; i++) {
        // Find local minimum (trough)
        if (gsr_uS[i] < gsr_uS[i-1] && gsr_uS[i] < gsr_uS[i+1]) {
            int trough = i;
            int peak_index = -1;

            // Search forward for next local maximum (peak)
            for (int j = i + 1; j < length - 1; j++) {
                if (gsr_uS[j] > gsr_uS[j-1] && gsr_uS[j] > gsr_uS[j+1]) {
                    peak_index = j;
                    float amplitude = gsr_uS[peak_index] - gsr_uS[trough];

                    if (amplitude >= SCR_THRESHOLD && (peak_index - last_peak_index) >= SCR_MIN_INTERVAL) {
                        scr_count++;
                           
                    printf("SCR %d detected: trough=%d, peak=%d, amplitude=%f\n",
                            scr_count, trough, peak_index, amplitude);
                        last_peak_index = peak_index;
                        
                    }

                    i = peak_index; // Skip to peak
                    break;
                }
            }
        }
    }

    return scr_count;
}
