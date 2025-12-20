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
#include <zephyr/smf.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <lvgl.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/drivers/sensor.h>
#include <string.h>

#include <time.h>
#include <zephyr/posix/time.h>
#include <zephyr/sys/timeutil.h>

#include "max30001.h"
#include "hpi_common_types.h"
#include "hw_module.h"
#include "ui/move_ui.h"
#include "hpi_sys.h"
#include "hpi_user_settings_api.h"

LOG_MODULE_REGISTER(smf_ecg, LOG_LEVEL_DBG);

SENSOR_DT_READ_IODEV(max30001_iodev, DT_ALIAS(max30001), SENSOR_CHAN_VOLTAGE);

K_MSGQ_DEFINE(q_ecg_sample, sizeof(struct hpi_ecg_bioz_sensor_data_t), 64, 1);  // Reduced from 128 to 64
/* Lightweight queue for BioZ-only samples to reduce copy overhead when ECG not needed */
K_MSGQ_DEFINE(q_bioz_sample, sizeof(struct hpi_bioz_sample_t), 64, 1);

/* `sem_ecg_start` is defined in `hw_module.c`; declare extern below. */
K_SEM_DEFINE(sem_ecg_lon, 0, 1);
K_SEM_DEFINE(sem_ecg_loff, 0, 1);
K_SEM_DEFINE(sem_ecg_cancel, 0, 1);

// GSR (BioZ) Control Semaphores - Independent from ECG
K_SEM_DEFINE(sem_gsr_start, 0, 1);
K_SEM_DEFINE(sem_gsr_cancel, 0, 1);

// GSR Measurement Timing (60 seconds for reliable stress index)
#define GSR_MEASUREMENT_DURATION_S 30
static int64_t gsr_measurement_start_time = 0;
static bool gsr_measurement_in_progress = false;
static uint32_t gsr_last_status_pub_s = 0; // Last published elapsed seconds

// HRV (Heart Rate Variability) Evaluation Control Semaphores - Independent from ECG
K_SEM_DEFINE(sem_hrv_eval_start, 0, 1);
K_SEM_DEFINE(sem_hrv_eval_cancel, 0, 1);
K_SEM_DEFINE(sem_hrv_eval_complete, 0, 1);

// HRV Measurement Timing (configurable duration, default 30 seconds for quick evaluation)
#define HRV_MEASUREMENT_DURATION_S 60  

static uint32_t hrv_last_status_pub_s = 0; // Last published elapsed seconds

// HRV state variables managed by data_module.c
extern struct hpi_hrv_interval_t hrv_intervals[HRV_MAX_INTERVALS];
extern volatile uint16_t hrv_interval_count;

// RTOS-safe lead contact state using atomic operations
// true = leads are in contact (on skin), false = leads are off
// static atomic_t hrv_lead_contact = ATOMIC_INIT(0);  // 0 = no contact, 1 = contact
// static atomic_t hrv_prev_lead_contact = ATOMIC_INIT(0);

ZBUS_CHAN_DECLARE(hrv_stat_chan);
K_MUTEX_DEFINE(hrv_eval_mutex);
K_MUTEX_DEFINE(hrv_timer_mutex);

K_SEM_DEFINE(sem_ecg_lead_on, 0, 1);
K_SEM_DEFINE(sem_ecg_lead_off, 0, 1);

K_SEM_DEFINE(sem_ecg_lead_on_local, 0, 1);
K_SEM_DEFINE(sem_ecg_lead_off_local, 0, 1);


K_SEM_DEFINE(sem_gsr_lead_on, 0, 1);
K_SEM_DEFINE(sem_gsr_lead_off, 0, 1);

ZBUS_CHAN_DECLARE(gsr_status_chan);

// Semaphore for lead reconnection during recording (triggers re-stabilization)
K_SEM_DEFINE(sem_ecg_lead_on_stabilize, 0, 1);

// Lead off debounce (500ms) to avoid false triggers
#define ECG_LEAD_OFF_DEBOUNCE_MS 500
static int64_t lead_off_debounce_start = 0;
static bool lead_off_debouncing = false;

// Mutex for protecting timer and countdown variables (used by non-ISR thread functions)
K_MUTEX_DEFINE(ecg_timer_mutex);

// NOTE: Removed ecg_state_mutex and ecg_filter_mutex - now using ISR-safe atomic operations

ZBUS_CHAN_DECLARE(ecg_stat_chan);
ZBUS_CHAN_DECLARE(ecg_lead_on_off_chan);

#define ECG_SAMPLING_INTERVAL_MS 125
#define BIOZ_SAMPLING_INTERVAL_MS 62  // ~16 Hz polling for 32 SPS BioZ to prevent FIFO overflow
#define ECG_RECORD_DURATION_S 30
#define ECG_STABILIZATION_DURATION_S 5  // Wait 5 seconds for signal to stabilize

// Define maximum sample limits for validation
#define MAX_ECG_SAMPLES 32
#define MAX_BIOZ_SAMPLES 32

// ECG Signal Smoothing Configuration
#define ECG_FILTER_WINDOW_SIZE 5  // Moving average window size
#define ECG_FILTER_BUFFER_SIZE ECG_FILTER_WINDOW_SIZE

// Allow runtime configuration of smoothing (optional)
#ifndef CONFIG_ECG_SMOOTHING_ENABLED
#define CONFIG_ECG_SMOOTHING_ENABLED 0  // Disable smoothing for testing
#endif

#if CONFIG_ECG_SMOOTHING_ENABLED
// ECG smoothing filter state
static int32_t ecg_filter_buffer[ECG_FILTER_BUFFER_SIZE];
static uint8_t ecg_filter_index = 0;
static bool ecg_filter_initialized = false;

/**
 * @brief Apply moving average filter to smooth ECG sample
 * @param raw_sample Raw ECG sample value
 * @return Smoothed ECG sample value
 * 
 * NOTE: ISR-safe version - uses spinlock instead of mutex
 */
static int32_t ecg_smooth_sample(int32_t raw_sample)
{
    static struct k_spinlock ecg_filter_lock;
    k_spinlock_key_t key = k_spin_lock(&ecg_filter_lock);
    
    // Add new sample to circular buffer
    ecg_filter_buffer[ecg_filter_index] = raw_sample;
    ecg_filter_index = (ecg_filter_index + 1) % ECG_FILTER_BUFFER_SIZE;
    
    // Calculate moving average
    int64_t sum = 0;
    uint8_t valid_samples = 0;
    
    if (!ecg_filter_initialized) {
        // During initialization, use only available samples
        for (int i = 0; i <= ecg_filter_index; i++) {
            sum += ecg_filter_buffer[i];
            valid_samples++;
        }
        
        if (ecg_filter_index == ECG_FILTER_BUFFER_SIZE - 1) {
            ecg_filter_initialized = true;
        }
    } else {
        // Use full window
        for (int i = 0; i < ECG_FILTER_BUFFER_SIZE; i++) {
            sum += ecg_filter_buffer[i];
        }
        valid_samples = ECG_FILTER_BUFFER_SIZE;
    }
    
    int32_t smoothed_sample = (int32_t)(sum / valid_samples);
    
    k_spin_unlock(&ecg_filter_lock, key);
    return smoothed_sample;
}

/**
 * @brief Reset ECG smoothing filter state
 * ISR-safe version using spinlock
 */
