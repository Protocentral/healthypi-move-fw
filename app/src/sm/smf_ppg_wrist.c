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
#include <zephyr/zbus/zbus.h>
#include <errno.h>

LOG_MODULE_REGISTER(smf_ppg_wrist, LOG_LEVEL_DBG);

#include "hw_module.h"
#include "max32664c.h"
#include "hpi_common_types.h"
#include "hpi_sys.h"
#include "ui/move_ui.h"

// State machine parameters
#define PPG_WRIST_SAMPLING_INTERVAL_MS 160
#define PPG_WRIST_ACTIVE_SAMPLING_INTERVAL_MS 160

// Timing parameters
#define OFFSKIN_THRESHOLD_S 20       // Duration for SCD "off-skin" before switching to Probing
#define PROBE_ENABLE_WAIT_S 20       // Duration of probing to monitor SCD state
#define PROBE_DISABLE_WAIT_BASE_S 1  // Base sleep duration between probing attempts
#define PROBE_DISABLE_WAIT_MAX_S 8   // Maximum sleep duration (incremental)
#define MAX_PROBE_ATTEMPTS 3         // Number of probe attempts before going to off-skin
#define MOTION_DETECTION_POLLING_S 5 // Polling interval for motion detection in off-skin state
#define OFFSKIN_TIMEOUT_MINUTES 10   // Timeout in Off-Skin state before transitioning to Probing

// SCD state definitions
#define SCD_STATE_UNDETECTED 0
#define SCD_STATE_OFF_SKIN 1
#define SCD_STATE_ON_SUBJECT 2
#define SCD_STATE_ON_SKIN 3

// Legacy definitions for compatibility
#define HPI_OFFSKIN_THRESHOLD_S OFFSKIN_THRESHOLD_S
#define HPI_PROBE_DURATION_S PROBE_ENABLE_WAIT_S

static const struct smf_state ppg_samp_states[];

K_SEM_DEFINE(sem_ppg_wrist_thread_start, 0, 1);
// State machine control variables
static bool off_skin_timer_active = false;
static int64_t off_skin_start_time = 0;
static uint32_t current_probe_attempt = 0;
static uint32_t current_probe_sleep_duration = PROBE_DISABLE_WAIT_BASE_S;
static bool probing_algorithm_enabled = false;

K_SEM_DEFINE(sem_ppg_wrist_on_skin, 0, 1);
K_SEM_DEFINE(sem_ppg_wrist_off_skin, 0, 1);
K_SEM_DEFINE(sem_ppg_wrist_motion_detected, 0, 1);
K_SEM_DEFINE(sem_ppg_wrist_motion_fifo, 0, 1);
K_SEM_DEFINE(sem_ppg_wrist_probe_timeout, 0, 1);
K_SEM_DEFINE(sem_ppg_wrist_offskin_timeout, 0, 1);

K_SEM_DEFINE(sem_start_one_shot_spo2, 0, 1);
K_SEM_DEFINE(sem_stop_one_shot_spo2, 0, 1);
K_SEM_DEFINE(sem_spo2_cancel, 0, 1);

K_MSGQ_DEFINE(q_ppg_wrist_sample, sizeof(struct hpi_ppg_wr_data_t), 64, 1);

// RTIO context with memory pool for async sensor reads
RTIO_DEFINE_WITH_MEMPOOL(max32664c_read_rtio_async_ctx, 4, 4, 4, 512, 4);
SENSOR_DT_READ_IODEV(max32664c_iodev, DT_ALIAS(max32664c), SENSOR_CHAN_VOLTAGE);

ZBUS_CHAN_DECLARE(spo2_chan);

enum ppg_fi_sm_state
{
    PPG_SAMP_STATE_ACTIVE,
    PPG_SAMP_STATE_PROBING,
    PPG_SAMP_STATE_OFF_SKIN,
    PPG_SAMP_STATE_MOTION_DETECT,
};

struct s_object
{
    struct smf_ctx ctx;
} sm_ctx_ppg_wr;

