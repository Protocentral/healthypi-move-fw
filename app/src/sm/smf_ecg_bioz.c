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
#include <zephyr/sys/timeutil.h>

#include "max30001.h"
#include "hpi_common_types.h"
#include "hw_module.h"
#include "hpi_dfu.h"
#include "ui/move_ui.h"
#include "hpi_sys.h"
#include "hpi_user_settings_api.h"
#include "hpi_watchdog.h"
#include "hpi_evt.h"

/* ECG/BioZ/HRV/GSR cross-module event object (see hpi_evt.h). Replaces the
 * former semaphore web between the UI screens, data_module, hw_module and
 * this SMF / the display. */
K_EVENT_DEFINE(ecg_evt);

LOG_MODULE_REGISTER(smf_ecg, LOG_LEVEL_DBG);

SENSOR_DT_READ_IODEV(max30001_iodev, DT_ALIAS(max30001), SENSOR_CHAN_VOLTAGE);

K_MSGQ_DEFINE(q_ecg_sample, sizeof(struct hpi_ecg_bioz_sensor_data_t), 64, 1);  // Reduced from 128 to 64
/* Lightweight queue for BioZ-only samples to reduce copy overhead when ECG not needed */
K_MSGQ_DEFINE(q_bioz_sample, sizeof(struct hpi_bioz_sample_t), 64, 1);

/* ECG start/cancel/complete and lead signals are now k_event bits on `ecg_evt`
 * (see hpi_evt.h). Only the internal lead-debounce semaphores remain below. */
K_SEM_DEFINE(sem_ecg_lon, 0, 1);
K_SEM_DEFINE(sem_ecg_loff, 0, 1);

// GSR (BioZ) Control Semaphores - Independent from ECG

// GSR Measurement Timing (60 seconds for reliable stress index)
#define GSR_MEASUREMENT_DURATION_S 30
static int64_t gsr_measurement_start_time = 0;
static bool gsr_measurement_in_progress = false;
static uint32_t gsr_last_status_pub_s = 0; // Last published elapsed seconds

// RTOS-safe lead contact state using atomic operations
// true = leads are in contact (on skin), false = leads are off
// static atomic_t hrv_lead_contact = ATOMIC_INIT(0);  // 0 = no contact, 1 = contact
// static atomic_t hrv_prev_lead_contact = ATOMIC_INIT(0);


K_SEM_DEFINE(sem_ecg_lead_on_local, 0, 1);
K_SEM_DEFINE(sem_ecg_lead_off_local, 0, 1);



ZBUS_CHAN_DECLARE(gsr_status_chan);

// Semaphore for lead reconnection during recording (triggers re-stabilization)
K_SEM_DEFINE(sem_ecg_lead_on_stabilize, 0, 1);

// Lead off debounce (500ms) to avoid false triggers
#define ECG_LEAD_OFF_DEBOUNCE_MS 500
static int64_t lead_off_debounce_start = 0;
static bool lead_off_debouncing = false;

/* Grace period tolerated when leads are lost mid-measurement (STABILIZING /
 * RECORDING) before the spot check is aborted. During the grace the countdown
 * is frozen and a "leads off - reconnect" warning is shown; if contact returns
 * within the window the measurement resumes, otherwise it aborts to IDLE. This
 * is on top of the 500 ms sensor-layer debounce, so momentary loss is tolerated
 * without cancelling the recording. */
#define ECG_LEAD_OFF_GRACE_MS 2500
static int64_t ecg_leadoff_grace_start = 0;

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

/* How long COMPLETE is held before falling back to IDLE, so the "ECG RECORDED"
 * confirmation is on screen long enough to read. */
#define ECG_COMPLETE_DWELL_MS 4000
static uint32_t ecg_complete_entry_ms;
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

static int ecg_last_timer_val = 0;
static int ecg_countdown_val = 0;
static int ecg_stabilization_countdown = 0;
static bool ecg_stabilization_complete = false;
bool ecg_cancellation = false;  // Flag to indicate if current ECG session was cancelled

// uint32_t gsr_countdown_val = 0;
// uint32_t gsr_last_timer_val = 0;
static int gsr_countdown_val = 0;
static uint32_t gsr_last_timer_val = 0;   /* tick deadline, wrap-safe unsigned delta */
K_MUTEX_DEFINE(gsr_timer_mutex);

static const struct smf_state ecg_states[];
struct s_ecg_object
{
    struct smf_ctx ctx;
} s_ecg_obj;

enum ecg_state
{
    HPI_ECG_STATE_IDLE,
    HPI_ECG_STATE_WAIT_FOR_LEAD,   // Waiting for user to place fingers on electrodes
    HPI_ECG_STATE_STABILIZING,      // 5 second stabilization after lead contact
    HPI_ECG_STATE_RECORDING,        // Active recording (30 seconds countdown)
    HPI_ECG_STATE_COMPLETE,

    HPI_ECG_STATE_GSR_MEASURE_ENTRY,
    HPI_ECG_STATE_GSR_MEASURE_STREAM,
    HPI_ECG_STATE_GSR_COMPLETE,
};

RTIO_DEFINE(max30001_read_rtio_poll_ctx, 1, 1);

static bool ecg_active = false;
static bool gsr_active = false;  // Independent GSR (BioZ) state
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

/**
 * @brief Configure ECG leads based on hand worn setting
 * @return 0 on success, negative error code on failure
 */
