#pragma once

#define SF_spo2 25 // sampling frequency
#define BUFFER_SIZE (SF_spo2 * 4)
#define MA4_SIZE 4 // DONOT CHANGE
#define min(x, y) ((x) < (y) ? (x) : (y))

#define FILTERORDER 161 /* DC Removal Numerator Coeff*/
#define NRCOEFF (0.992)
#define HRV_LIMIT 20

void calculate_pnn_rmssd(unsigned int array[], float *pnn50, float *rmssd);
float calculate_sdnn(unsigned int array[]);
float calculate_mean(unsigned int array[]);
int calculate_hrvmin(unsigned int array[]);
int calculate_hrvmax(unsigned int array[]);
//struct calculate_hrv(uint8_t heart_rate);
void calculate_hrv (int32_t heart_rate, int32_t *hrv_max, int32_t *hrv_min, float *mean, float *sdnn, float *pnn, float *rmssd, bool *hrv_ready_flag);