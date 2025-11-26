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
#include "arm_math.h"
#include "arm_const_structs.h"
#include <string.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(hrv_algos, LOG_LEVEL_DBG);

K_MUTEX_DEFINE(hrv_mutex);


// extern struct k_sem sem_ecg_complete;

bool collecting = false;
//static uint16_t rr_interval_buffer[HRV_LIMIT];
int64_t last_measurement_time = 0;

// Required buffer sizes to process LF and HF power
float32_t rr_time[MAX_RR_INTERVALS + 1]; // Time taken to collect samples (cumulative time processed from RR intervals)
float32_t rr_values[MAX_RR_INTERVALS + 1]; // RR intervals in seconds
float32_t interp_signal[FFT_SIZE * 4];  // Larger buffer for interpolated signal
float32_t fft_input[FFT_SIZE * 2];      // Complex FFT input
float32_t fft_output[FFT_SIZE * 2];     // Complex FFT output
float32_t psd[FFT_SIZE];                // Power spectral density
float32_t window[FFT_SIZE];             // Hanning window

// Static variables for HRV frequency analysis
float lf_power_compact = 0.0f;
float hf_power_compact = 0.0f;
float stress_score_compact = 0.0f;
float sdnn_val = 0.0f;
float rmssd_val = 0.0f;

int interval_counter = 0;

// Static state for HRV calculations, initialization of structure
static hrv_state_t hrv_state = {
    .sample_count = 0,
    .buffer_index = 0,
    .buffer_full = false,
    .cached_mean = 0.0f,
    .mean_valid = false
};
static time_domain tm_metrics = {
    .mean = 0,
    .rmssd = 0,
    .hrv_max = 0,
    .hrv_min = 0,
    .pnn50 = 0,
    .sdnn = 0
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
        LOG_INF("HRV %d intervals collection started...",HRV_LIMIT);
    }
    k_mutex_unlock(&hrv_mutex);
}

