#ifndef GSR_ALGO_H
#define GSR_ALGO_H

#include <stdint.h>

#define SCR_THRESHOLD_DEFAULT 150    // Default threshold, adjust per calibration
#define MIN_PEAK_GAP 10              // Minimum samples between SCR peaks (~0.3 sec at 32Hz)


// Function to calculate SCR count from GSR buffer
int calculate_scr_count(int32_t *gsr_buffer, int sample_count);
//int calculate_scr_count(int32_t *gsr_buffer, uint16_t sample_count);


#endif // GSR_ALGO_H
