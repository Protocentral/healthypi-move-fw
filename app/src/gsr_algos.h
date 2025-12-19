#ifndef GSR_ALGO_H
#define GSR_ALGO_H

#include <stdint.h>

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
void convert_raw_to_uS(int32_t *raw_data, float *gsr_data, int length);
void smooth_gsr(float *data, int length, int window);
void remove_baseline(float *data, int length, int window);
#endif // GSR_ALGO_H
