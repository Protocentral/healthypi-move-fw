#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "hrv_algos.h"
#include "ui/move_ui.h"

LOG_MODULE_REGISTER(hrv_algos, LOG_LEVEL_DBG);

#define HRV_COLLECTION_MS 30000 
K_MUTEX_DEFINE(hrv_mutex);

static bool collecting = false;
static double rr_interval_buffer[HRV_LIMIT];

//HRV calculation state
typedef struct {
    double rr_intervals[HRV_LIMIT];
    int sample_count;
    int buffer_index;
    bool buffer_full;
    
    // Cached values to avoid recalculation
    float cached_mean;
    bool mean_valid;
} hrv_state_t;

// Static state for HRV calculations
static hrv_state_t hrv_state = {
    .sample_count = 0,
    .buffer_index = 0,
    .buffer_full = false,
    .cached_mean = 0.0f,
    .mean_valid = false
};

/**
 * @brief Add a new RR interval to the circular buffer
 * @param rr_interval RR interval in milliseconds
 */
static void hrv_add_sample(double rr_interval)
{
    k_mutex_lock(&hrv_mutex, K_FOREVER);
    hrv_state.rr_intervals[hrv_state.buffer_index] = rr_interval;
    hrv_state.buffer_index = (hrv_state.buffer_index + 1) % HRV_LIMIT;
    
    if (hrv_state.sample_count < HRV_LIMIT) {
        hrv_state.sample_count++;
    } else {
        hrv_state.buffer_full = true;
    }
    
    // Invalidate cached values when new data is added
    hrv_state.mean_valid = false;
    k_mutex_unlock(&hrv_mutex);
}

void start_hrv_collection(void)
{
    k_mutex_lock(&hrv_mutex, K_FOREVER);
    if (!collecting) {
        collecting = true;
        hrv_reset();
        LOG_INF("HRV 30 intervals collection started...");
    }
    k_mutex_unlock(&hrv_mutex);
}

void stop_hrv_collection(void)
{
   
    if (collecting) {
        LOG_INF("HRV 30 intervals collection stopped.");
        collecting = false;
       
    }

}


/**
 * @brief Calculate mean RR interval with caching
 * @return Mean RR interval in milliseconds
 */
float hrv_calculate_mean(void)
{
    if (hrv_state.mean_valid) {
        return hrv_state.cached_mean;
    }
    
    if (hrv_state.sample_count == 0) {
        return 0.0f;
    }
    
    uint64_t sum = 0;
    int count = hrv_state.buffer_full ? HRV_LIMIT : hrv_state.sample_count;
    
    for (int i = 0; i < count; i++) {
        sum += hrv_state.rr_intervals[i];
    }
    
    hrv_state.cached_mean = (float)sum / count;
    hrv_state.mean_valid = true;
    
    return hrv_state.cached_mean;
}

/**
 * @brief Calculate SDNN (Standard Deviation of NN intervals)
 * @return SDNN value in milliseconds
 */
float hrv_calculate_sdnn(void)
{
    if (hrv_state.sample_count < 2) {
        return 0.0f;
    }
    
    float mean = hrv_calculate_mean();
    float sum_squared_diff = 0.0f;
    int count = hrv_state.buffer_full ? HRV_LIMIT : hrv_state.sample_count;
    
    for (int i = 0; i < count; i++) {
        float diff = (float)hrv_state.rr_intervals[i] - mean;
        sum_squared_diff += diff * diff;
    }
    
    return sqrtf(sum_squared_diff / (count - 1));
}

/**
 * @brief Calculate RMSSD (Root Mean Square of Successive Differences)
 * @return RMSSD value in milliseconds
 */
float hrv_calculate_rmssd(void)
{
    if (hrv_state.sample_count < 2) {
        return 0.0f;
    }
    
    float sum_squared_diff = 0.0f;
    int count = hrv_state.buffer_full ? HRV_LIMIT : hrv_state.sample_count;
    int differences = 0;
    
    for (int i = 0; i < count - 1; i++) {
        int32_t diff = (int32_t)hrv_state.rr_intervals[(i + 1) % HRV_LIMIT] - 
                       (int32_t)hrv_state.rr_intervals[i];
        sum_squared_diff += (float)(diff * diff);
        differences++;
    }
    
    if (differences == 0) {
        return 0.0f;
    }
    
    return sqrtf(sum_squared_diff / differences);
}

/**
 * @brief Calculate pNN50 (percentage of successive RR intervals that differ by more than 50ms)
 * @return pNN50 value as a percentage (0.0 to 1.0)
 */
