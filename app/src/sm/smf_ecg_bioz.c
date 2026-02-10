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
K_SEM_DEFINE(sem_ecg_lead_timeout, 0, 1);  // Signaled when lead placement times out

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

// Lead off debounce (500ms) to avoid false triggers
#define ECG_LEAD_OFF_DEBOUNCE_MS 500
static int64_t lead_off_debounce_start = 0;
static bool lead_off_debouncing = false;

// Lead ON debounce in SMF (500ms) to avoid false triggers when leads are unstable
#define ECG_LEAD_ON_DEBOUNCE_MS 500
static int64_t smf_lead_on_debounce_start = 0;
static bool smf_lead_on_debouncing = false;

// Mutex for protecting timer and countdown variables (used by non-ISR thread functions)
K_MUTEX_DEFINE(ecg_timer_mutex);

// NOTE: Removed ecg_state_mutex and ecg_filter_mutex - now using ISR-safe atomic operations

ZBUS_CHAN_DECLARE(ecg_stat_chan);
ZBUS_CHAN_DECLARE(ecg_lead_on_off_chan);

#define ECG_SAMPLING_INTERVAL_MS 125
#define BIOZ_SAMPLING_INTERVAL_MS 62  // ~16 Hz polling for 32 SPS BioZ to prevent FIFO overflow
#define ECG_RECORD_DURATION_S 30
#define ECG_STABILIZATION_DURATION_S 5  // Wait 5 seconds for signal to stabilize
#define ECG_LEAD_PLACEMENT_TIMEOUT_S 15 // Timeout if leads not placed within 15 seconds

// Track when we started waiting for leads (0 = not waiting)
static int64_t lead_placement_wait_start = 0;

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

static uint32_t ecg_last_timer_val = 0;
static int ecg_countdown_val = 0;
static int ecg_stabilization_countdown = 0;
bool ecg_cancellation = false;


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
    HPI_ECG_STATE_WAIT_FOR_LEAD,    // Waiting for user to place fingers on electrodes
    HPI_ECG_STATE_RECORDING,        // Stabilization phase + active recording

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

static atomic_t g_gsr_bg_active = ATOMIC_INIT(0);
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

// Function to clear lead placement timeout (called when leads are connected)
void hpi_ecg_clear_lead_placement_timeout(void)
{
    lead_placement_wait_start = 0;
    LOG_DBG("ECG SMF: Lead placement timeout cleared");
}