static void ecg_smooth_reset(void)
{
    static struct k_spinlock ecg_filter_lock;
    k_spinlock_key_t key = k_spin_lock(&ecg_filter_lock);
    memset(ecg_filter_buffer, 0, sizeof(ecg_filter_buffer));
    ecg_filter_index = 0;
    ecg_filter_initialized = false;
    k_spin_unlock(&ecg_filter_lock, key);
}
#else
// ECG smoothing disabled - no-op functions
static int32_t ecg_smooth_sample(int32_t raw_sample)
{
    return raw_sample;
}

static void ecg_smooth_reset(void)
{
    // No-op when smoothing is disabled
}
#endif

static int ecg_last_timer_val = 0;
static int ecg_countdown_val = 0;
static int ecg_stabilization_countdown = 0;
static bool ecg_stabilization_complete = false;


// uint32_t gsr_countdown_val = 0;
// uint32_t gsr_last_timer_val = 0;
static int gsr_countdown_val = 0;
static int64_t gsr_last_timer_val = 0;
K_MUTEX_DEFINE(gsr_timer_mutex);

static const struct smf_state ecg_states[];
struct s_ecg_object
{
    struct smf_ctx ctx;
} s_ecg_obj;

enum ecg_state
{
    HPI_ECG_STATE_IDLE,
    HPI_ECG_STATE_STABILIZING,
    HPI_ECG_STATE_STREAM,
    HPI_ECG_STATE_LEADOFF,
    HPI_ECG_STATE_COMPLETE,

    HPI_ECG_STATE_GSR_MEASURE_ENTRY,
    HPI_ECG_STATE_GSR_MEASURE_STREAM,
    HPI_ECG_STATE_GSR_COMPLETE,
};

RTIO_DEFINE(max30001_read_rtio_poll_ctx, 1, 1);

static bool ecg_active = false;
static bool gsr_active = false;  // Independent GSR (BioZ) state
static bool hrv_active = false;  // Independent HRV evaluation state
static bool m_ecg_lead_on_off = true;  // true = leads OFF, false = leads ON (initialized to OFF state)
static bool gsr_contact_ok = false;
static bool m_gsr_lead_on_off = true;  // true = leads OFF, false = leads ON (initialized to OFF state)
static bool prev_gsr_contact_ok = false; // Track previous contact state

static uint16_t m_ecg_hr = 0;

static int hw_max30001_ecg_enable(void);
static int hw_max30001_ecg_disable(void);

// EXTERNS
extern const struct device *const max30001_dev;
extern struct k_sem sem_ecg_start;
extern struct k_sem sem_ecg_complete;
extern struct k_sem sem_ecg_complete_reset;
//extern bool hrv_active;

extern struct k_sem sem_gsr_complete;
extern struct k_sem sem_gsr_complete_reset;
/**
 * @brief Configure ECG leads based on hand worn setting
 * @return 0 on success, negative error code on failure
 */
static int hw_max30001_configure_leads(void)
{
    struct sensor_value lead_config;
    uint8_t hand_worn = hpi_user_settings_get_hand_worn();
    
    // Set lead configuration: 0 = Left hand, 1 = Right hand
    lead_config.val1 = hand_worn;
    lead_config.val2 = 0;
    
    int ret = sensor_attr_set(max30001_dev, SENSOR_CHAN_ALL, MAX30001_ATTR_LEAD_CONFIG, &lead_config);
    if (ret != 0) {
        LOG_ERR("Failed to configure ECG leads for %s hand: %d", 
                hand_worn ? "right" : "left", ret);
        return ret;
    }
    
    LOG_INF("ECG leads configured for %s hand", hand_worn ? "right" : "left");
    return 0;
}

/**
 * @brief Reconfigure ECG leads when hand worn setting changes
 * This function can be called from UI when the hand worn setting is changed
 */
void reconfigure_ecg_leads_for_hand_worn(void)
{
    // Only reconfigure if ECG is currently active
    if (ecg_active && max30001_dev != NULL) {
        hw_max30001_configure_leads();
    }
}



// Thread-safe accessors for shared variables - ISR-safe versions using atomic operations
static bool get_ecg_active(void)
{
    // Use atomic read for ISR safety - single bool read is typically atomic on most architectures
    return ecg_active;
}

static void set_ecg_active(bool active)
{
    // Use atomic write for ISR safety - single bool write is typically atomic on most architectures
    ecg_active = active;
}

static bool get_gsr_active(void)
{
    // Use atomic read for ISR safety - single bool read is typically atomic on most architectures
    return gsr_active;
}

static void set_gsr_active(bool active)
{
    // Use atomic write for ISR safety - single bool write is typically atomic on most architectures
    gsr_active = active;
}

static bool get_hrv_active(void)
{
    // Use atomic read for ISR safety - single bool read is typically atomic on most architectures
    return hrv_active;
}

static void set_hrv_active(bool active)
{
    // Use atomic write for ISR safety - single bool write is typically atomic on most architectures
    hrv_active = active;
}

static bool get_ecg_lead_on_off(void)
{
    // Use atomic read for ISR safety - single bool read is typically atomic on most architectures
    return m_ecg_lead_on_off;
}

static void set_ecg_lead_on_off(bool state)
{
    // Use atomic write for ISR safety - single bool write is typically atomic on most architectures
    m_ecg_lead_on_off = state;
}

static bool get_gsr_lead_on_off(void)
{
    // Use atomic read for ISR safety - single bool read is typically atomic on most architectures
    return m_gsr_lead_on_off;
}

static void set_gsr_lead_on_off(bool state)
{
    // Use atomic write for ISR safety - single bool write is typically atomic on most architectures
    m_gsr_lead_on_off = state;
}

static uint16_t get_ecg_hr(void)
{
    // Use atomic read for ISR safety - single uint16_t read is typically atomic on most architectures
    return m_ecg_hr;
}

static void set_ecg_hr(uint16_t hr)
{
    // Use atomic write for ISR safety - single uint16_t write is typically atomic on most architectures
    m_ecg_hr = hr;
}

// Thread-safe accessors for timer variables
static void set_ecg_timer_values(uint32_t last_timer, int countdown)
{
    k_mutex_lock(&ecg_timer_mutex, K_FOREVER);
    ecg_last_timer_val = last_timer;
    ecg_countdown_val = countdown;
    k_mutex_unlock(&ecg_timer_mutex);
}

static void get_ecg_timer_values(uint32_t *last_timer, int *countdown)
{
    k_mutex_lock(&ecg_timer_mutex, K_FOREVER);
    if (last_timer) *last_timer = ecg_last_timer_val;
    if (countdown) *countdown = ecg_countdown_val;
    k_mutex_unlock(&ecg_timer_mutex);
}

// Function to reset ECG timer countdown to full duration (30s)
void hpi_ecg_reset_countdown_timer(void)
{
    k_mutex_lock(&ecg_timer_mutex, K_FOREVER);
    ecg_countdown_val = ECG_RECORD_DURATION_S;  // Reset to 30 seconds
    ecg_last_timer_val = k_uptime_get_32();     // Update timestamp
    k_mutex_unlock(&ecg_timer_mutex);
    
    LOG_INF("ECG SMF: Timer countdown RESET to %d seconds", ECG_RECORD_DURATION_S);
    
    // Immediately publish the reset timer value to update the display
    struct hpi_ecg_status_t ecg_stat = {
        .ts_complete = 0,
        .status = HPI_ECG_STATUS_STREAMING,
        .hr = get_ecg_hr(),
        .progress_timer = ECG_RECORD_DURATION_S};  // Show 30 seconds
    zbus_chan_pub(&ecg_stat_chan, &ecg_stat, K_NO_WAIT);
}

