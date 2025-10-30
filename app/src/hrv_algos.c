// #include <zephyr/kernel.h>
// #include <zephyr/logging/log.h>
// #include <zephyr/device.h>
// #include <zephyr/drivers/sensor.h>
// #include <stdio.h>
// #include <math.h>
// #include <string.h>
// #include "hrv_algos.h"

// LOG_MODULE_REGISTER(hrv_algos, LOG_LEVEL_DBG);

// // HRV calculation state
// typedef struct {
//     uint32_t rr_intervals[HRV_LIMIT];
//     int sample_count;
//     int buffer_index;
//     bool buffer_full;
    
//     // Cached values to avoid recalculation
//     float cached_mean;
//     bool mean_valid;
// } hrv_state_t;

// // Static state for HRV calculations
// static hrv_state_t hrv_state = {
//     .sample_count = 0,
//     .buffer_index = 0,
//     .buffer_full = false,
//     .cached_mean = 0.0f,
//     .mean_valid = false
// };

// /**
//  * @brief Add a new RR interval to the circular buffer
//  * @param rr_interval RR interval in milliseconds
//  */
// static void hrv_add_sample(uint32_t rr_interval)
// {
//     hrv_state.rr_intervals[hrv_state.buffer_index] = rr_interval;
//     hrv_state.buffer_index = (hrv_state.buffer_index + 1) % HRV_LIMIT;
    
//     if (hrv_state.sample_count < HRV_LIMIT) {
//         hrv_state.sample_count++;
//     } else {
//         hrv_state.buffer_full = true;
//     }
    
//     // Invalidate cached values when new data is added
//     hrv_state.mean_valid = false;
// }

// /**
//  * @brief Calculate mean RR interval with caching
//  * @return Mean RR interval in milliseconds
//  */
// static float hrv_calculate_mean(void)
// {
//     if (hrv_state.mean_valid) {
//         return hrv_state.cached_mean;
//     }
    
//     if (hrv_state.sample_count == 0) {
//         return 0.0f;
//     }
    
//     uint64_t sum = 0;
//     int count = hrv_state.buffer_full ? HRV_LIMIT : hrv_state.sample_count;
    
//     for (int i = 0; i < count; i++) {
//         sum += hrv_state.rr_intervals[i];
//     }
    
//     hrv_state.cached_mean = (float)sum / count;
//     hrv_state.mean_valid = true;
    
//     return hrv_state.cached_mean;
// }

// /**
//  * @brief Calculate SDNN (Standard Deviation of NN intervals)
//  * @return SDNN value in milliseconds
//  */
// static float hrv_calculate_sdnn(void)
// {
//     if (hrv_state.sample_count < 2) {
//         return 0.0f;
//     }
    
//     float mean = hrv_calculate_mean();
//     float sum_squared_diff = 0.0f;
//     int count = hrv_state.buffer_full ? HRV_LIMIT : hrv_state.sample_count;
    
//     for (int i = 0; i < count; i++) {
//         float diff = (float)hrv_state.rr_intervals[i] - mean;
//         sum_squared_diff += diff * diff;
//     }
    
//     return sqrtf(sum_squared_diff / (count - 1));
// }

// /**
//  * @brief Calculate RMSSD (Root Mean Square of Successive Differences)
//  * @return RMSSD value in milliseconds
//  */
// static float hrv_calculate_rmssd(void)
// {
//     if (hrv_state.sample_count < 2) {
//         return 0.0f;
//     }
    
//     float sum_squared_diff = 0.0f;
//     int count = hrv_state.buffer_full ? HRV_LIMIT : hrv_state.sample_count;
//     int differences = 0;
    
//     for (int i = 0; i < count - 1; i++) {
//         int32_t diff = (int32_t)hrv_state.rr_intervals[(i + 1) % HRV_LIMIT] - 
//                        (int32_t)hrv_state.rr_intervals[i];
//         sum_squared_diff += (float)(diff * diff);
//         differences++;
//     }
    
//     if (differences == 0) {
//         return 0.0f;
//     }
    
//     return sqrtf(sum_squared_diff / differences);
// }

// /**
//  * @brief Calculate pNN50 (percentage of successive RR intervals that differ by more than 50ms)
//  * @return pNN50 value as a percentage (0.0 to 1.0)
//  */
// static float hrv_calculate_pnn50(void)
// {
//     if (hrv_state.sample_count < 2) {
//         return 0.0f;
//     }
    
//     int count_over_50ms = 0;
//     int count = hrv_state.buffer_full ? HRV_LIMIT : hrv_state.sample_count;
//     int total_differences = 0;
    
//     for (int i = 0; i < count - 1; i++) {
//         int32_t diff = abs((int32_t)hrv_state.rr_intervals[(i + 1) % HRV_LIMIT] - 
//                           (int32_t)hrv_state.rr_intervals[i]);
//         if (diff > 50) {
//             count_over_50ms++;
//         }
//         total_differences++;
//     }
    