static uint16_t smf_ppg_spo2_last_measured_value = 0;
static int64_t smf_ppg_spo2_last_measured_time;

// Local variables for measured SPO2 and status
static uint16_t measured_spo2 = 0;
static enum spo2_meas_state measured_spo2_status = SPO2_MEAS_UNK;

// Mutex for thread-safe access to measured SPO2 variables
K_MUTEX_DEFINE(mutex_measured_spo2);

static int m_curr_state;
static bool spo2_measurement_in_progress = false;
static enum max32664c_scd_states m_curr_scd_state;

// Externs
extern struct k_sem sem_ppg_wrist_sm_start;

// Work handlers
void work_off_skin_threshold_handler(struct k_work *work)
{
    if (off_skin_timer_active)
    {
        LOG_INF("Off-skin threshold reached after %d seconds - transitioning to PROBING state", OFFSKIN_THRESHOLD_S);
        off_skin_timer_active = false;
        k_sem_give(&sem_ppg_wrist_off_skin);
    }
}
K_WORK_DELAYABLE_DEFINE(work_off_skin_threshold, work_off_skin_threshold_handler);

void work_probe_enable_handler(struct k_work *work)
{
    probing_algorithm_enabled = false;
    hw_max32664c_stop_algo();
    k_sem_give(&sem_ppg_wrist_probe_timeout);
}
K_WORK_DELAYABLE_DEFINE(work_probe_enable, work_probe_enable_handler);

void work_probe_sleep_handler(struct k_work *work)
{
    probing_algorithm_enabled = true;
    hw_max32664c_set_op_mode(MAX32664C_OP_MODE_SCD, MAX32664C_ALGO_MODE_CONT_HRM);
    k_work_schedule(&work_probe_enable, K_SECONDS(PROBE_ENABLE_WAIT_S));
}
K_WORK_DELAYABLE_DEFINE(work_probe_sleep, work_probe_sleep_handler);

void work_offskin_timeout_handler(struct k_work *work)
{
    k_sem_give(&sem_ppg_wrist_offskin_timeout);
}
K_WORK_DELAYABLE_DEFINE(work_offskin_timeout, work_offskin_timeout_handler);

static void set_measured_spo2(uint16_t spo2_value, enum spo2_meas_state status)
{
    if (k_mutex_lock(&mutex_measured_spo2, K_MSEC(100)) == 0)
    {
        measured_spo2 = spo2_value;
        measured_spo2_status = status;
        k_mutex_unlock(&mutex_measured_spo2);
    }
    else
    {
        LOG_WRN("Failed to acquire mutex for setting measured SPO2");
    }
}

static int get_measured_spo2(uint16_t *spo2_value, enum spo2_meas_state *status)
{
    if (spo2_value == NULL || status == NULL)
    {
        return -EINVAL;
    }

    if (k_mutex_lock(&mutex_measured_spo2, K_MSEC(100)) == 0)
    {
        *spo2_value = measured_spo2;
        *status = measured_spo2_status;
        k_mutex_unlock(&mutex_measured_spo2);
        return 0;
    }
    else
    {
        LOG_WRN("Failed to acquire mutex for getting measured SPO2");
        return -EBUSY;
    }
}