static int hw_max30001_configure_leads(void)
{
    struct sensor_value lead_config;
    uint8_t hand_worn = hpi_user_settings_get_hand_worn();

    /* Set lead configuration: 0 = Left hand, 1 = Right hand
    But to get inverted output for default handworn setting(left hand), we need to invert the values
    so that Left => Inverted ECG output(1), Right => Non-inverted ECG output(0)*/
    lead_config.val1 = !hand_worn; 
    lead_config.val2 = 0;

    int ret = sensor_attr_set(max30001_dev, SENSOR_CHAN_ALL, MAX30001_ATTR_LEAD_CONFIG, &lead_config);
    if (ret < 0) {
        LOG_ERR("Failed to set ECG lead configuration for %s hand (err %d)", hand_worn ? "right" : "left", ret);
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

// Function to reset ECG timer countdown to full duration
void hpi_ecg_reset_countdown_timer(void)
{
    int duration = ECG_RECORD_DURATION_S;

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

/* Publish a GSR status frame. Centralizes the gsr_status_chan publish the display
 * thread renders the inline EDA monitor phase from - mirrors ecg_pub_stream(). */
static void gsr_pub_status(uint8_t status, int remaining, bool contact)
{
    struct hpi_gsr_status_t s = {
        .elapsed_s = (uint16_t)(GSR_MEASUREMENT_DURATION_S - remaining),
        .remaining_s = (uint16_t)remaining,
        .total_s = GSR_MEASUREMENT_DURATION_S,
        .active = (status == HPI_GSR_STATUS_STREAMING),
        .contact = contact,
        .status = status,
    };
    zbus_chan_pub(&gsr_status_chan, &s, K_NO_WAIT);
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

    /* DFU quiesce: don't touch the MAX30001 during a BLE OTA (free the bus/CPU
     * for the SMP + QSPI write path). Resumes when the flag clears. */
    if (hpi_dfu_is_active()) {
        return;
    }

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
      //  LOG_DBG("BIOZ Lead OFF detected (no skin contact)");
        k_event_post(&ecg_evt, EVT_GSR_LEAD_OFF);
        set_gsr_lead_on_off(true);
         gsr_contact_ok = false;

    } else {

       // LOG_DBG("BIOZ Lead ON detected (skin contact OK)");
        k_event_post(&ecg_evt, EVT_GSR_LEAD_ON);
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

    /* DFU quiesce (see work_ecg_sample_handler). */
    if (hpi_dfu_is_active()) {
        return;
    }

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

/* P3: when the MAX30001 DRDY (INTB) interrupt drives acquisition, the fast
 * sampling timers are not started. This is a *runtime* flag (not a compile-time
 * switch) so that if the driver's trigger init fails at boot, we transparently
 * fall back to the poll timers instead of going silent. In non-trigger builds
 * it is a compile-time false and the guarded timer starts stay unconditional. */
#ifdef CONFIG_MAX30001_TRIGGER
static bool s_use_drdy_trigger;

/* Runs in GPIO-callback (ISR) context. EINT (ECG FIFO) and BINT (BioZ FIFO)
 * both route to INTB, so submit the read matching the active mode: the full ECG
 * decode drains both ECG+BioZ; the BioZ-only path covers GSR-only streaming. A
 * spurious edge is harmless - sensor_read on an idle FIFO returns 0 samples. */
static void max30001_drdy_handler(const struct device *dev)
{
    ARG_UNUSED(dev);
    if (get_ecg_active())
    {
        k_work_submit(&work_ecg_sample);
    }
    else if (get_gsr_active())
    {
        k_work_submit(&work_bioz_sample);
    }
}
#else
static const bool s_use_drdy_trigger = false;
#endif

static inline void ecg_start_sampling(void)
{
    if (!s_use_drdy_trigger)
    {
        k_timer_start(&tmr_ecg_sampling, K_MSEC(ECG_SAMPLING_INTERVAL_MS), K_MSEC(ECG_SAMPLING_INTERVAL_MS));
    }
}

static inline void bioz_start_sampling(void)
{
    if (!s_use_drdy_trigger)
    {
        k_timer_start(&tmr_bioz_sampling, K_MSEC(BIOZ_SAMPLING_INTERVAL_MS), K_MSEC(BIOZ_SAMPLING_INTERVAL_MS));
    }
}

/* P2 error/recovery: the MAX30001 enable/disable ops are blocking SPI attr
 * writes and can fail on a transient bus glitch. A failed enable used to hard-
 * fail the measurement (ECG silently aborted to idle; GSR spun in its entry
 * state retrying forever with no backoff). Wrap the critical commands with
 * bounded retry + exponential backoff and track a consecutive-failure streak so
 * a wedged sensor becomes visible -- mirrors the MAX32664C wrist-hub pattern in
 * smf_ppg_wrist.c. Backoff runs only on failure, so the normal path is unchanged. */
#define ECG_SENS_CMD_RETRIES 3
static atomic_t ecg_sens_err_streak = ATOMIC_INIT(0);

static int ecg_sens_attr_set(enum sensor_attribute attr, int val1, const char *what)
{
    struct sensor_value sv = { .val1 = val1, .val2 = 0 };
    int ret = 0;
    uint32_t backoff = 20;
    for (int attempt = 0; attempt <= ECG_SENS_CMD_RETRIES; attempt++)
    {
        ret = sensor_attr_set(max30001_dev, SENSOR_CHAN_ALL, attr, &sv);
        if (ret == 0)
        {
            atomic_set(&ecg_sens_err_streak, 0);
            return 0;
        }
        LOG_WRN("MAX30001 %s=%d failed (%d) [try %d/%d]",
                what, val1, ret, attempt + 1, ECG_SENS_CMD_RETRIES + 1);
        if (attempt < ECG_SENS_CMD_RETRIES)
        {
            k_msleep(backoff);
            backoff *= 2; /* exponential backoff */
        }
    }
    long streak = atomic_add(&ecg_sens_err_streak, 1) + 1;
    LOG_ERR("MAX30001 unresponsive: %s=%d failed after %d tries (err streak %ld)",
            what, val1, ECG_SENS_CMD_RETRIES + 1, streak);
    return ret;
}

static int hw_max30001_bioz_enable(void) __attribute__((unused));
static int hw_max30001_bioz_enable(void)
{
    struct sensor_value bioz_mode_set;
    bioz_mode_set.val1 = 1;
    return sensor_attr_set(max30001_dev, SENSOR_CHAN_ALL, MAX30001_ATTR_BIOZ_ENABLED, &bioz_mode_set);
}

static int hw_max30001_ecg_enable(void)
{
    int ret = ecg_sens_attr_set(MAX30001_ATTR_ECG_ENABLED, 1, "ecg_enable");
    if (ret == 0) {
        set_ecg_active(true);

        // Configure leads based on hand worn setting
        int lead_ret = hw_max30001_configure_leads();
        if (lead_ret != 0) {
            LOG_WRN("Failed to configure ECG leads, using default configuration");
        }
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
    int ret = ecg_sens_attr_set(MAX30001_ATTR_BIOZ_ENABLED, 1, "gsr_enable");
    if (ret == 0) {
        set_gsr_active(true);
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

    bioz_start_sampling();
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

    // Reset ECG state flags
    hpi_data_set_ecg_record_active(false);
    hpi_ecg_timer_reset();
    ecg_cancellation = false;

    /* Announce IDLE so the UI (inline ECG monitor) resyncs to its idle view
     * whenever the SMF returns to idle - completion, cancel, lead-off abort or
     * lead-placement timeout. Without this the monitor's own g_ecg_active can
     * drift out of sync with the SMF (stale "measuring" / invisible capture). */
    struct hpi_ecg_status_t idle_stat = {
        .ts_complete = 0,
        .status = HPI_ECG_STATUS_IDLE,
        .hr = 0,
        .progress_timer = 0};
    zbus_chan_pub(&ecg_stat_chan, &idle_stat, K_NO_WAIT);
}

static enum smf_state_result st_ecg_idle_run(void *o)
{
    // Handle ECG start request
    if (hpi_evt_consume(&ecg_evt, EVT_ECG_START))
    {
        /* A fresh Start supersedes any stale Cancel that lingers set - e.g. a
         * UI cancel that landed *after* the 30 s auto-complete already returned
         * us to idle (idle never consumes the Cancel bits). Left set, the very
         * next state (WAIT_FOR_LEAD) would consume it and abort this new
         * measurement immediately. Clear them so Start begins from a clean slate. */
        k_event_clear(&ecg_evt, EVT_ECG_CANCEL);
        LOG_INF("ECG SMF: ECG start requested - transitioning to WAIT_FOR_LEAD");
        smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_WAIT_FOR_LEAD]);
    }

    // Handle GSR start request
    if (hpi_evt_consume(&ecg_evt, EVT_GSR_START))
    {
        smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_GSR_MEASURE_ENTRY]);
    }
    return SMF_EVENT_HANDLED;
}

static enum smf_state_result st_gsr_entry_run(void *o)
{
    ARG_UNUSED(o);
    
    LOG_INF("Starting GSR (BioZ) measurement for %d seconds", GSR_MEASUREMENT_DURATION_S);

    int ret = hw_max30001_gsr_enable();

    if (ret != 0)
    {
        /* Bail to idle instead of spinning in this entry state re-issuing the
         * enable every loop with no backoff (the helper already did bounded
         * retry + backoff before returning failure). */
        LOG_ERR("GSR enable failed after retries - aborting to idle");
        gsr_pub_status(HPI_GSR_STATUS_IDLE, 0, false);   /* release the tile's measuring view */
        smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_IDLE]);
        return SMF_EVENT_HANDLED;
    }
       
    hpi_data_set_gsr_measurement_active(true);
    hpi_data_set_gsr_record_active(true);
    gsr_measurement_in_progress = true;

   // remaining_timer_s = GSR_MEASUREMENT_DURATION_S;
    k_mutex_lock(&gsr_timer_mutex, K_FOREVER);
    gsr_countdown_val = GSR_MEASUREMENT_DURATION_S;
    /* Arm the tick deadline. Without this it keeps the previous capture's stamp
     * and the first second is consumed the moment the stream state runs. */
    gsr_last_timer_val = k_uptime_get_32();
    k_mutex_unlock(&gsr_timer_mutex);
    prev_gsr_contact_ok =  gsr_contact_ok;  // reset state

    bioz_start_sampling();

    bool current_lead_off = get_gsr_lead_on_off();
    if (current_lead_off) {
        k_event_post(&ecg_evt, EVT_GSR_LEAD_OFF);
    } else {
        k_event_post(&ecg_evt, EVT_GSR_LEAD_ON);
    }

    /* Announce the capture so the inline EDA monitor leaves its idle view. */
    gsr_pub_status(HPI_GSR_STATUS_STREAMING, GSR_MEASUREMENT_DURATION_S, !current_lead_off);

    smf_set_state(SMF_CTX(&s_ecg_obj),
                  &ecg_states[HPI_ECG_STATE_GSR_MEASURE_STREAM]);
    return SMF_EVENT_HANDLED;
}


