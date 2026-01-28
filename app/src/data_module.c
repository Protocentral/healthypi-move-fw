/*
 * HealthyPi Move
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Author: Ashwin Whitchurch, Protocentral Electronics
 * Contact: ashwin@protocentral.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <stdio.h>
#include <math.h>
#include <arm_math.h>

#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <time.h>

LOG_MODULE_REGISTER(data_module, LOG_LEVEL_DBG);

#include "max30001.h"

#include "hw_module.h"
#include "hpi_common_types.h"
#include "fs_module.h"
#include "ble_module.h"
#include "hrv_algos.h"
#include "ui/move_ui.h"
#include "hpi_sys.h"

#include "log_module.h"
#include "recording_module.h"
#include "gsr_algos.h"

#if defined(CONFIG_HPI_GSR_STRESS_INDEX)
ZBUS_CHAN_DECLARE(gsr_stress_chan);
#endif

// ProtoCentral data formats
#define CES_CMDIF_PKT_START_1 0x0A
#define CES_CMDIF_PKT_START_2 0xFA
#define CES_CMDIF_TYPE_DATA 0x02
#define CES_CMDIF_PKT_STOP 0x0B
#define DATA_LEN 22

#define LOG_SAMPLE_RATE_SPS 125
#define SAMPLE_BUFF_WATERMARK 8

char DataPacket[DATA_LEN];
const char DataPacketFooter[2] = {0, CES_CMDIF_PKT_STOP};
const char DataPacketHeader[5] = {CES_CMDIF_PKT_START_1, CES_CMDIF_PKT_START_2, DATA_LEN, 0, CES_CMDIF_TYPE_DATA};

extern const struct device *const max30001_dev;
extern const struct device *const max32664d_dev;

static bool settings_send_usb_enabled = false;
static bool settings_send_ble_enabled = true;
static bool settings_plot_enabled = true;

extern struct k_sem sem_hrv_eval_complete;

enum hpi5_data_format
{
    DATA_FMT_OPENVIEW,
    DATA_FMT_PLAIN_TEXT,
} hpi5_data_format_t;

static bool settings_log_data_enabled = true; // true;
static int settings_data_format = DATA_FMT_OPENVIEW;

// struct hpi_ecg_bioz_sensor_data_t log_buffer[LOG_BUFFER_LENGTH];

uint16_t current_session_log_counter = 0;
uint16_t current_session_log_id = 0;
char session_id_str[5];

static bool is_ecg_record_active = false;

static int32_t ecg_record_buffer[ECG_RECORD_BUFFER_SAMPLES]; // 128 Hz * 30 seconds = 3840 samples (15.36KB)
static volatile uint16_t ecg_record_counter = 0;
K_MUTEX_DEFINE(mutex_is_ecg_record_active);

static bool is_gsr_record_active = false;
static int32_t gsr_record_buffer[GSR_RECORD_BUFFER_SAMPLES]; // e.g., 32Hz * 30s = 960 samples
static volatile uint16_t gsr_record_counter = 0;
K_MUTEX_DEFINE(mutex_is_gsr_record_active);

static int g_last_scr_count = 0;
K_MUTEX_DEFINE(mutex_is_hrv_record_active);
static uint16_t rr_buffer[HRV_MAX_INTERVALS];

static bool is_gsr_measurement_active = false;

// HRV (Heart Rate Variability) Evaluation State - Separate from ECG recording
static bool is_hrv_eval_active = false;
static struct hpi_hrv_eval_result_t hrv_eval_result = {0};
struct hpi_hrv_interval_t hrv_intervals[HRV_MAX_INTERVALS];  // R-to-R interval buffer (shared with state machine)
volatile uint16_t hrv_interval_count = 0;                     // Number of intervals collected (shared with state machine)
K_MUTEX_DEFINE(mutex_is_hrv_eval_active);

// Track last R-to-R interval to detect new beats
static uint16_t last_rtor_value = 0;
K_MUTEX_DEFINE(mutex_is_gsr_measurement_active);

static uint32_t last_hr_update_time = 0;

K_MUTEX_DEFINE(mutex_hr_change);

// Externs

ZBUS_CHAN_DECLARE(hr_chan);

ZBUS_CHAN_DECLARE(ecg_stat_chan);

extern struct k_msgq q_ecg_sample;
extern struct k_msgq q_bioz_sample;
extern struct k_msgq q_ppg_wrist_sample;
extern struct k_msgq q_ppg_fi_sample;

extern struct k_msgq q_plot_ecg;
extern struct k_msgq q_plot_ppg_wrist;
extern struct k_msgq q_plot_ppg_fi;
extern struct k_msgq q_plot_hrv;
extern struct k_msgq q_plot_gsr;
extern struct k_sem sem_ecg_complete;

void sendData(int32_t ecg_sample, int32_t bioz_sample, uint32_t raw_red, uint32_t raw_ir, int32_t temp, uint8_t hr,
              uint8_t bpt_status, uint8_t spo2, bool _bioZSkipSample)
{
    DataPacket[0] = ecg_sample;
    DataPacket[1] = ecg_sample >> 8;
    DataPacket[2] = ecg_sample >> 16;
    DataPacket[3] = ecg_sample >> 24;

    DataPacket[4] = bioz_sample;
    DataPacket[5] = bioz_sample >> 8;
    DataPacket[6] = bioz_sample >> 16;
    DataPacket[7] = bioz_sample >> 24;

    if (_bioZSkipSample == false)
    {
        DataPacket[8] = 0x00;
    }
    else
    {
        DataPacket[8] = 0xFF;
    }

    DataPacket[9] = raw_red;
    DataPacket[10] = raw_red >> 8;
    DataPacket[11] = raw_red >> 16;
    DataPacket[12] = raw_red >> 24;

    DataPacket[13] = raw_ir;
    DataPacket[14] = raw_ir >> 8;
    DataPacket[15] = raw_ir >> 16;
    DataPacket[16] = raw_ir >> 24;

    DataPacket[17] = temp;
    DataPacket[18] = temp >> 8;

    DataPacket[19] = spo2;
    DataPacket[20] = hr;
    DataPacket[21] = bpt_status;

    if (settings_send_usb_enabled)
    {
        send_usb_cdc(DataPacketHeader, 5);
        send_usb_cdc(DataPacket, DATA_LEN);
        send_usb_cdc(DataPacketFooter, 2);
    }
}

static int hpi_get_trend_stats(uint16_t *in_array, uint16_t in_array_len, uint16_t *out_max, uint16_t *out_min, uint16_t *out_mean)
{
    if (in_array_len == 0)
    {
        return -1;
    }

    uint16_t max = in_array[0];
    uint16_t min = in_array[0];
    uint32_t sum = 0;

    for (int i = 0; i < in_array_len; i++)
    {
        if (in_array[i] > max)
        {
            max = in_array[i];
        }
        if (in_array[i] < min)
        {
            min = in_array[i];
        }
        sum += in_array[i];
    }

    *out_max = max;
    *out_min = min;
    *out_mean = sum / in_array_len;

    return 0;
}

void send_data_text(int32_t ecg_sample, int32_t bioz_sample, int32_t raw_red)
{
    char data[100];
    double f_ecg_sample = (double)ecg_sample / 1000;
    double f_bioz_sample = (double)bioz_sample / 1000;
    double f_raw_red = (double)raw_red / 1000;

    sprintf(data, "%.3f\t%.3f\t%.3f\r\n", f_ecg_sample, f_bioz_sample, f_raw_red);

    if (settings_send_usb_enabled)
    {
        send_usb_cdc(data, strlen(data));
    }
}

void send_data_text_1(int32_t in_sample)
{
    char data[100];
    float f_in_sample = (float)in_sample / 1000;

    sprintf(data, "%.3f\r\n", f_in_sample);
    send_usb_cdc(data, strlen(data));
}

void hpi_data_hrv_record_to_file(bool active)
{
    k_mutex_lock(&mutex_is_hrv_record_active, K_FOREVER);

        if(hrv_interval_count > 0)
        {
            if (hrv_interval_count > HRV_MAX_INTERVALS)
            {
                LOG_ERR("HRV counter overflow detected: %d > %d - clamping to max",
                        hrv_interval_count, HRV_MAX_INTERVALS);
                hrv_interval_count = HRV_MAX_INTERVALS;
            }

            struct tm tm_sys_time = hpi_sys_get_sys_time();
            int64_t log_time = timeutil_timegm64(&tm_sys_time);

            // Copying intervals to rr_buffer
            for (int i = 0; i < hrv_interval_count; i++) {
                    rr_buffer[i] = hrv_intervals[i].rtor_ms;
            }
            
            // Calling calculation function
             hpi_hrv_frequency_compact_update_spectrum(rr_buffer, hrv_interval_count);

             LOG_INF("HRV recording stopped - writing %d samples to file ",hrv_interval_count);

             hpi_write_hrv_record_file(rr_buffer,hrv_interval_count, log_time);
    
             LOG_INF("HRV file write completed");

           //  k_sem_give(&sem_hrv_eval_complete);
        }
    
    
         k_mutex_unlock(&mutex_is_hrv_record_active);
    

}
void hpi_data_set_ecg_record_active(bool active)
{
    k_mutex_lock(&mutex_is_ecg_record_active, K_FOREVER);
    is_ecg_record_active = active;

    if (active)
    {
        // Starting new recording - reset buffer and counter
        ecg_record_counter = 0;
        memset(ecg_record_buffer, 0, sizeof(ecg_record_buffer));
        LOG_INF("ECG recording started - buffer reset");
    }
    else
    {
        // Stopping recording - write file SYNCHRONOUSLY with mutex held
        // This prevents race condition where new recording could start before write completes
        if (ecg_record_counter > 0)
        {
            // Validate counter is within bounds before writing
            if (ecg_record_counter > ECG_RECORD_BUFFER_SAMPLES) {
                LOG_ERR("ECG counter overflow detected: %d > %d - clamping to max",
                        ecg_record_counter, ECG_RECORD_BUFFER_SAMPLES);
                ecg_record_counter = ECG_RECORD_BUFFER_SAMPLES;
            }
            
            struct tm tm_sys_time = hpi_sys_get_sys_time();
            int64_t log_time = timeutil_timegm64(&tm_sys_time);
            
            LOG_INF("ECG recording stopped - writing %d samples to file (%.1f seconds @ 128Hz)", 
                    ecg_record_counter, (float)ecg_record_counter / 128.0f);
            
            // Write actual collected samples, not full buffer size
            hpi_write_ecg_record_file(ecg_record_buffer, ecg_record_counter, log_time);
            
            LOG_INF("ECG file write completed");

        }
        else
        {
            LOG_WRN("ECG recording stopped but no samples collected");
        }
    }
    k_mutex_unlock(&mutex_is_ecg_record_active);
}

void hpi_data_set_gsr_record_active(bool active)
{
    k_mutex_lock(&mutex_is_gsr_record_active, K_FOREVER);
    is_gsr_record_active = active;

    if (active)
    {
        // Starting new recording
        gsr_record_counter = 0;
        memset(gsr_record_buffer, 0, sizeof(gsr_record_buffer));
        LOG_INF("GSR recording started - buffer reset");
    }
    else
    {
        // Stopping recording - write file synchronously
        if (gsr_record_counter > 0)
        {
            if (gsr_record_counter > GSR_RECORD_BUFFER_SAMPLES) {
                LOG_ERR("GSR counter overflow detected: %d > %d - clamping to max",
                        gsr_record_counter, GSR_RECORD_BUFFER_SAMPLES);
                gsr_record_counter = GSR_RECORD_BUFFER_SAMPLES;
            }

            struct tm tm_sys_time = hpi_sys_get_sys_time();
            int64_t log_time = timeutil_timegm64(&tm_sys_time);

            LOG_INF("GSR recording stopped - writing %d samples to file (%.1f seconds @ 32Hz)",
                    gsr_record_counter, (float)gsr_record_counter / 32.0f);

            hpi_write_gsr_record_file(gsr_record_buffer, gsr_record_counter, log_time);
            LOG_INF("GSR file write completed");

            if (!is_gsr_record_active && gsr_record_counter > 0)
            {
                // Calculate duration in seconds
                int duration_sec = gsr_record_counter / 32;  // 32 Hz sample rate
                if (duration_sec < 1) {
                    duration_sec = 1;
                }

#if defined(CONFIG_HPI_GSR_STRESS_INDEX)
                // Calculate comprehensive stress index from buffered samples
                static struct hpi_gsr_stress_index_t stress_data = {0};
                calculate_gsr_stress_index(gsr_record_buffer, gsr_record_counter,
                                           duration_sec, &stress_data);

                if (stress_data.stress_data_ready) {
                    // Publish stress data via ZBus
                    zbus_chan_pub(&gsr_stress_chan, &stress_data, K_NO_WAIT);
                    LOG_INF("GSR stress published: level=%u, tonic=%u.%02u uS, SCR=%u/min",
                            stress_data.stress_level,
                            stress_data.tonic_level_x100 / 100,
                            stress_data.tonic_level_x100 % 100,
                            stress_data.peaks_per_minute);

                    g_last_scr_count = stress_data.peaks_per_minute;
                    // Store full stress data for persistent display
                    hpi_sys_set_last_gsr_stress(stress_data.stress_level,
                                                stress_data.tonic_level_x100,
                                                stress_data.peaks_per_minute,
                                                log_time);
                }
#else
                int scr_count = calculate_scr_count(gsr_record_buffer, gsr_record_counter);
                LOG_INF("SCR count: %d", scr_count);
                g_last_scr_count = scr_count;
                hpi_sys_set_last_gsr_update(g_last_scr_count, log_time);
#endif
            }
        }
        else
        {
            LOG_WRN("GSR recording stopped but no samples collected");
        }
    }

    k_mutex_unlock(&mutex_is_gsr_record_active);
}

bool hpi_data_is_gsr_record_active(void)
{
    bool active;
    k_mutex_lock(&mutex_is_gsr_record_active, K_FOREVER);
    active = is_gsr_record_active;
    k_mutex_unlock(&mutex_is_gsr_record_active);
    return active;
}

int hpi_data_get_last_scr_count(void)
{
    return g_last_scr_count;
}

void hpi_data_reset_gsr_record_buffer(void)
{
    k_mutex_lock(&mutex_is_gsr_record_active, K_FOREVER);
    // Reset buffer and counter without saving (for contact lost / restart)
    gsr_record_counter = 0;
    memset(gsr_record_buffer, 0, sizeof(gsr_record_buffer));
    LOG_DBG("GSR recording buffer reset");
    k_mutex_unlock(&mutex_is_gsr_record_active);

}

void hpi_data_reset_ecg_record_buffer(void)
{
    k_mutex_lock(&mutex_is_ecg_record_active, K_FOREVER);
    // Reset buffer and counter without saving (for lead-off restart)
    ecg_record_counter = 0;
    memset(ecg_record_buffer, 0, sizeof(ecg_record_buffer));
    LOG_INF("ECG recording buffer reset (discard incomplete data)");
    k_mutex_unlock(&mutex_is_ecg_record_active);
}

bool hpi_data_is_ecg_record_active(void)
{
    bool active;
    k_mutex_lock(&mutex_is_ecg_record_active, K_FOREVER);
    active = is_ecg_record_active;
    k_mutex_unlock(&mutex_is_ecg_record_active);
    return active;
}

void hpi_data_set_gsr_measurement_active(bool active)
{
    k_mutex_lock(&mutex_is_gsr_measurement_active, K_FOREVER);
    is_gsr_measurement_active = active;
    k_mutex_unlock(&mutex_is_gsr_measurement_active);
}

bool hpi_data_is_gsr_measurement_active(void)
{
    bool active = false;
    k_mutex_lock(&mutex_is_gsr_measurement_active, K_FOREVER);
    active = is_gsr_measurement_active;
    k_mutex_unlock(&mutex_is_gsr_measurement_active);
    return active;
}

/**
 * @brief Set HRV evaluation active/inactive state
 * @param active true to start evaluation, false to stop
 */