static void sensor_ppg_wrist_decode(uint8_t *buf, uint32_t buf_len)
{
    const struct max32664c_encoded_data *edata = (const struct max32664c_encoded_data *)buf;
    struct hpi_ppg_wr_data_t ppg_sensor_sample;

    uint16_t _n_samples = edata->num_samples;

    if (edata->chip_op_mode == MAX32664C_OP_MODE_SCD)
    {
        m_curr_scd_state = edata->scd_state;

        if (edata->scd_state == SCD_STATE_ON_SKIN)
        {
            // Cancel off-skin timer if active
            if (off_skin_timer_active)
            {
                off_skin_timer_active = false;
                k_work_cancel_delayable(&work_off_skin_threshold);
            }

            k_sem_give(&sem_ppg_wrist_on_skin);
        }
        else if (edata->scd_state == SCD_STATE_OFF_SKIN)
        {
            // Start off-skin timer if in ACTIVE state and not already started
            if (!off_skin_timer_active && m_curr_state == PPG_SAMP_STATE_ACTIVE)
            {
                off_skin_timer_active = true;
                off_skin_start_time = k_uptime_get();
                k_work_schedule(&work_off_skin_threshold, K_SECONDS(OFFSKIN_THRESHOLD_S));
            }
        }
        return;
    }
    else if (edata->chip_op_mode == MAX32664C_OP_MODE_WAKE_ON_MOTION)
    {
        if (m_curr_state == PPG_SAMP_STATE_OFF_SKIN || m_curr_state == PPG_SAMP_STATE_MOTION_DETECT)
        {
            if (edata->num_samples > 0)
            {
                k_sem_give(&sem_ppg_wrist_motion_fifo);
            }
            /* Also notify generic motion detected semaphore for compatibility */
            k_sem_give(&sem_ppg_wrist_motion_detected);
        }
        return;
    }
    else if (edata->chip_op_mode == MAX32664C_OP_MODE_ALGO_AEC || edata->chip_op_mode == MAX32664C_OP_MODE_ALGO_AGC || edata->chip_op_mode == MAX32664C_OP_MODE_ALGO_EXTENDED)
    {
        if (_n_samples > 8)
        {
            _n_samples = 8;
        }
        if (_n_samples > 0)
        {
            ppg_sensor_sample.ppg_num_samples = _n_samples;

            for (int i = 0; i < _n_samples; i++)
            {
                ppg_sensor_sample.raw_red[i] = edata->red_samples[i];
                ppg_sensor_sample.raw_ir[i] = edata->ir_samples[i];
                ppg_sensor_sample.raw_green[i] = edata->green_samples[i];
            }

            if (edata->chip_op_mode == MAX32664C_OP_MODE_RAW)
            {
                ppg_sensor_sample.hr = 0;
                ppg_sensor_sample.spo2 = 0;
                ppg_sensor_sample.rtor = 0;
                ppg_sensor_sample.scd_state = 0;
            }
            else
            {
                ppg_sensor_sample.hr = edata->hr;
                ppg_sensor_sample.spo2 = edata->spo2;
                ppg_sensor_sample.rtor = edata->rtor;
                ppg_sensor_sample.scd_state = edata->scd_state;
                ppg_sensor_sample.hr_confidence = edata->hr_confidence;
                ppg_sensor_sample.spo2_confidence = edata->spo2_confidence;
                ppg_sensor_sample.spo2_excessive_motion = edata->spo2_excessive_motion;
                ppg_sensor_sample.spo2_valid_percent_complete = edata->spo2_valid_percent_complete;
                ppg_sensor_sample.spo2_state = edata->spo2_state;
                ppg_sensor_sample.spo2_low_pi = edata->spo2_low_pi;
            }

            // Update current SCD state for general tracking
            m_curr_scd_state = ppg_sensor_sample.scd_state;

            // Process SCD state changes for power optimization in ACTIVE state
            if (m_curr_state == PPG_SAMP_STATE_ACTIVE && edata->chip_op_mode == MAX32664C_OP_MODE_ALGO_AEC)
            {
                if (ppg_sensor_sample.scd_state == MAX32664C_SCD_STATE_ON_SKIN)
                {
                    // Reset off-skin timer if back on skin
                    if (off_skin_timer_active)
                    {
                        off_skin_timer_active = false;
                        k_work_cancel_delayable(&work_off_skin_threshold);
                    }
                }
                else if (ppg_sensor_sample.scd_state == MAX32664C_SCD_STATE_OFF_SKIN)
                {
                    // Start off-skin timer if not already started
                    if (!off_skin_timer_active)
                    {
                        off_skin_timer_active = true;
                        off_skin_start_time = k_uptime_get();
                        k_work_schedule(&work_off_skin_threshold, K_SECONDS(OFFSKIN_THRESHOLD_S));
                    }
                }
            }

            if ((ppg_sensor_sample.spo2_valid_percent_complete == 100) && spo2_measurement_in_progress)
            {
                k_sem_give(&sem_stop_one_shot_spo2);
                if (ppg_sensor_sample.spo2_confidence > 50)
                {
                    struct hpi_spo2_point_t spo2_chan_value = {
                        .timestamp = hw_get_sys_time_ts(),
                        .spo2 = ppg_sensor_sample.spo2,
                    };
                    zbus_chan_pub(&spo2_chan, &spo2_chan_value, K_SECONDS(1));

                    smf_ppg_spo2_last_measured_value = ppg_sensor_sample.spo2;
                    smf_ppg_spo2_last_measured_time = hw_get_sys_time_ts();
                    hpi_sys_set_last_spo2_update(ppg_sensor_sample.spo2, smf_ppg_spo2_last_measured_time);
                    set_measured_spo2(ppg_sensor_sample.spo2, SPO2_MEAS_SUCCESS);
                }
                spo2_measurement_in_progress = false;
            }

            if (ppg_sensor_sample.spo2_state == SPO2_MEAS_TIMEOUT)
            {
                k_sem_give(&sem_stop_one_shot_spo2);
                set_measured_spo2(0, SPO2_MEAS_TIMEOUT);
                spo2_measurement_in_progress = false;
            }

            m_curr_scd_state = ppg_sensor_sample.scd_state;
            if (ppg_sensor_sample.scd_state == MAX32664C_SCD_STATE_ON_SKIN)
            {
                k_msgq_put(&q_ppg_wrist_sample, &ppg_sensor_sample, K_MSEC(1));
            }
        }
    }
}

