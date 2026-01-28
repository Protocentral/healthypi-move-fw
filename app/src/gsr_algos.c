#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <math.h>
#include <string.h>
#include "gsr_algos.h"
#include "hpi_common_types.h"

LOG_MODULE_REGISTER(gsr_algos, LOG_LEVEL_DBG);

#define SCR_THRESHOLD    0.03f   // Threshold in µS (typical 0.03 µS)
#define SCR_MIN_INTERVAL 32      // Minimum interval between SCRs (1 s at 32 Hz)
#define GSR_MAX_SAMPLES  1024    // Maximum expected GSR samples

// Static buffers
static float gsr_uS[GSR_MAX_SAMPLES];
static float smooth_temp[GSR_MAX_SAMPLES];
static float baseline_temp[GSR_MAX_SAMPLES];

/*
 * Convert driver-provided BioZ data to µS (microsiemens)
 *
 * The MAX30001 driver now performs the ADC-to-conductance conversion internally
 * using the datasheet formula and outputs values as fixed-point (µS × 100).
 * This function simply converts from fixed-point to float.
 *
 * Driver conversion formula (in max30001.h):
 *   Z (Ω) = ADC × VREF / (2^19 × CGMAG × GAIN)
 *   Conductance (µS) = 1/Z × 10^6
 */