void hpi_gsr_reset_countdown_timer(void)
{
    k_mutex_lock(&gsr_timer_mutex, K_FOREVER);

    gsr_countdown_val = GSR_MEASUREMENT_DURATION_S;  // Reset to full duration (e.g., 30s)
    gsr_last_timer_val = k_uptime_get_32();           // Update timestamp

    k_mutex_unlock(&gsr_timer_mutex);

    LOG_DBG("GSR SMF: Timer countdown RESET to %d seconds", GSR_MEASUREMENT_DURATION_S);

    // Immediately publish the reset timer value via ZBus
    struct hpi_gsr_status_t gsr_stat = {
        .elapsed_s = 0,
        .remaining_s = GSR_MEASUREMENT_DURATION_S,
        .total_s = GSR_MEASUREMENT_DURATION_S,
        .active = false,  // Not active since timer just reset
    };

     //extern const struct zbus_channel gsr_status_chan;
                zbus_chan_pub(&gsr_status_chan, &gsr_stat, K_NO_WAIT);
   // zbus_chan_pub(&gsr_status_chan, &gsr_stat, K_NO_WAIT);


}


// Function to reser HRV timer countdown to full duration (configurable)
void hpi_hrv_reset_countdown_timer(void)
{
   k_mutex_lock(&ecg_timer_mutex, K_FOREVER);
    ecg_countdown_val = HRV_MEASUREMENT_DURATION_S ; // Reset to 30 seconds
    ecg_last_timer_val = k_uptime_get_32();     // Update timestamp
    k_mutex_unlock(&ecg_timer_mutex);
    
    LOG_INF("ECG SMF: HRV Timer countdown RESET to %d seconds", HRV_MEASUREMENT_DURATION_S);
    
    // Immediately publish the reset timer value to update the display
    struct hpi_ecg_status_t ecg_stat = {
        .ts_complete = 0,
        .status = HPI_ECG_STATUS_STREAMING,
        .hr = get_ecg_hr(),
        .progress_timer = HRV_MEASUREMENT_DURATION_S};  // Show 30 seconds
    zbus_chan_pub(&ecg_stat_chan, &ecg_stat, K_NO_WAIT);
}

static void set_ecg_stabilization_values(int stabilization_countdown, bool complete)
{
    k_mutex_lock(&ecg_timer_mutex, K_FOREVER);
    ecg_stabilization_countdown = stabilization_countdown;
    ecg_stabilization_complete = complete;
    k_mutex_unlock(&ecg_timer_mutex);
}

static void get_ecg_stabilization_values(int *stabilization_countdown, bool *complete)
{
    k_mutex_lock(&ecg_timer_mutex, K_FOREVER);
    if (stabilization_countdown) *stabilization_countdown = ecg_stabilization_countdown;
    if (complete) *complete = ecg_stabilization_complete;
    k_mutex_unlock(&ecg_timer_mutex);
}

static void work_ecg_lon_handler(struct k_work *work)
{
    // Don't disable/enable ECG during recording to avoid sample loss
    // Just log the lead-on event
    LOG_INF("ECG leads connected");
}
K_WORK_DEFINE(work_ecg_lon, work_ecg_lon_handler);

static void work_ecg_loff_handler(struct k_work *work)
{
    // Lead off detected
}
K_WORK_DEFINE(work_ecg_loff, work_ecg_loff_handler);

static void sensor_ecg_process_decode(uint8_t *buf, uint32_t buf_len)
{
    // Input validation
    if (!buf || buf_len < sizeof(struct max30001_encoded_data)) {
        LOG_ERR("Invalid buffer parameters: buf=%s, len=%u, required=%u", 
                buf ? "OK" : "NULL", buf_len, (uint32_t)sizeof(struct max30001_encoded_data));
        return;
    }

    const struct max30001_encoded_data *edata = (const struct max30001_encoded_data *)buf;
    struct hpi_ecg_bioz_sensor_data_t ecg_sensor_sample;

    uint8_t ecg_num_samples = edata->num_samples_ecg;
    uint8_t bioz_samples = edata->num_samples_bioz;

    // Validate sample counts to prevent buffer overflows
    if (ecg_num_samples > MAX_ECG_SAMPLES || bioz_samples > MAX_BIOZ_SAMPLES) {
        LOG_ERR("Sample count exceeds limits: ECG=%u (max %u), BioZ=%u (max %u)", 
                ecg_num_samples, MAX_ECG_SAMPLES, bioz_samples, MAX_BIOZ_SAMPLES);
        return;
    }

    // printk("ECG NS: %d ", ecg_samples);
    // printk("BioZ NS: %d ", bioz_samples);

    if (ecg_num_samples > 0 || bioz_samples > 0) 
    {
    ecg_sensor_sample.ecg_num_samples = edata->num_samples_ecg;
    ecg_sensor_sample.bioz_num_samples = edata->num_samples_bioz;

        // Apply smoothing filter to ECG samples if enabled
        for (int i = 0; i < edata->num_samples_ecg; i++)
        {
            int32_t raw_sample = edata->ecg_samples[i];
            ecg_sensor_sample.ecg_samples[i] = ecg_smooth_sample(raw_sample);
        }

        for (int i = 0; i < edata->num_samples_bioz; i++)
        {
            ecg_sensor_sample.bioz_sample[i] = edata->bioz_samples[i];
        }

    ecg_sensor_sample.hr = edata->hr;
    ecg_sensor_sample.rtor = edata->rri;

        set_ecg_hr(edata->hr);
        // ecg_bioz_sensor_sample.rrint = edata->rri;

        // LOG_DBG("RRI: %d", edata->rri);

    ecg_sensor_sample.ecg_lead_off = edata->ecg_lead_off;

        // Thread-safe lead detection logic with debouncing
        bool current_lead_state = get_ecg_lead_on_off();
        // LOG_DBG("ECG sensor data: ecg_lead_off=%d, current_lead_state=%s, debouncing=%s", 
        //         edata->ecg_lead_off, current_lead_state ? "OFF" : "ON", 
        //         lead_off_debouncing ? "yes" : "no");
        
        // Handle Lead OFF with debounce
        if (edata->ecg_lead_off == 1)
        {
            if (current_lead_state == false)
            {
                // Lead OFF detected - start or continue debounce timer
                if (!lead_off_debouncing) {
                    lead_off_debouncing = true;
                    lead_off_debounce_start = k_uptime_get();
                    LOG_INF("ECG Lead OFF detected - starting debounce timer");
                } else {
                    // Check if debounce period elapsed
                    int64_t elapsed = k_uptime_get() - lead_off_debounce_start;
                    if (elapsed >= ECG_LEAD_OFF_DEBOUNCE_MS) {
                        LOG_INF("ECG Lead OFF confirmed after %lld ms - signaling display thread", elapsed);
                        set_ecg_lead_on_off(true);
                        k_work_submit(&work_ecg_loff);
                        
                        // Signal display thread for UI update
                        LOG_INF("ECG SMF: Giving sem_ecg_lead_off semaphore");
                       // atomic_set(&hrv_lead_contact, 0);  // RTOS-safe: leads off
                        k_sem_give(&sem_ecg_lead_off);
                        
                        // Reset debounce state
                        lead_off_debouncing = false;
                    }
                }
            }
            // else: already in lead-off state, nothing to do
        }
        // Handle Lead ON (immediate, cancels debounce)
        else if (edata->ecg_lead_off == 0)
        {
            // Cancel any ongoing debounce first
            if (lead_off_debouncing) {
                LOG_INF("ECG Lead OFF debounce cancelled - leads reconnected before timeout");
                lead_off_debouncing = false;
            }
            
            // Signal lead ON if state changed
            if (current_lead_state == true) {
                LOG_INF("ECG Lead ON detected (edata->ecg_lead_off=0) - signaling display thread");
                set_ecg_lead_on_off(false);
                k_work_submit(&work_ecg_lon);
                
                // Signal display thread for UI update  
                LOG_INF("ECG SMF: Giving sem_ecg_lead_on semaphore");
               // atomic_set(&hrv_lead_contact, 1);  // RTOS-safe: leads on
                k_sem_give(&sem_ecg_lead_on);
            }
            // else: already in lead-on state, nothing to do
        }

        if (get_ecg_active() || get_gsr_active())
        {
    int ret = k_msgq_put(&q_ecg_sample, &ecg_sensor_sample, K_NO_WAIT);
            if (ret != 0) {
                LOG_WRN("ECG/GSR sample dropped - queue full (ret=%d)", ret);
            }
        }
    }
    else
    {
        // No samples available
    }
}