// RTIO completion handling work item
static void sensor_rtio_completion_handler(struct k_work *work)
{
    struct rtio_cqe *cqe;
    uint8_t *buf;
    uint32_t buf_len;
    int rc;

    // Process all available completion events
    while ((cqe = rtio_cqe_consume(&max32664c_read_rtio_async_ctx)) != NULL)
    {
        if (cqe->result < 0)
        {
            LOG_ERR("Async sensor read failed: %d", cqe->result);
            rtio_cqe_release(&max32664c_read_rtio_async_ctx, cqe);
            continue;
        }

        // Get the buffer from the mempool
        rc = rtio_cqe_get_mempool_buffer(&max32664c_read_rtio_async_ctx, cqe, &buf, &buf_len);
        if (rc != 0)
        {
            LOG_ERR("Failed to get mempool buffer: %d", rc);
            rtio_cqe_release(&max32664c_read_rtio_async_ctx, cqe);
            continue;
        }

        // Process the sensor data
        sensor_ppg_wrist_decode(buf, buf_len);

        // Release the buffer back to the mempool
        rtio_release_buffer(&max32664c_read_rtio_async_ctx, buf, buf_len);

        // Release the completion queue entry
        rtio_cqe_release(&max32664c_read_rtio_async_ctx, cqe);
    }
}

K_WORK_DEFINE(sensor_rtio_completion_work, sensor_rtio_completion_handler);

// Separate work item for initiating async sensor reads
static void sensor_read_work_handler(struct k_work *work)
{
    int ret;

    // Process any pending completions first
    k_work_submit(&sensor_rtio_completion_work);

    // Start async sensor read with mempool
    ret = sensor_read_async_mempool(&max32664c_iodev, &max32664c_read_rtio_async_ctx, &max32664c_iodev);
    if (ret < 0)
    {
        LOG_ERR("Failed to start async sensor read: %d", ret);
        return;
    }

    // The read is now in progress - completion will be handled by the RTIO completion work
}

K_WORK_DEFINE(sensor_read_work, sensor_read_work_handler);

void work_sample_handler(struct k_work *work)
{
    k_work_submit(&sensor_read_work);
}

K_WORK_DEFINE(work_sample, work_sample_handler);