// Function to reset ECG timer countdown to full duration (30s for ECG, 60s for HRV)
void hpi_ecg_reset_countdown_timer(void)
{
    int duration = get_hrv_active() ? HRV_MEASUREMENT_DURATION_S : ECG_RECORD_DURATION_S;

    k_mutex_lock(&ecg_timer_mutex, K_FOREVER);
    ecg_countdown_val = duration;
    ecg_last_timer_val = k_uptime_get_32();
    k_mutex_unlock(&ecg_timer_mutex);

    LOG_INF("ECG SMF: Timer countdown RESET to %d seconds", duration);

    // Immediately publish the reset timer value to update the display
    struct hpi_ecg_status_t ecg_stat = {
        .ts_complete = 0,
        .status = HPI_ECG_STATUS_STREAMING,
        .hr = get_ecg_hr(),
        .progress_timer = duration};
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


// Function to reset HRV timer countdown to full duration (60 seconds)
// Note: This now just calls hpi_ecg_reset_countdown_timer() which handles both ECG and HRV
void hpi_hrv_reset_countdown_timer(void)
{
    hpi_ecg_reset_countdown_timer();
}

static void set_ecg_stabilization_countdown(int countdown)
{
    k_mutex_lock(&ecg_timer_mutex, K_FOREVER);
    ecg_stabilization_countdown = countdown;
    k_mutex_unlock(&ecg_timer_mutex);
}

static int get_ecg_stabilization_countdown(void)
{
    k_mutex_lock(&ecg_timer_mutex, K_FOREVER);
    int val = ecg_stabilization_countdown;
    k_mutex_unlock(&ecg_timer_mutex);
    return val;
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
                        LOG_INF("ECG Lead OFF confirmed after %lld ms", elapsed);
                        // Only update state variable - SMF will handle signaling display thread
                        set_ecg_lead_on_off(true);
                        k_work_submit(&work_ecg_loff);

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
                LOG_INF("ECG Lead ON detected (edata->ecg_lead_off=0)");
                // Only update state variable - SMF will handle signaling display thread
                set_ecg_lead_on_off(false);
                k_work_submit(&work_ecg_lon);
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
    //LOG_INF("Sensor read returned %d bytes", ret);
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
      //  LOG_DBG("BIOZ Lead OFF detected (no skin contact)");
        k_sem_give(&sem_gsr_lead_off);
        set_gsr_lead_on_off(true);
         gsr_contact_ok = false;

    } else {

       // LOG_DBG("BIOZ Lead ON detected (skin contact OK)");
        k_sem_give(&sem_gsr_lead_on);
        set_gsr_lead_on_off(false);
         gsr_contact_ok = true;
    }

    if (get_gsr_active() || hpi_recording_is_signal_enabled(REC_SIGNAL_GSR)) {
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
    if (get_gsr_active() || hpi_recording_is_signal_enabled(REC_SIGNAL_GSR)) {
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

void gsr_background_start(void)
{
    /* Already running? do nothing */
    if (atomic_get(&g_gsr_bg_active)) {
        return;
    }

    LOG_INF("GSR background START");

    atomic_set(&g_gsr_bg_active, 1);
    set_gsr_active(true);
    hpi_data_set_gsr_measurement_active(false);
    hpi_data_reset_gsr_record_buffer();
    hpi_data_set_gsr_record_active(false);
    hw_max30001_gsr_enable();

    k_timer_start(&tmr_bioz_sampling,
                  K_MSEC(BIOZ_SAMPLING_INTERVAL_MS),
                  K_MSEC(BIOZ_SAMPLING_INTERVAL_MS));
}

void gsr_background_stop(void)
{
    /* Not running? do nothing */
    if (!atomic_get(&g_gsr_bg_active)) {
        return;
    }

    LOG_INF("GSR background STOP");

    atomic_set(&g_gsr_bg_active, 0);

    if( !hpi_data_is_gsr_measurement_active() ) {
      set_gsr_active(false);
      k_timer_stop(&tmr_bioz_sampling);
      hw_max30001_gsr_disable();
    }
    
}

struct hpi_hrv_eval_result_t g_hrv_result;

static void st_ecg_idle_entry(void *o)
{
    int ret;

    LOG_INF("ECG SMF: Entering IDLE state");

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

    // Reset all ECG/HRV state flags
    
    set_hrv_active(false);
    hpi_data_set_hrv_eval_active(false);
    hpi_data_set_ecg_record_active(false);
    hpi_ecg_timer_reset();
    ecg_cancellation = false;
}

static void st_ecg_idle_run(void *o)
{
    // Handle ECG start request
    if (k_sem_take(&sem_ecg_start, K_NO_WAIT) == 0)
    {
        LOG_INF("ECG SMF: ECG start requested - transitioning to WAIT_FOR_LEAD");
        set_hrv_active(false);
        smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_WAIT_FOR_LEAD]);
    }

    // Handle GSR start request
    if (k_sem_take(&sem_gsr_start, K_NO_WAIT) == 0)
    {
        smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_GSR_MEASURE_ENTRY]);
    }

    // Handle HRV evaluation start request
    if (k_sem_take(&sem_hrv_eval_start, K_NO_WAIT) == 0)
    {
        LOG_INF("ECG SMF: HRV start requested - transitioning to WAIT_FOR_LEAD");
        set_hrv_active(true);
        hpi_data_set_hrv_eval_active(true);
        smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_WAIT_FOR_LEAD]);
    }
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
        hpi_data_set_gsr_record_active(false);
        smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_IDLE]);
    }

    prev_gsr_contact_ok = contact_ok;
}
static void st_gsr_stream_exit(void *o)
{
    LOG_DBG("BioZ SM Stream Exit");
     // k_timer_stop(&tmr_bioz_sampling);
   if (!hpi_recording_is_signal_enabled(REC_SIGNAL_GSR))
    {
        hpi_data_set_gsr_measurement_active(false);
        k_timer_stop(&tmr_bioz_sampling);
        int  ret = hw_max30001_gsr_disable();

        if (ret != 0) {
            LOG_ERR("Failed to disable GSR in complete : %d", ret);
        }
    }
}