//     if (total_differences == 0) {
//         return 0.0f;
//     }
    
//     return (float)count_over_50ms / total_differences;
// }

// /**
//  * @brief Find minimum RR interval in the buffer
//  * @return Minimum RR interval in milliseconds
//  */
// static uint32_t hrv_calculate_min(void)
// {
//     if (hrv_state.sample_count == 0) {
//         return 0;
//     }
    
//     uint32_t min_val = hrv_state.rr_intervals[0];
//     int count = hrv_state.buffer_full ? HRV_LIMIT : hrv_state.sample_count;
    
//     for (int i = 1; i < count; i++) {
//         if (hrv_state.rr_intervals[i] < min_val) {
//             min_val = hrv_state.rr_intervals[i];
//         }
//     }
    
//     return min_val;
// }

// /**
//  * @brief Find maximum RR interval in the buffer
//  * @return Maximum RR interval in milliseconds
//  */
// static uint32_t hrv_calculate_max(void)
// {
//     if (hrv_state.sample_count == 0) {
//         return 0;
//     }
    
//     uint32_t max_val = hrv_state.rr_intervals[0];
//     int count = hrv_state.buffer_full ? HRV_LIMIT : hrv_state.sample_count;
    
//     for (int i = 1; i < count; i++) {
//         if (hrv_state.rr_intervals[i] > max_val) {
//             max_val = hrv_state.rr_intervals[i];
//         }
//     }
    
//     return max_val;
// }

// /**
//  * @brief Reset HRV calculation state
//  */
// void hrv_reset(void)
// {
//     memset(&hrv_state, 0, sizeof(hrv_state_t));
//     LOG_DBG("HRV calculation state reset");
// }

// /**
//  * @brief Get current number of samples in the buffer
//  * @return Number of samples (0 to HRV_LIMIT)
//  */
// int hrv_get_sample_count(void)
// {
//     return hrv_state.sample_count;
// }

// /**
//  * @brief Check if HRV buffer is ready for reliable calculations
//  * @return true if buffer has enough samples for reliable HRV metrics
//  */
// bool hrv_is_ready(void)
// {
//     return hrv_state.sample_count >= HRV_LIMIT;
// }

// /**
//  * @brief Main HRV calculation function
//  * @param rr_interval New RR interval in milliseconds
//  * @param hrv_max Pointer to store maximum RR interval
//  * @param hrv_min Pointer to store minimum RR interval  
//  * @param mean Pointer to store mean RR interval
//  * @param sdnn Pointer to store SDNN value
//  * @param pnn50 Pointer to store pNN50 value
//  * @param rmssd Pointer to store RMSSD value
//  * @param hrv_ready_flag Pointer to store readiness flag
//  */
// void calculate_hrv(int32_t rr_interval, int32_t *hrv_max, int32_t *hrv_min, 
//                    float *mean, float *sdnn, float *pnn50, float *rmssd, 
//                    bool *hrv_ready_flag)
// {
//     // Validate input parameters
//     if (!hrv_max || !hrv_min || !mean || !sdnn || !pnn50 || !rmssd || !hrv_ready_flag) {
//         LOG_ERR("Invalid NULL pointer in calculate_hrv");
//         return;
//     }
    
//     // Validate RR interval range (typical human range: 300-2000ms)
//     if (rr_interval < 300 || rr_interval > 2000) {
//         LOG_WRN("RR interval %d ms out of typical range", rr_interval);
//     }
    
//     // Add new sample to buffer
//     hrv_add_sample((uint32_t)rr_interval);
    
//     // Calculate metrics
//     *mean = hrv_calculate_mean();
//     *sdnn = hrv_calculate_sdnn();
//     *rmssd = hrv_calculate_rmssd();
//     *pnn50 = hrv_calculate_pnn50();
//     *hrv_max = (int32_t)hrv_calculate_max();
//     *hrv_min = (int32_t)hrv_calculate_min();
//     *hrv_ready_flag = hrv_is_ready();
    
//     LOG_DBG("HRV: RR=%d, Mean=%.1f, SDNN=%.1f, RMSSD=%.1f, pNN50=%.3f, Ready=%s",
//             rr_interval, *mean, *sdnn, *rmssd, *pnn50, 
//             *hrv_ready_flag ? "true" : "false");
// }

// /**
//  * @brief Get raw RR interval buffer for frequency domain analysis
//  * @param buffer Pointer to buffer to copy RR intervals to
//  * @param buffer_size Size of the buffer
//  * @return Number of samples copied
//  */
// int hrv_get_rr_intervals(float *buffer, int buffer_size)
// {
//     if (!buffer || buffer_size <= 0) {
//         return 0;
//     }
    