void stop_hrv_collection(void)
{
   
    if (collecting) {
        LOG_INF("HRV %d intervals collection stopped.", HRV_LIMIT);
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

// void on_new_rr_interval_detected(uint16_t rr_interval)
// {
//     LOG_INF("New RR interval detected : %d ms", rr_interval);

//     if (!collecting) 
//     return;

//     hrv_add_sample(rr_interval);

//     interval_counter = hrv_state.sample_count;

//     bool should_process = false;
    
//     k_mutex_lock(&hrv_mutex, K_FOREVER);
//     if (collecting && hrv_state.sample_count >= HRV_LIMIT) {
//         collecting = false;
//         should_process = true;
//     }
//     k_mutex_unlock(&hrv_mutex);

//     if (should_process) {

       // int sample_count = hrv_get_rr_intervals(rr_interval_buffer, HRV_LIMIT);

        
        // struct tm tm_sys_time = hpi_sys_get_sys_time();
        // int64_t timestamp = timeutil_timegm64(&tm_sys_time);

        // // If timestamp invalid, fallback to uptime-based timestamp
        // if (timestamp < 1577836800LL) {   // anything before year 2020 is invalid
        //     int64_t up_sec = k_uptime_get() / 1000;

        //     // Base timestamp: Jan 1 2025 00:00:00
        //     timestamp = 1735689600LL + up_sec;
        // }

       // LOG_INF("Timestamp : %d", timestamp);

       //hpi_write_hrv_rr_record_file(rr_interval_buffer, sample_count, timestamp);
       
        // tm_metrics.mean = hrv_calculate_mean();
        // tm_metrics.sdnn = hrv_calculate_sdnn();
        // tm_metrics.rmssd = hrv_calculate_rmssd();
        // tm_metrics.pnn50 = hrv_calculate_pnn50();    
        // tm_metrics.hrv_min = (float)hrv_calculate_min();
        // tm_metrics.hrv_max = (float)hrv_calculate_max();
       

        // LOG_INF("HRV 30 interval Collection Complete: \nSamples=%d\nMean=%.1f\nSDNN=%.1f\n",hrv_state.sample_count,tm_metrics.mean, tm_metrics.sdnn);
        // LOG_INF("RMSSD : %.1f\nPnn50: %.1f\nMAX : %.1f\nMIN : %.1f", tm_metrics.rmssd, tm_metrics.pnn50, tm_metrics.hrv_max, tm_metrics.hrv_min);

     //   hpi_hrv_frequency_compact_update_spectrum(hrv_state.rr_intervals, hrv_state.sample_count);

       // k_sem_give(&sem_ecg_complete);
  
//     }
// }

static float32_t linear_interp(float32_t x, float32_t x0, float32_t x1, float32_t y0, float32_t y1)
{
    if (x1 == x0) return y0;
    return y0 + (x - x0) * (y1 - y0) / (x1 - x0);
}

/* Interpolate RR intervals to evenly sampled signal */

static uint32_t interpolate_rr_intervals(uint16_t *rr_ms, uint32_t num_intervals,float32_t fs,float32_t *rr_time, float32_t *rr_values,
    float32_t *interp_signal,uint32_t max_interp_samples)
 {
    //Convert RR intervals to seconds and create time vector
    rr_time[0] = 0.0f;
    rr_values[0] = rr_ms[0] / 1000.0f;
    
    for (uint32_t i = 0; i < num_intervals; i++)
     {
        rr_values[i + 1] = rr_ms[i] / 1000.0f;
        rr_time[i + 1] = rr_time[i] + rr_values[i + 1];
     }    

    // Calculate number of interpolated samples
    float32_t total_time = rr_time[num_intervals];
    uint32_t num_samples = (uint32_t)(total_time * fs);
  
    
    if (num_samples > max_interp_samples) {
        num_samples = max_interp_samples;
    }
    
    // Interpolate using linear interpolation (simple and fast)
    float32_t dt = 1.0f / fs;
    uint32_t idx = 0;
    
    for (uint32_t i = 0; i < num_samples; i++)
    {
        float32_t t = i * dt;
        
        // Find the interval containing time t
        while (idx < num_intervals && rr_time[idx + 1] < t) 
            idx++;
        
        
        if (idx >= num_intervals) 
            break;
        
        
        // Linear interpolation
        interp_signal[i] = linear_interp(t, rr_time[idx], rr_time[idx + 1],rr_values[idx], rr_values[idx + 1]);
    }
    
    return num_samples;
}

/* Create Hanning window using CMSIS-DSP */

static void create_hanning_window(float32_t *window, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) {
        window[i] = 0.5f - 0.5f * arm_cos_f32(2.0f * PI * i / (size - 1));
    }
}

/* Remove mean from signal using CMSIS-DSP */
static void remove_mean(float32_t *signal, uint32_t length)
 {
        float32_t sum = 0.0f;

        for (uint32_t i = 0; i < length; i++) 
        {
            sum += signal[i];
        }

        float32_t mean = sum / length;

        for (uint32_t i = 0; i < length; i++) {
            signal[i] -= mean;
        }

}

/* Calculate PSD using Welch's method with CMSIS-DSP FFT */

static void calculate_psd_welch(float32_t *signal, uint32_t signal_len,float32_t *window, float32_t *fft_input,float32_t *fft_output, 
    float32_t *psd,uint32_t fft_size, float32_t fs) 
{
    // Initialize PSD to zero
    memset(psd, 0, sizeof(float32_t) * fft_size);
    
    // Calculate step size for 50% overlap
    uint32_t step = fft_size / 2;
    uint32_t num_segments = 0;
    
    // FFT instance (use appropriate size from arm_const_structs.h)
    const arm_cfft_instance_f32 *fft_instance;
    
    // Select appropriate FFT instance based on size
    switch(fft_size) {
        case 64 : fft_instance = &arm_cfft_sR_f32_len64; break;
        case 256:  fft_instance = &arm_cfft_sR_f32_len256; break;
        case 512:  fft_instance = &arm_cfft_sR_f32_len512; break;
        case 1024: fft_instance = &arm_cfft_sR_f32_len1024; break;
        default:   return; // Unsupported FFT size
    }
    
    // Process overlapping segments
    for (uint32_t start = 0; start + fft_size <= signal_len; start += step) 
    {
        // Copy segment and apply window

        for (uint32_t i = 0; i < fft_size; i++) {
            float32_t windowed = signal[start + i] * window[i];
            fft_input[2 * i] = windowed;      // Real part
            fft_input[2 * i + 1] = 0.0f;      // Imaginary part
        }

    
        // Perform FFT
        arm_copy_f32(fft_input, fft_output, fft_size * 2);
        arm_cfft_f32(fft_instance, fft_output, 0, 1);
        
        // Calculate magnitude squared and accumulate
        for (uint32_t i = 0; i < fft_size; i++) {
            float32_t real = fft_output[2 * i];
            float32_t imag = fft_output[2 * i + 1];
            psd[i] += (real * real + imag * imag);
        }
        
        num_segments++;
    }
    
    // Average the PSD and normalize
    if (num_segments > 0) {
        //LOG_INF("Number of segments: %d", num_segments);
        float32_t scale = 1.0f / (num_segments * fs * fft_size);
        arm_scale_f32(psd, scale, psd, fft_size);
    }
}

/* Integrate power in frequency band using trapezoidal rule */

static float32_t integrate_band_power(float32_t *psd, uint32_t fft_size,float32_t fs, float32_t f_low, float32_t f_high)
 {
    float32_t df = fs / fft_size;
    uint32_t idx_low = (uint32_t)(f_low / df);
    uint32_t idx_high = (uint32_t)(f_high / df);
    
    // Clamp indices
    if (idx_high >= fft_size / 2) idx_high = fft_size / 2 - 1;
    if (idx_low > idx_high) return 0.0f;
    
    // Trapezoidal integration
    float32_t power = 0.0f;
    for (uint32_t i = idx_low; i < idx_high; i++) {
        power += (psd[i] + psd[i + 1]) * 0.5f * df;
    }
    
    // Convert from s^2 to ms^2
    power *= 1000000.0f;
    
    return power;
}

void hpi_hrv_frequency_compact_update_spectrum(uint16_t *rr_intervals, int num_intervals)
 {

    LOG_INF("Updating HRV Frequency Compact Spectrum with %d intervals", num_intervals);

      tm_metrics.mean = hrv_calculate_mean();
      tm_metrics.sdnn = hrv_calculate_sdnn();
      tm_metrics.rmssd = hrv_calculate_rmssd();
      tm_metrics.pnn50 = hrv_calculate_pnn50();    
      tm_metrics.hrv_min = (float)hrv_calculate_min();
      tm_metrics.hrv_max = (float)hrv_calculate_max();

      LOG_INF("HRV 30 interval Collection Complete: \nSamples=%d\nMean=%.1f\nSDNN=%.1f\n",num_intervals,tm_metrics.mean, tm_metrics.sdnn);
      LOG_INF("RMSSD : %.1f\nPnn50: %.1f\nMAX : %.1f\nMIN : %.1f", tm_metrics.rmssd, tm_metrics.pnn50, tm_metrics.hrv_max, tm_metrics.hrv_min);

    // Interpolate RR intervals

    uint32_t num_interp_samples = interpolate_rr_intervals(rr_intervals, num_intervals,INTERP_FS,rr_time,rr_values,
        interp_signal,FFT_SIZE * 4);

    // Remove mean
    remove_mean(interp_signal, num_interp_samples);

     // Create Hanning window
    create_hanning_window(window, FFT_SIZE);

    // Calculate PSD using Welch's method
    calculate_psd_welch(interp_signal, num_interp_samples, window, fft_input, fft_output, psd, FFT_SIZE, INTERP_FS);

    // Integrate power in LF, HF bands    
    lf_power_compact = integrate_band_power(psd, FFT_SIZE, INTERP_FS, LF_LOW, LF_HIGH);
    hf_power_compact = integrate_band_power(psd, FFT_SIZE, INTERP_FS,HF_LOW, HF_HIGH);
    stress_score_compact = get_stress_percentage(lf_power_compact, hf_power_compact);
    sdnn_val = tm_metrics.sdnn;
    rmssd_val = tm_metrics.rmssd; 

    float ratio = lf_power_compact / hf_power_compact;//hpi_get_lf_hf_ratio();
    if (ratio > 0.0f) 
    {
        int64_t now_ts = hw_get_sys_time_ts();
        hpi_sys_get_last_hrv_update((uint16_t)(ratio * 100), now_ts);
    }

    LOG_INF("LF Power (Compact): %f", lf_power_compact);
    LOG_INF("HF Power (Compact): %f", hf_power_compact);
    LOG_INF("LF/HF Ratio (Compact): %f", lf_power_compact/hf_power_compact);
    LOG_INF("Stress Score (Compact): %f", stress_score_compact);
    LOG_INF("SDNN : %f", sdnn_val);
    LOG_INF("RMSSD : %f", rmssd_val);

    
 }