static void st_gsr_complete_run(void *o)
{
    LOG_INF("GSR COMPLETE");
   // hpi_data_set_gsr_measurement_active(false);
    hpi_data_set_gsr_record_active(false);
   

    k_sem_give(&sem_gsr_complete_reset);

    smf_set_state(SMF_CTX(&s_ecg_obj),
                  &ecg_states[HPI_ECG_STATE_IDLE]);
    // Note: HRV eval start is now handled in st_ecg_idle_run() to ensure it's processed
} 
struct hpi_hrv_eval_result_t hpi_data_get_hrv_result(void)
{
    return g_hrv_result;
}

/*
 * =============================================================================
 * ECG/HRV STATE MACHINE
 * =============================================================================
 *
 * ARCHITECTURE:
 * - SMF polls sensor state directly via get_ecg_lead_on_off()
 * - SMF is the ONLY entity that signals display thread (sem_ecg_lead_on/off)
 * - Sensor layer only updates state variable, does NOT signal semaphores
 *
 * FLOW (3 states):
 *   IDLE -> WAIT_FOR_LEAD -> RECORDING -> IDLE
 *
 * RECORDING has two internal phases:
 *   1. Stabilization (5s) — signal settles, NOT buffered
 *   2. Buffer recording (30s ECG / 60s HRV)
 *
 * Lead-off during RECORDING pauses the countdown/timer without changing
 * state.  Lead-on resumes.  This avoids the old bounce between states
 * that caused the "stuck at stabilising" bug.
 *
 * LEAD DETECTION:
 * - get_ecg_lead_on_off() returns: true = leads OFF, false = leads ON
 * - Sensor layer handles debouncing internally before updating state
 * - SMF polls state every 100ms (thread sleep interval)
 */

// Track previous lead state to detect transitions
static bool smf_last_lead_off = true;  // Start assuming leads are off

/*
 * WAIT_FOR_LEAD STATE
 * - Display "leads off" message
 * - Wait for lead contact or timeout (15s)
 */
static void st_ecg_wait_for_lead_entry(void *o)
{
    int ret;

    LOG_INF("ECG SMF: Entering WAIT_FOR_LEAD state");

    // Enable ECG hardware if not already active
    if (!get_ecg_active()) {
        ret = hw_max30001_ecg_enable();
        if (ret != 0) {
            LOG_ERR("Failed to enable ECG: %d", ret);
            smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_IDLE]);
            return;
        }
        k_timer_start(&tmr_ecg_sampling, K_MSEC(ECG_SAMPLING_INTERVAL_MS), K_MSEC(ECG_SAMPLING_INTERVAL_MS));
    }

    // Reset smoothing filter
    ecg_smooth_reset();

    // Start lead placement timeout
    lead_placement_wait_start = k_uptime_get();
    LOG_INF("ECG SMF: %d second timeout for lead placement", ECG_LEAD_PLACEMENT_TIMEOUT_S);

    // Initialize lead tracking - assume leads are off when entering this state
    smf_last_lead_off = true;

    // Reset lead ON debounce state
    smf_lead_on_debouncing = false;
    smf_lead_on_debounce_start = 0;

    // Signal display: leads are OFF
    k_sem_give(&sem_ecg_lead_off);

    // Publish status
    int duration = get_hrv_active() ? HRV_MEASUREMENT_DURATION_S : ECG_RECORD_DURATION_S;
    struct hpi_ecg_status_t ecg_stat = {
        .ts_complete = 0,
        .status = HPI_ECG_STATUS_STREAMING,
        .hr = 0,
        .progress_timer = duration + ECG_STABILIZATION_DURATION_S};
    zbus_chan_pub(&ecg_stat_chan, &ecg_stat, K_NO_WAIT);
}

