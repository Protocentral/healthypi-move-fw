 #include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <lvgl.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "hpi_common_types.h"
#include "ui/move_ui.h"
#include "arm_math.h"
#include "arm_const_structs.h"
#include <string.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(hpi_disp_scr_hrv_frequency_compact, LOG_LEVEL_DBG);

#define MAX_RR_INTERVALS 30        // Maximum RR intervals to process
#define INTERP_FS 4.0f             // Interpolation sampling frequency (Hz)
#define FFT_SIZE 64                // Must be power of 2
#define WELCH_OVERLAP 0.5f         // 50% overlap for Welch method

// Frequency band definitions (Hz)
#define LF_LOW   0.04f
#define LF_HIGH  0.15f
#define HF_LOW   0.15f
#define HF_HIGH  0.4f

// Required buffer sizes to process LF and HF power

float32_t rr_time[MAX_RR_INTERVALS + 1]; // Time taken to collect samples (cumulative time processed from RR intervals)
float32_t rr_values[MAX_RR_INTERVALS + 1]; // RR intervals in seconds
float32_t interp_signal[FFT_SIZE * 4];  // Larger buffer for interpolated signal
float32_t fft_input[FFT_SIZE * 2];      // Complex FFT input
float32_t fft_output[FFT_SIZE * 2];     // Complex FFT output
float32_t psd[FFT_SIZE];                // Power spectral density
float32_t window[FFT_SIZE];             // Hanning window


// GUI Screen object
lv_obj_t *scr_hrv_frequency_compact;

// GUI Labels - minimal set
static lv_obj_t *label_lf_hf_ratio_compact;
static lv_obj_t *label_stress;
static lv_obj_t *label_stress_level_compact;
static lv_obj_t *arc_stress_gauge;
static lv_obj_t *label_sdnn;
static lv_obj_t *label_rmssd;
// Externs
extern lv_style_t style_white_large;
extern lv_style_t style_white_medium;
extern lv_style_t style_white_small;
extern lv_style_t style_scr_black;

// Static variables for HRV frequency analysis
static float lf_power_compact = 0.0f;
static float hf_power_compact = 0.0f;
static float stress_score_compact = 0.0f;
static float sdnn_val = 0.0f;
static float rmssd_val = 0.0f;

static void lvgl_update_cb(void *user_data)
{
    hpi_hrv_frequency_compact_update_display();
}

// Simplified stress assessment for compact display
static int get_stress_percentage(float lf, float hf) {
    if (hf <= 0) return 100;
    float ratio = lf / hf;
    int stress_pct = (int)((ratio / 4.0f) * 100);
    return stress_pct > 100 ? 100 : stress_pct;
}

static lv_color_t get_stress_arc_color(int stress_percentage) {
    if (stress_percentage < 25) {
        return lv_palette_main(LV_PALETTE_GREEN);
    } else if (stress_percentage < 50) {
        return lv_palette_main(LV_PALETTE_YELLOW);
    } else if (stress_percentage < 75) {
        return lv_palette_main(LV_PALETTE_ORANGE);
    } else {
        return lv_palette_main(LV_PALETTE_RED);
    }
}

void gesture_handler(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_BOTTOM) {
        gesture_down_scr_spl_hrv();
    }
}

void gesture_down_scr_spl_hrv(void)
{
    printk("Exit HRV Frequency Compact\n");
    hpi_load_screen(SCR_HRV_SUMMARY, SCROLL_DOWN);
}