float hrv_calculate_pnn50(void)
{
    if (hrv_state.sample_count < 2) {
        return 0.0f;
    }
    
    int count_over_50ms = 0;
    int count = hrv_state.buffer_full ? HRV_LIMIT : hrv_state.sample_count;
    int total_differences = 0;
    
    for (int i = 0; i < count - 1; i++) {
        int32_t diff = abs((int32_t)hrv_state.rr_intervals[(i + 1) % HRV_LIMIT] - 
                          (int32_t)hrv_state.rr_intervals[i]);
        if (diff > 50) {
            count_over_50ms++;
        }
        total_differences++;
    }
    
    if (total_differences == 0) {
        return 0.0f;
    }
    
    return (float)count_over_50ms / total_differences;
}

/**
 * @brief Find minimum RR interval in the buffer
 * @return Minimum RR interval in milliseconds
 */
uint32_t hrv_calculate_min(void)
{
    if (hrv_state.sample_count == 0) {
        return 0;
    }
    
    uint32_t min_val = hrv_state.rr_intervals[0];
    int count = hrv_state.buffer_full ? HRV_LIMIT : hrv_state.sample_count;
    
    for (int i = 1; i < count; i++) {
        if (hrv_state.rr_intervals[i] < min_val) {
            min_val = hrv_state.rr_intervals[i];
        }
    }
    
    return min_val;
}

/**
 * @brief Find maximum RR interval in the buffer
 * @return Maximum RR interval in milliseconds
 */
uint32_t hrv_calculate_max(void)
{
    if (hrv_state.sample_count == 0) {
        return 0;
    }
    
    uint32_t max_val = hrv_state.rr_intervals[0];
    int count = hrv_state.buffer_full ? HRV_LIMIT : hrv_state.sample_count;
    
    for (int i = 1; i < count; i++) {
        if (hrv_state.rr_intervals[i] > max_val) {
            max_val = hrv_state.rr_intervals[i];
        }
    }
    
    return max_val;
}

/**
 * @brief Reset HRV calculation state
 */
void hrv_reset(void)
{
    k_mutex_lock(&hrv_mutex, K_FOREVER);
    memset(&hrv_state, 0, sizeof(hrv_state_t));
    LOG_DBG("HRV calculation state reset");
    k_mutex_unlock(&hrv_mutex);
}

/**
 * @brief Get current number of samples in the buffer
 * @return Number of samples (0 to HRV_LIMIT)
 */
int hrv_get_sample_count(void)
{
    int count;
    count = hrv_state.sample_count;
    return count;
}

/**
 * @brief Check if HRV buffer is ready for reliable calculations
 * @return true if buffer has enough samples for reliable HRV metrics
 */
bool hrv_is_ready(void)
{
    return hrv_state.sample_count >= HRV_LIMIT;
}

/**
 * @brief Get raw RR interval buffer for frequency domain analysis
 * @param buffer Pointer to buffer to copy RR intervals to
 * @param buffer_size Size of the buffer
 * @return Number of samples copied
 */
int hrv_get_rr_intervals(double *buffer, int buffer_size)
{
    if (!buffer || buffer_size <= 0) {
        return 0;
    }
    
    int count = hrv_state.buffer_full ? HRV_LIMIT : hrv_state.sample_count;
    int samples_to_copy = (count < buffer_size) ? count : buffer_size;
    
    for (int i = 0; i < samples_to_copy; i++) {
        buffer[i] = hrv_state.rr_intervals[i];
    }
    
    return samples_to_copy;
}

/**
 * @brief Update HRV screens with new RR interval data
 * @param rr_interval New RR interval in milliseconds
 */

//void update_hrv_screens_with_new_data(double rr_interval)
void on_new_rr_interval_detected(double rr_interval)
{
    LOG_INF("New RR interval detected : %.0f ms", rr_interval);

  
    if (!collecting) 
    return;


    hrv_add_sample(rr_interval);

    bool should_process = false;
    
    k_mutex_lock(&hrv_mutex, K_FOREVER);
    if (collecting && hrv_state.sample_count >= HRV_LIMIT) {
        collecting = false;
        should_process = true;
    }
    k_mutex_unlock(&hrv_mutex);

    if (should_process) {
        
        int sample_count = hrv_get_rr_intervals(rr_interval_buffer, HRV_LIMIT);

       
        float mean = hrv_calculate_mean();
        float sdnn = hrv_calculate_sdnn();
        float rmssd = hrv_calculate_rmssd();
        float pnn50 = hrv_calculate_pnn50();    
        float hrv_max = (float)hrv_calculate_max();
        float hrv_min = (float)hrv_calculate_min();

        LOG_INF("HRV 30 interval Collection Complete: \nSamples=%d\nMean=%.1f\nSDNN=%.1f\nRMSSD=%.1f\npNN50=%.3f\nMAX=%.2f\nMIN=%.2f",sample_count, mean, sdnn, rmssd, pnn50, hrv_max, hrv_min);

        hpi_load_scr_spl(SCR_SPL_HRV_FREQUENCY, SCROLL_UP, (uint8_t)SCR_HRV_SUMMARY, 0, 0, 0);

        hpi_hrv_frequency_compact_update_spectrum(rr_interval_buffer, sample_count);
      
    }
}