static void st_ecg_wait_for_lead_run(void *o)
{
    // Check for timeout
    if (lead_placement_wait_start != 0) {
        int64_t elapsed_ms = k_uptime_get() - lead_placement_wait_start;
        if (elapsed_ms >= (ECG_LEAD_PLACEMENT_TIMEOUT_S * 1000)) {
            LOG_INF("ECG SMF: Lead placement timeout");
            lead_placement_wait_start = 0;
            k_sem_give(&sem_ecg_lead_timeout);
            ecg_cancellation = true; // To protect sample writing when timing out from WAIT_FOR_LEAD
            smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_IDLE]);
            return;
        }
    }

    // Poll sensor for lead status
    bool current_lead_off = get_ecg_lead_on_off();

    // Detect lead ON with debouncing to avoid false triggers from unstable contact
    if (!current_lead_off) {
        // Lead appears to be ON
        if (!smf_lead_on_debouncing) {
            // Start debounce timer
            smf_lead_on_debouncing = true;
            smf_lead_on_debounce_start = k_uptime_get();
            LOG_INF("ECG SMF: Lead ON detected - starting debounce timer");
        } else {
            // Check if debounce period elapsed
            int64_t elapsed = k_uptime_get() - smf_lead_on_debounce_start;
            if (elapsed >= ECG_LEAD_ON_DEBOUNCE_MS) {
                LOG_INF("ECG SMF: Lead ON confirmed after %lld ms - transitioning to RECORDING", elapsed);
                lead_placement_wait_start = 0;
                smf_lead_on_debouncing = false;
                smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_RECORDING]);
                return;
            }
        }
    } else {
        // Lead is OFF - cancel any ongoing debounce
        if (smf_lead_on_debouncing) {
            LOG_INF("ECG SMF: Lead ON debounce cancelled - lead went off");
            smf_lead_on_debouncing = false;
        }
    }
    smf_last_lead_off = current_lead_off;

    // Handle cancellation
    if (k_sem_take(&sem_ecg_cancel, K_NO_WAIT) == 0) {
        LOG_INF("ECG SMF: Cancelled in WAIT_FOR_LEAD");
        ecg_cancellation = true;
        smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_IDLE]);
        return;
    }
    if (k_sem_take(&sem_hrv_eval_cancel, K_NO_WAIT) == 0) {
        LOG_INF("ECG SMF: HRV cancelled in WAIT_FOR_LEAD");
        ecg_cancellation = true;
        smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_IDLE]);
        return;
    }
}

static void st_ecg_wait_for_lead_exit(void *o)
{
    LOG_DBG("ECG SMF: Exiting WAIT_FOR_LEAD state");
    lead_placement_wait_start = 0;
    smf_lead_on_debouncing = false;
    smf_lead_on_debounce_start = 0;
}

/*
 * =============================================================================
 * RECORDING STATE (merged: stabilization + recording + completion)
 * =============================================================================
 *
 * Two internal phases, tracked by ecg_stabilization_countdown:
 *   1. Stabilization phase (countdown > 0): signal settles, samples displayed
 *      but NOT buffered. Lead-off pauses the countdown (no state change).
 *   2. Recording phase (countdown <= 0): samples buffered for 30s ECG / 60s HRV.
 *      Lead-off pauses recording timer (no buffer discard, no state change).
 *
 * This eliminates the old bounce between STABILIZING ↔ WAIT_FOR_LEAD that
 * caused the "stuck at stabilising" bug when electrode contact was marginal.
 */
static bool recording_phase_started = false;  // true once stabilization is done

