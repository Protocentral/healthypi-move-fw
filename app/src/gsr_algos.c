#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <math.h>
#include <string.h>
#include "gsr_algos.h"

LOG_MODULE_REGISTER(gsr_algos, LOG_LEVEL_DBG);

#define SCR_THRESHOLD    0.03f   // Threshold in µS (typical 0.03 µS)
#define SCR_MIN_INTERVAL 32      // Minimum interval between SCRs (1 s at 32 Hz)
#define GSR_MAX_SAMPLES  1024    // Maximum expected GSR samples

// Static buffers
static float gsr_uS[GSR_MAX_SAMPLES];
static float smooth_temp[GSR_MAX_SAMPLES];
static float baseline_temp[GSR_MAX_SAMPLES];

// Hardware configuration from DTS (boards/protocentral/healthypi_move/nrf5340_cpuapp_common.dtsi):
// bioz-gain = 1 → 20 V/V
// bioz_cgmag = 2 → 16 µA
#define BIOZ_V_REF      1.0f        // MAX30001 internal reference voltage (V)
#define BIOZ_GAIN       20.0f       // bioz-gain=1 → 20 V/V
#define BIOZ_I_MAG      16e-6f      // bioz_cgmag=2 → 16 µA excitation current
#define BIOZ_FS_24BIT   8388608.0f  // 2^23 full scale for 24-bit signed ADC
#define BIOZ_MIN_Z_OHMS 0.1f        // Minimum valid impedance to avoid divide-by-zero

// Convert raw 24-bit MAX30001 BIOZ counts to µS (microsiemens)
// Formula: Conductance (µS) = 1 / Impedance (Ω) × 1,000,000
// Where: Impedance = V_electrode / I_excitation
//        V_electrode = (raw / 2^23) × (Vref / Gain)
void convert_raw_to_uS(const int32_t *raw_data, float *gsr_data, int length)
{
    for (int i = 0; i < length; i++)
    {
        // Step 1: Calculate electrode voltage from ADC counts
        float v_electrode = ((float)raw_data[i] / BIOZ_FS_24BIT) * (BIOZ_V_REF / BIOZ_GAIN);

        // Step 2: Calculate impedance (Z = V / I), use absolute value
        float impedance = fabsf(v_electrode / BIOZ_I_MAG);

        // Step 3: Convert to conductance in microsiemens (µS)
        // Guard against divide-by-zero for short circuits or noise
        if (impedance < BIOZ_MIN_Z_OHMS) {
            gsr_data[i] = 0.0f;
        } else {
            gsr_data[i] = (1.0f / impedance) * 1e6f;
        }
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

    // Bounds check to prevent buffer overflow
    if (raw_gsr_data == NULL || length <= 0 || length > GSR_MAX_SAMPLES) {
        LOG_ERR("Invalid GSR data: ptr=%p, length=%d, max=%d", raw_gsr_data, length, GSR_MAX_SAMPLES);
        return 0;
    }

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