static enum smf_state_result st_gsr_stream_run(void *o)
{
    ARG_UNUSED(o);

    bool contact_ok = (get_gsr_lead_on_off() == 0); // TRUE when skin contact

    /* Contact lost mid-capture: the partial trace is unusable, so discard it and
     * restart the countdown (which then stays frozen until contact returns).
     * This used to live in the display thread's legacy GSR plot-screen case, i.e.
     * it only ran while that screen happened to be up - it belongs with the capture. */
    if (prev_gsr_contact_ok && !contact_ok) {
        LOG_INF("GSR SMF: skin contact lost - discarding partial capture");
        hpi_data_reset_gsr_record_buffer();
        k_mutex_lock(&gsr_timer_mutex, K_FOREVER);
        gsr_countdown_val = GSR_MEASUREMENT_DURATION_S;
        gsr_last_timer_val = k_uptime_get_32();
        k_mutex_unlock(&gsr_timer_mutex);
    }

    k_mutex_lock(&gsr_timer_mutex, K_FOREVER);

    if (!contact_ok) {
        // Timer frozen when no skin contact - no logging needed (high frequency)
    } else {
        // Decrement timer every second
        uint32_t now = k_uptime_get_32();
        if ((now - gsr_last_timer_val) >= 1000) {
            /* Advance the deadline by exactly one second rather than resampling
             * the clock: the run loop notices the tick late, and resampling would
             * discard that lateness every tick and drift the countdown slow. */
            gsr_last_timer_val += 1000;
            if (gsr_countdown_val > 0) {
                gsr_countdown_val--;
            }
        }
    }