static void work_ecg_sample_handler(struct k_work *work)
{
    uint8_t ecg_bioz_buf[512];
    int ret;
    
    ret = sensor_read(&max30001_iodev, &max30001_read_rtio_poll_ctx, ecg_bioz_buf, sizeof(ecg_bioz_buf));
    if (ret < 0) {
        LOG_ERR("Error reading sensor data: %d", ret);
        return;
    }
    if (ret == 0) {
        return;
    }
    sensor_ecg_process_decode(ecg_bioz_buf, ret);
}

K_WORK_DEFINE(work_ecg_sample, work_ecg_sample_handler);
/**
 * @brief Process only BioZ samples from the encoded RTIO buffer.
 * This is a lightweight decoder used when only GSR/BioZ sampling is active.
 */
static void sensor_bioz_only_process_decode(uint8_t *buf, uint32_t buf_len)
{
    if (!buf || buf_len < sizeof(struct max30001_encoded_data)) {
        LOG_ERR("Invalid buffer parameters for BioZ-only decode: buf=%s, len=%u, required=%u",
                buf ? "OK" : "NULL", buf_len, (uint32_t)sizeof(struct max30001_encoded_data));
        return;
    }

    const struct max30001_encoded_data *edata = (const struct max30001_encoded_data *)buf;
    struct hpi_ecg_bioz_sensor_data_t sample;

    uint8_t bioz_samples = edata->num_samples_bioz;
    if (bioz_samples == 0) {
        return;
    }

    if (bioz_samples > MAX_BIOZ_SAMPLES) {
        LOG_ERR("BioZ sample count exceeds limit: %u (max %u)", bioz_samples, MAX_BIOZ_SAMPLES);
        return;
    }

    // Zero out ECG portion since this decoder only handles BioZ
    sample.ecg_num_samples = 0;
    sample.bioz_num_samples = bioz_samples;
    for (int i = 0; i < bioz_samples; i++) {
        sample.bioz_sample[i] = edata->bioz_samples[i];
    }

    sample.hr = edata->hr;
    sample.rtor = edata->rri;
    sample.ecg_lead_off = edata->ecg_lead_off;


  //  LOG_DBG("GSR sensor data: bioz_lead_off=%d", edata->bioz_lead_off);
    
    if (edata->bioz_lead_off == 1)
    {
        LOG_DBG("BIOZ Lead OFF detected (no skin contact)");
        k_sem_give(&sem_gsr_lead_off);
        set_gsr_lead_on_off(true);
         gsr_contact_ok = false;

    } else {

        LOG_DBG("BIOZ Lead ON detected (skin contact OK)");
        k_sem_give(&sem_gsr_lead_on);
        set_gsr_lead_on_off(false);
         gsr_contact_ok = true;
    }

    if (get_gsr_active()) {
        struct hpi_bioz_sample_t bsample = {0};
        bsample.bioz_num_samples = sample.bioz_num_samples;
        bsample.bioz_lead_off = sample.bioz_lead_off;
        bsample.timestamp = k_uptime_get();
        for (int i = 0; i < sample.bioz_num_samples && i < BIOZ_POINTS_PER_SAMPLE; i++) {
            bsample.bioz_samples[i] = sample.bioz_sample[i];
        }
        int ret = k_msgq_put(&q_bioz_sample, &bsample, K_NO_WAIT);
        if (ret != 0) {
            LOG_WRN("BioZ sample dropped - bqueue full (ret=%d)", ret);
        }
    }
}

static void work_bioz_sample_handler(struct k_work *work)
{
    uint8_t ecg_bioz_buf[512];
    int ret;

    ret = sensor_read(&max30001_iodev, &max30001_read_rtio_poll_ctx, ecg_bioz_buf, sizeof(ecg_bioz_buf));
    if (ret < 0) {
        LOG_ERR("Error reading sensor data (BioZ only): %d", ret);
        return;
    }
    if (ret == 0) {
        return;
    }
    sensor_bioz_only_process_decode(ecg_bioz_buf, ret);
}

K_WORK_DEFINE(work_bioz_sample, work_bioz_sample_handler);

static void ecg_sampling_handler(struct k_timer *dummy)
{
    if (get_ecg_active()) {
        k_work_submit(&work_ecg_sample);
    }
}

static void bioz_sampling_handler(struct k_timer *dummy)
{
    if (get_gsr_active()) {
        k_work_submit(&work_bioz_sample);
    }
}

K_TIMER_DEFINE(tmr_ecg_sampling, ecg_sampling_handler, NULL);
K_TIMER_DEFINE(tmr_bioz_sampling, bioz_sampling_handler, NULL);

static int hw_max30001_bioz_enable(void) __attribute__((unused));
static int hw_max30001_bioz_enable(void)
{
    struct sensor_value bioz_mode_set;
    bioz_mode_set.val1 = 1;
    return sensor_attr_set(max30001_dev, SENSOR_CHAN_ALL, MAX30001_ATTR_BIOZ_ENABLED, &bioz_mode_set);
}

static int hw_max30001_ecg_enable(void)
{
    struct sensor_value ecg_mode_set;
    ecg_mode_set.val1 = 1;
    int ret = sensor_attr_set(max30001_dev, SENSOR_CHAN_ALL, MAX30001_ATTR_ECG_ENABLED, &ecg_mode_set);
    if (ret == 0) {
        set_ecg_active(true);
        
        // Configure leads based on hand worn setting
        int lead_ret = hw_max30001_configure_leads();
        if (lead_ret != 0) {
            LOG_WRN("Failed to configure ECG leads, using default configuration");
        }
    } else {
        LOG_ERR("Failed to enable ECG: %d", ret);
    }
    return ret;
}