void hpi_data_set_hrv_eval_active(bool active)
{
    k_mutex_lock(&mutex_is_hrv_eval_active, K_FOREVER);
    is_hrv_eval_active = active;
    
    if (active) {
        // Reset result when starting new evaluation
        memset(&hrv_eval_result, 0, sizeof(hrv_eval_result));
        last_rtor_value = 0;
        LOG_INF("HRV evaluation started - buffer reset");
    }
    
    k_mutex_unlock(&mutex_is_hrv_eval_active);
}

/**
 * @brief Check if HRV evaluation is active
 * @return true if evaluation in progress, false otherwise
 */
bool hpi_data_is_hrv_eval_active(void)
{
    bool active = false;
    k_mutex_lock(&mutex_is_hrv_eval_active, K_FOREVER);
    active = is_hrv_eval_active;
    k_mutex_unlock(&mutex_is_hrv_eval_active);
    return active;
}

/**
 * @brief Add R-to-R interval to HRV evaluation buffer
 * @param rtor_ms R-to-R interval in milliseconds
 */
void hpi_data_add_hrv_interval(uint16_t rtor_ms)
{
    k_mutex_lock(&mutex_is_hrv_eval_active, K_FOREVER);
    
    // if (is_hrv_eval_active && hrv_interval_count < HRV_MAX_INTERVALS && rtor_ms > 0 && rtor_ms < 2000) {
    if (is_hrv_eval_active && hrv_interval_count < HRV_MAX_INTERVALS && rtor_ms >= 300 && rtor_ms <= 1500) {
        // Detect new beat: RtoR value should change between samples
        // Only add if different from last (to avoid duplicate intervals)
        if (rtor_ms != last_rtor_value) {
            LOG_INF("New RR interval detected : %d", rtor_ms);
            hrv_intervals[hrv_interval_count].rtor_ms = rtor_ms;
            hrv_intervals[hrv_interval_count].timestamp = k_uptime_get();
            hrv_interval_count++;
            last_rtor_value = rtor_ms;
            
            if (hrv_interval_count % 10 == 0) {
                LOG_DBG("HRV: collected %d intervals", hrv_interval_count);
            }
        }
    }
    
    k_mutex_unlock(&mutex_is_hrv_eval_active);
}