void ppg_wrist_sampling_handler(struct k_timer *dummy)
{
    k_work_submit(&work_sample);
}

K_TIMER_DEFINE(tmr_ppg_wrist_sampling, ppg_wrist_sampling_handler, NULL);

// ACTIVE STATE - Normal operation with AEC/HRM algorithms
// Entry handler
static void ppg_samp_state_active_entry(void *obj)
{
    m_curr_state = PPG_SAMP_STATE_ACTIVE;

    // Reset power optimization variables
    off_skin_timer_active = false;
    current_probe_attempt = 0;
    current_probe_sleep_duration = PROBE_DISABLE_WAIT_BASE_S;
    probing_algorithm_enabled = true;

    // Cancel any leftover work timers from previous states
    k_work_cancel_delayable(&work_off_skin_threshold);
    k_work_cancel_delayable(&work_probe_enable);
    k_work_cancel_delayable(&work_probe_sleep);
    k_work_cancel_delayable(&work_offskin_timeout);

    // Ensure wake-on-motion is disabled when entering active state
    hw_max32664c_set_op_mode(MAX32664C_OP_MODE_EXIT_WAKE_ON_MOTION, MAX32664C_ALGO_MODE_NONE);
    k_msleep(50); // Allow time for mode change

    // Enable normal algorithm operation
    hw_max32664c_set_op_mode(MAX32664C_OP_MODE_ALGO_AEC, MAX32664C_ALGO_MODE_CONT_HRM);

    // Use faster sampling rate in active mode for responsive detection
    k_timer_start(&tmr_ppg_wrist_sampling, K_MSEC(PPG_WRIST_ACTIVE_SAMPLING_INTERVAL_MS), K_MSEC(PPG_WRIST_ACTIVE_SAMPLING_INTERVAL_MS));

    hpi_sys_set_device_on_skin(true);
}

static void st_ppg_samp_active_run(void *o)
{
    while (true)
    {
        // Check for off-skin detection to trigger transition to probing
        if (k_sem_take(&sem_ppg_wrist_off_skin, K_NO_WAIT) == 0)
        {
            smf_set_state(SMF_CTX(&sm_ctx_ppg_wr), &ppg_samp_states[PPG_SAMP_STATE_PROBING]);
            return; // Exit when transitioning to new state
        }

        // Yield to other threads
        k_msleep(10);
    }
}

// PROBING STATE - Intermittent algorithm operation to check for skin contact
static void st_ppg_samp_probing_entry(void *o)
{
    m_curr_state = PPG_SAMP_STATE_PROBING;

    // Cancel any leftover timers from previous states
    k_work_cancel_delayable(&work_off_skin_threshold);
    k_work_cancel_delayable(&work_offskin_timeout);

    // Reset flags and counters
    probing_algorithm_enabled = true;

    // Ensure wake-on-motion is disabled when entering probing state
    hw_max32664c_set_op_mode(MAX32664C_OP_MODE_EXIT_WAKE_ON_MOTION, MAX32664C_ALGO_MODE_NONE);
    k_msleep(50); // Allow time for mode change

    // Enable SCD mode to check for skin contact
    hw_max32664c_set_op_mode(MAX32664C_OP_MODE_SCD, MAX32664C_ALGO_MODE_CONT_HRM);

    // Use normal sampling rate for probing
    k_timer_start(&tmr_ppg_wrist_sampling, K_MSEC(PPG_WRIST_SAMPLING_INTERVAL_MS), K_MSEC(PPG_WRIST_SAMPLING_INTERVAL_MS));

    // Start probe enable timer
    k_work_schedule(&work_probe_enable, K_SECONDS(PROBE_ENABLE_WAIT_S));
}