static int hw_max30001_bioz_disable(void)
{
    struct sensor_value bioz_mode_set;
    bioz_mode_set.val1 = 0;
    int ret = sensor_attr_set(max30001_dev, SENSOR_CHAN_ALL, MAX30001_ATTR_BIOZ_ENABLED, &bioz_mode_set);
    if (ret != 0) {
        LOG_ERR("Failed to disable BioZ: %d", ret);
    }
    return ret;
}

static int hw_max30001_ecg_disable(void)
{
    struct sensor_value ecg_mode_set;
    ecg_mode_set.val1 = 0;
    int ret = sensor_attr_set(max30001_dev, SENSOR_CHAN_ALL, MAX30001_ATTR_ECG_ENABLED, &ecg_mode_set);
    if (ret == 0) {
        set_ecg_active(false);
    } else {
        LOG_ERR("Failed to disable ECG: %d", ret);
    }
    return ret;
}

// GSR (BioZ) specific hardware control functions
static int hw_max30001_gsr_enable(void)
{
    struct sensor_value bioz_mode_set;
    bioz_mode_set.val1 = 1;
    int ret = sensor_attr_set(max30001_dev, SENSOR_CHAN_ALL, MAX30001_ATTR_BIOZ_ENABLED, &bioz_mode_set);
    if (ret == 0) {
        set_gsr_active(true);
    } else {
        LOG_ERR("Failed to enable GSR (BioZ): %d", ret);
    }
    return ret;
}

static int hw_max30001_gsr_disable(void)
{
    struct sensor_value bioz_mode_set;
    bioz_mode_set.val1 = 0;
    int ret = sensor_attr_set(max30001_dev, SENSOR_CHAN_ALL, MAX30001_ATTR_BIOZ_ENABLED, &bioz_mode_set);
    if (ret == 0) {
        set_gsr_active(false);
    } else {
        LOG_ERR("Failed to disable GSR (BioZ): %d", ret);
    }
    return ret;
}
  struct hpi_hrv_eval_result_t g_hrv_result;
static void st_ecg_idle_entry(void *o)
{
    int ret;
    
    ret = hw_max30001_ecg_disable();
    if (ret != 0) {
        LOG_ERR("Failed to disable ECG in idle entry: %d", ret);
    }
    
    // Only stop timers if GSR is also not active
    if (!get_gsr_active()) {
        k_timer_stop(&tmr_ecg_sampling);
        k_timer_stop(&tmr_bioz_sampling);
        
        ret = hw_max30001_bioz_disable();
        if (ret != 0) {
            LOG_ERR("Failed to disable BioZ in idle entry: %d", ret);
        }
    }
}static void st_ecg_idle_run(void *o)
{
    // LOG_DBG("ECG/BioZ SM Idle Run");
    if (k_sem_take(&sem_ecg_start, K_NO_WAIT) == 0)
    {
        smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_STABILIZING]);
    }
    
    if (k_sem_take(&sem_gsr_start, K_NO_WAIT) == 0)
    {
        smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_GSR_MEASURE_ENTRY]);
    }

    // Handle HRV evaluation start from idle state
    if (k_sem_take(&sem_hrv_eval_start, K_NO_WAIT) == 0)
    {
        LOG_INF("Starting HRV evaluation from IDLE - transitioning to STABILIZING state");
        set_hrv_active(true);
        smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_STABILIZING]);
    }

  /*  // Handle independent GSR (BioZ) control
    if (k_sem_take(&sem_gsr_start, K_NO_WAIT) == 0)
    {
        LOG_INF("Starting GSR (BioZ) measurement for %d seconds", GSR_MEASUREMENT_DURATION_S);
        int ret = hw_max30001_gsr_enable();
        if (ret == 0) {
            hpi_data_set_gsr_measurement_active(true);
            hpi_data_set_gsr_record_active(true);   //  Start recording data 

            gsr_measurement_start_time = k_uptime_get();
            gsr_measurement_in_progress = true;
            prev_gsr_contact_ok = gsr_contact_ok; // Initialize previous state
            k_timer_start(&tmr_bioz_sampling, K_MSEC(BIOZ_SAMPLING_INTERVAL_MS), K_MSEC(BIOZ_SAMPLING_INTERVAL_MS));
            LOG_INF("GSR (BioZ) measurement started successfully");
        } else {
            LOG_ERR("Failed to start GSR (BioZ) measurement: %d", ret);
            gsr_measurement_in_progress = false;
        }
    }
    
    if (k_sem_take(&sem_gsr_cancel, K_NO_WAIT) == 0)
    {
        LOG_INF("Stopping GSR (BioZ) measurement");
        int ret = hw_max30001_gsr_disable();
        if (ret == 0) {
            hpi_data_set_gsr_measurement_active(false);
            gsr_measurement_in_progress = false;
            // Reset recording buffer (discard incomplete data on manual stop)
            hpi_data_reset_gsr_record_buffer();
            hpi_data_set_gsr_record_active(false);
            // Only stop bioz timer if ECG is not active
            if (!get_ecg_active()) {
                k_timer_stop(&tmr_bioz_sampling);
            }
            LOG_INF("GSR (BioZ) measurement stopped successfully");
        } else {
            LOG_ERR("Failed to stop GSR (BioZ) measurement: %d", ret);
        }
    }
    
    // Auto-complete GSR measurement after duration


    if (gsr_measurement_in_progress)
    {
        // Detect skin contact change
        if (!gsr_contact_ok) {
            // No contact → reset timer display to 30 s
            hpi_gsr_reset_countdown_timer();
        } else if (!prev_gsr_contact_ok && gsr_contact_ok) {
            // Contact regained → restart countdown from 30 s
            gsr_measurement_start_time = k_uptime_get();
            gsr_last_status_pub_s = 0;
            LOG_INF("GSR contact regained: countdown restarted from %d s", GSR_MEASUREMENT_DURATION_S);
        }
        prev_gsr_contact_ok = gsr_contact_ok;

        // Countdown logic
        if (gsr_contact_ok) 
        {
            int64_t elapsed_ms = k_uptime_get() - gsr_measurement_start_time;
            if (elapsed_ms >= (GSR_MEASUREMENT_DURATION_S * 1000)) {
                // Measurement complete
                LOG_INF("GSR measurement complete after %d seconds", GSR_MEASUREMENT_DURATION_S);
                hw_max30001_gsr_disable();
                hpi_data_set_gsr_measurement_active(false);
                hpi_data_set_gsr_record_active(false);

                gsr_measurement_in_progress = false;
                if (!get_ecg_active()) {
                    k_timer_stop(&tmr_bioz_sampling);
                }
                hpi_load_screen(SCR_GSR, SCROLL_DOWN);
            } else {
                // Publish countdown status
                uint32_t elapsed_s = elapsed_ms / 1000;
                if (elapsed_s != gsr_last_status_pub_s && elapsed_s <= GSR_MEASUREMENT_DURATION_S) {
                    gsr_last_status_pub_s = elapsed_s;

                #if defined(CONFIG_HPI_GSR_SCREEN)
                struct hpi_gsr_status_t gsr_status = {
                .elapsed_s = (uint16_t)elapsed_s,
                .remaining_s = (uint16_t)(GSR_MEASUREMENT_DURATION_S - elapsed_s),
                .total_s = GSR_MEASUREMENT_DURATION_S,
                .active = true,
                };
                zbus_chan_pub(&gsr_status_chan, &gsr_status, K_NO_WAIT);
                #endif
              }
            }
        }
    }  */
}