    int remaining = gsr_countdown_val;

    k_mutex_unlock(&gsr_timer_mutex);

    gsr_pub_status(HPI_GSR_STATUS_STREAMING, remaining, contact_ok);

    // Complete state check
    if (gsr_countdown_val <= 0 && contact_ok) {
        smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_GSR_COMPLETE]);
    }


    // Handle semaphores
    if (hpi_evt_consume(&ecg_evt, EVT_GSR_COMPLETE)) {
        LOG_INF("GSR SMF: Buffer full signal received - switching to COMPLETE state");
        ecg_cancellation = true;
        smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_GSR_COMPLETE]);
    }

    if (hpi_evt_consume(&ecg_evt, EVT_GSR_CANCEL)) {
        LOG_DBG("GSR cancelled");
        ecg_cancellation = true;
        /* Flag the H-REC session as cancelled before anything touches it:
         * reset_buffer() must not reopen a fresh record, and
         * set_..._active(false) must drop it rather than store it.
         * `ecg_cancellation` cannot serve here - it is also set true on a
         * *successful* GSR completion (the EVT_GSR_COMPLETE branch above). */
        hpi_data_set_gsr_cancelled(true);
        hpi_data_reset_gsr_record_buffer();
        hpi_data_set_gsr_record_active(false);
        gsr_pub_status(HPI_GSR_STATUS_IDLE, 0, false);   /* tile back to its idle view */
        smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_IDLE]);
    }

    prev_gsr_contact_ok = contact_ok;
    return SMF_EVENT_HANDLED;
}
static void st_gsr_stream_exit(void *o)
{
    LOG_DBG("BioZ SM Stream Exit");
     // k_timer_stop(&tmr_bioz_sampling);
   if (true) /* recording removed — always finalize GSR measurement */
    {
        hpi_data_set_gsr_measurement_active(false);
        k_timer_stop(&tmr_bioz_sampling);
        int  ret = hw_max30001_gsr_disable();

        if (ret != 0) {
            LOG_ERR("Failed to disable GSR in complete : %d", ret);
        }
    }
}

static enum smf_state_result st_gsr_complete_run(void *o)
{
    LOG_INF("GSR COMPLETE");
   // hpi_data_set_gsr_measurement_active(false);
    hpi_data_set_gsr_record_active(false);

    /* The tile stops rendering the capture; the display thread routes EVT_GSR_RESET
     * to the v2 results screen (SCR_SPL_GSR_COMPLETE), which owns the result view. */
    gsr_pub_status(HPI_GSR_STATUS_COMPLETE, 0, true);

    k_event_post(&ecg_evt, EVT_GSR_RESET);

    smf_set_state(SMF_CTX(&s_ecg_obj),
                  &ecg_states[HPI_ECG_STATE_IDLE]);
    return SMF_EVENT_HANDLED;
}

