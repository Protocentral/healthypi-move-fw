#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <math.h>
#include <string.h>
#include "hrv_algos.h"
#include "ui/move_ui.h"
#include "log_module.h"
#include "hpi_sys.h"
#include "arm_math.h"
#include "arm_const_structs.h"
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(hrv_algos, LOG_LEVEL_DBG);

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

static time_domain tm_metrics = {
    .mean = 0,
    .rmssd = 0,
    .hrv_max = 0,
    .hrv_min = 0,
    .pnn50 = 0,
    .sdnn = 0
};


/* Calculate mean RR interval with caching */

float hrv_calculate_mean(uint16_t *rr_buffer, int count)
{
    float sum = 0;
    for( int i = 0; i < count ; i++)
    {
          sum += rr_buffer[i];
    }
    return (float)sum / count;
    
}

/* Calculate SDNN (Standard Deviation of NN intervals) */

float hrv_calculate_sdnn(uint16_t * rr_buffer, int count)
{ 
    if (count < 2) return 0.0f;
    float mean = hrv_calculate_mean(rr_buffer, count);
    float sum_squared_diff = 0.0f;
    for (int i = 0; i < count; i++) {
        float diff = (float)rr_buffer[i] - mean;
        sum_squared_diff += diff * diff;
    }
    return sqrtf(sum_squared_diff / (count - 1));
}

/* Calculate RMSSD (Root Mean Square of Successive Differences) */

float hrv_calculate_rmssd(uint16_t * rr_buffer, int count)
{
    if (count < 2) return 0.0f;
    float sum_squared_diff = 0.0f;
    for (int i = 0; i < count - 1; i++) {
        float diff = rr_buffer[i + 1] - rr_buffer[i];
        sum_squared_diff += diff * diff;
    }
    return sqrtf(sum_squared_diff / (count - 1));
}

/* Calculate pNN50 (percentage of successive RR intervals that differ by more than 50ms) */

float hrv_calculate_pnn50(uint16_t * rr_buffer, int count)
{  
    if (count < 2) return 0.0f;
    int count_over_50ms = 0;
    for (int i = 0; i < count - 1; i++) {
        if (abs((int32_t)rr_buffer[i + 1] - (int32_t)rr_buffer[i]) > 50) {
            count_over_50ms++;
        }
    }
    return ((float)count_over_50ms * 100) / (count - 1);
}

/* Find minimum RR interval in the buffer */

uint32_t hrv_calculate_min(uint16_t * rr_buffer, int count)
{
     if (count == 0) return 0;
    uint32_t min_val = rr_buffer[0];
    for (int i = 1; i < count; i++)
    {
        if (rr_buffer[i] < min_val) 
            min_val = rr_buffer[i];
    }
    return min_val;
}

/* Find maximum RR interval in the buffer */

uint32_t hrv_calculate_max(uint16_t * rr_buffer, int count)
{
    if (count == 0) return 0;
    uint32_t max_val = rr_buffer[0];
    for (int i = 1; i < count; i++)
    {
        if (rr_buffer[i] > max_val) 
             max_val = rr_buffer[i];
    }
    return max_val;
}

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
    rr_values[0] = rr_ms[0] / 1000.0f;  // First interval at t=0
    
    for (uint32_t i = 1; i < num_intervals; i++) { 
        rr_values[i] = rr_ms[i] / 1000.0f;
        rr_time[i] = rr_time[i-1] + rr_values[i-1];    
    }

    // Calculate number of interpolated samples
    rr_time[num_intervals] = rr_time[num_intervals-1] + rr_values[num_intervals-1];
    float32_t total_time = rr_time[num_intervals];
   
    LOG_INF("Total RR time: %.2f seconds", total_time);
    uint32_t num_samples = (uint32_t)(total_time * fs);
    
    if (num_samples > max_interp_samples) {
        num_samples = max_interp_samples;
    }

    LOG_INF("Number of interpolated samples: %d", num_samples);
    
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
         window[i] = 0.5f * (1.0f - arm_cos_f32(2.0f * PI * i / (size - 1)));
    }
}

