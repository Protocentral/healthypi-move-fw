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
#include "hpi_dfu.h"
#include "max32664c.h"
#include "hpi_common_types.h"
#include "hpi_sys.h"
#include "hpi_watchdog.h"
#include "hpi_evt.h"
#include "ui/move_ui.h"

/* Wrist one-shot SpO2 control signals (see hpi_evt.h): start/cancel from the
 * SpO2 UI screens, stop from the RTIO decode workqueue -> control thread. */
K_EVENT_DEFINE(spo2_evt);

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

/* P2: event-driven inbox for the sampling SMF.
 *
 * The 4 run handlers used to busy-poll six K_NO_WAIT semaphores with a
 * k_msleep(10) spin (and the outer thread added k_msleep(500)), which added
 * up to ~500 ms latency per transition and never fed a watchdog. They now
 * block on this single k_event with a bounded housekeeping timeout: a posted
 * bit wakes the handler immediately, and the timeout drives watchdog feeding.
 * Bits are cleared on each state entry so a stale post cannot cause a spurious
 * transition (matching the old max-count-1, K_NO_WAIT-consume semantics). */
K_EVENT_DEFINE(ppg_wr_events);
#define EVT_ON_SKIN         BIT(0)
#define EVT_OFF_SKIN        BIT(1)
#define EVT_MOTION_DETECTED BIT(2)
#define EVT_MOTION_FIFO     BIT(3)
#define EVT_PROBE_TIMEOUT   BIT(4)
#define EVT_OFFSKIN_TIMEOUT BIT(5)
#define EVT_ALL (EVT_ON_SKIN | EVT_OFF_SKIN | EVT_MOTION_DETECTED | \
                 EVT_MOTION_FIFO | EVT_PROBE_TIMEOUT | EVT_OFFSKIN_TIMEOUT)

/* Housekeeping wake period: run handlers return to feed the watchdog at least
 * this often even when no event fires. Must be < the registered WDT timeout. */
#define PPG_WR_HOUSEKEEP_MS 2000

/* Task-watchdog channels (-1 until lazily registered). One per thread:
 * the SMF sampling thread and the SpO2 one-shot control thread. */
static int wdt_ch_ppg_wr = -1;
static int wdt_ch_ppg_ctrl = -1;


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

/* PPG sampling state: written by the SMF thread (state entry fns), read from the
 * RTIO decode in the workqueue context -> atomic for cross-thread visibility. */
static atomic_t m_curr_state = ATOMIC_INIT(0);
/* Written by the ctrl thread (start/stop/cancel) and the RTIO decode workqueue
 * (on completion/timeout), read by both of those plus the SMF state-entry fns.
 * atomic for cross-thread visibility, same as m_curr_state. */
static atomic_t spo2_measurement_in_progress = ATOMIC_INIT(0);
static enum max32664c_scd_states m_curr_scd_state;

// Externs
extern struct k_sem sem_ppg_wrist_sm_start;

/* P2 error/recovery: the MAX32664C op-mode commands are blocking I2C and can
 * fail on a transient bus glitch. Previously every return code was discarded
 * (a failed mode change silently produced no data). Wrap them with bounded
 * retry + exponential backoff, and track a consecutive-failure streak so a
 * wedged hub becomes visible (LOG_ERR) instead of failing silently. The backoff
 * runs only on failure, so the normal path timing is unchanged. */
#define WR_HUB_CMD_RETRIES 3
static atomic_t wr_hub_err_streak = ATOMIC_INIT(0);

static void wr_hub_note_result(int ret, const char *what, uint32_t a, uint32_t b)
{
    if (ret == 0)
    {
        atomic_set(&wr_hub_err_streak, 0);
        return;
    }
    long streak = atomic_add(&wr_hub_err_streak, 1) + 1;
    LOG_ERR("MAX32664C hub unresponsive: %s(%u,%u) failed after %d tries (err streak %ld)",
            what, a, b, WR_HUB_CMD_RETRIES + 1, streak);
}