static void st_gsr_entry_run(void *o)
{
    ARG_UNUSED(o);
    
    LOG_INF("Starting GSR (BioZ) measurement for %d seconds", GSR_MEASUREMENT_DURATION_S);

    int ret = hw_max30001_gsr_enable();

    if (ret != 0) 
    {
        LOG_ERR("Failed to enable GSR entry: %d", ret);
        return;
    }
       
    hpi_data_set_gsr_measurement_active(true);
    hpi_data_set_gsr_record_active(true);
    gsr_measurement_in_progress = true;

   // remaining_timer_s = GSR_MEASUREMENT_DURATION_S;
    gsr_countdown_val = GSR_MEASUREMENT_DURATION_S;
    prev_gsr_contact_ok =  gsr_contact_ok;  // reset state

    k_timer_start(&tmr_bioz_sampling,
                  K_MSEC(BIOZ_SAMPLING_INTERVAL_MS),
                  K_MSEC(BIOZ_SAMPLING_INTERVAL_MS));

    bool current_lead_off = get_gsr_lead_on_off();
    if (current_lead_off) {
        k_sem_give(&sem_gsr_lead_off);
    } else {
        k_sem_give(&sem_gsr_lead_on);
    }

    smf_set_state(SMF_CTX(&s_ecg_obj),
                  &ecg_states[HPI_ECG_STATE_GSR_MEASURE_STREAM]);
}


static void st_gsr_stream_run(void *o)
{
    ARG_UNUSED(o);

    bool contact_ok = (get_gsr_lead_on_off() == 0); // TRUE when skin contact

    k_mutex_lock(&gsr_timer_mutex, K_FOREVER);

    if (!contact_ok) {
        // Timer frozen when no skin contact - no logging needed (high frequency)
    } else {
        // Decrement timer every second
        int64_t now = k_uptime_get_32();
        if (now - gsr_last_timer_val >= 1000) {
            gsr_last_timer_val = now;
            if (gsr_countdown_val > 0) {
                gsr_countdown_val--;
            }
        }
    }
  
    // Prepare status for UI
    struct hpi_gsr_status_t status = {
        .remaining_s = gsr_countdown_val,
        .total_s = GSR_MEASUREMENT_DURATION_S,
        .active = contact_ok,
    };

    // LOG_INF("GSR SMF: Timer = %d/%d s", 
    //         gsr_countdown_val, GSR_MEASUREMENT_DURATION_S);

     // Publish status to UI
    zbus_chan_pub(&gsr_status_chan, &status, K_NO_WAIT);

    k_mutex_unlock(&gsr_timer_mutex);

    // Complete state check
    if (gsr_countdown_val <= 0 && contact_ok) {
        smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_GSR_COMPLETE]);
    }


    // Handle semaphores
    if (k_sem_take(&sem_gsr_complete, K_NO_WAIT) == 0) {
        LOG_INF("GSR SMF: Buffer full signal received - switching to COMPLETE state");
        smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_GSR_COMPLETE]);
    }

    if (k_sem_take(&sem_gsr_cancel, K_NO_WAIT) == 0) {
        LOG_DBG("GSR cancelled");
        hpi_data_reset_gsr_record_buffer();

        smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_IDLE]);
    }

    prev_gsr_contact_ok = contact_ok;
}
static void st_gsr_stream_exit(void *o)
{
    LOG_DBG("BioZ SM Stream Exit");
    k_timer_stop(&tmr_bioz_sampling);
}

static void st_gsr_complete_run(void *o)
{
    LOG_INF("GSR COMPLETE");
    hpi_data_set_gsr_measurement_active(false);
    hpi_data_set_gsr_record_active(false);
    int  ret = hw_max30001_gsr_disable();

    if (ret != 0) {
        LOG_ERR("Failed to disable GSR in complete : %d", ret);
    }

    k_sem_give(&sem_gsr_complete_reset);

    smf_set_state(SMF_CTX(&s_ecg_obj),
                  &ecg_states[HPI_ECG_STATE_IDLE]);
    // Note: HRV eval start is now handled in st_ecg_idle_run() to ensure it's processed
} 
struct hpi_hrv_eval_result_t hpi_data_get_hrv_result(void)
{
                return g_hrv_result;   // return by value
}

static void st_ecg_stream_entry(void *o)
{
    int ret;
    bool stabilization_complete;

    // Check if coming from stabilization 
    get_ecg_stabilization_values(NULL, &stabilization_complete);
    
    if (!stabilization_complete) {
        ret = hw_max30001_ecg_enable();
        if (ret != 0) {
            LOG_ERR("Failed to enable ECG in stream entry: %d", ret);
            return;
        }
        k_timer_start(&tmr_ecg_sampling, K_MSEC(ECG_SAMPLING_INTERVAL_MS), K_MSEC(ECG_SAMPLING_INTERVAL_MS));
    }
    
    // Start actual recording
    hpi_data_set_ecg_record_active(true);

    if(get_hrv_active())
    {
        LOG_INF("HRV evaluation starting - initializing HRV data collection");
        hrv_interval_count = 0;
        memset(hrv_intervals, 0, sizeof(hrv_intervals));
        hrv_last_status_pub_s = 0;
  
    }
    
    // Timer initialization - determine duration based on measurement type
    int measurement_duration = get_hrv_active() ? HRV_MEASUREMENT_DURATION_S : ECG_RECORD_DURATION_S;
    
    // Start paused until lead ON is detected
    set_ecg_timer_values(k_uptime_get_32(), measurement_duration);
    hpi_ecg_timer_reset();  // Start in reset state (paused, waiting for leads)
    
    // Log the measurement type being started
    if (get_hrv_active()) {
        LOG_INF("HRV evaluation recording started - duration %d seconds", measurement_duration);
    } else {
        LOG_INF("ECG recording started - duration %d seconds", measurement_duration);
    }
    
    // Publish initial timer status to update display immediately
    struct hpi_ecg_status_t ecg_stat = {
        .ts_complete = 0,
        .status = HPI_ECG_STATUS_STREAMING,
        .hr = get_ecg_hr(),
        .progress_timer = measurement_duration};
    zbus_chan_pub(&ecg_stat_chan, &ecg_stat, K_NO_WAIT);
    
    // Initialize screen with current lead state - signal display thread
    bool current_lead_off = get_ecg_lead_on_off();
    if (current_lead_off) {
        k_sem_give(&sem_ecg_lead_off);
    } else {
        k_sem_give(&sem_ecg_lead_on);
    }
    
    LOG_INF("Recording started - waiting for leads to be connected");
    LOG_INF("Timer will start automatically when leads are detected");
}