static void st_ecg_recording_entry(void *o)
{
    int duration = get_hrv_active() ? HRV_MEASUREMENT_DURATION_S : ECG_RECORD_DURATION_S;

    LOG_INF("ECG SMF: Entering RECORDING state (stabilize %ds then record %ds)",
            ECG_STABILIZATION_DURATION_S, duration);

    // Reset smoothing filter for fresh signal
    ecg_smooth_reset();

    // Initialize stabilization countdown — recording starts when this reaches 0
    set_ecg_stabilization_countdown(ECG_STABILIZATION_DURATION_S);
    recording_phase_started = false;

    // Initialize the per-second tick timer reference
    set_ecg_timer_values(k_uptime_get_32(), duration);

    // Drain any stale buffer-full semaphore from a previous aborted session
    k_sem_reset(&sem_ecg_complete);

    // Lead tracking — leads are ON when entering from WAIT_FOR_LEAD
    smf_last_lead_off = false;

    // Signal display: leads are ON
    k_sem_give(&sem_ecg_lead_on);

    // Clear HRV buffers
    if (get_hrv_active()) {
        hrv_interval_count = 0;
        memset(hrv_intervals, 0, sizeof(hrv_intervals));
        hrv_last_status_pub_s = 0;
    }

    // Publish initial status (stabilization + recording total)
    struct hpi_ecg_status_t ecg_stat = {
        .ts_complete = 0,
        .status = HPI_ECG_STATUS_STREAMING,
        .hr = 0,
        .progress_timer = duration + ECG_STABILIZATION_DURATION_S};
    zbus_chan_pub(&ecg_stat_chan, &ecg_stat, K_NO_WAIT);
}

static void st_ecg_recording_run(void *o)
{
    bool current_lead_off = get_ecg_lead_on_off();

    /* ----------------------------------------------------------
     * Lead-off / lead-on handling — NO state change, just pause
     * ---------------------------------------------------------- */
    if (!smf_last_lead_off && current_lead_off) {
        // Lead just went OFF
        LOG_INF("ECG SMF: Lead OFF during RECORDING - pausing");
        hpi_ecg_timer_pause();
        k_sem_give(&sem_ecg_lead_off);
    }
    if (smf_last_lead_off && !current_lead_off) {
        // Lead just came back ON
        LOG_INF("ECG SMF: Lead ON during RECORDING - resuming");
        // If we haven't started the recording phase yet, reset stabilization
        // so the full settling time runs with good contact
        if (!recording_phase_started) {
            set_ecg_stabilization_countdown(ECG_STABILIZATION_DURATION_S);
            LOG_INF("ECG SMF: Stabilization countdown reset to %d", ECG_STABILIZATION_DURATION_S);
        }
        k_sem_give(&sem_ecg_lead_on);
    }
    smf_last_lead_off = current_lead_off;

    // While leads are off, timers are frozen — skip countdown logic
    if (current_lead_off) {
        goto check_cancel;
    }

    /* ----------------------------------------------------------
     * Per-second tick
     * ---------------------------------------------------------- */
    uint32_t last_timer;
    int countdown;
    get_ecg_timer_values(&last_timer, &countdown);

    uint32_t now = k_uptime_get_32();
    if ((now - last_timer) < 1000) {
        goto check_buffer;
    }
    set_ecg_timer_values(now, countdown);  // update tick reference

    int stab = get_ecg_stabilization_countdown();

    if (stab > 0) {
        /* ---------- Stabilization phase ---------- */
        stab--;
        set_ecg_stabilization_countdown(stab);
        LOG_INF("ECG SMF: Stabilizing: %d", stab);

        int duration = get_hrv_active() ? HRV_MEASUREMENT_DURATION_S : ECG_RECORD_DURATION_S;
        struct hpi_ecg_status_t ecg_stat = {
            .ts_complete = 0,
            .status = HPI_ECG_STATUS_STREAMING,
            .hr = get_ecg_hr(),
            .progress_timer = duration + stab};
        zbus_chan_pub(&ecg_stat_chan, &ecg_stat, K_NO_WAIT);

        if (stab <= 0) {
            // Stabilization done — begin actual recording
            LOG_INF("ECG SMF: Stabilization complete - starting buffer recording");
            recording_phase_started = true;
            hpi_data_set_ecg_record_active(true);
            hpi_ecg_timer_start();

            // Re-init HRV collection at recording start
            if (get_hrv_active()) {
                hrv_interval_count = 0;
                memset(hrv_intervals, 0, sizeof(hrv_intervals));
                hrv_last_status_pub_s = 0;
            }
        }
    } else {
        /* ---------- Recording phase ---------- */
        countdown--;
        set_ecg_timer_values(now, countdown);

        LOG_INF("ECG SMF: Recording: %ds remaining", countdown);

        struct hpi_ecg_status_t ecg_stat = {
            .ts_complete = 0,
            .status = HPI_ECG_STATUS_STREAMING,
            .hr = get_ecg_hr(),
            .progress_timer = countdown};
        zbus_chan_pub(&ecg_stat_chan, &ecg_stat, K_NO_WAIT);

        if (countdown <= 0) {
            LOG_INF("ECG SMF: Recording complete (timer)");
            smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_IDLE]);
            return;
        }
    }