void draw_scr_hrv_frequency_compact(enum scroll_dir m_scroll_dir, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{

        scr_hrv_frequency_compact = lv_obj_create(NULL);
        lv_obj_clear_flag(scr_hrv_frequency_compact, LV_OBJ_FLAG_SCROLLABLE);
        draw_scr_common(scr_hrv_frequency_compact);

        lv_obj_t *cont_main = lv_obj_create(scr_hrv_frequency_compact);
        lv_obj_set_size(cont_main, 360, 360);
        lv_obj_center(cont_main);
        lv_obj_add_style(cont_main, &style_scr_black, 0);
        lv_obj_set_style_pad_all(cont_main, 10, LV_PART_MAIN);
        lv_obj_set_style_border_width(cont_main, 0, LV_PART_MAIN);

        // Title
        lv_obj_t *label_title = lv_label_create(cont_main);
        lv_label_set_text(label_title, "HRV Frequency");
        lv_obj_add_style(label_title, &style_white_medium, 0);
        lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 5);

        // --- Row container for LF & HF ---
        lv_obj_t *cont_top = lv_obj_create(cont_main);
        lv_obj_set_size(cont_top, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_add_style(cont_top, &style_scr_black, 0);
        lv_obj_set_style_bg_opa(cont_top, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(cont_top, 0, LV_PART_MAIN);
        lv_obj_set_flex_flow(cont_top, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(cont_top, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_align(cont_top, LV_ALIGN_TOP_MID, 0, 40);

        // LF Power label
        label_sdnn = lv_label_create(cont_top);
        lv_label_set_text(label_sdnn, "SDNN: 0.00");
        lv_obj_set_style_text_color(label_sdnn, lv_color_hex(0xFF7070), 0); 
        lv_obj_add_style(label_sdnn, &style_white_small, 0);

        // HF Power label
        label_rmssd = lv_label_create(cont_top);
        lv_label_set_text(label_rmssd, "RMSSD: 0.00");
        lv_obj_set_style_text_color(label_rmssd, lv_color_hex(0x70A0FF), 0);  
        lv_obj_add_style(label_rmssd, &style_white_small, 0);

        // --- Stress gauge ---
        arc_stress_gauge = lv_arc_create(cont_main);
        lv_obj_set_size(arc_stress_gauge, 170, 170);
        lv_arc_set_rotation(arc_stress_gauge, 135);
        lv_arc_set_bg_angles(arc_stress_gauge, 0, 270);
        lv_arc_set_value(arc_stress_gauge, 0);
        lv_obj_clear_flag(arc_stress_gauge, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_style(arc_stress_gauge, NULL, LV_PART_KNOB);
        lv_obj_align(arc_stress_gauge, LV_ALIGN_CENTER, 0, 10);

        // Stress label inside arc
        label_stress_level_compact = lv_label_create(cont_main);
        lv_label_set_text(label_stress_level_compact, "Low");
        lv_obj_add_style(label_stress_level_compact, &style_white_medium, 0);
        lv_obj_align_to(label_stress_level_compact, arc_stress_gauge, LV_ALIGN_CENTER, 0, 0);

        // --- Bottom metrics (LF/HF + Stress %) ---
        lv_obj_t *cont_bottom = lv_obj_create(cont_main);
        lv_obj_set_size(cont_bottom, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_add_style(cont_bottom, &style_scr_black, 0);
        lv_obj_set_style_bg_opa(cont_bottom, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(cont_bottom, 0, LV_PART_MAIN);
        lv_obj_set_flex_flow(cont_bottom, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(cont_bottom, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_align(cont_bottom, LV_ALIGN_BOTTOM_MID, 0, -10);

        // label_stress = lv_obj_create(cont_bottom);
        // lv_label_set_text(label_stress, "Stress");
        // lv_obj_set_style_text_color(label_stress, lv_color_hex(0x8000FF), 0);
        //  lv_obj_add_style(label_stress, &style_white_small, 0);

        label_lf_hf_ratio_compact = lv_label_create(cont_bottom);
        lv_label_set_text(label_lf_hf_ratio_compact, "LF/HF: 0.00");
        lv_obj_set_style_text_color(label_lf_hf_ratio_compact, lv_color_hex(0xC080FF), 0);  
        lv_obj_add_style(label_lf_hf_ratio_compact, &style_white_small, 0);

        // Gesture handler
        lv_obj_add_event_cb(scr_hrv_frequency_compact, gesture_handler, LV_EVENT_GESTURE, NULL);
        hpi_disp_set_curr_screen(SCR_SPL_HRV_FREQUENCY);
        hpi_show_screen(scr_hrv_frequency_compact, m_scroll_dir);
        lv_async_call(lvgl_update_cb, NULL);
}


void hpi_hrv_frequency_compact_update_display(void)
{

    float ratio = 0.0f;
   
    ratio = lf_power_compact / hf_power_compact;

    int ratio_int = (int)ratio;
    int ratio_dec = (int)((ratio - ratio_int) * 100);

    int sdnn_int = (int)sdnn_val;
    int sdnn_dec = (int)((sdnn_val - sdnn_int) * 100);

    int rmssd_int = (int)rmssd_val;
    int rmssd_dec = (int)((rmssd_val - rmssd_int) * 100);

    if(label_sdnn)
        lv_label_set_text_fmt(label_sdnn,"SDNN: %d.%02d",sdnn_int, abs(sdnn_dec));
    
    if(label_rmssd)
        lv_label_set_text_fmt(label_rmssd,"RMSSD: %d.%02d", rmssd_int, abs(rmssd_dec));

    if (label_lf_hf_ratio_compact)
        lv_label_set_text_fmt(label_lf_hf_ratio_compact, "LF/HF: %d.%02d", ratio_int, abs(ratio_dec));

    
    // Update stress arc gauge
    if (arc_stress_gauge != NULL) {
        lv_arc_set_value(arc_stress_gauge, (int)stress_score_compact);
        lv_obj_set_style_arc_color(arc_stress_gauge, get_stress_arc_color((int)stress_score_compact), LV_PART_INDICATOR);
    }
    

    // Update stress label with very short text
    if (label_stress_level_compact != NULL) {
        const char* stress_text;
        if (stress_score_compact < 25) {
            stress_text = "Low";
        } else if (stress_score_compact < 50) {
            stress_text = "Med";
        } else if (stress_score_compact < 75) {
            stress_text = "High";
        } else {
            stress_text = "Max";
        }
        
        lv_label_set_text(label_stress_level_compact, stress_text);
        lv_obj_set_style_text_color(label_stress_level_compact, get_stress_arc_color((int)stress_score_compact), 0);
    }
    
   
}
float hpi_get_lf_hf_ratio(void) {
   

    return (hf_power_compact == 0.0f) ? 0.0f : lf_power_compact / hf_power_compact;

}


 /* Linear interpolation for RR intervals */
 
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

 void hpi_hrv_frequency_compact_update_spectrum(uint16_t *rr_intervals, int num_intervals, float sdnn, float rmssd)
 {
   
    LOG_INF("Updating HRV Frequency Compact Spectrum with %d intervals", num_intervals);

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
    sdnn_val = sdnn;
    rmssd_val = rmssd; 

    LOG_INF("LF Power (Compact): %f", lf_power_compact);
    LOG_INF("HF Power (Compact): %f", hf_power_compact);
    LOG_INF("LF/HF Ratio (Compact): %f", lf_power_compact/hf_power_compact);
    LOG_INF("Stress Score (Compact): %f", stress_score_compact);
    LOG_INF("SDNN : %f", sdnn_val);
    LOG_INF("RMSSD : %f", rmssd_val);

    
 }