/*
 * =============================================================================
 * ECG/HRV STATE MACHINE - REDESIGNED
 * =============================================================================
 *
 * ARCHITECTURE:
 * - SMF polls sensor state directly via get_ecg_lead_on_off()
 * - SMF is the ONLY entity that signals display thread (sem_ecg_lead_on/off)
 * - Sensor layer only updates state variable, does NOT signal semaphores
 * - This eliminates race conditions between display thread and SMF
 *
 * FLOW:
 *   IDLE -> WAIT_FOR_LEAD -> STABILIZING -> RECORDING -> COMPLETE -> IDLE
 *              ^                  |            |
 *              |                  v            v
 *              +------------------+------------+
 *                    (on lead off)
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
            LOG_ERR("ECG enable failed after retries - aborting to idle");
            /* Surface the failure instead of silently vanishing to idle. The
             * display does not render this status yet (forward hook), but the
             * error is now on the channel + logged as a fault streak. */
            struct hpi_ecg_status_t err_stat = {
                .ts_complete = 0,
                .status = HPI_ECG_STATUS_ERROR,
                .hr = 0,
                .progress_timer = 0};
            zbus_chan_pub(&ecg_stat_chan, &err_stat, K_NO_WAIT);
            smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_IDLE]);
            return;
        }
        ecg_start_sampling();
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
    k_event_post(&ecg_evt, EVT_ECG_LEAD_OFF);

    // Publish status: WAITING for initial lead contact. A dedicated sentinel
    // (not the stabilize/record countdown range) lets the inline monitor show a
    // "place fingers on electrodes" prompt distinct from the stabilize count.
    struct hpi_ecg_status_t ecg_stat = {
        .ts_complete = 0,
        .status = HPI_ECG_STATUS_STREAMING,
        .hr = 0,
        .progress_timer = HPI_ECG_UI_WAIT_LEADS};
    zbus_chan_pub(&ecg_stat_chan, &ecg_stat, K_NO_WAIT);
}

static enum smf_state_result st_ecg_wait_for_lead_run(void *o)
{
    // Check for timeout
    if (lead_placement_wait_start != 0) {
        int64_t elapsed_ms = k_uptime_get() - lead_placement_wait_start;
        if (elapsed_ms >= (ECG_LEAD_PLACEMENT_TIMEOUT_S * 1000)) {
            LOG_INF("ECG SMF: Lead placement timeout");
            lead_placement_wait_start = 0;
            k_event_post(&ecg_evt, EVT_ECG_LEAD_TIMEOUT);
            smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_IDLE]);
            return SMF_EVENT_HANDLED;
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
                LOG_INF("ECG SMF: Lead ON confirmed after %lld ms - transitioning to STABILIZING", elapsed);
                lead_placement_wait_start = 0;
                smf_lead_on_debouncing = false;
                smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_STABILIZING]);
                return SMF_EVENT_HANDLED;
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
    if (hpi_evt_consume(&ecg_evt, EVT_ECG_CANCEL)) {
        LOG_INF("ECG SMF: Cancelled in WAIT_FOR_LEAD");
        ecg_cancellation = true;
        smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_IDLE]);
        return SMF_EVENT_HANDLED;
    }
    return SMF_EVENT_HANDLED;
}

static void st_ecg_wait_for_lead_exit(void *o)
{
    LOG_DBG("ECG SMF: Exiting WAIT_FOR_LEAD state");
    lead_placement_wait_start = 0;
    smf_lead_on_debouncing = false;
    smf_lead_on_debounce_start = 0;
}

/*
 * STABILIZING STATE
 * - 5 second countdown while leads remain connected
 * - If leads go off, return to WAIT_FOR_LEAD
 */
/* Publish a STREAMING status with a given progress_timer (real countdown or a
 * UI sentinel like HPI_ECG_UI_LEADS_OFF). Centralizes the ecg_stat_chan publish
 * the display thread renders the monitor phase from. */
static void ecg_pub_stream(uint16_t progress)
{
    struct hpi_ecg_status_t s = {
        .ts_complete = 0,
        .status = HPI_ECG_STATUS_STREAMING,
        .hr = get_ecg_hr(),
        .progress_timer = progress};
    zbus_chan_pub(&ecg_stat_chan, &s, K_NO_WAIT);
}

/*
 * Shared lead-off grace handler for the STABILIZING / RECORDING run loops.
 * Returns true if the caller should stop this tick (leads are off, or we just
 * aborted) - the caller must `return SMF_EVENT_HANDLED`. Returns false when
 * leads are on and the measurement should proceed (resuming from grace if it
 * had been in one). `resume_progress` is the progress_timer to re-publish when
 * contact returns so the UI leaves the "leads off" warning.
 */
static bool ecg_leadoff_grace(uint16_t resume_progress, const char *phase)
{
    if (get_ecg_lead_on_off()) {   /* leads OFF */
        if (ecg_leadoff_grace_start == 0) {
            ecg_leadoff_grace_start = k_uptime_get();
            LOG_INF("ECG SMF: Lead OFF during %s - %d ms grace", phase, ECG_LEAD_OFF_GRACE_MS);
            ecg_pub_stream(HPI_ECG_UI_LEADS_OFF);   /* show "reconnect" warning */
        } else if ((k_uptime_get() - ecg_leadoff_grace_start) >= ECG_LEAD_OFF_GRACE_MS) {
            LOG_INF("ECG SMF: Lead-off grace expired (%s) - aborting to IDLE", phase);
            k_event_post(&ecg_evt, EVT_ECG_LEADOFF_ABORT);
            ecg_cancellation = true;
            ecg_leadoff_grace_start = 0;
            smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_IDLE]);
        }
        return true;   /* off (or aborted): freeze this tick */
    }
    if (ecg_leadoff_grace_start != 0) {   /* leads returned within grace */
        LOG_INF("ECG SMF: Leads reconnected within grace - resuming %s", phase);
        ecg_leadoff_grace_start = 0;
        ecg_pub_stream(resume_progress);  /* leave the warning, restore the phase view */
    }
    return false;
}