/* Remove mean from signal using CMSIS-DSP */
static void remove_mean(float32_t *signal, uint32_t length)
 {
        
    if (length == 0) return;
    
    float32_t sum = 0.0f;
    
    // Calculate initial mean
    for (uint32_t i = 0; i < length; i++) {
        sum += signal[i];
    }
    
    float32_t initial_mean = sum / length;

    // Remove mean
    for (uint32_t i = 0; i < length; i++) {
        signal[i] -= initial_mean;
    }
    
    // Verify mean is now zero
    sum = 0.0f;
    for (uint32_t i = 0; i < length; i++) {
        sum += signal[i];
    }
    
    float32_t final_mean = sum / length;
    LOG_DBG("Final mean after removal: %.10f s (should be ~0)", final_mean);  

}

/* Calculate PSD using Welch's method with CMSIS-DSP FFT */

static void calculate_psd_welch(float32_t *signal, uint32_t signal_len,float32_t *window, float32_t *fft_input,float32_t *fft_output, 
    float32_t *psd,uint32_t fft_size, float32_t fs) 
{

    if (signal_len < fft_size) {
      LOG_WRN("signal_len (%u) < fft_size (%u) -> no PSD", signal_len, fft_size);
      return;
    }
    // Initialize PSD to zero
    memset(psd, 0, sizeof(float32_t) * fft_size);
    
     float32_t window_power = 0.0f;
    for (uint32_t i = 0; i < fft_size; i++) {
        window_power += window[i] * window[i];
    }
     LOG_DBG("Window power: %.3f", window_power);
    // Calculate step size for 50% overlap
    uint32_t step = fft_size / 2;
    uint32_t num_segments = 0;
    
    // FFT instance (use appropriate size from arm_const_structs.h)
    const arm_cfft_instance_f32 *fft_instance;
    
    // Select appropriate FFT instance based on size
    switch(fft_size) {
        case 64 : fft_instance = &arm_cfft_sR_f32_len64; break;
        case 128: fft_instance = &arm_cfft_sR_f32_len128; break;
        case 256:  fft_instance = &arm_cfft_sR_f32_len256; break;
        case 512:  fft_instance = &arm_cfft_sR_f32_len512; break;
        case 1024: fft_instance = &arm_cfft_sR_f32_len1024; break;
        default:   return; // Unsupported FFT size
    }
    
    // Process overlapping segments
   // for (uint32_t start = 0; start + fft_size <= signal_len; start += step) 
   for (uint32_t start = 0; start <= signal_len - fft_size; start += step) 
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
        for (uint32_t i = 0; i < fft_size; i++) 
        {
            float32_t real = fft_output[2 * i];
            float32_t imag = fft_output[2 * i + 1];
            psd[i] += (real * real + imag * imag);
        }
        
        num_segments++;
    }
    LOG_INF("Processed %d segments (signal_len=%u, fft_size=%u, step=%u)", 
            num_segments, signal_len, fft_size, step);

    if (num_segments == 0) {
      LOG_WRN("No PSD segments processed (signal_len=%u, fft_size=%u)", signal_len, fft_size);
      return;
      }
    // Average the PSD and normalize
     LOG_INF("Number of segments: %d", num_segments);
     float32_t scale = 1.0f / (num_segments * window_power * fs );
     LOG_DBG("Scaling factor: %.6f (window_power=%.3f, fs=%.1f, N=%d)", scale, window_power, fs, num_segments);
     arm_scale_f32(psd, scale, psd, fft_size);

}

/* Integrate power in frequency band using trapezoidal rule */