static void st_ppg_samp_probing_run(void *o)
{
    while (true)
    {
        // Check for on-skin detection during probing
        if (k_sem_take(&sem_ppg_wrist_on_skin, K_NO_WAIT) == 0)
        {
            smf_set_state(SMF_CTX(&sm_ctx_ppg_wr), &ppg_samp_states[PPG_SAMP_STATE_ACTIVE]);
            return; // Exit when transitioning to new state
        }

        // Check for probe timeout
        if (k_sem_take(&sem_ppg_wrist_probe_timeout, K_NO_WAIT) == 0)
        {
            current_probe_attempt++;

            if (current_probe_attempt >= MAX_PROBE_ATTEMPTS)
            {
                m_curr_state = PPG_SAMP_STATE_OFF_SKIN;
                smf_set_state(SMF_CTX(&sm_ctx_ppg_wr), &ppg_samp_states[PPG_SAMP_STATE_OFF_SKIN]);
                return; // Exit when transitioning to new state
            }
            else
            {

                // Disable algorithm for power saving
                probing_algorithm_enabled = false;
                hw_max32664c_stop_algo();

                // Schedule next probe attempt with incremental sleep duration
                k_work_schedule(&work_probe_sleep, K_SECONDS(current_probe_sleep_duration));

                // Increase sleep duration for next attempt (up to maximum)
                if (current_probe_sleep_duration < PROBE_DISABLE_WAIT_MAX_S)
                {
                    current_probe_sleep_duration *= 2;
                    if (current_probe_sleep_duration > PROBE_DISABLE_WAIT_MAX_S)
                    {
                        current_probe_sleep_duration = PROBE_DISABLE_WAIT_MAX_S;
                    }
                }
            }
        }

        // Yield to other threads
        k_msleep(10);
    }
}

// OFF_SKIN STATE - Low power mode with motion detection
static void st_ppg_samp_off_skin_entry(void *o)
{
    m_curr_state = PPG_SAMP_STATE_OFF_SKIN;

    // Cancel any leftover timers from previous states
    k_work_cancel_delayable(&work_off_skin_threshold);
    k_work_cancel_delayable(&work_probe_enable);
    k_work_cancel_delayable(&work_probe_sleep);

    // Reset variables for off-skin state
    probing_algorithm_enabled = false;
    current_probe_attempt = 0;
    current_probe_sleep_duration = PROBE_DISABLE_WAIT_BASE_S;

    // Stop all algorithms for maximum power savings
    hw_max32664c_stop_algo();

    // Configure accelerometer for wake-up on motion (as per datasheet)
    // Command: AA 46 04 00 01 [WUFC] [ATH]
    // WUFC: 0x05 (0.2 seconds), ATH: 0x08 (0.5g)
    hw_max32664c_set_op_mode(MAX32664C_OP_MODE_WAKE_ON_MOTION, MAX32664C_ALGO_MODE_NONE);

    // Force state variable to OFF_SKIN after setting wake-on-motion
    m_curr_state = PPG_SAMP_STATE_OFF_SKIN;

    // Use motion detection polling interval for power savings
    k_timer_start(&tmr_ppg_wrist_sampling, K_SECONDS(MOTION_DETECTION_POLLING_S), K_SECONDS(MOTION_DETECTION_POLLING_S));

    // Start off-skin timeout (10 minutes)
    k_work_schedule(&work_offskin_timeout, K_MINUTES(OFFSKIN_TIMEOUT_MINUTES));

    hpi_sys_set_device_on_skin(false);
}

static void st_ppg_samp_off_skin_run(void *o)
{
    while (true)
    {
        // Check for motion detection
        if (k_sem_take(&sem_ppg_wrist_motion_detected, K_NO_WAIT) == 0)
        {
            // Transition to motion detect state to poll FIFO before fully waking
            k_work_cancel_delayable(&work_offskin_timeout);
            smf_set_state(SMF_CTX(&sm_ctx_ppg_wr), &ppg_samp_states[PPG_SAMP_STATE_MOTION_DETECT]);
            return; // Exit when transitioning to new state
        }

        // Check for off-skin timeout (transition back to probing)
        if (k_sem_take(&sem_ppg_wrist_offskin_timeout, K_NO_WAIT) == 0)
        {
            smf_set_state(SMF_CTX(&sm_ctx_ppg_wr), &ppg_samp_states[PPG_SAMP_STATE_PROBING]);
            return; // Exit when transitioning to new state
        }

        // Yield to other threads
        k_msleep(10);
    }
}