static void st_ecg_stabilizing_entry(void *o)
{
    LOG_INF("ECG SMF: Entering STABILIZING state - %d seconds", ECG_STABILIZATION_DURATION_S);
    ecg_leadoff_grace_start = 0;

    // Reset smoothing filter
    ecg_smooth_reset();

    // Initialize stabilization countdown
    set_ecg_stabilization_values(ECG_STABILIZATION_DURATION_S, false);
    set_ecg_timer_values(k_uptime_get_32(), 0);

    // Lead tracking - leads are ON when entering stabilizing
    smf_last_lead_off = false;

    // Signal display: leads are ON
    k_event_post(&ecg_evt, EVT_ECG_LEAD_ON);

    // Publish status
    int duration = ECG_RECORD_DURATION_S;
    struct hpi_ecg_status_t ecg_stat = {
        .ts_complete = 0,
        .status = HPI_ECG_STATUS_STREAMING,
        .hr = 0,
        .progress_timer = duration + ECG_STABILIZATION_DURATION_S};
    zbus_chan_pub(&ecg_stat_chan, &ecg_stat, K_NO_WAIT);
}

static enum smf_state_result st_ecg_stabilizing_run(void *o)
{
    // Explicit cancel wins in every phase, including during a leads-off warning.
    if (hpi_evt_consume(&ecg_evt, EVT_ECG_CANCEL)) {
        LOG_INF("ECG SMF: Cancelled during STABILIZING");
        ecg_cancellation = true;
        ecg_leadoff_grace_start = 0;
        smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_IDLE]);
        return SMF_EVENT_HANDLED;
    }

    // Lead-off grace: tolerate momentary contact loss (freeze + warn), abort
    // only if leads stay off past the grace window.
    int stabilization_countdown;
    get_ecg_stabilization_values(&stabilization_countdown, NULL);
    int stab_duration = ECG_RECORD_DURATION_S;
    if (ecg_leadoff_grace(stab_duration + stabilization_countdown, "STABILIZING")) {
        set_ecg_timer_values(k_uptime_get_32(), 0);   /* freeze the stabilize clock */
        return SMF_EVENT_HANDLED;
    }

    // Count down stabilization timer
    uint32_t last_timer;
    get_ecg_timer_values(&last_timer, NULL);

    uint32_t now = k_uptime_get_32();
    if ((now - last_timer) >= 1000) {
        stabilization_countdown--;
        LOG_INF("ECG SMF: Stabilization: %d", stabilization_countdown);

        /* Advance the deadline by exactly one second rather than resampling the
         * clock: the run loop notices the tick late, and resampling would discard
         * that lateness every tick and drift the countdown slow. */
        set_ecg_timer_values(last_timer + 1000, 0);
        set_ecg_stabilization_values(stabilization_countdown, false);

        // Publish progress
        int duration = ECG_RECORD_DURATION_S;
        struct hpi_ecg_status_t ecg_stat = {
            .ts_complete = 0,
            .status = HPI_ECG_STATUS_STREAMING,
            .hr = get_ecg_hr(),
            .progress_timer = duration + stabilization_countdown};
        zbus_chan_pub(&ecg_stat_chan, &ecg_stat, K_NO_WAIT);

        if (stabilization_countdown <= 0) {
            LOG_INF("ECG SMF: Stabilization complete - transitioning to RECORDING");
            smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_RECORDING]);
            return SMF_EVENT_HANDLED;
        }
    }

    return SMF_EVENT_HANDLED;   /* cancel handled at top of run */
}

static void st_ecg_stabilizing_exit(void *o)
{
    LOG_DBG("ECG SMF: Exiting STABILIZING state");
    set_ecg_stabilization_values(0, true);
}

/*
 * RECORDING STATE
 * - Active recording with countdown (ECG_RECORD_DURATION_S)
 * - If leads go off, reset buffer and return to WAIT_FOR_LEAD
 */
static void st_ecg_recording_entry(void *o)
{
    int duration = ECG_RECORD_DURATION_S;

    LOG_INF("ECG SMF: Entering RECORDING state - %d seconds", duration);
    ecg_leadoff_grace_start = 0;

    // Start recording
    hpi_data_set_ecg_record_active(true);

    // Initialize countdown timer
    set_ecg_timer_values(k_uptime_get_32(), duration);

    // Start UI timer
    hpi_ecg_timer_start();

    // Lead tracking - leads are ON when entering recording
    smf_last_lead_off = false;

    // Publish initial status
    struct hpi_ecg_status_t ecg_stat = {
        .ts_complete = 0,
        .status = HPI_ECG_STATUS_STREAMING,
        .hr = get_ecg_hr(),
        .progress_timer = duration};
    zbus_chan_pub(&ecg_stat_chan, &ecg_stat, K_NO_WAIT);
}