static float32_t integrate_band_power(float32_t *psd, uint32_t fft_size,float32_t fs, float32_t f_low, float32_t f_high)
 {
    float32_t df = fs / fft_size;
    uint32_t idx_low = (uint32_t)roundf(f_low / df);
    uint32_t idx_high = (uint32_t)roundf(f_high / df);
    
    // Clamp indices
    if (idx_high >= fft_size / 2) idx_high = fft_size / 2 - 1;
    if (idx_low > idx_high) return 0.0f;
    
    // Trapezoidal integration
    float32_t power = 0.0f;

   // LOG_DBG("---- Per-bin power (%.2f-%.2f Hz) ----", f_low, f_high);
    for (uint32_t i = idx_low; i <= idx_high; i++) {
        float32_t power_bin_s2 = psd[i] * df;            // in s^2
      //  LOG_DBG("Bin %3d | Freq = %.3f Hz | PSD = %.6f", i, (double)(i * df), (double)psd[i]);
    }
    
    for (uint32_t i = idx_low; i < idx_high; i++) {
      power += (psd[i] + psd[i + 1]) * 0.5f * df;
    }
    
    // Convert from s^2 to ms^2
    power *= 1000000.0f;

   // LOG_DBG("Integrated power from %.2f Hz to %.2f Hz: %.3f ms^2", (double)f_low, (double)f_high, (double)power);
    
    return power;
}

void hpi_hrv_frequency_compact_update_spectrum(uint16_t *rr_intervals, int num_intervals)
 {

      tm_metrics.mean = hrv_calculate_mean(rr_intervals, num_intervals);
      tm_metrics.sdnn = hrv_calculate_sdnn(rr_intervals, num_intervals);
      tm_metrics.rmssd = hrv_calculate_rmssd(rr_intervals, num_intervals);
      tm_metrics.pnn50 = hrv_calculate_pnn50(rr_intervals, num_intervals);    
      tm_metrics.hrv_min = (float)hrv_calculate_min(rr_intervals, num_intervals);
      tm_metrics.hrv_max = (float)hrv_calculate_max(rr_intervals, num_intervals);


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
    lf_power_compact = integrate_band_power(psd,FFT_SIZE, INTERP_FS, LF_LOW, LF_HIGH);
    hf_power_compact = integrate_band_power(psd, FFT_SIZE, INTERP_FS,HF_LOW, HF_HIGH);
    stress_score_compact = get_stress_percentage(lf_power_compact, hf_power_compact);
    sdnn_val = tm_metrics.sdnn;
    rmssd_val = tm_metrics.rmssd; 

    float ratio = hpi_get_lf_hf_ratio();
    if (ratio > 0.0f)
    {
        int64_t now_ts = hw_get_sys_time_ts();
        // Convert to storage format: ratio*100, sdnn*10, rmssd*10
        uint16_t lf_hf_x100 = (uint16_t)(ratio * 100);
        uint16_t sdnn_x10 = (uint16_t)(sdnn_val * 10);
        uint16_t rmssd_x10 = (uint16_t)(rmssd_val * 10);
        LOG_INF("Saving HRV to settings: LF/HF=%u (ratio=%.2f), SDNN=%u, RMSSD=%u, ts=%lld",
                lf_hf_x100, ratio, sdnn_x10, rmssd_x10, now_ts);
        hpi_sys_set_last_hrv_update(lf_hf_x100, sdnn_x10, rmssd_x10, now_ts);
    }
    else
    {
        LOG_WRN("HRV ratio is zero or negative (%.2f), not saving to settings", ratio);
    }


    LOG_INF("HRV interval Collection Completed: \nSamples : %d\nMean : %.1f\nSDNN : %.1f\nRMSSD : %.1f\nPnn50 : %.1f\nMIN : %.1f ms - %.1f bpm\nMAX : %.1f ms - %.1f bpm",
             num_intervals,tm_metrics.mean, sdnn_val,rmssd_val, tm_metrics.pnn50, 
             tm_metrics.hrv_max, (60000/tm_metrics.hrv_max), tm_metrics.hrv_min, (60000/tm_metrics.hrv_min));

    LOG_INF("LF Power (Compact): %f", lf_power_compact);
    LOG_INF("HF Power (Compact): %f", hf_power_compact);
    LOG_INF("LF/HF Ratio (Compact): %f", lf_power_compact / hf_power_compact);
    LOG_INF("Stress Score (Compact): %f", stress_score_compact);
   
 }
 float hpi_get_lf_hf_ratio(void) {
   
    return (hf_power_compact == 0.0f) ? 0.0f : lf_power_compact / hf_power_compact;
}