// MOTION_DETECT STATE - Poll FIFO after wake-on-motion to confirm motion and safely exit wake mode
static void st_ppg_samp_motion_detect_entry(void *o)
{
    m_curr_state = PPG_SAMP_STATE_MOTION_DETECT;

    // Shorter polling for FIFO contents
    k_timer_start(&tmr_ppg_wrist_sampling, K_SECONDS(1), K_SECONDS(1));
}

static void st_ppg_samp_motion_detect_run(void *o)
{
    const int max_checks = 5;
    int checks = 0;

    while (true)
    {
        // Wait briefly for FIFO samples reported by the driver
        if (k_sem_take(&sem_ppg_wrist_motion_fifo, K_SECONDS(1)) == 0)
        {
            // We have accel samples in the hub FIFO - now disable wake-on-motion and restart algorithms
            hw_max32664c_set_op_mode(MAX32664C_OP_MODE_EXIT_WAKE_ON_MOTION, MAX32664C_ALGO_MODE_NONE);
            k_msleep(50);
            // Move to ACTIVE state where algorithms will be re-enabled
            smf_set_state(SMF_CTX(&sm_ctx_ppg_wr), &ppg_samp_states[PPG_SAMP_STATE_ACTIVE]);
            return;
        }

        checks++;
        if (checks >= max_checks)
        {
            // No FIFO samples observed - return to OFF_SKIN and re-arm wake-on-motion
            smf_set_state(SMF_CTX(&sm_ctx_ppg_wr), &ppg_samp_states[PPG_SAMP_STATE_OFF_SKIN]);
            return;
        }

        k_msleep(10);
    }
}

// State machine with four states: ACTIVE, PROBING, OFF_SKIN, MOTION_DETECT
static const struct smf_state ppg_samp_states[] = {
    [PPG_SAMP_STATE_ACTIVE] = SMF_CREATE_STATE(ppg_samp_state_active_entry, st_ppg_samp_active_run, NULL, NULL, NULL),
    [PPG_SAMP_STATE_PROBING] = SMF_CREATE_STATE(st_ppg_samp_probing_entry, st_ppg_samp_probing_run, NULL, NULL, NULL),
    [PPG_SAMP_STATE_OFF_SKIN] = SMF_CREATE_STATE(st_ppg_samp_off_skin_entry, st_ppg_samp_off_skin_run, NULL, NULL, NULL),
    [PPG_SAMP_STATE_MOTION_DETECT] = SMF_CREATE_STATE(st_ppg_samp_motion_detect_entry, st_ppg_samp_motion_detect_run, NULL, NULL, NULL),
};

static void smf_ppg_wrist_thread(void)
{
    int32_t ret;

    k_sem_take(&sem_ppg_wrist_sm_start, K_FOREVER);

    if (hw_is_max32664c_present() == false)
    {
        LOG_ERR("MAX32664C device not present. Not starting PPG SMF");
        return;
    }

    smf_set_initial(SMF_CTX(&sm_ctx_ppg_wr), &ppg_samp_states[PPG_SAMP_STATE_ACTIVE]);

    k_timer_start(&tmr_ppg_wrist_sampling, K_MSEC(PPG_WRIST_SAMPLING_INTERVAL_MS), K_MSEC(PPG_WRIST_SAMPLING_INTERVAL_MS));

    LOG_INF("PPG State Machine Thread starting");
    for (;;)
    {
        ret = smf_run_state(SMF_CTX(&sm_ctx_ppg_wr));

        if (ret)
        {
            LOG_ERR("Error in PPG State Machine");
            break;
        }

        k_msleep(500);
    }
}