//     int count = hrv_state.buffer_full ? HRV_LIMIT : hrv_state.sample_count;
//     int samples_to_copy = (count < buffer_size) ? count : buffer_size;
    
//     for (int i = 0; i < samples_to_copy; i++) {
//         buffer[i] = (float)hrv_state.rr_intervals[i];
//     }
    
//     return samples_to_copy;
// }

// /**
//  * @brief Update HRV screens with new RR interval data
//  * @param rr_interval New RR interval in milliseconds
//  */
// void update_hrv_screens_with_new_data(float rr_interval)
// {
//     // Calculate HRV metrics using optimized algorithm
//     int32_t hrv_max, hrv_min;
//     float mean, sdnn, pnn50, rmssd;
//     bool hrv_ready;

//     calculate_hrv((int32_t)rr_interval, &hrv_max, &hrv_min, &mean, &sdnn, &pnn50, &rmssd, &hrv_ready);

//     if (hrv_ready) {
//         // Update HRV Summary Screen
//         extern void hpi_hrv_summary_update_metrics(float sdnn, float rmssd, float pnn50, float mean_rr);
//         extern void hpi_hrv_summary_draw_rr_plot(float rr_interval);
        
//         hpi_hrv_summary_update_metrics(sdnn, rmssd, pnn50, mean);
//         hpi_hrv_summary_draw_rr_plot(rr_interval);

//         // Update HRV Frequency Screen (less frequently for performance)
//         static int freq_update_counter = 0;
//         if (++freq_update_counter >= 10) {
//             static float rr_buffer[HRV_LIMIT];
//             int sample_count = hrv_get_rr_intervals(rr_buffer, HRV_LIMIT);
            
//             if (sample_count >= HRV_LIMIT) {
//                 extern void hpi_hrv_frequency_compact_update_spectrum(float *rr_intervals, int num_intervals);
//                 hpi_hrv_frequency_compact_update_spectrum(rr_buffer, sample_count);
//             }
//             freq_update_counter = 0;
//         }
//     }
// }

// /**
//  * @brief Callback for new RR interval detection from sensor
//  * @param rr_ms RR interval in milliseconds
//  */
// void on_new_rr_interval_detected(float rr_ms)
// {

    
//     hpi_disp_hrv_update_rtor((int)rr_ms);
//     hpi_disp_hrv_draw_plot_rtor(rr_ms);

//     // Update new optimized HRV screens
//     update_hrv_screens_with_new_data(rr_ms);
// }

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
// HRV calculation state
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
    hrv_state.rr_intervals[hrv_state.buffer_index] = rr_interval;
    hrv_state.buffer_index = (hrv_state.buffer_index + 1) % HRV_LIMIT;
    
    if (hrv_state.sample_count < HRV_LIMIT) {
        hrv_state.sample_count++;
    } else {
        hrv_state.buffer_full = true;
    }
    
    // Invalidate cached values when new data is added
    hrv_state.mean_valid = false;
}



static bool collecting = false;
static int64_t collection_start_time = 0;

void start_hrv_collection(void)
{
    if (!collecting) {
        collecting = true;
        collection_start_time = k_uptime_get();
        hrv_reset();
        LOG_INF("HRV 30s collection started...");
    }
}

void stop_hrv_collection(void)
{
    collecting = false;
}


/**
 * @brief Calculate mean RR interval with caching
 * @return Mean RR interval in milliseconds
 */
static float hrv_calculate_mean(void)
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
static float hrv_calculate_sdnn(void)
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
static float hrv_calculate_rmssd(void)
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
static float hrv_calculate_pnn50(void)
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
static uint32_t hrv_calculate_min(void)
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
static uint32_t hrv_calculate_max(void)
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
    memset(&hrv_state, 0, sizeof(hrv_state_t));
    LOG_DBG("HRV calculation state reset");
}

/**
 * @brief Get current number of samples in the buffer
 * @return Number of samples (0 to HRV_LIMIT)
 */