void convert_raw_to_uS(const int32_t *raw_data, float *gsr_data, int length)
{
    for (int i = 0; i < length; i++)
    {
        /* Driver outputs conductance as fixed-point: µS × 100
         * Divide by 100 to get actual µS value */
        gsr_data[i] = (float)raw_data[i] / 100.0f;
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

// Calculate SCR count from conductance data
int calculate_scr_count(int32_t *gsr_data, int length)
{
    int scr_count = 0;
    int last_peak_index = -SCR_MIN_INTERVAL;

    // Bounds check to prevent buffer overflow
    if (gsr_data == NULL || length <= 0 || length > GSR_MAX_SAMPLES) {
        LOG_ERR("Invalid GSR data: ptr=%p, length=%d, max=%d", gsr_data, length, GSR_MAX_SAMPLES);
        return 0;
    }

    // 1. Convert fixed-point conductance to float µS
    convert_raw_to_uS(gsr_data, gsr_uS, length);

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
                        LOG_DBG("SCR %d detected: trough=%d, peak=%d, amplitude=%.4f",
                                scr_count, trough, peak_index, (double)amplitude);
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

/**
 * @brief Calculate GSR stress index from sample buffer
 *
 * Algorithm:
 * 1. Convert fixed-point conductance to float µS
 * 2. Calculate tonic level (SCL) as mean of smoothed signal
 * 3. Extract phasic component and detect SCR peaks
 * 4. Calculate stress level based on tonic level, SCR rate, and peak amplitude
 */
void calculate_gsr_stress_index(const int32_t *gsr_data, int sample_count,
                                 int duration_sec, struct hpi_gsr_stress_index_t *result)
{
    if (result == NULL) {
        return;
    }

    // Initialize result
    memset(result, 0, sizeof(struct hpi_gsr_stress_index_t));

    // Validate inputs
    if (gsr_data == NULL || sample_count <= 0 || sample_count > GSR_MAX_SAMPLES || duration_sec <= 0) {
        LOG_ERR("Invalid stress index input: ptr=%p, count=%d, duration=%d",
                gsr_data, sample_count, duration_sec);
        return;
    }

    // Step 1: Convert fixed-point conductance to float µS
    convert_raw_to_uS(gsr_data, gsr_uS, sample_count);

    // Step 2: Calculate tonic level (SCL) - mean conductance before baseline removal
    float tonic_sum = 0.0f;
    for (int i = 0; i < sample_count; i++) {
        tonic_sum += gsr_uS[i];
    }
    float tonic_level = tonic_sum / sample_count;

    // Store tonic level (x100 for integer storage)
    result->tonic_level_x100 = (uint16_t)(tonic_level * 100.0f);

    // Step 3: Smooth the signal for peak detection
    smooth_gsr(gsr_uS, sample_count, 5);

    // Step 4: Remove baseline to get phasic component
    remove_baseline(gsr_uS, sample_count, 128);

    // Step 5: Detect SCR peaks and calculate metrics
    int scr_count = 0;
    int last_peak_index = -SCR_MIN_INTERVAL;
    float peak_amplitude_sum = 0.0f;
    float max_peak_amplitude = 0.0f;

    for (int i = 1; i < sample_count - 1; i++) {
        // Find local minimum (trough)
        if (gsr_uS[i] < gsr_uS[i - 1] && gsr_uS[i] < gsr_uS[i + 1]) {
            int trough = i;

            // Search forward for next local maximum (peak)
            for (int j = i + 1; j < sample_count - 1; j++) {
                if (gsr_uS[j] > gsr_uS[j - 1] && gsr_uS[j] > gsr_uS[j + 1]) {
                    float amplitude = gsr_uS[j] - gsr_uS[trough];

                    if (amplitude >= SCR_THRESHOLD && (j - last_peak_index) >= SCR_MIN_INTERVAL) {
                        scr_count++;
                        peak_amplitude_sum += amplitude;
                        if (amplitude > max_peak_amplitude) {
                            max_peak_amplitude = amplitude;
                        }
                        last_peak_index = j;
                        LOG_DBG("SCR peak %d: amp=%.4f uS", scr_count, (double)amplitude);
                    }
                    i = j;
                    break;
                }
            }
        }
    }

    // Calculate peaks per minute
    result->peaks_per_minute = (uint8_t)((scr_count * 60) / duration_sec);

    // Calculate mean peak amplitude (x100 for integer storage)
    if (scr_count > 0) {
        result->mean_peak_amplitude_x100 = (uint16_t)((peak_amplitude_sum / scr_count) * 100.0f);
        result->phasic_amplitude_x100 = (uint16_t)(max_peak_amplitude * 100.0f);
    }

    // Step 6: Calculate composite stress level (0-100)
    // Weighted formula based on:
    // - Tonic level: Higher baseline = higher arousal (weight: 40%)
    // - SCR rate: More responses = higher stress (weight: 40%)
    // - Peak amplitude: Larger responses = stronger reactions (weight: 20%)

    // Tonic component (0-40 points)
    // Typical range: 2-20 µS, map to 0-40
    float tonic_score = 0.0f;
    if (tonic_level < 2.0f) {
        tonic_score = 0.0f;
    } else if (tonic_level > 20.0f) {
        tonic_score = 40.0f;
    } else {
        tonic_score = ((tonic_level - 2.0f) / 18.0f) * 40.0f;
    }

    // SCR rate component (0-40 points)
    // Typical range: 0-10 /min at rest, map to 0-40
    float scr_rate = (float)(scr_count * 60) / duration_sec;
    float scr_score = 0.0f;
    if (scr_rate > 10.0f) {
        scr_score = 40.0f;
    } else {
        scr_score = (scr_rate / 10.0f) * 40.0f;
    }

    // Peak amplitude component (0-20 points)
    // Typical range: 0-0.5 µS, map to 0-20
    float amp_score = 0.0f;
    if (scr_count > 0) {
        float mean_amp = peak_amplitude_sum / scr_count;
        if (mean_amp > 0.5f) {
            amp_score = 20.0f;
        } else {
            amp_score = (mean_amp / 0.5f) * 20.0f;
        }
    }

    // Combine scores
    float stress_level = tonic_score + scr_score + amp_score;
    if (stress_level > 100.0f) {
        stress_level = 100.0f;
    }
    result->stress_level = (uint8_t)stress_level;

    // Mark data as ready
    result->stress_data_ready = true;
    result->last_peak_timestamp = k_uptime_get();

    LOG_INF("GSR Stress Index: level=%u, tonic=%.2f uS, SCR=%u/min, mean_amp=%.3f uS",
            result->stress_level, (double)tonic_level, result->peaks_per_minute,
            scr_count > 0 ? (double)(peak_amplitude_sum / scr_count) : 0.0);
}