static int wr_set_op_mode(uint8_t op_mode, uint8_t algo_mode)
{
    int ret = 0;
    uint32_t backoff = 20;
    for (int attempt = 0; attempt <= WR_HUB_CMD_RETRIES; attempt++)
    {
        ret = hw_max32664c_set_op_mode(op_mode, algo_mode);
        if (ret == 0)
        {
            atomic_set(&wr_hub_err_streak, 0);
            return 0;
        }
        LOG_WRN("MAX32664C set_op_mode(%u,%u) failed (%d) [try %d/%d]",
                op_mode, algo_mode, ret, attempt + 1, WR_HUB_CMD_RETRIES + 1);
        if (attempt < WR_HUB_CMD_RETRIES)
        {
            k_msleep(backoff);
            backoff *= 2; /* exponential backoff */
        }
    }
    wr_hub_note_result(ret, "set_op_mode", op_mode, algo_mode);
    return ret;
}

static int wr_stop_algo(void)
{
    int ret = 0;
    uint32_t backoff = 20;
    for (int attempt = 0; attempt <= WR_HUB_CMD_RETRIES; attempt++)
    {
        ret = hw_max32664c_stop_algo();
        if (ret == 0)
        {
            atomic_set(&wr_hub_err_streak, 0);
            return 0;
        }
        LOG_WRN("MAX32664C stop_algo failed (%d) [try %d/%d]", ret, attempt + 1, WR_HUB_CMD_RETRIES + 1);
        if (attempt < WR_HUB_CMD_RETRIES)
        {
            k_msleep(backoff);
            backoff *= 2; /* exponential backoff */
        }
    }
    wr_hub_note_result(ret, "stop_algo", 0, 0);
    return ret;
}

// Work handlers
void work_off_skin_threshold_handler(struct k_work *work)
{
    if (off_skin_timer_active)
    {
        LOG_INF("Off-skin threshold reached after %d seconds - transitioning to PROBING state", OFFSKIN_THRESHOLD_S);
        off_skin_timer_active = false;
        k_event_post(&ppg_wr_events, EVT_OFF_SKIN);
    }
}
K_WORK_DELAYABLE_DEFINE(work_off_skin_threshold, work_off_skin_threshold_handler);

void work_probe_enable_handler(struct k_work *work)
{
    probing_algorithm_enabled = false;
    wr_stop_algo();
    k_event_post(&ppg_wr_events, EVT_PROBE_TIMEOUT);
}
K_WORK_DELAYABLE_DEFINE(work_probe_enable, work_probe_enable_handler);

void work_probe_sleep_handler(struct k_work *work)
{
    probing_algorithm_enabled = true;
    wr_set_op_mode(MAX32664C_OP_MODE_SCD, MAX32664C_ALGO_MODE_CONT_HRM);
    k_work_schedule(&work_probe_enable, K_SECONDS(PROBE_ENABLE_WAIT_S));
}
K_WORK_DELAYABLE_DEFINE(work_probe_sleep, work_probe_sleep_handler);