int hrv_get_sample_count(void)
{
    return hrv_state.sample_count;
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
 * @brief Main HRV calculation function
 * @param rr_interval New RR interval in milliseconds
 * @param hrv_max Pointer to store maximum RR interval
 * @param hrv_min Pointer to store minimum RR interval  
 * @param mean Pointer to store mean RR interval
 * @param sdnn Pointer to store SDNN value
 * @param pnn50 Pointer to store pNN50 value
 * @param rmssd Pointer to store RMSSD value
 * @param hrv_ready_flag Pointer to store readiness flag
 */
/*void calculate_hrv(int32_t rr_interval, int32_t *hrv_max, int32_t *hrv_min, 
                   float *mean, float *sdnn, float *pnn50, float *rmssd, 
                   bool *hrv_ready_flag)
{
    // Validate input parameters
    if (!hrv_max || !hrv_min || !mean || !sdnn || !pnn50 || !rmssd || !hrv_ready_flag) {
        LOG_ERR("Invalid NULL pointer in calculate_hrv");
        return;
    }
    
    // Validate RR interval range (typical human range: 300-2000ms)
    if (rr_interval < 300 || rr_interval > 2000) {
        LOG_WRN("RR interval %d ms out of typical range", rr_interval);
    }
    
    // Add new sample to buffer
    hrv_add_sample((uint32_t)rr_interval);
    
    if(hrv_ready_flag == true)
    {
    // Calculate metrics
    *mean = hrv_calculate_mean();
    *sdnn = hrv_calculate_sdnn();
    *rmssd = hrv_calculate_rmssd();
    *pnn50 = hrv_calculate_pnn50();
    *hrv_max = (int32_t)hrv_calculate_max();
    *hrv_min = (int32_t)hrv_calculate_min();
    *hrv_ready_flag = hrv_is_ready();
    
    LOG_DBG("HRV: RR=%d, Mean=%.1f, SDNN=%.1f, RMSSD=%.1f, pNN50=%.3f, Ready=%s",
            rr_interval, *mean, *sdnn, *rmssd, *pnn50, 
            *hrv_ready_flag ? "true" : "false");
    }    
}
*/
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
// void update_hrv_screens_with_new_data(float rr_interval)
// {
//     // Calculate HRV metrics using optimized algorithm
//     int32_t hrv_max, hrv_min;
//     float mean, sdnn, pnn50, rmssd;
//     bool hrv_ready;

//     calculate_hrv((int32_t)rr_interval, &hrv_max, &hrv_min, &mean, &sdnn, &pnn50, &rmssd, &hrv_ready);

//     if (hrv_ready)
//      {
//         // Update HRV Summary Screen
//        //extern void hpi_hrv_summary_update_metrics(float sdnn, float rmssd, float pnn50, float mean_rr);
//        // extern void hpi_hrv_summary_draw_rr_plot(float rr_interval);
        
//        // hpi_hrv_summary_update_metrics(sdnn, rmssd, pnn50, mean);
//         //hpi_hrv_summary_draw_rr_plot(rr_interval);

//         // Update HRV Frequency Screen (less frequently for performance)
//         static int freq_update_counter = 0;
//         LOG_INF("Frequency update counter: %d", freq_update_counter );
//         if (++freq_update_counter >= 10) {
//             static float rr_buffer[HRV_LIMIT];
//             int sample_count = hrv_get_rr_intervals(rr_buffer, HRV_LIMIT);
                
//                 if (sample_count >= HRV_LIMIT) {
//                     extern void hpi_hrv_frequency_compact_update_spectrum(float *rr_intervals, int num_intervals);
//                     LOG_INF("Updating HRV Frequency Compact Spectrum from HRV Algos");
                    

//                     hpi_hrv_frequency_compact_update_spectrum(rr_buffer, sample_count);
//                 }
//             freq_update_counter = 0;
//         }
//     }
// }
 

void update_hrv_screens_with_new_data(double rr_interval)
{
    if (!collecting) 
    return; 

    if (rr_interval < 300 || rr_interval > 2000) {
        LOG_WRN("RR interval %.1f ms out of typical range", rr_interval);
        return;
    }

    hrv_add_sample(rr_interval);

   
    if (k_uptime_get() - collection_start_time >= HRV_COLLECTION_MS) {
        collecting = false;

        static double rr_buffer[HRV_LIMIT];
        int sample_count = hrv_get_rr_intervals(rr_buffer, HRV_LIMIT);

        if (sample_count > 5)
         {
            float mean = hrv_calculate_mean();
            float sdnn = hrv_calculate_sdnn();
            float rmssd = hrv_calculate_rmssd();
            float pnn50 = hrv_calculate_pnn50();    
            float hrv_max = (float)hrv_calculate_max();
            float hrv_min = (float)hrv_calculate_min();

            LOG_INF("HRV 30s Collection Complete: \nSamples=%d\nMean=%.1f\nSDNN=%.1f\nRMSSD=%.1f\npNN50=%.3f\nMAX=%.2f\nMIN=%.2f",sample_count, mean, sdnn, rmssd, pnn50, hrv_max, hrv_min);

            extern void hpi_hrv_frequency_compact_update_spectrum(double *rr_intervals, int num_intervals);
            hpi_hrv_frequency_compact_update_spectrum(rr_buffer, sample_count);
        }
    }
}

/**
 * @brief Callback for new RR interval detection from sensor
 * @param rr_ms RR interval in milliseconds
 */
void on_new_rr_interval_detected(double rr_ms)
{

    LOG_INF("New RR interval detected(Inside function): %.0f ms", rr_ms);

    update_hrv_screens_with_new_data(rr_ms); 
}

