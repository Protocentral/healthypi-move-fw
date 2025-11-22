#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "hrv_algos.h"
#include "ui/move_ui.h"
#include "log_module.h"
#include "hpi_sys.h"

LOG_MODULE_REGISTER(hrv_algos, LOG_LEVEL_DBG);

K_MUTEX_DEFINE(hrv_mutex);

extern struct k_sem sem_ecg_stop;
extern struct k_sem sem_ecg_complete;

bool collecting = false;
static uint16_t rr_interval_buffer[HRV_LIMIT];
int64_t last_measurement_time = 0;

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

// Static state for HRV calculations
static hrv_state_t hrv_state = {
    .sample_count = 0,
    .buffer_index = 0,
    .buffer_full = false,
    .cached_mean = 0.0f,
    .mean_valid = false
};

/* Add a new RR interval to the circular buffer */

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


/* Calculate mean RR interval with caching */

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

/* Calculate SDNN (Standard Deviation of NN intervals) */

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

/* Calculate RMSSD (Root Mean Square of Successive Differences) */

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

/* Calculate pNN50 (percentage of successive RR intervals that differ by more than 50ms) */

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
    
    return ((float)count_over_50ms * 100) / total_differences;
}

/* Find minimum RR interval in the buffer */

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

/* Find maximum RR interval in the buffer */

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

/* Reset HRV calculation state */

void hrv_reset(void)
{
    k_mutex_lock(&hrv_mutex, K_FOREVER);
    memset(&hrv_state, 0, sizeof(hrv_state_t));
    LOG_DBG("HRV calculation state reset");
    last_measurement_time = k_uptime_get();
    k_mutex_unlock(&hrv_mutex);
}

/* Get current number of samples in the buffer */

int hrv_get_sample_count(void)
{
    int count;
    count = hrv_state.sample_count;
    return count;
}

/* Check if HRV buffer is ready for reliable calculations */

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
int hrv_get_rr_intervals(uint16_t *buffer, int buffer_size)
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

/* Update HRV screens with new RR interval data */

void on_new_rr_interval_detected(uint16_t rr_interval)
{
    LOG_INF("New RR interval detected : %d ms", rr_interval);

  
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

        
        struct tm tm_sys_time = hpi_sys_get_sys_time();
        int64_t timestamp = timeutil_timegm64(&tm_sys_time);

        // If timestamp invalid, fallback to uptime-based timestamp
        if (timestamp < 1577836800LL) {   // anything before year 2020 is invalid
            int64_t up_sec = k_uptime_get() / 1000;

            // Base timestamp: Jan 1 2025 00:00:00
            timestamp = 1735689600LL + up_sec;
        }

         LOG_INF("Timestamp : %d", timestamp);

      hpi_write_hrv_rr_record_file(rr_interval_buffer, sample_count, timestamp);
       
        float mean = hrv_calculate_mean();
        float sdnn = hrv_calculate_sdnn();
        float rmssd = hrv_calculate_rmssd();
        float pnn50 = hrv_calculate_pnn50();    
        float hrv_min = (float)hrv_calculate_min();
        float hrv_max = (float)hrv_calculate_max();
       

        LOG_INF("HRV 30 interval Collection Complete: \nSamples=%d\nMean=%.1f\nSDNN=%.1f\nRMSSD=%.1f\npNN50=%.3f\nMAX=%.2f\nMIN=%.2f",sample_count, mean, sdnn, rmssd, pnn50, hrv_max, hrv_min);

        hpi_hrv_frequency_compact_update_spectrum(rr_interval_buffer, sample_count, sdnn, rmssd);


       // k_sem_give(&sem_ecg_stop);
        k_sem_give(&sem_ecg_complete);

       // hpi_load_scr_spl(SCR_SPL_HRV_FREQUENCY, SCROLL_UP, (uint8_t)SCR_HRV, 0, 0, 0);

        hpi_load_scr_spl(SCR_SPL_HRV_FREQUENCY, SCROLL_UP, (uint8_t)SCR_SPL_HRV_FREQUENCY, 0, 0, 0);

       
      
    }
}