void work_offskin_timeout_handler(struct k_work *work)
{
    k_event_post(&ppg_wr_events, EVT_OFFSKIN_TIMEOUT);
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
    /* Initialize to zero to prevent garbage values in unused fields */
    struct hpi_ppg_wr_data_t ppg_sensor_sample = {0};

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

            k_event_post(&ppg_wr_events, EVT_ON_SKIN);
        }
        else if (edata->scd_state == SCD_STATE_OFF_SKIN)
        {
            // Start off-skin timer if in ACTIVE state and not already started
            if (!off_skin_timer_active && atomic_get(&m_curr_state) == PPG_SAMP_STATE_ACTIVE)
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
        if (atomic_get(&m_curr_state) == PPG_SAMP_STATE_OFF_SKIN || atomic_get(&m_curr_state) == PPG_SAMP_STATE_MOTION_DETECT)
        {
            if (edata->num_samples > 0)
            {
                k_event_post(&ppg_wr_events, EVT_MOTION_FIFO);
            }
            /* Also notify generic motion-detected event for compatibility */
            k_event_post(&ppg_wr_events, EVT_MOTION_DETECTED);
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
                ppg_sensor_sample.rtor_confidence = 0;
                ppg_sensor_sample.scd_state = 0;
            }
            else
            {
                ppg_sensor_sample.hr = edata->hr;
                ppg_sensor_sample.spo2 = edata->spo2;
                ppg_sensor_sample.rtor = edata->rtor;
                /* P3: the hub reports a confidence for the R-R interval and this was
                 * never copied -- the field existed in hpi_ppg_wr_data_t and was always
                 * zero. Without it the HRV gate cannot reject a bad beat. */
                ppg_sensor_sample.rtor_confidence = edata->rtor_confidence;
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
            if (atomic_get(&m_curr_state) == PPG_SAMP_STATE_ACTIVE && edata->chip_op_mode == MAX32664C_OP_MODE_ALGO_AEC)
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

            if ((ppg_sensor_sample.spo2_valid_percent_complete == 100) && atomic_get(&spo2_measurement_in_progress))
            {
                k_event_post(&spo2_evt, EVT_SPO2_STOP);
                if (ppg_sensor_sample.spo2_confidence > 50)
                {
                    struct hpi_spo2_point_t spo2_chan_value = {
                        .timestamp = hw_get_sys_time_ts(),
                        .spo2 = ppg_sensor_sample.spo2,
                    };
                    zbus_chan_pub(&spo2_chan, &spo2_chan_value, K_SECONDS(1));

                    smf_ppg_spo2_last_measured_value = ppg_sensor_sample.spo2;
                    smf_ppg_spo2_last_measured_time = hw_get_sys_time_ts();
                    /* persistence removed — health store will ingest this (H1) */
                    set_measured_spo2(ppg_sensor_sample.spo2, SPO2_MEAS_SUCCESS);
                }
                else
                {
                   LOG_DBG("SpO2 invalid: conf=%d, motion=%d, low_pi=%d, scd=%d",
                   ppg_sensor_sample.spo2_confidence,
                   ppg_sensor_sample.spo2_excessive_motion,
                   ppg_sensor_sample.spo2_low_pi,
                   ppg_sensor_sample.scd_state);
                }
                atomic_set(&spo2_measurement_in_progress, 0);
            }
            else if (atomic_get(&spo2_measurement_in_progress))
            {
                LOG_INF("Spo2 : %d | Confidence : %d | Progress : %d | SCD : %d | Low PI : %d",
                   ppg_sensor_sample.spo2,
                   ppg_sensor_sample.spo2_confidence,
                   ppg_sensor_sample.spo2_valid_percent_complete,
                   ppg_sensor_sample.scd_state,
                   ppg_sensor_sample.spo2_low_pi);
            }

            if (ppg_sensor_sample.spo2_state == SPO2_MEAS_TIMEOUT)
            {
                k_event_post(&spo2_evt, EVT_SPO2_STOP);
                set_measured_spo2(0, SPO2_MEAS_TIMEOUT);
                atomic_set(&spo2_measurement_in_progress, 0);
            }

            m_curr_scd_state = ppg_sensor_sample.scd_state;
            /* Forward raw PPG samples whenever the hub is not *positively* off-skin.
             *
             * Previously this gated on SCD == ON_SKIN (the fully-locked state). On a
             * cold boot the MAX32664C AEC/SCD has not converged yet, so scd_state sits
             * at UNKNOWN(0)/ON_OBJECT(2) for the first several seconds and every raw
             * sample was dropped -> opening the Raw PPG screen directly showed only
             * "No Signal". Running an SpO2 measurement first warmed up the algorithm
             * and locked SCD to ON_SKIN, which is why Raw PPG worked only afterwards.
             *
             * The raw view is a diagnostic waveform and renders its own "No Skin
             * Contact" overlay from scd_state, and the HR zbus publish is gated
             * separately on ON_SKIN in data_module, so delivering warm-up/on-object
             * samples here is safe. Only a definite OFF_SKIN reading is suppressed,
             * which keeps true off-skin noise out of BLE streaming and recording. */
            if (ppg_sensor_sample.scd_state != MAX32664C_SCD_STATE_OFF_SKIN)
            {
                if (k_msgq_put(&q_ppg_wrist_sample, &ppg_sensor_sample, K_MSEC(1)) != 0)
                {
                    static uint32_t wr_drops = 0;
                    if ((++wr_drops % 10) == 0)
                    {
                        LOG_WRN("q_ppg_wrist_sample full - dropped %u wrist PPG batches", wr_drops);
                    }
                }
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

    /* DFU quiesce: stop wrist-PPG hub I2C traffic during a BLE OTA to free the
     * bus/CPU for the SMP + QSPI write path. Resumes when the flag clears. */
    if (hpi_dfu_is_active())
    {
        return;
    }

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
    k_event_clear(&ppg_wr_events, EVT_ALL); // fresh inbox for this state
    if (atomic_get(&spo2_measurement_in_progress))
    {
        LOG_INF("Spo2 ACTIVE -> staying in Spo2 algo");
        wr_set_op_mode(MAX32664C_OP_MODE_ALGO_AEC, MAX32664C_ALGO_MODE_CONT_HR_SHOT_SPO2);
        return;
    }
    atomic_set(&m_curr_state, PPG_SAMP_STATE_ACTIVE);

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
    wr_set_op_mode(MAX32664C_OP_MODE_EXIT_WAKE_ON_MOTION, MAX32664C_ALGO_MODE_NONE);
    k_msleep(50); // Allow time for mode change

    // Enable normal algorithm operation
    wr_set_op_mode(MAX32664C_OP_MODE_ALGO_AEC, MAX32664C_ALGO_MODE_CONT_HRM);

    // Use faster sampling rate in active mode for responsive detection
    k_timer_start(&tmr_ppg_wrist_sampling, K_MSEC(PPG_WRIST_ACTIVE_SAMPLING_INTERVAL_MS), K_MSEC(PPG_WRIST_ACTIVE_SAMPLING_INTERVAL_MS));

    hpi_sys_set_device_on_skin(true);
}

static enum smf_state_result st_ppg_samp_active_run(void *o)
{
    // Block until off-skin is detected (or wake to feed the watchdog).
    uint32_t ev = k_event_wait(&ppg_wr_events, EVT_OFF_SKIN, false, K_MSEC(PPG_WR_HOUSEKEEP_MS));
    hpi_watchdog_feed(wdt_ch_ppg_wr);

    if (ev & EVT_OFF_SKIN)
    {
        k_event_clear(&ppg_wr_events, EVT_OFF_SKIN);
        smf_set_state(SMF_CTX(&sm_ctx_ppg_wr), &ppg_samp_states[PPG_SAMP_STATE_PROBING]);
    }
    return SMF_EVENT_HANDLED;
}

// PROBING STATE - Intermittent algorithm operation to check for skin contact
static void st_ppg_samp_probing_entry(void *o)
{
    k_event_clear(&ppg_wr_events, EVT_ALL); // fresh inbox for this state
    if (atomic_get(&spo2_measurement_in_progress))
    {
        LOG_INF("Spo2 ACTIVE -> staying in Spo2 algo");
        wr_set_op_mode(MAX32664C_OP_MODE_ALGO_AEC, MAX32664C_ALGO_MODE_CONT_HR_SHOT_SPO2);
        return;
    }
    atomic_set(&m_curr_state, PPG_SAMP_STATE_PROBING);

    // Cancel any leftover timers from previous states
    k_work_cancel_delayable(&work_off_skin_threshold);
    k_work_cancel_delayable(&work_offskin_timeout);

    // Reset flags and counters
    probing_algorithm_enabled = true;

    // Ensure wake-on-motion is disabled when entering probing state
    wr_set_op_mode(MAX32664C_OP_MODE_EXIT_WAKE_ON_MOTION, MAX32664C_ALGO_MODE_NONE);
    k_msleep(50); // Allow time for mode change

    // Enable SCD mode to check for skin contact
    wr_set_op_mode(MAX32664C_OP_MODE_SCD, MAX32664C_ALGO_MODE_CONT_HRM);
    LOG_INF("Entered continuous HRM mode for probing");

    // Use normal sampling rate for probing
    k_timer_start(&tmr_ppg_wrist_sampling, K_MSEC(PPG_WRIST_SAMPLING_INTERVAL_MS), K_MSEC(PPG_WRIST_SAMPLING_INTERVAL_MS));

    // Start probe enable timer
    k_work_schedule(&work_probe_enable, K_SECONDS(PROBE_ENABLE_WAIT_S));
}

static enum smf_state_result st_ppg_samp_probing_run(void *o)
{
    // Block for on-skin detection or probe timeout (or wake to feed the watchdog).
    uint32_t ev = k_event_wait(&ppg_wr_events, EVT_ON_SKIN | EVT_PROBE_TIMEOUT, false, K_MSEC(PPG_WR_HOUSEKEEP_MS));
    hpi_watchdog_feed(wdt_ch_ppg_wr);

    // Check for on-skin detection during probing
    if (ev & EVT_ON_SKIN)
    {
        k_event_clear(&ppg_wr_events, EVT_ON_SKIN);
        LOG_INF("SCD detected skin contact during probing - transitioning to ACTIVE state");
        smf_set_state(SMF_CTX(&sm_ctx_ppg_wr), &ppg_samp_states[PPG_SAMP_STATE_ACTIVE]);
        return SMF_EVENT_HANDLED;
    }

    // Check for probe timeout
    if (ev & EVT_PROBE_TIMEOUT)
    {
        k_event_clear(&ppg_wr_events, EVT_PROBE_TIMEOUT);
        current_probe_attempt++;

        if (current_probe_attempt >= MAX_PROBE_ATTEMPTS)
        {
            atomic_set(&m_curr_state, PPG_SAMP_STATE_OFF_SKIN);
            smf_set_state(SMF_CTX(&sm_ctx_ppg_wr), &ppg_samp_states[PPG_SAMP_STATE_OFF_SKIN]);
            return SMF_EVENT_HANDLED;
        }

        // Disable algorithm for power saving
        probing_algorithm_enabled = false;
        wr_stop_algo();

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
    return SMF_EVENT_HANDLED;
}

// OFF_SKIN STATE - Low power mode with motion detection
static void st_ppg_samp_off_skin_entry(void *o)
{
    k_event_clear(&ppg_wr_events, EVT_ALL); // fresh inbox for this state
    if (atomic_get(&spo2_measurement_in_progress))
    {
        LOG_INF("Spo2 ACTIVE -> staying in Spo2 algo");
        wr_set_op_mode(MAX32664C_OP_MODE_ALGO_AEC, MAX32664C_ALGO_MODE_CONT_HR_SHOT_SPO2);
        return;
    }
    atomic_set(&m_curr_state, PPG_SAMP_STATE_OFF_SKIN);

    // Cancel any leftover timers from previous states
    k_work_cancel_delayable(&work_off_skin_threshold);
    k_work_cancel_delayable(&work_probe_enable);
    k_work_cancel_delayable(&work_probe_sleep);

    // Reset variables for off-skin state
    probing_algorithm_enabled = false;
    current_probe_attempt = 0;
    current_probe_sleep_duration = PROBE_DISABLE_WAIT_BASE_S;

    // Stop all algorithms for maximum power savings
    wr_stop_algo();

    // Configure accelerometer for wake-up on motion (as per datasheet)
    // Command: AA 46 04 00 01 [WUFC] [ATH]
    // WUFC: 0x05 (0.2 seconds), ATH: 0x08 (0.5g)
    wr_set_op_mode(MAX32664C_OP_MODE_WAKE_ON_MOTION, MAX32664C_ALGO_MODE_NONE);

    // Force state variable to OFF_SKIN after setting wake-on-motion
    atomic_set(&m_curr_state, PPG_SAMP_STATE_OFF_SKIN);

    // Use motion detection polling interval for power savings
    k_timer_start(&tmr_ppg_wrist_sampling, K_SECONDS(MOTION_DETECTION_POLLING_S), K_SECONDS(MOTION_DETECTION_POLLING_S));

    // Start off-skin timeout (10 minutes)
    k_work_schedule(&work_offskin_timeout, K_MINUTES(OFFSKIN_TIMEOUT_MINUTES));

    hpi_sys_set_device_on_skin(false);
}

static enum smf_state_result st_ppg_samp_off_skin_run(void *o)
{
    // Block for motion or the off-skin timeout (or wake to feed the watchdog).
    uint32_t ev = k_event_wait(&ppg_wr_events, EVT_MOTION_DETECTED | EVT_OFFSKIN_TIMEOUT, false, K_MSEC(PPG_WR_HOUSEKEEP_MS));
    hpi_watchdog_feed(wdt_ch_ppg_wr);

    // Check for motion detection
    if (ev & EVT_MOTION_DETECTED)
    {
        k_event_clear(&ppg_wr_events, EVT_MOTION_DETECTED);
        // Transition to motion detect state to poll FIFO before fully waking
        k_work_cancel_delayable(&work_offskin_timeout);
        smf_set_state(SMF_CTX(&sm_ctx_ppg_wr), &ppg_samp_states[PPG_SAMP_STATE_MOTION_DETECT]);
        return SMF_EVENT_HANDLED;
    }

    // Check for off-skin timeout (transition back to probing)
    if (ev & EVT_OFFSKIN_TIMEOUT)
    {
        k_event_clear(&ppg_wr_events, EVT_OFFSKIN_TIMEOUT);
        smf_set_state(SMF_CTX(&sm_ctx_ppg_wr), &ppg_samp_states[PPG_SAMP_STATE_PROBING]);
        return SMF_EVENT_HANDLED;
    }
    return SMF_EVENT_HANDLED;
}

// MOTION_DETECT STATE - Poll FIFO after wake-on-motion to confirm motion and safely exit wake mode
static void st_ppg_samp_motion_detect_entry(void *o)
{
    k_event_clear(&ppg_wr_events, EVT_ALL); // fresh inbox for this state
    atomic_set(&m_curr_state, PPG_SAMP_STATE_MOTION_DETECT);

    // Shorter polling for FIFO contents
    k_timer_start(&tmr_ppg_wrist_sampling, K_SECONDS(1), K_SECONDS(1));
}

static enum smf_state_result st_ppg_samp_motion_detect_run(void *o)
{
    // Bounded wait: give the hub FIFO up to max_checks x 1 s to report accel
    // samples after wake-on-motion. Blocks on the event (no busy-poll) and
    // feeds the watchdog between checks.
    const int max_checks = 5;

    for (int checks = 0; checks < max_checks; checks++)
    {
        uint32_t ev = k_event_wait(&ppg_wr_events, EVT_MOTION_FIFO, false, K_SECONDS(1));
        hpi_watchdog_feed(wdt_ch_ppg_wr);

        if (ev & EVT_MOTION_FIFO)
        {
            k_event_clear(&ppg_wr_events, EVT_MOTION_FIFO);
            // We have accel samples in the hub FIFO - now disable wake-on-motion and restart algorithms
            wr_set_op_mode(MAX32664C_OP_MODE_EXIT_WAKE_ON_MOTION, MAX32664C_ALGO_MODE_NONE);
            k_msleep(50); // TODO(P2 step 2): convert settle delay to a timed state
            // Move to ACTIVE state where algorithms will be re-enabled
            smf_set_state(SMF_CTX(&sm_ctx_ppg_wr), &ppg_samp_states[PPG_SAMP_STATE_ACTIVE]);
            return SMF_EVENT_HANDLED;
        }
    }

    // No FIFO samples observed - return to OFF_SKIN and re-arm wake-on-motion
    smf_set_state(SMF_CTX(&sm_ctx_ppg_wr), &ppg_samp_states[PPG_SAMP_STATE_OFF_SKIN]);
    return SMF_EVENT_HANDLED;
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
        /* Lazy task-watchdog registration: succeeds once the watchdog is
         * initialised after boot. The run handlers block on the event inbox
         * with a bounded timeout and feed this channel, so a stalled SMF now
         * triggers a controlled reset instead of hanging silently. */
        if (wdt_ch_ppg_wr < 0)
        {
            wdt_ch_ppg_wr = hpi_watchdog_register("smf_ppg_wrist", 10000);
        }

        ret = smf_run_state(SMF_CTX(&sm_ctx_ppg_wr));

        if (ret)
        {
            LOG_ERR("Error in PPG State Machine");
            break;
        }
        /* No k_msleep here: each state's run handler blocks on the event
         * inbox (k_event_wait with PPG_WR_HOUSEKEEP_MS timeout), which paces
         * the loop and feeds the watchdog. */
    }
}

static void ppg_wrist_ctrl_thread(void)
{
    /* Block on the spo2_evt inbox (start/cancel posted by the SpO2 UI screens,
     * stop posted by the decode workqueue). k_event_wait waits on all three
     * bits at once; the thread consumes all three every wake so none linger.
     * The bounded timeout paces watchdog feeding (SpO2 command sequencing below
     * sleeps at most ~1.6 s, well under the 10 s WDT window). */
    for (;;)
    {
        /* Block until a start/stop/cancel request arrives (or wake to feed the
         * watchdog). All three are consumed below, so no bit lingers set. */
        k_event_wait(&spo2_evt, EVT_SPO2_START | EVT_SPO2_STOP | EVT_SPO2_CANCEL,
                     false, K_MSEC(PPG_WR_HOUSEKEEP_MS));

        if (wdt_ch_ppg_ctrl < 0)
        {
            wdt_ch_ppg_ctrl = hpi_watchdog_register("ppg_wrist_ctrl", 10000);
        }
        hpi_watchdog_feed(wdt_ch_ppg_ctrl);

        bool start_req = hpi_evt_consume(&spo2_evt, EVT_SPO2_START);
        if (start_req)
        {
            /* A fresh Start is the user's latest intent. Any Stop/Cancel bits
             * that coalesced into this same wake belong to a *previous*
             * measurement - the ~1.2 s of op-mode sleeps below widen the
             * window in which a prior measurement's Stop (decode) or Cancel
             * (UI) can pile up behind a new Start. Honoring them after arming
             * would immediately tear the new measurement down to CONT_HRM
             * while the UI still shows "measuring" -> stuck. Drop them so the
             * new measurement starts from a clean slate. */
            k_event_clear(&spo2_evt, EVT_SPO2_STOP | EVT_SPO2_CANCEL);

            // smf_set_terminate(SMF_CTX(&sm_ctx_ppg_wr);
            LOG_DBG("Stopping PPG Sampling");
            k_timer_stop(&tmr_ppg_wrist_sampling);

            LOG_DBG("Starting One Shot SpO2");

            /* No navigation here: the SpO2 idle tile opens SCR_SPL_SPO2_MEASURE
             * itself when it posts EVT_SPO2_START, so this thread never has to
             * touch the UI. (The finger SMF does navigate, because it must find
             * and power its sensor before there is anything to show.) */

            wr_set_op_mode(MAX32664C_OP_MODE_STOP_ALGO, MAX32664C_ALGO_MODE_NONE);
            k_msleep(600);
            wr_set_op_mode(MAX32664C_OP_MODE_ALGO_AEC, MAX32664C_ALGO_MODE_CONT_HR_SHOT_SPO2);
            k_msleep(600);
            k_timer_start(&tmr_ppg_wrist_sampling, K_MSEC(PPG_WRIST_SAMPLING_INTERVAL_MS), K_MSEC(PPG_WRIST_SAMPLING_INTERVAL_MS));

            atomic_set(&spo2_measurement_in_progress, 1);
            /* H-REC: bracket the wrist SpO2 spot-check. Completion/timeout post
             * EVT_SPO2_STOP (handled below) and user cancel posts EVT_SPO2_CANCEL,
             * so every teardown path funnels through the two handlers below. */
            hpi_data_set_ppg_wrist_record_active(true);
        }

        if (!start_req && hpi_evt_consume(&spo2_evt, EVT_SPO2_STOP))
        {
            LOG_DBG("Stopping One Shot SpO2");
            k_timer_stop(&tmr_ppg_wrist_sampling);
            atomic_set(&spo2_measurement_in_progress, 0);
            hpi_data_set_ppg_wrist_record_active(false);   // H-REC: finalize capture
            wr_set_op_mode(MAX32664C_OP_MODE_STOP_ALGO, MAX32664C_ALGO_MODE_NONE);
            uint16_t m_est_spo2 = 0;
            enum spo2_meas_state m_est_spo2_status = SPO2_MEAS_UNK;
            get_measured_spo2(&m_est_spo2, &m_est_spo2_status);
            if (m_est_spo2_status == SPO2_MEAS_SUCCESS)
            {
                LOG_DBG("SPO2 Measurement Successful: %d", m_est_spo2);
                /* The measure screen routes to SCR_SPL_SPO2_RESULT off the same
                 * sample that completed the spot check, so there is no UI work
                 * to do here. */
            }
            else if (m_est_spo2_status == SPO2_MEAS_TIMEOUT)
            {
                LOG_DBG("SPO2 Measurement Timeout");
            }
            else
            {
                LOG_DBG("SPO2 Measurement Unknown Status");
            }

            k_msleep(1000);

            LOG_DBG("Switching to Continuous Sampling HR");
            wr_set_op_mode(MAX32664C_OP_MODE_ALGO_AEC, MAX32664C_ALGO_MODE_CONT_HRM);
            k_msleep(600);
            k_timer_start(&tmr_ppg_wrist_sampling, K_MSEC(PPG_WRIST_SAMPLING_INTERVAL_MS), K_MSEC(PPG_WRIST_SAMPLING_INTERVAL_MS));
        }

        /* Handle user-initiated cancellation of SpO2 measurement */
        if (!start_req && hpi_evt_consume(&spo2_evt, EVT_SPO2_CANCEL))
        {
            if (atomic_get(&spo2_measurement_in_progress))
            {
                LOG_DBG("Cancelling One Shot SpO2 (user initiated)");
                k_timer_stop(&tmr_ppg_wrist_sampling);
                atomic_set(&spo2_measurement_in_progress, 0);
                hpi_data_set_ppg_wrist_record_active(false);   // H-REC: finalize capture
                wr_set_op_mode(MAX32664C_OP_MODE_STOP_ALGO, MAX32664C_ALGO_MODE_NONE);

                k_msleep(600);

                /* Return to continuous HR monitoring mode */
                LOG_DBG("Switching to Continuous Sampling HR after cancel");
                wr_set_op_mode(MAX32664C_OP_MODE_ALGO_AEC, MAX32664C_ALGO_MODE_CONT_HRM);
                k_msleep(600);
                k_timer_start(&tmr_ppg_wrist_sampling, K_MSEC(PPG_WRIST_SAMPLING_INTERVAL_MS), K_MSEC(PPG_WRIST_SAMPLING_INTERVAL_MS));
            }
        }
        /* No trailing k_msleep: the k_event_wait at the top of the loop blocks
         * until a request arrives or the housekeeping timeout elapses. */
    }
}

#define PPG_CTRL_THREAD_STACKSIZE 1024
#define PPG_CTRL_THREAD_PRIORITY 7

#define SMF_PPG_THREAD_STACKSIZE 4096
#define SMF_PPG_THREAD_PRIORITY 7

K_THREAD_DEFINE(smf_ppg_thread_id, SMF_PPG_THREAD_STACKSIZE, smf_ppg_wrist_thread, NULL, NULL, NULL, SMF_PPG_THREAD_PRIORITY, 0, 1000);
K_THREAD_DEFINE(ppg_ctrl_thread_id, PPG_CTRL_THREAD_STACKSIZE, ppg_wrist_ctrl_thread, NULL, NULL, NULL, PPG_CTRL_THREAD_PRIORITY, 0, 0);