/**
 * @brief Get HRV evaluation results
 * @return Pointer to HRV evaluation result structure
 */
struct hpi_hrv_eval_result_t *hpi_data_get_hrv_eval_result(void)
{
    return &hrv_eval_result;
}
void hpi_data_reset_hrv_record_buffer(void)
{
    k_mutex_lock(&mutex_is_hrv_eval_active, K_FOREVER);
    // Reset buffer and counter without saving (for lead-off restart)
    hrv_interval_count = 0;
    memset(hrv_intervals, 0, sizeof(hrv_intervals));
    LOG_INF("HRV recording buffer reset (discard incomplete data)");
    is_hrv_eval_active = false;
    k_mutex_unlock(&mutex_is_hrv_eval_active);
}

void data_thread(void)
{
    struct hpi_ecg_bioz_sensor_data_t ecg_sensor_sample;
    struct hpi_ppg_wr_data_t ppg_wr_sensor_sample;
    struct hpi_ppg_fi_data_t ppg_fi_sensor_sample;
    struct hpi_bioz_sample_t bsample;

    static uint32_t hr_zbus_last_pub_time = 0;

    LOG_INF("Data Thread starting");

    for (;;)
    {
        bool processed_data = false;

        // Process all available ECG samples (unchanged)
        if (k_msgq_get(&q_ecg_sample, &ecg_sensor_sample, K_NO_WAIT) == 0)
        {
            processed_data = true;
            if (settings_send_ble_enabled)
            {

                ble_ecg_notify(ecg_sensor_sample.ecg_samples, ecg_sensor_sample.ecg_num_samples);
                ble_gsr_notify(ecg_sensor_sample.ecg_samples, ecg_sensor_sample.ecg_num_samples);
            }
            if (settings_plot_enabled)
            {
                int ret = k_msgq_put(&q_plot_ecg, &ecg_sensor_sample, K_NO_WAIT);
                if (ret != 0)
                {
                    static uint32_t plot_drops = 0;
                    plot_drops++;
                    if ((plot_drops % 10) == 0)
                    {
                        LOG_WRN("Plot queue full - dropped %u ECG sample batches", plot_drops);
                    }
                }
            }

            

            // ECG recording buffer management with mutex protection
            // Fixed: No circular buffer - linear recording only, stop when full
            // IMPORTANT: Only record samples when leads are connected (not lead-off)
            // This prevents buffer from filling with garbage data when leads are removed

            k_mutex_lock(&mutex_is_ecg_record_active, K_FOREVER);
            if (is_ecg_record_active == true && !is_hrv_eval_active && !ecg_sensor_sample.ecg_lead_off)
            {
                int samples_to_copy = ecg_sensor_sample.ecg_num_samples;
                int space_left = ECG_RECORD_BUFFER_SAMPLES - ecg_record_counter;

                // Defensive check: prevent counter from exceeding buffer size
                if (ecg_record_counter >= ECG_RECORD_BUFFER_SAMPLES) {
                    LOG_ERR("ECG buffer counter overflow detected: %d >= %d - stopping recording",
                            ecg_record_counter, ECG_RECORD_BUFFER_SAMPLES);
                    extern struct k_sem sem_ecg_complete;
                    k_sem_give(&sem_ecg_complete);
                    k_mutex_unlock(&mutex_is_ecg_record_active);
                    continue;  // Skip this sample batch
                }

                if (samples_to_copy <= space_left)
                {
                    // Copy samples to buffer
                    memcpy(&ecg_record_buffer[ecg_record_counter], 
                        ecg_sensor_sample.ecg_samples, 
                        samples_to_copy * sizeof(int32_t));
                    ecg_record_counter += samples_to_copy;
                    
                    // Check if buffer is exactly full
                    if (ecg_record_counter >= ECG_RECORD_BUFFER_SAMPLES)
                    {
                        LOG_INF("ECG buffer full - collected %d samples (30.0 seconds @ 128Hz)", 
                                ecg_record_counter);
                        
                       
                        
                            LOG_INF("Signaling state machine to stop recording");
                            
                            // Signal state machine that buffer is full
                            // State machine will call hpi_data_set_ecg_record_active(false)
                            // which will write the file synchronously
                            extern struct k_sem sem_ecg_complete;
                            k_sem_give(&sem_ecg_complete);
                        }
                       
                }
                else
                {
                    // Not enough space - copy what fits and stop
                    if (space_left > 0)
                    {
                        memcpy(&ecg_record_buffer[ecg_record_counter], 
                            ecg_sensor_sample.ecg_samples, 
                            space_left * sizeof(int32_t));
                        ecg_record_counter += space_left;
                    }
                    
                    LOG_WRN("ECG buffer full mid-batch - collected %d samples, discarded %d", 
                            ecg_record_counter, samples_to_copy - space_left);
                  
                    LOG_INF("Signaling state machine to stop recording");
                        
                     // Signal state machine that buffer is full
                    extern struct k_sem sem_ecg_complete;
                    k_sem_give(&sem_ecg_complete);
                    
                          
                }
            
            }
        
            k_mutex_unlock(&mutex_is_ecg_record_active);
            
            // HRV interval capture - only when leads are connected
            // Skip when lead-off to prevent garbage values from corrupting HRV data
           // LOG_INF("HRV Eval Active : %s", is_hrv_eval_active ? "True" : "False");
            if (is_hrv_eval_active && ecg_sensor_sample.rtor > 0 && !ecg_sensor_sample.ecg_lead_off)
            {
                // Capture R-to-R intervals for HRV analysis
                // RtoR value is in milliseconds from the MAX30001 sensor
                hpi_data_add_hrv_interval(ecg_sensor_sample.rtor);
            }
        
        }
        if (k_msgq_get(&q_bioz_sample, &bsample, K_NO_WAIT) == 0)
        {
            /* ---------------------------------------
            * 1. Convert RAW BioZ → GSR (µS)
            * --------------------------------------- */
            // float gsr_float[BIOZ_MAX_SAMPLES] = {0};
            // convert_raw_to_uS(bsample.bioz_samples, gsr_float, bsample.bioz_num_samples);
            // /* Create a new struct for safe message passing */
            // struct hpi_bioz_sample_t gsr_sample = bsample;

            // /* Copy converted float to int32_t buffer for BLE / plot / record */
            // for (uint8_t i = 0; i < bsample.bioz_num_samples; i++)
            // {
            //     /* Multiply by 100 if you want to store as int with 2 decimal precision */
            //     gsr_sample.bioz_samples[i] = (int32_t)(gsr_float[i] * 100.0f);
            // }

            // /* ---------------------------------------
            // * 2. BLE notification
            // * --------------------------------------- */
            // if (settings_send_ble_enabled)
            // {
            //     ble_gsr_notify(gsr_sample.bioz_samples, gsr_sample.bioz_num_samples);
            // }
            processed_data = true;
            if (settings_send_ble_enabled)
            {
                ble_gsr_notify(bsample.bioz_samples, bsample.bioz_num_samples);
            }
            if (settings_plot_enabled)
            {
                int ret = k_msgq_put(&q_plot_gsr, &bsample, K_NO_WAIT);
                if (ret != 0)
                {
                    static uint32_t plot_drops = 0;
                    plot_drops++;
                    if ((plot_drops % 10) == 0)
                    {
                        LOG_WRN("Plot queue full - dropped %u GSR sample batches", plot_drops);
                    }
                }
            }

            // Background recording: GSR samples
            if (hpi_recording_is_signal_enabled(REC_SIGNAL_GSR))
            {
                hpi_rec_add_gsr_samples(bsample.bioz_samples, bsample.bioz_num_samples);
            //    LOG_WRN("Added %d GSR samples to background recording",bsample.bioz_num_samples);
                if(!is_gsr_record_active)
                {
                    hpi_data_set_gsr_measurement_active(false);
                }
            }
        k_mutex_lock(&mutex_is_gsr_record_active, K_FOREVER);

       // LOG_DBG("is_gsr_record_active=%d, is_measurement_active=%d, gsr_record_counter=%d",is_gsr_record_active, hpi_data_is_gsr_measurement_active(), gsr_record_counter);
        if (is_gsr_record_active == true)
        {
            int samples_to_copy = bsample.bioz_num_samples;
            int space_left = GSR_RECORD_BUFFER_SAMPLES - gsr_record_counter;

            // Defensive check: prevent overflow
            if (gsr_record_counter >= GSR_RECORD_BUFFER_SAMPLES)
            {
                LOG_ERR("GSR buffer overflow detected");
                extern struct k_sem sem_gsr_complete;
                k_sem_give(&sem_gsr_complete);
                k_mutex_unlock(&mutex_is_gsr_record_active);
                continue;
            }

            if (samples_to_copy <= space_left)
            {
                memcpy(&gsr_record_buffer[gsr_record_counter],
                    bsample.bioz_samples,
                    samples_to_copy * sizeof(int32_t));

                gsr_record_counter += samples_to_copy;

                // Completed exactly full buffer
                if (gsr_record_counter >= GSR_RECORD_BUFFER_SAMPLES)
                {
                    LOG_WRN("GSR buffer full - collected %d samples(30.0 seconds @ 32Hz)", gsr_record_counter);
                    LOG_INF("Signaling GSR state machine to stop recording");
                    
                    is_gsr_record_active = false;   // 🔴 CRITICAL
                    extern struct k_sem sem_gsr_complete;
                    k_sem_give(&sem_gsr_complete);
                }
            }
            else
            {
                // Copy what fits
                if (space_left > 0)
                {
                    memcpy(&gsr_record_buffer[gsr_record_counter],
                        bsample.bioz_samples,
                        space_left * sizeof(int32_t));

                    gsr_record_counter += space_left;
                }

             //   LOG_WRN("GSR buffer full mid-batch - dropped samples");
                LOG_WRN("GSR buffer full mid-batch - collected %d samples, discarded %d",gsr_record_counter, samples_to_copy - space_left);
                LOG_INF("Signaling GSR state machine to stop recording");

                extern struct k_sem sem_gsr_complete;
                k_sem_give(&sem_gsr_complete);
            }
        }

        k_mutex_unlock(&mutex_is_gsr_record_active);
        }

        if (k_msgq_get(&q_ppg_fi_sample, &ppg_fi_sensor_sample, K_NO_WAIT) == 0)
        {
            processed_data = true;
            if (settings_send_ble_enabled)
            {
                ble_ppg_notify_fi(ppg_fi_sensor_sample.raw_ir, ppg_fi_sensor_sample.ppg_num_samples);
            }
            if (settings_plot_enabled)
            {
                k_msgq_put(&q_plot_ppg_fi, &ppg_fi_sensor_sample, K_NO_WAIT);
            }

            // Background recording: PPG Finger samples
            if (hpi_recording_is_signal_enabled(REC_SIGNAL_PPG_FINGER))
            {
                hpi_rec_add_ppg_finger_samples(ppg_fi_sensor_sample.raw_ir,
                                                ppg_fi_sensor_sample.raw_red,
                                                ppg_fi_sensor_sample.ppg_num_samples);
            }
        }

        // Check if PPG data is available
        if (k_msgq_get(&q_ppg_wrist_sample, &ppg_wr_sensor_sample, K_NO_WAIT) == 0)
        {
            processed_data = true;
            if (settings_send_ble_enabled)
            {
                ble_ppg_notify_wr(ppg_wr_sensor_sample.raw_green, ppg_wr_sensor_sample.ppg_num_samples);
            }
            if (settings_plot_enabled)
            {
                k_msgq_put(&q_plot_ppg_wrist, &ppg_wr_sensor_sample, K_NO_WAIT);
            }

            // Background recording: PPG Wrist samples
            if (hpi_recording_is_signal_enabled(REC_SIGNAL_PPG_WRIST))
            {
                hpi_rec_add_ppg_wrist_samples(ppg_wr_sensor_sample.raw_ir,
                                               ppg_wr_sensor_sample.raw_red,
                                               ppg_wr_sensor_sample.raw_green,
                                               ppg_wr_sensor_sample.ppg_num_samples);
            }

            if (settings_send_usb_enabled)
            {
            }

            if (ppg_wr_sensor_sample.scd_state == HPI_PPG_SCD_ON_SKIN)
            {
                if (ppg_wr_sensor_sample.hr_confidence > 75)
                {
                    if (hr_zbus_last_pub_time == 0)
                    {
                        hr_zbus_last_pub_time = k_uptime_seconds();
                    }
                    if ((k_uptime_seconds() - hr_zbus_last_pub_time) > 2)
                    {
                        struct hpi_hr_t hr_chan_value = {
                            .timestamp = hw_get_sys_time_ts(),
                            .hr = ppg_wr_sensor_sample.hr,
                            .hr_ready_flag = true,
                        };
                        zbus_chan_pub(&hr_chan, &hr_chan_value, K_SECONDS(1));
                        hr_zbus_last_pub_time = k_uptime_seconds();
                    }
                }
            }
        }

        // Sleep longer if no data was processed to reduce CPU usage
        if (processed_data)
        {
            k_yield(); // Give other threads a chance to run
        }
        else
        {
            k_sleep(K_MSEC(1)); // Reduced sleep time to process samples faster
        }
    }
}

#define DATA_THREAD_STACKSIZE 4096
#define DATA_THREAD_PRIORITY 5 // Higher priority to process samples faster

K_THREAD_DEFINE(data_thread_id, DATA_THREAD_STACKSIZE, data_thread, NULL, NULL, NULL, DATA_THREAD_PRIORITY, 0, 1000);