static void st_ecg_stream_run(void *o)
{
    // Check for lead reconnection requiring stabilization
    if (k_sem_take(&sem_ecg_lead_on_stabilize, K_NO_WAIT) == 0)
    {
        LOG_INF("ECG SMF: Lead reconnected - entering stabilization phase");
        
        // Reset software smoothing filter for clean start
        ecg_smooth_reset();
        
        // Note: We don't reset FIFO here - the sensor is already running
        // and FIFO reset during active operation might cause issues
        // The stabilization period will naturally discard transient samples
        
        // Transition to stabilizing state
        smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_STABILIZING]);
        return;
    }

    // LOG_DBG("ECG/BioZ SM Stream Run");
    // Stream for ECG duration (30s)
    if (hpi_data_is_ecg_record_active() == true)
    {
        uint32_t last_timer;
        int countdown;
        get_ecg_timer_values(&last_timer, &countdown);
        
        // Only count down if timer is actually running (leads are on)
        if (hpi_ecg_timer_is_running() && (k_uptime_get_32() - last_timer) >= 1000)
        {
            countdown--;
            set_ecg_timer_values(k_uptime_get_32(), countdown);
            
            LOG_DBG("ECG SMF: Timer countdown: %ds remaining (timer running)", countdown);

            struct hpi_ecg_status_t ecg_stat = {
                .ts_complete = 0,
                .status = HPI_ECG_STATUS_STREAMING,
                .hr = get_ecg_hr(),
                .progress_timer = countdown};
            zbus_chan_pub(&ecg_stat_chan, &ecg_stat, K_NO_WAIT);

            if (countdown <= 0)
            {
                LOG_INF("ECG SMF: Timer completed - switching to COMPLETE state");
                smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_COMPLETE]);
            }
        }
        // If timer is paused (leads off), still update the display but don't count down
        else if (!hpi_ecg_timer_is_running() && (k_uptime_get_32() - last_timer) >= 1000)
        {
            // Update timestamp to prevent rapid firing but keep countdown unchanged
            set_ecg_timer_values(k_uptime_get_32(), countdown);
            
            LOG_DBG("ECG SMF: Timer paused: %ds remaining (timer not running)", countdown);
            
            struct hpi_ecg_status_t ecg_stat = {
                .ts_complete = 0,
                .status = HPI_ECG_STATUS_STREAMING,
                .hr = get_ecg_hr(),
                .progress_timer = countdown};  // Keep same countdown value
            zbus_chan_pub(&ecg_stat_chan, &ecg_stat, K_NO_WAIT);
        }
    }

    // Check if buffer is full (signaled by data module)
    // This provides redundant protection in case timer and buffer get out of sync
    if (k_sem_take(&sem_ecg_complete, K_NO_WAIT) == 0)
    {
        LOG_INF("ECG SMF: Buffer full signal received - switching to COMPLETE state");
        smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_COMPLETE]);
    }

    // Handle GSR start/stop even during ECG recording
    if (k_sem_take(&sem_gsr_start, K_NO_WAIT) == 0)
    {
        LOG_INF("Starting GSR during ECG recording");
        hw_max30001_gsr_enable();
        hpi_data_set_gsr_measurement_active(true);
    }
    
    if (k_sem_take(&sem_gsr_cancel, K_NO_WAIT) == 0)
    {
        LOG_INF("Stopping GSR during ECG recording");
        hw_max30001_gsr_disable();
        hpi_data_set_gsr_measurement_active(false);
    }

    if (k_sem_take(&sem_ecg_cancel, K_NO_WAIT) == 0)
    {
        LOG_DBG("ECG cancelled");
        smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_IDLE]);
    }

    if(k_sem_take(&sem_hrv_eval_cancel, K_NO_WAIT) == 0)
    {
        LOG_DBG("HRV evaluation cancelled");
        set_hrv_active(false);
        hpi_data_set_hrv_eval_active(false);
        smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_IDLE]);
    }
}

static void st_ecg_stream_exit(void *o)
{
    LOG_DBG("ECG/BioZ SM Stream Exit");
    
    // Reset timer when exiting stream state
    hpi_ecg_timer_reset();
        
    hpi_data_set_ecg_record_active(false);
  

    // Reset timer
    set_ecg_timer_values(0, 0);
    set_ecg_stabilization_values(0, false); 
}

static void st_ecg_complete_entry(void *o)
{
    LOG_DBG("ECG/BioZ SM Complete Entry");
    int ret;
    
    // Only stop timer if GSR is also not active
    if (!get_gsr_active()) {
        k_timer_stop(&tmr_ecg_sampling);
        k_timer_stop(&tmr_bioz_sampling);
    }
    
    ret = hw_max30001_ecg_disable();
    if (ret != 0) {
        LOG_ERR("Failed to disable ECG in complete entry: %d", ret);
    }

    // Handle HRV post-processing if HRV evaluation was active
    if (get_hrv_active()) {
        LOG_INF("HRV evaluation complete - processing results");
        // Call HRV post-processing function (FFT, frequency analysis, stress index)
        // This will update hrv_eval_result and write to file
        hpi_data_hrv_record_to_file(true);
        hpi_data_set_hrv_eval_active(false);
        // Signal HRV complete
        k_sem_give(&sem_hrv_eval_complete);
        // Reset HRV active flag
        set_hrv_active(false);
    } else {
        // ECG recording complete - signal ECG completion
        k_sem_give(&sem_ecg_complete);
    }
}

static void st_ecg_complete_run(void *o)
{
    // Handle GSR start/stop during ECG complete phase
    if (k_sem_take(&sem_gsr_start, K_NO_WAIT) == 0)
    {
        LOG_INF("Starting GSR during ECG complete phase");
        hw_max30001_gsr_enable();
        hpi_data_set_gsr_measurement_active(true);
    }
    
    if (k_sem_take(&sem_gsr_cancel, K_NO_WAIT) == 0)
    {
        LOG_INF("Stopping GSR during ECG complete phase");
        hw_max30001_gsr_disable();
        hpi_data_set_gsr_measurement_active(false);
    }
    
    // ECG complete - return to idle unless new operation requested
    smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_IDLE]);
}

static void st_ecg_complete_exit(void *o)
{
    LOG_DBG("ECG/BioZ SM Complete Exit");
}

static void st_ecg_leadoff_entry(void *o)
{
    LOG_DBG("ECG/BioZ SM Leadoff Entry");
    // hw_max30001_ecg_disable();
    // hw_max30001_bioz_disable();
    // Timers are managed separately for ECG and BioZ
}

static void st_ecg_leadoff_run(void *o)
{
    // Handle GSR start/stop during ECG leadoff
    if (k_sem_take(&sem_gsr_start, K_NO_WAIT) == 0)
    {
        LOG_INF("Starting GSR during ECG leadoff");
        hw_max30001_gsr_enable();
        hpi_data_set_gsr_measurement_active(true);
    }
    
    if (k_sem_take(&sem_gsr_cancel, K_NO_WAIT) == 0)
    {
        LOG_INF("Stopping GSR during ECG leadoff");
        hw_max30001_gsr_disable();
        hpi_data_set_gsr_measurement_active(false);
    }

    // LOG_DBG("ECG/BioZ SM Leadoff Run");
    if (k_sem_take(&sem_ecg_lead_on_local, K_NO_WAIT) == 0)
    {
        smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_STREAM]);
    }
}