static enum smf_state_result st_ecg_recording_run(void *o)
{
    // Explicit cancel / buffer-full win in every phase, incl. a leads-off warn.
    if (hpi_evt_consume(&ecg_evt, EVT_ECG_CANCEL)) {
        LOG_INF("ECG SMF: Cancelled during RECORDING");
        ecg_cancellation = true;
        ecg_leadoff_grace_start = 0;
        smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_IDLE]);
        return SMF_EVENT_HANDLED;
    }
    if (hpi_evt_consume(&ecg_evt, EVT_ECG_COMPLETE)) {
        LOG_INF("ECG SMF: Buffer full - completing");
        ecg_cancellation = false; // Not a cancellation, just normal completion
        smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_COMPLETE]);
        return SMF_EVENT_HANDLED;
    }

    // Count down recording timer
    uint32_t last_timer;
    int countdown;
    get_ecg_timer_values(&last_timer, &countdown);

    // Lead-off grace: tolerate momentary contact loss (freeze the countdown +
    // show the "reconnect" warning); abort + discard only if leads stay off
    // past the grace window.
    if (ecg_leadoff_grace((uint16_t)countdown, "RECORDING")) {
        if (ecg_leadoff_grace_start == 0) {   /* grace just expired -> aborted */
            hpi_ecg_timer_pause();
            hpi_data_reset_ecg_record_buffer();
        } else {
            set_ecg_timer_values(k_uptime_get_32(), countdown);   /* freeze */
        }
        return SMF_EVENT_HANDLED;
    }

    uint32_t now = k_uptime_get_32();
    if ((now - last_timer) >= 1000) {
        countdown--;
        /* Advance the deadline by exactly one second rather than resampling the
         * clock: the run loop notices the tick late, and resampling would discard
         * that lateness every tick, drifting the countdown slow until the buffer
         * fills (a fixed 3840 samples) while the UI still shows seconds left. */
        set_ecg_timer_values(last_timer + 1000, countdown);

        LOG_INF("ECG SMF: Recording: %ds remaining", countdown);

        struct hpi_ecg_status_t ecg_stat = {
            .ts_complete = 0,
            .status = HPI_ECG_STATUS_STREAMING,
            .hr = get_ecg_hr(),
            .progress_timer = countdown};
        zbus_chan_pub(&ecg_stat_chan, &ecg_stat, K_NO_WAIT);

        if (countdown <= 0) {
            LOG_INF("ECG SMF: Recording complete");
            smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_COMPLETE]);
            return SMF_EVENT_HANDLED;
        }
    }

    return SMF_EVENT_HANDLED;   /* cancel / complete handled at top of run */
}

static void st_ecg_recording_exit(void *o)
{
    LOG_INF("ECG SMF: Exiting RECORDING state");
    hpi_ecg_timer_reset();
}

static void st_ecg_complete_entry(void *o)
{
    LOG_INF("ECG SMF: Entering COMPLETE state");
    int ret;

    /* Latch before the branches below mutate the flag. */
    const bool success = !ecg_cancellation;

    ecg_complete_entry_ms = k_uptime_get_32();

    // Stop recording - this is the successful completion path
    hpi_data_set_ecg_record_active(false);

    // Only stop timer if GSR is also not active
    if (!get_gsr_active()) {
        k_timer_stop(&tmr_ecg_sampling);
        k_timer_stop(&tmr_bioz_sampling);
    }

    ret = hw_max30001_ecg_disable();
    if (ret != 0) {
        LOG_ERR("Failed to disable ECG in complete entry: %d", ret);
    }

    // ECG recording complete - signal ECG completion
    if (!ecg_cancellation)
    {
        k_event_post(&ecg_evt, EVT_ECG_RESET);
        ecg_cancellation = true; // To avoid duplicate file write in idle state
    }

    /* Announce COMPLETE so the inline monitor can show its "ECG RECORDED" tick
     * and latch the session HR. The final HR is what the health store records
     * (hs_ecg_listener gates on COMPLETE); the display path also needs it —
     * publishing 0 here used to wipe m_disp_ecg_hr / subj_ecg back to "--"
     * right after a successful capture. */
    if (success) {
        struct hpi_ecg_status_t done_stat = {
            .ts_complete = hw_get_sys_time_ts(),
            .status = HPI_ECG_STATUS_COMPLETE,
            .hr = get_ecg_hr(),
            .progress_timer = 0};
        zbus_chan_pub(&ecg_stat_chan, &done_stat, K_NO_WAIT);
    }
}

static enum smf_state_result st_ecg_complete_run(void *o)
{
    // Handle GSR start/stop during ECG complete phase
    if (hpi_evt_consume(&ecg_evt, EVT_GSR_START))
    {
        LOG_INF("Starting GSR during ECG complete phase");
        hw_max30001_gsr_enable();
        hpi_data_set_gsr_measurement_active(true);
    }
    
    if (hpi_evt_consume(&ecg_evt, EVT_GSR_CANCEL))
    {
        LOG_INF("Stopping GSR during ECG complete phase");
        hw_max30001_gsr_disable();
        hpi_data_set_gsr_measurement_active(false);
    }
    
    /* Hold COMPLETE briefly so the confirmation is readable, then fall back to
     * IDLE. A fresh Start cuts the dwell short - the user is done reading. */
    if (k_event_test(&ecg_evt, EVT_ECG_START) == 0 &&
        (k_uptime_get_32() - ecg_complete_entry_ms) < ECG_COMPLETE_DWELL_MS) {
        return SMF_EVENT_HANDLED;
    }

