#ifndef GSR_ALGO_H
#define GSR_ALGO_H

#include <stdint.h>
#include <stdbool.h>

// Forward declaration for stress index structure
struct hpi_gsr_stress_index_t;

/**
 * @brief Calculate SCR (Skin Conductance Response) count from GSR buffer
 *
 * Processes raw BioZ samples to detect SCR peaks indicating arousal/stress responses.
 * Uses signal smoothing, baseline removal, and peak detection algorithm.
 *
 * @param gsr_buffer Pointer to raw 24-bit BioZ samples from MAX30001
 * @param sample_count Number of samples in buffer (max 1024)
 * @return Number of SCR events detected, or 0 on error
 */
int calculate_scr_count(int32_t *gsr_buffer, int sample_count);

/**
 * @brief Calculate GSR stress index from raw sample buffer
 *
 * Processes accumulated GSR samples to calculate stress metrics:
 * - Tonic Level (SCL): Baseline skin conductance
 * - SCR Count: Number of phasic responses per minute
 * - Peak Amplitude: Mean amplitude of detected SCR peaks
 * - Stress Level: Composite score 0-100
 *
 * @param raw_gsr_data Pointer to raw 24-bit BioZ samples from MAX30001
 * @param sample_count Number of samples in buffer (typically 960 for 30s @ 32Hz)
 * @param duration_sec Duration of measurement in seconds
 * @param result Output structure for stress metrics
 */
void calculate_gsr_stress_index(const int32_t *raw_gsr_data, int sample_count,
                                 int duration_sec, struct hpi_gsr_stress_index_t *result);

void convert_raw_to_uS(const int32_t *raw_data, float *gsr_data, int length);
void smooth_gsr(float *data, int length, int window);
void remove_baseline(float *data, int length, int window);

#endif // GSR_ALGO_H