static void ppg_wrist_ctrl_thread(void)
{
    for (;;)
    {
        if (k_sem_take(&sem_start_one_shot_spo2, K_NO_WAIT) == 0)
        {
            // smf_set_terminate(SMF_CTX(&sm_ctx_ppg_wr);
            LOG_DBG("Stopping PPG Sampling");
            k_timer_stop(&tmr_ppg_wrist_sampling);

            LOG_DBG("Starting One Shot SpO2");

            hpi_load_scr_spl(SCR_SPL_SPO2_MEASURE, SCROLL_UP, (uint8_t)SCR_SPO2, SPO2_SOURCE_PPG_WR, 0, 0);

            hw_max32664c_set_op_mode(MAX32664C_OP_MODE_STOP_ALGO, MAX32664C_ALGO_MODE_NONE);
            k_msleep(600);
            hw_max32664c_set_op_mode(MAX32664C_OP_MODE_ALGO_AEC, MAX32664C_ALGO_MODE_CONT_HR_SHOT_SPO2);
            k_msleep(600);
            k_timer_start(&tmr_ppg_wrist_sampling, K_MSEC(PPG_WRIST_SAMPLING_INTERVAL_MS), K_MSEC(PPG_WRIST_SAMPLING_INTERVAL_MS));

            spo2_measurement_in_progress = true;
        }

        if (k_sem_take(&sem_stop_one_shot_spo2, K_NO_WAIT) == 0)
        {
            LOG_DBG("Stopping One Shot SpO2");
            k_timer_stop(&tmr_ppg_wrist_sampling);
            spo2_measurement_in_progress = false;
            hw_max32664c_set_op_mode(MAX32664C_OP_MODE_STOP_ALGO, MAX32664C_ALGO_MODE_NONE);
            uint16_t m_est_spo2 = 0;
            enum spo2_meas_state m_est_spo2_status = SPO2_MEAS_UNK;
            get_measured_spo2(&m_est_spo2, &m_est_spo2_status);
            if (m_est_spo2_status == SPO2_MEAS_SUCCESS)
            {
                LOG_DBG("SPO2 Measurement Successful: %d", m_est_spo2);
                hpi_load_scr_spl(SCR_SPL_SPO2_COMPLETE, SCROLL_NONE, SCR_SPO2, m_est_spo2, 0, 0);
            }
            else if (m_est_spo2_status == SPO2_MEAS_TIMEOUT)
            {
                LOG_DBG("SPO2 Measurement Timeout");
                hpi_load_scr_spl(SCR_SPL_SPO2_TIMEOUT, SCROLL_NONE, SCR_SPO2, m_est_spo2, 0, 0);
            }
            else
            {
                LOG_DBG("SPO2 Measurement Unknown Status");
            }

            k_msleep(1000);

            LOG_DBG("Switching to Continuous Sampling HR");
            hw_max32664c_set_op_mode(MAX32664C_OP_MODE_ALGO_AEC, MAX32664C_ALGO_MODE_CONT_HRM);
            k_msleep(600);
            k_timer_start(&tmr_ppg_wrist_sampling, K_MSEC(PPG_WRIST_SAMPLING_INTERVAL_MS), K_MSEC(PPG_WRIST_SAMPLING_INTERVAL_MS));
        }

        k_msleep(100);
    }
}

#define PPG_CTRL_THREAD_STACKSIZE 1024
#define PPG_CTRL_THREAD_PRIORITY 7

#define SMF_PPG_THREAD_STACKSIZE 4096
#define SMF_PPG_THREAD_PRIORITY 7

K_THREAD_DEFINE(smf_ppg_thread_id, SMF_PPG_THREAD_STACKSIZE, smf_ppg_wrist_thread, NULL, NULL, NULL, SMF_PPG_THREAD_PRIORITY, 0, 1000);
K_THREAD_DEFINE(ppg_ctrl_thread_id, PPG_CTRL_THREAD_STACKSIZE, ppg_wrist_ctrl_thread, NULL, NULL, NULL, PPG_CTRL_THREAD_PRIORITY, 0, 0);