    // ECG complete - return to idle unless new operation requested
    smf_set_state(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_IDLE]);
    return SMF_EVENT_HANDLED;
}

static void st_ecg_complete_exit(void *o)
{
    LOG_DBG("ECG/BioZ SM Complete Exit");
}

/*
 * =============================================================================
 * STATE TABLE - Defines all ECG/HRV state machine states
 * =============================================================================
 * Flow:
 *   IDLE -> WAIT_FOR_LEAD -> STABILIZING -> RECORDING -> COMPLETE -> IDLE
 *              ^                  |            |
 *              |                  v            v
 *              +------------------+------------+
 *                    (on lead off)
 */
static const struct smf_state ecg_states[] = {
    [HPI_ECG_STATE_IDLE] = SMF_CREATE_STATE(st_ecg_idle_entry, st_ecg_idle_run, NULL, NULL, NULL),
    [HPI_ECG_STATE_WAIT_FOR_LEAD] = SMF_CREATE_STATE(st_ecg_wait_for_lead_entry, st_ecg_wait_for_lead_run, st_ecg_wait_for_lead_exit, NULL, NULL),
    [HPI_ECG_STATE_STABILIZING] = SMF_CREATE_STATE(st_ecg_stabilizing_entry, st_ecg_stabilizing_run, st_ecg_stabilizing_exit, NULL, NULL),
    [HPI_ECG_STATE_RECORDING] = SMF_CREATE_STATE(st_ecg_recording_entry, st_ecg_recording_run, st_ecg_recording_exit, NULL, NULL),
    [HPI_ECG_STATE_COMPLETE] = SMF_CREATE_STATE(st_ecg_complete_entry, st_ecg_complete_run, st_ecg_complete_exit, NULL, NULL),

    [HPI_ECG_STATE_GSR_MEASURE_ENTRY]  = SMF_CREATE_STATE(NULL, st_gsr_entry_run, NULL, NULL, NULL),
    [HPI_ECG_STATE_GSR_MEASURE_STREAM] = SMF_CREATE_STATE(NULL, st_gsr_stream_run, st_gsr_stream_exit, NULL, NULL),
    [HPI_ECG_STATE_GSR_COMPLETE]       = SMF_CREATE_STATE(NULL, st_gsr_complete_run, NULL, NULL, NULL),
};

void smf_ecg_thread(void)
{
    int ret;

    // Wait for HW module to finish bringing up the MAX30001. Uses a dedicated
    // handshake bit (not EVT_ECG_START) so bring-up can never look like a user
    // "start measurement" and spuriously arm the SMF.
    k_event_wait(&ecg_evt, EVT_ECG_HW_READY, false, K_FOREVER);
    k_event_clear(&ecg_evt, EVT_ECG_HW_READY);

    LOG_INF("ECG SMF Thread Started");

#ifdef CONFIG_MAX30001_TRIGGER
    /* P3: arm DRDY (INTB) interrupt-driven acquisition. Only if registration
     * succeeds do we suppress the poll timers (see s_use_drdy_trigger); a
     * failure leaves the flag false so the timers still run. */
    if (max30001_dev != NULL &&
        max30001_trigger_set_handler(max30001_dev, max30001_drdy_handler) == 0)
    {
        s_use_drdy_trigger = true;
        LOG_INF("MAX30001 acquisition: INTB interrupt-driven (poll timers disabled)");
    }
    else
    {
        LOG_WRN("MAX30001 DRDY arm failed; falling back to poll-timer acquisition");
    }
#endif

    smf_set_initial(SMF_CTX(&s_ecg_obj), &ecg_states[HPI_ECG_STATE_IDLE]);

    /* P2: task-watchdog coverage for the ECG SMF thread (previously only
     * smf_display was watched). Registered lazily once the watchdog is up.
     *
     * The 100 ms tick is intentionally kept: unlike the wrist sampling SMF,
     * this loop is not a busy-poll. Its time-based states (GSR/lead/stabilize/
     * recording countdowns) and per-tick GSR status publishing to the UI need
     * periodic execution, so the tick is load-bearing, not spin. It is well
     * under the 10 s watchdog window, so one feed per tick is sufficient. */
    int wdt_ch = -1;

    for (;;)
    {
        if (wdt_ch < 0)
        {
            wdt_ch = hpi_watchdog_register("smf_ecg", 10000);
        }

        ret = smf_run_state(SMF_CTX(&s_ecg_obj));
        if (ret != 0)
        {
            LOG_ERR("SMF Run error: %d", ret);
            break;
        }

        hpi_watchdog_feed(wdt_ch);
        k_msleep(100);
    }
}

// Increased from 1024 to 4096 bytes to accommodate file write operations
// File writes require ~500-700 bytes for LittleFS operations, path buffers,
// and file structures. 1024 bytes was causing stack overflow crashes.
// Priority 11 (lowest-priority background orchestrator). Was 10, but NCS 3.2
// uses priority 10 for the Bluetooth long workqueue (CONFIG_BT_LONG_WQ_PRIO=10);
// keeping the ECG SMF off that level avoids contending with BT housekeeping.
K_THREAD_DEFINE(smf_ecg_thread_id, 4096, smf_ecg_thread, NULL, NULL, NULL, 11, 0, 0);