check_buffer:
    // Check for buffer full (only meaningful during recording phase)
    if (recording_phase_started && k_sem_take(&sem_ecg_complete, K_NO_WAIT) == 0) {
        LOG_INF("ECG SMF: Buffer full - completing");
        smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_IDLE]);
        return;
    }

check_cancel:
    // Handle cancellation
    if (k_sem_take(&sem_ecg_cancel, K_NO_WAIT) == 0) {
        LOG_INF("ECG SMF: Cancelled during RECORDING");
        ecg_cancellation = true;
        smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_IDLE]);
        return;
    }
    if (k_sem_take(&sem_hrv_eval_cancel, K_NO_WAIT) == 0) {
        LOG_INF("ECG SMF: HRV cancelled during RECORDING");
        ecg_cancellation = true;
        smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_IDLE]);
        return;
    }
}

static void st_ecg_recording_exit(void *o)
{
    LOG_INF("ECG SMF: Exiting RECORDING state");
    int ret;

    // Stop recording and write file if recording phase was active
    if (recording_phase_started) {
        if(!ecg_cancellation)
          hpi_data_set_ecg_record_active(false);
          //ecg_cancellation = false;  // reset cancellation flag for next session
    }

    hpi_ecg_timer_reset();

    // Only stop timers if GSR is also not active
    if (!get_gsr_active()) {
        k_timer_stop(&tmr_ecg_sampling);
        k_timer_stop(&tmr_bioz_sampling);
    }

    ret = hw_max30001_ecg_disable();
    if (ret != 0) {
        LOG_ERR("Failed to disable ECG in recording exit: %d", ret);
    }

    // Handle HRV post-processing if HRV evaluation was active
    if (get_hrv_active() && !ecg_cancellation) {
        LOG_INF("HRV evaluation complete - processing results");
        hpi_data_hrv_record_to_file(true);
        hpi_data_set_hrv_eval_active(false);
        k_sem_give(&sem_hrv_eval_complete);
        set_hrv_active(false);
        ecg_cancellation = true;  // prevent ecg sample writing after hrv record phase.
    } else if (recording_phase_started && !ecg_cancellation) {
        // ECG recording complete — signal display to show completion screen
        k_sem_give(&sem_ecg_complete_reset);

    }

    recording_phase_started = false;
    //smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_IDLE]);
}

/*
 * =============================================================================
 * STATE TABLE - Defines all ECG/HRV state machine states
 * =============================================================================
 * Flow:
 *   IDLE -> WAIT_FOR_LEAD -> RECORDING -> IDLE
 *
 * RECORDING has two internal phases:
 *   1. Stabilization (5s) — signal settles, lead-off pauses countdown
 *   2. Buffer recording (30s ECG / 60s HRV) — lead-off pauses timer
 */
static const struct smf_state ecg_states[] = {
    [HPI_ECG_STATE_IDLE] = SMF_CREATE_STATE(st_ecg_idle_entry, st_ecg_idle_run, NULL, NULL, NULL),
    [HPI_ECG_STATE_WAIT_FOR_LEAD] = SMF_CREATE_STATE(st_ecg_wait_for_lead_entry, st_ecg_wait_for_lead_run, st_ecg_wait_for_lead_exit, NULL, NULL),
    [HPI_ECG_STATE_RECORDING] = SMF_CREATE_STATE(st_ecg_recording_entry, st_ecg_recording_run, st_ecg_recording_exit, NULL, NULL),

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