static void st_ecg_stabilizing_entry(void *o)
{
    LOG_DBG("ECG/BioZ SM Stabilizing Entry");
    int ret;

    // Check if this is initial stabilization or re-stabilization during recording
    bool is_recording_active = hpi_data_is_ecg_record_active();

    // Always reset ECG smoothing filter for clean start
    ecg_smooth_reset();

    // Only enable ECG and start sampling if not already active (initial start)
    if (!is_recording_active) {
        LOG_INF("Initial stabilization - enabling ECG and starting sampling");
        
        // Enable ECG but don't start recording yet
        ret = hw_max30001_ecg_enable();
        if (ret != 0) {
            LOG_ERR("Failed to enable ECG in stabilizing entry: %d", ret);
            return;
        }
        
        k_timer_start(&tmr_ecg_sampling, K_MSEC(ECG_SAMPLING_INTERVAL_MS), K_MSEC(ECG_SAMPLING_INTERVAL_MS));
    } else {
        LOG_INF("Re-stabilization during active recording - syncing MAX30001");
        
        // SYNCH command resets internal decimation filters and timing
        // without affecting configuration registers (gain, leads, etc.)
        //max30001_synch(max30001_dev);
    }
    
    // Init stabilization values
    set_ecg_stabilization_values(ECG_STABILIZATION_DURATION_S, false);
    set_ecg_timer_values(k_uptime_get_32(), 0);
    
     // Initialize HRV if evaluation is being started
    //  if (get_hrv_active()) {
    //      LOG_INF("HRV evaluation starting - initializing HRV data collection");
    //      hpi_data_set_hrv_eval_active(true);
    //      hrv_interval_count = 0;
    //      memset(hrv_intervals, 0, sizeof(hrv_intervals));
    //      hrv_last_status_pub_s = 0;
    //  }
    
    int duration = is_recording_active ? ECG_RECORD_DURATION_S : HRV_MEASUREMENT_DURATION_S;
    // Publish status indicating stabilization phase
    struct hpi_ecg_status_t ecg_stat = {
        .ts_complete = 0,
        .status = HPI_ECG_STATUS_STREAMING, // TODO: Consider adding HPI_ECG_STATUS_STABILIZING
        .hr = 0,
        .progress_timer = duration + ECG_STABILIZATION_DURATION_S};
    zbus_chan_pub(&ecg_stat_chan, &ecg_stat, K_NO_WAIT);
    
    LOG_INF("ECG stabilization started - waiting %d seconds", ECG_STABILIZATION_DURATION_S);
#if CONFIG_ECG_SMOOTHING_ENABLED
    LOG_INF("ECG smoothing enabled with window size %d", ECG_FILTER_WINDOW_SIZE);
#else
    LOG_INF("ECG smoothing disabled - using raw samples");
#endif
}

static void st_ecg_stabilizing_run(void *o)
{
    // Count down stabilization timer
    uint32_t last_timer;
    int stabilization_countdown;
    get_ecg_timer_values(&last_timer, NULL);
    get_ecg_stabilization_values(&stabilization_countdown, NULL);
    
    if ((k_uptime_get_32() - last_timer) >= 1000)
    {
        stabilization_countdown--;
        LOG_DBG("ECG stabilization timer: %d", stabilization_countdown);
        
        // Update timer values atomically
        set_ecg_timer_values(k_uptime_get_32(), 0);
        set_ecg_stabilization_values(stabilization_countdown, false);

       int duration = get_hrv_active() ? HRV_MEASUREMENT_DURATION_S : ECG_RECORD_DURATION_S;
        // Update progress - total time includes stabilization + recording
        struct hpi_ecg_status_t ecg_stat = {
            .ts_complete = 0,
            .status = HPI_ECG_STATUS_STREAMING,
            .hr = get_ecg_hr(),
            .progress_timer = duration + stabilization_countdown};
        zbus_chan_pub(&ecg_stat_chan, &ecg_stat, K_NO_WAIT);

        if (stabilization_countdown <= 0)
        {
            LOG_INF("ECG stabilization complete - starting recording");
            smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_STREAM]);
        }
    }

    // Handle GSR start/stop during ECG stabilization
    if (k_sem_take(&sem_gsr_start, K_NO_WAIT) == 0)
    {
        LOG_INF("Starting GSR during ECG stabilization");
        hw_max30001_gsr_enable();
        hpi_data_set_gsr_measurement_active(true);
    }
    
    if (k_sem_take(&sem_gsr_cancel, K_NO_WAIT) == 0)
    {
        LOG_INF("Stopping GSR during ECG stabilization");
        hw_max30001_gsr_disable();
        hpi_data_set_gsr_measurement_active(false);
    }

    // Allow cancellation during stabilization
    if (k_sem_take(&sem_ecg_cancel, K_NO_WAIT) == 0)
    {
        LOG_DBG("ECG cancelled during stabilization");
        smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_IDLE]);
    }

     if(k_sem_take(&sem_hrv_eval_cancel, K_NO_WAIT) == 0)
    {
        LOG_DBG("HRV evaluation cancelled during stabilization");
        set_hrv_active(false);
        hpi_data_set_hrv_eval_active(false);
        smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_IDLE]);
    }
}

static void st_ecg_stabilizing_exit(void *o)
{
    LOG_DBG("ECG/BioZ SM Stabilizing Exit");
    set_ecg_stabilization_values(0, true);
    
    // If this was a re-stabilization during recording, restart the timer
    bool is_recording_active = hpi_data_is_ecg_record_active();
    if (is_recording_active) {
        LOG_INF("Stabilization complete - resuming recording with timer start");
        hpi_ecg_timer_start();
    }
}

static const struct smf_state ecg_states[] = {
    [HPI_ECG_STATE_IDLE] = SMF_CREATE_STATE(st_ecg_idle_entry, st_ecg_idle_run, NULL, NULL, NULL),
    [HPI_ECG_STATE_STABILIZING] = SMF_CREATE_STATE(st_ecg_stabilizing_entry, st_ecg_stabilizing_run, st_ecg_stabilizing_exit, NULL, NULL),
    [HPI_ECG_STATE_STREAM] = SMF_CREATE_STATE(st_ecg_stream_entry, st_ecg_stream_run, st_ecg_stream_exit, NULL, NULL),
    [HPI_ECG_STATE_LEADOFF] = SMF_CREATE_STATE(st_ecg_leadoff_entry, st_ecg_leadoff_run, NULL, NULL, NULL),
    [HPI_ECG_STATE_COMPLETE] = SMF_CREATE_STATE(st_ecg_complete_entry, st_ecg_complete_run, st_ecg_complete_exit, NULL, NULL),
    
    [HPI_ECG_STATE_GSR_MEASURE_ENTRY]  = SMF_CREATE_STATE(NULL, st_gsr_entry_run, NULL, NULL, NULL),
    [HPI_ECG_STATE_GSR_MEASURE_STREAM] = SMF_CREATE_STATE(NULL, st_gsr_stream_run, st_gsr_stream_exit, NULL, NULL),
    [HPI_ECG_STATE_GSR_COMPLETE]       = SMF_CREATE_STATE(NULL, st_gsr_complete_run, NULL, NULL, NULL),
};

void smf_ecg_thread(void)
{
    int ret;

    // Wait for HW module to init ECG
    k_sem_take(&sem_ecg_start, K_FOREVER);

    LOG_INF("ECG SMF Thread Started");

    smf_set_initial(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_IDLE]);

    for (;;)
    {
        ret = smf_run_state(SMF_CTX(&s_ecg_obj));
        if (ret != 0)
        {
            LOG_ERR("SMF Run error: %d", ret);
            break;
        }
        k_msleep(100);
    }
}

// Increased from 1024 to 4096 bytes to accommodate file write operations
// File writes require ~500-700 bytes for LittleFS operations, path buffers,
// and file structures. 1024 bytes was causing stack overflow crashes.
K_THREAD_DEFINE(smf_ecg_thread_id, 4096, smf_ecg_thread, NULL, NULL, NULL, 10, 0, 0);
