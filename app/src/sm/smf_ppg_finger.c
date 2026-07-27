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

LOG_MODULE_REGISTER(smf_ppg_finger, LOG_LEVEL_DBG);

#include "hw_module.h"
#include "max32664d.h"
#include "hpi_common_types.h"
#include "fs_module.h"
#include "ui/move_ui.h"
#include "hpi_sys.h"
#include "hpi_watchdog.h"
#include "hpi_evt.h"

/* Finger PPG (BPT/SpO2) cross-module event object (see hpi_evt.h). Replaces the
 * former semaphore web between the UI screens, hw_module and this SMF / the
 * display. Internal complete/contact/sampling sems stay as
 * semaphores. */
K_EVENT_DEFINE(fi_evt);

#define PPG_FI_SAMPLING_INTERVAL_MS 20
#define MAX30101_SENSOR_ID 0x15
#define BPT_CAL_TIMEOUT_MS 15000
#define FINGER_SENSOR_CONTACT_TIMEOUT_MS 30000


// Note: sem_bpt_sensor_found and sem_spo2_sensor_found removed - they were never consumed
// If UI needs notification, use ZBus channel instead

K_SEM_DEFINE(sem_start_fi_sampling, 0, 1);
K_SEM_DEFINE(sem_stop_fi_sampling, 0, 1);

K_SEM_DEFINE(sem_bpt_est_complete, 0, 1);
K_SEM_DEFINE(sem_bpt_cal_complete, 0, 1);
K_SEM_DEFINE(sem_spo2_est_complete, 0, 1);


K_SEM_DEFINE(sem_finger_contact_off, 0, 1);
K_SEM_DEFINE(sem_finger_contact_on, 0, 1);

ZBUS_CHAN_DECLARE(bpt_chan);
ZBUS_CHAN_DECLARE(spo2_chan);

K_MSGQ_DEFINE(q_ppg_fi_sample, sizeof(struct hpi_ppg_fi_data_t), 64, 1);

SENSOR_DT_READ_IODEV(max32664d_iodev, DT_ALIAS(max32664d), {SENSOR_CHAN_VOLTAGE});
RTIO_DEFINE(max32664d_read_rtio_poll_ctx, 8, 8);

static const struct smf_state ppg_fi_states[];

enum ppg_fi_op_modes
{
    PPG_FI_OP_MODE_IDLE,
    PPG_FI_OP_MODE_BPT_EST,
    PPG_FI_OP_MODE_BPT_CAL,
    PPG_FI_OP_MODE_SPO2_EST,
};

enum ppg_fi_sm_state
{
    PPG_FI_STATE_IDLE,
    PPG_FI_STATE_CHECK_SENSOR,
    PPG_FI_STATE_SENSOR_FAIL,

    PPG_FI_STATE_BPT_EST,
    PPG_FI_STATE_BPT_EST_DONE,
    PPG_FI_STATE_BPT_EST_FAIL,

    PPG_FI_STATE_BPT_CAL,
    PPG_FI_STATE_BPT_CAL_WAIT,
    PPG_FI_STATE_BPT_CAL_DONE,
    PPG_FI_STATE_BPT_CAL_FAIL,

    PPG_FI_STATE_SPO2_EST,
    PPG_FI_STATE_SPO2_EST_DONE,

    PPG_FI_STATE_WAIT_FOR_CONTACT,
};

struct s_ppg_fi_object
{
    struct smf_ctx ctx;
    uint8_t ppg_fi_op_mode;
    uint8_t bpt_cal_curr_index;

} sf_obj;

static uint8_t volatile sens_decode_ppg_fi_op_mode = PPG_FI_OP_MODE_IDLE;

uint8_t bpt_cal_vector_buf[CAL_VECTOR_SIZE] = {0};

// Forward declarations

static void hw_bpt_start_cal(int cal_index, int cal_sys, int cal_dia);
static void hpi_bpt_fetch_cal_vector(uint8_t *bpt_cal_vector_buf, uint8_t l_cal_index);
static void hpi_bpt_stop(void);

/* BPT/SpO2 completion flags: set in sensor_ppg_finger_decode (sys workqueue),
 * read and reset in SMF state fns (smf_ppg_finger_thread) -> atomic. */
static atomic_t bpt_process_done = ATOMIC_INIT(0);
static atomic_t spo2_process_done = ATOMIC_INIT(0);

/* Number of BPT cal vectors that loaded on the most recent estimation start
 * (set in wait-for-contact entry, read at the BPT_EST transition to fail loud on
 * an unloadable/missing calibration). Finger SMF thread context only. */
static int m_bpt_cal_loaded;

static uint8_t m_cal_index;
static uint8_t m_cal_sys;
static uint8_t m_cal_dia;
static uint8_t m_cal_hr;

static uint8_t m_est_sys;
static uint8_t m_est_dia;
static uint8_t m_est_hr;
static uint8_t m_est_spo2;
static uint8_t m_est_spo2_conf;

K_MUTEX_DEFINE(mutex_bpt_cal_set);

// Externs
extern const struct device *const max32664d_dev;

static int64_t smf_ppg_fi_spo2_last_measured_time;

static int64_t wait_start_ts;
static atomic_t finger_contact_ok = ATOMIC_INIT(0); /* decode(workqueue) writes, SMF reads/resets */  
static bool prev_finger_contact_ok = false;
static int64_t contact_lost_start_ts = 0;
static uint8_t contact_debounce_ts = 0;

void hpi_bpt_set_cal_vals(uint8_t cal_index, uint8_t cal_sys, uint8_t cal_dia)
{
    k_mutex_lock(&mutex_bpt_cal_set, K_FOREVER);
    m_cal_index = cal_index;
    m_cal_sys = cal_sys;
    m_cal_dia = cal_dia;
    k_mutex_unlock(&mutex_bpt_cal_set);
}

/* ---- BPT calibration control API (HPI_HS group v2, cmds 8-11) ----
 * Called from the SMP/BLE (mgmt handler) thread, so these must NOT block: they
 * only post fi_evt bits, set cal values, or read a cached snapshot. The finger
 * SMF thread does the actual MAX32664D cal work. Live [status,progress] feedback
 * is polled via hpi_bpt_cal_status (SMP is client→response only). Cal is a fixed
 * 3 points (idx 0..2). Snapshot bytes are updated in the decode path; single-byte
 * loads/stores are atomic on Cortex-M, so a poll can't tear. */
static volatile uint8_t s_cal_st   = 0;      /* latest bpt_status (see HPI_HS_API) */
static volatile uint8_t s_cal_prog = 0;      /* latest bpt_progress 0..100         */
static atomic_t s_cal_run = ATOMIC_INIT(0);  /* a cal point is in flight           */

int hpi_bpt_cal_enter(void)
{
    /* Idempotent: entering cal mode while already in it just re-arms. */
    LOG_INF("hpi_bpt_cal_enter: posting EVT_BPT_ENTER_CAL");
    k_event_post(&fi_evt, EVT_BPT_ENTER_CAL);
    return 0;
}

int hpi_bpt_cal_point(uint8_t sys, uint8_t dia, uint8_t idx)
{
    if (idx >= 3) {
        LOG_WRN("hpi_bpt_cal_point: idx=%u out of range -> EINVAL", idx);
        return -EINVAL;             /* calibration is a fixed 3 points (0..2) */
    }
    if (atomic_get(&s_cal_run)) {
        LOG_WRN("hpi_bpt_cal_point: point already running -> EBUSY");
        return -EBUSY;              /* a point is already being measured */
    }
    hpi_bpt_set_cal_vals(idx, sys, dia);
    sf_obj.bpt_cal_curr_index = idx;   /* reported by hpi_bpt_cal_status (BLE cmd 10 + on-watch POINT n/3) */
    atomic_set(&s_cal_run, 1);
    s_cal_st = 0;
    s_cal_prog = 0;
    LOG_INF("hpi_bpt_cal_point: sys=%u dia=%u idx=%u -> posting EVT_BPT_CAL_START",
            sys, dia, idx);
    k_event_post(&fi_evt, EVT_BPT_CAL_START);
    return 0;
}

int hpi_bpt_cal_end(void)
{
    LOG_INF("hpi_bpt_cal_end: posting EVT_BPT_EXIT_CAL");
    atomic_set(&s_cal_run, 0);
    k_event_post(&fi_evt, EVT_BPT_EXIT_CAL);
    return 0;
}

void hpi_bpt_cal_status(uint8_t *st, uint8_t *prog, uint8_t *idx, bool *run)
{
    if (st)   { *st   = s_cal_st; }
    if (prog) { *prog = s_cal_prog; }
    if (idx)  { *idx  = sf_obj.bpt_cal_curr_index; }
    if (run)  { *run  = atomic_get(&s_cal_run) != 0; }
}


static void sensor_ppg_finger_decode(uint8_t *buf, uint32_t buf_len, uint8_t m_ppg_op_mode)
{
    const struct max32664d_encoded_data *edata = (const struct max32664d_encoded_data *)buf;
    struct hpi_ppg_fi_data_t ppg_sensor_sample;

    uint8_t finger_status = edata->bpt_status ;

    /* Check finger contact status
       finger_status = 1 -> Good signal
       finger_status = 2 -> Success
       finger_status = 4 -> Motion detected
    */
   
    bool current_contact = (finger_status == 4 || finger_status == 1 || finger_status == 2);

   if (current_contact) {
    contact_debounce_ts= 0;
    
    // Only set to true if it was false before
    if (!atomic_get(&finger_contact_ok)) {
        atomic_set(&finger_contact_ok, 1);
        LOG_INF("Finger Contact status: %d", finger_status);
        
        // Signal that contact was just detected
        if (!prev_finger_contact_ok) {
            k_sem_give(&sem_finger_contact_on);
            wait_start_ts = 0;  // Reset wait timer
        }
     }
    } else {
        if(contact_debounce_ts == 0)
            contact_debounce_ts = k_uptime_get();
        else if (k_uptime_get() - contact_debounce_ts >= 1000) {  // Require 1000ms(1 sec) of bad readings
            // Only set to false if it was true before
            if (atomic_get(&finger_contact_ok)) {
                atomic_set(&finger_contact_ok, 0);
                //LOG_INF("Finger contact lost (confirmed after %d ms)", k_uptime_get() - contact_debounce_ts);
                
                // Signal that contact was just lost
                if (prev_finger_contact_ok) {
                    k_sem_give(&sem_finger_contact_off);
                }
            }
        }
    }

    // Update previous state
    prev_finger_contact_ok = atomic_get(&finger_contact_ok);

    uint16_t _n_samples = edata->num_samples;
    // Cap to the FI PPG points per sample (driver may return up to 32)
    if (_n_samples > BPT_PPG_POINTS_PER_SAMPLE)
    {
        _n_samples = BPT_PPG_POINTS_PER_SAMPLE;
    }

    if (_n_samples > 0)
    {
        ppg_sensor_sample.ppg_num_samples = _n_samples;

        for (int i = 0; i < _n_samples; i++)
        {
            ppg_sensor_sample.raw_red[i] = edata->red_samples[i];
            ppg_sensor_sample.raw_ir[i] = edata->ir_samples[i];
        }
        ppg_sensor_sample.hr = edata->hr;
        ppg_sensor_sample.spo2 = edata->spo2;

        if (m_ppg_op_mode == PPG_FI_OP_MODE_BPT_EST || m_ppg_op_mode == PPG_FI_OP_MODE_BPT_CAL)
        {
            ppg_sensor_sample.bp_sys = edata->bpt_sys;
            ppg_sensor_sample.bp_dia = edata->bpt_dia;
            ppg_sensor_sample.bpt_status = edata->bpt_status;
            ppg_sensor_sample.bpt_progress = edata->bpt_progress;
        }
        else if (m_ppg_op_mode == PPG_FI_OP_MODE_SPO2_EST)
        {
            ppg_sensor_sample.spo2_valid_percent_complete = edata->spo2_conf;
            if (edata->spo2_conf < 70)
            {
                ppg_sensor_sample.spo2_state = SPO2_MEAS_COMPUTATION;
            }
            else if (atomic_get(&spo2_process_done) == 0)
            {
                ppg_sensor_sample.spo2_state = SPO2_MEAS_SUCCESS;
                LOG_INF("SpO2 Measurement Done");
                hpi_bpt_stop();  // Stop the sensor algorithms
                /* Publish so the health store + display last-value path see the
                 * reading (same channel the wrist SMF uses). Without this the
                 * idle tile never got a timestamp for a finger spot check. */
                if (edata->spo2 > 0) {
                    struct hpi_spo2_point_t spo2_pt = {
                        .timestamp = hw_get_sys_time_ts(),
                        .spo2 = edata->spo2,
                    };
                    zbus_chan_pub(&spo2_chan, &spo2_pt, K_SECONDS(1));
                }
                k_sem_give(&sem_spo2_est_complete);
                atomic_set(&spo2_process_done, 1);
            }
        }

        if (k_msgq_put(&q_ppg_fi_sample, &ppg_sensor_sample, K_MSEC(1)) != 0)
        {
            static uint32_t fi_drops = 0;
            if ((++fi_drops % 10) == 0)
            {
                LOG_WRN("q_ppg_fi_sample full - dropped %u finger PPG batches", fi_drops);
            }
        }
        // k_sem_give(&sem_ppg_finger_sample_trigger);

        // LOG_DBG("Status: %d Progress: %d Sys: %d Dia: %d SpO2: %d", edata->bpt_status, edata->bpt_progress, edata->bpt_sys, edata->bpt_dia, edata->spo2);

        if (m_ppg_op_mode == PPG_FI_OP_MODE_BPT_EST || m_ppg_op_mode == PPG_FI_OP_MODE_BPT_CAL)
        {
            struct hpi_bpt_t bpt_data = {
                .timestamp = hw_get_sys_time_ts(),
                .sys = edata->bpt_sys,
                .dia = edata->bpt_dia,
                .hr = edata->hr,
                .status = edata->bpt_status,
                .progress = edata->bpt_progress,
            };
            zbus_chan_pub(&bpt_chan, &bpt_data, K_SECONDS(1));

            /* Cache for the polled BPT_CAL_STATUS command. Terminal status ends
             * the in-flight point: 2 = point complete, 6 = cal failed. */
            if (m_ppg_op_mode == PPG_FI_OP_MODE_BPT_CAL) {
                s_cal_st   = edata->bpt_status;
                s_cal_prog = edata->bpt_progress;
                if (edata->bpt_status == 2 || edata->bpt_status == 6) {
                    atomic_set(&s_cal_run, 0);
                }
            }

            if (edata->bpt_progress == 100 && atomic_get(&bpt_process_done) == 0)
            {
                hpi_bpt_stop();
                if (m_ppg_op_mode == PPG_FI_OP_MODE_BPT_CAL)
                {
                    // BPT Calibration done
                    LOG_INF("BPT Calibration Done");
                    /* Clear the in-flight flag on the SAME edge that completes the
                     * point. It used to be cleared only on bpt_status 2/6 above,
                     * but the point actually ends HERE, on progress == 100, which
                     * immediately stops the hub and the sampling loop. If the hub's
                     * terminal status byte landed in a report that arrived after
                     * that, it was never decoded, s_cal_run stayed set forever, and
                     * every later CAL_POINT returned -EBUSY -- the app's "no option
                     * to continue to the third point", intermittent by nature. */
                    atomic_set(&s_cal_run, 0);
                    k_sem_give(&sem_bpt_cal_complete);
                    m_cal_hr = edata->hr;
                }
                else if (m_ppg_op_mode == PPG_FI_OP_MODE_BPT_EST)
                {
                    // BPT Estimation done
                    LOG_INF("BPT Estimation Done");
                    k_sem_give(&sem_bpt_est_complete);
                    m_est_dia = edata->bpt_dia;
                    m_est_sys = edata->bpt_sys;
                    m_est_hr = edata->hr;
                    m_est_spo2 = edata->spo2;
                }
                atomic_set(&bpt_process_done, 1);
            }
        }
        else if (m_ppg_op_mode == PPG_FI_OP_MODE_SPO2_EST)
        {
            // SpO2 Estimation done
            m_est_spo2 = edata->spo2;
            m_est_spo2_conf = edata->spo2_conf;

            if (m_est_spo2 > 0 && m_est_spo2_conf > 50) {
                smf_ppg_fi_spo2_last_measured_time = hw_get_sys_time_ts();
            }

            LOG_DBG("SpO2: %d | Confidence: %d", edata->spo2, edata->spo2_conf);

            // k_sem_give(&sem_bpt_est_complete);
        }
    }
}

void work_fi_sample_handler(struct k_work *work)
{
    uint8_t data_buf[384];

    int ret = 0;
    static int consecutive_timeouts = 0;
    static int sample_count = 0;
    sample_count++;
    if (sample_count <= 5 || sample_count % 10 == 0) {
        LOG_DBG("Reading sensor data (sample #%d)...", sample_count);
    }
    ret = sensor_read(&max32664d_iodev, &max32664d_read_rtio_poll_ctx, data_buf, sizeof(data_buf));
    if (ret < 0)
    {
        if (ret == -ETIMEDOUT) {
            consecutive_timeouts++;
            LOG_WRN("Sensor read timed out (%d). consecutive_timeouts=%d", ret, consecutive_timeouts);
            if (consecutive_timeouts >= 3) {
                LOG_ERR("Multiple consecutive sensor timeouts, stopping sampling");
                k_sem_give(&sem_stop_fi_sampling);
                consecutive_timeouts = 0;
            }
        } else {
            LOG_ERR("Error reading sensor data: %d", ret);
        }
        return;
    }
    consecutive_timeouts = 0;
    sensor_ppg_finger_decode(data_buf, sizeof(data_buf), sens_decode_ppg_fi_op_mode);
}
K_WORK_DEFINE(work_fi_sample, work_fi_sample_handler);

static void ppg_fi_sampling_handler(struct k_timer *timer_id)
{
    k_work_submit(&work_fi_sample);
}

K_TIMER_DEFINE(tmr_ppg_fi_sampling, ppg_fi_sampling_handler, NULL);

static void hw_bpt_encode_date_time(struct tm *curr_time, uint32_t *date, uint32_t *time)
{
    struct tm timeinfo;
    timeinfo.tm_year = curr_time->tm_year;
    timeinfo.tm_mon = curr_time->tm_mon;
    timeinfo.tm_mday = curr_time->tm_mday;
    timeinfo.tm_hour = curr_time->tm_hour;
    timeinfo.tm_min = curr_time->tm_min;
    timeinfo.tm_sec = curr_time->tm_sec;

    uint32_t encoded_date = (((timeinfo.tm_year + 1900) * 10000) + ((timeinfo.tm_mon + 1) * 100) + timeinfo.tm_mday);
    uint32_t encoded_time = ((timeinfo.tm_hour * 10000) + (timeinfo.tm_min * 100) + timeinfo.tm_sec);

    LOG_DBG("Encoded Date: %d, Time: %d", encoded_date, encoded_time);

    *date = encoded_date;
    *time = encoded_time;
}

/*
 * Loads the BPT calibration vectors into the sensor and starts BPT estimation
 * (also used to arm finger-contact detection for the SpO2 flow). Returns the
 * number of cal vectors that actually loaded (0..3). A per-vector load failure
 * used to be swallowed with LOG_ERR and estimation proceeded anyway -> a
 * present-but-corrupt cal produced a silent *uncalibrated* BP estimate. The
 * caller now uses the count to fail loud (CAL_REQUIRED) when nothing loaded,
 * for BPT estimation specifically (SpO2 does not use BP cal).
 */
static int hw_bpt_start_est(void)
{
    LOG_INF("Starting BPT Estimation");
    // ppg_fi_op_mode = PPG_FI_OP_MODE_BPT_EST;
    atomic_set(&bpt_process_done, 0);

    struct tm curr_time = hpi_sys_get_current_time();

    uint32_t date, time;
    hw_bpt_encode_date_time(&curr_time, &date, &time);

    struct sensor_value data_time_val;
    data_time_val.val1 = date; // Date
    data_time_val.val2 = time; // Time
    sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664D_ATTR_SET_DATE_TIME, &data_time_val);

    char m_file_name[32];
    int cal_loaded = 0;

    for (int i = 0; i < 3; i++)
    {
        snprintf(m_file_name, sizeof(m_file_name), "/lfs/sys/bpt_cal_%d", i);
        // Load calibration vector 0
        if (fs_load_file_to_buffer(m_file_name, bpt_cal_vector_buf, CAL_VECTOR_SIZE) == 0)
        {
            max32664d_set_bpt_cal_vector(max32664d_dev, i, bpt_cal_vector_buf);
            LOG_INF("Loaded calibration vector %d", i);
            cal_loaded++;
        }
        else
        {
            LOG_ERR("Failed to load calibration vector %d", i);
        }
        k_sleep(K_MSEC(40));
    }

    struct sensor_value mode_val;
    mode_val.val1 = MAX32664D_OP_MODE_BPT_EST;
    sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664D_ATTR_OP_MODE, &mode_val);

    k_sleep(K_MSEC(1000));

    return cal_loaded;
}

static void hw_bpt_start_cal(int cal_index, int cal_sys, int cal_dia)
{
    LOG_INF("Starting BPT Calibration");
    // ppg_fi_op_mode = PPG_FI_OP_MODE_BPT_CAL;
    atomic_set(&bpt_process_done, 0);
    // Set the date and time for the BPT calibration
    struct tm curr_time = hpi_sys_get_current_time();

    uint32_t date, time;
    hw_bpt_encode_date_time(&curr_time, &date, &time);

    struct sensor_value data_time_val;
    data_time_val.val1 = date; // Date
    data_time_val.val2 = time; // Time
    sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664D_ATTR_SET_DATE_TIME, &data_time_val);

    struct sensor_value cal_idx_val;
    cal_idx_val.val1 = cal_index;
    sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664D_ATTR_CAL_SET_CURR_INDEX, &cal_idx_val);

    struct sensor_value cal_sys_val;
    cal_sys_val.val1 = cal_sys;
    sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664D_ATTR_CAL_SET_CURR_SYS, &cal_sys_val);

    struct sensor_value cal_dia_val;
    cal_dia_val.val1 = cal_dia;
    sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664D_ATTR_CAL_SET_CURR_DIA, &cal_dia_val);

    struct sensor_value mode_val;
    mode_val.val1 = MAX32664D_OP_MODE_BPT_CAL_START;
    sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664D_ATTR_OP_MODE, &mode_val);
}

static void hpi_bpt_stop(void)
{
    struct sensor_value mode_val;
    mode_val.val1 = MAX32664D_ATTR_STOP_EST;
    sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664D_ATTR_STOP_EST, &mode_val);
}

static void hpi_bpt_fetch_cal_vector(uint8_t *bpt_cal_vector_buf, uint8_t l_cal_index)
{
    char cal_file_name[32];
    struct sensor_value fetch_cal;
    fetch_cal.val1 = 0;
    sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664D_ATTR_CAL_FETCH_VECTOR, &fetch_cal);

    max32664d_load_bpt_cal_vector(max32664d_dev, bpt_cal_vector_buf);
    snprintf(cal_file_name, sizeof(cal_file_name), "/lfs/sys/bpt_cal_%d", l_cal_index);

    fs_write_buffer_to_file(cal_file_name, bpt_cal_vector_buf, CAL_VECTOR_SIZE);
}

void hpi_bpt_abort(void)
{
    atomic_set(&s_cal_run, 0);   /* BPT_CAL_STATUS: no point in flight after abort */

    /* Stop sampling first */
    k_sem_give(&sem_stop_fi_sampling);

    /* Ask the sensor driver to stop algorithms and power down */
    /* Use the attribute-based stop (driver handles algorithm stop) */
    hpi_bpt_stop();

    /* Power off sensor since idle entry no longer does this */
    hpi_hw_fi_sensor_off();

    /* Ensure state machine goes to IDLE */
    smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_IDLE]);

}

static void st_ppg_fing_idle_entry(void *o)
{
    LOG_DBG("PPG Finger SM Idle Entry");
    k_sem_give(&sem_stop_fi_sampling);
    atomic_set(&bpt_process_done, 0);
    atomic_set(&spo2_process_done, 0);
    sens_decode_ppg_fi_op_mode = PPG_FI_OP_MODE_IDLE;

    // Clear any stale cancel semaphores to prevent issues on next measurement attempt
    // These might have been given during cancellation and not consumed
    k_event_clear(&fi_evt, EVT_FI_SPO2_CANCEL);
    k_event_clear(&fi_evt, EVT_FI_BPT_EST_CANCEL);
    k_event_clear(&fi_evt, EVT_FI_BPT_CAL_CANCEL);
    atomic_set(&finger_contact_ok, 0); 
    contact_lost_start_ts = 0;
    contact_debounce_ts = 0;
}

// Add a new function to check if calibration data exists
static bool hpi_bpt_cal_data_available(void)
{
    char m_file_name[32];
    
    // Check if at least one calibration file exists
    for (int i = 0; i < 3; i++)
    {
        snprintf(m_file_name, sizeof(m_file_name), "/lfs/sys/bpt_cal_%d", i);
        if (fs_check_file_exists(m_file_name) == 0) // Assuming fs_check_file_exists returns 0 if file exists
        {
            LOG_DBG("Calibration data found for index %d", i);
            return true;
        }
    }
    
    LOG_WRN("No calibration data found");
    return false;
}

static enum smf_state_result st_ppg_fing_idle_run(void *o)
{
    // LOG_DBG("PPG Finger SM Idle Running");
    struct s_ppg_fi_object *s = (struct s_ppg_fi_object *)o;

    if (hpi_evt_consume(&fi_evt, EVT_BPT_EST_START))
    {
        // Check if calibration data is available before proceeding
        if (hpi_bpt_cal_data_available())
        {
            s->ppg_fi_op_mode = PPG_FI_OP_MODE_BPT_EST;
            smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_CHECK_SENSOR]);
        }
        else
        {
            LOG_WRN("BPT Calibration required before estimation");
            // Display message that calibration is required
            
            hpi_load_scr_spl(SCR_SPL_BPT_CAL_REQUIRED, SCROLL_NONE, SCR_BPT, 0, 0, 0);
            // Stay in idle state
        }
    }

    if (hpi_evt_consume(&fi_evt, EVT_BPT_ENTER_CAL))
    {
        LOG_INF("sem_bpt_enter_mode_cal received - entering BPT calibration mode");
        s->ppg_fi_op_mode = PPG_FI_OP_MODE_BPT_CAL;
        // Route through CHECK_SENSOR to ensure sensor is connected before showing wait screen
        smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_CHECK_SENSOR]);
    }

    if (hpi_evt_consume(&fi_evt, EVT_FI_SPO2_START))
    {
        s->ppg_fi_op_mode = PPG_FI_OP_MODE_SPO2_EST;
        smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_CHECK_SENSOR]);
    }

    /* Allow cal to start from IDLE if EVT_BPT_CAL_START arrives without a prior
     * EVT_BPT_ENTER_CAL (UI may skip the intermediate mode). Route CHECK_SENSOR first. */
    if (hpi_evt_consume(&fi_evt, EVT_BPT_CAL_START))
    {
        LOG_INF("BPT calibration start from IDLE - checking sensor first");
        s->ppg_fi_op_mode = PPG_FI_OP_MODE_BPT_CAL;
        // Store that we have cal values ready and should proceed to calibration after sensor check
        // The CHECK_SENSOR state will route to BPT_CAL_WAIT, then BPT_CAL_WAIT will see
        // that cal values are set and can proceed
        smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_CHECK_SENSOR]);
    }
    return SMF_EVENT_HANDLED;
}

static void bpt_cal_timeout_handler(struct k_timer *timer_id)
{
    LOG_ERR("BPT Calibration Timeout");
    k_sem_give(&sem_stop_fi_sampling); // Stop sampling if needed
    hpi_load_scr_spl(SCR_SPL_SPO2_BPT_TIMEOUT, SCROLL_NONE, SCR_BPT, 0, 0, 0);
    smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_BPT_CAL_FAIL]); // Transition to failure state
}

// Declare a timer for the timeout
K_TIMER_DEFINE(tmr_bpt_cal_timeout, bpt_cal_timeout_handler, NULL);

static void st_ppg_fi_cal_wait_entry(void *o)
{
    k_event_clear(&fi_evt, EVT_BPT_EXIT_CAL);
    k_event_clear(&fi_evt, EVT_FI_BPT_CAL_CANCEL);
    LOG_DBG("PPG Finger SM BPT Calibration Wait Entry");
    hpi_load_scr_spl(SCR_SPL_BPT_CAL_PROGRESS, SCROLL_NONE, SCR_BPT, 0, 0, 0);

    // Start the timeout timer
    // k_timer_start(&tmr_bpt_cal_timeout, K_MSEC(BPT_CAL_TIMEOUT_MS), K_NO_WAIT);
}

static enum smf_state_result st_ppg_fi_cal_wait_run(void *o)
{

   // LOG_DBG("PPG Finger SM BPT Calibration Running");
    if (hpi_evt_consume(&fi_evt, EVT_BPT_CAL_START))
    {
        LOG_INF("sem_bpt_cal_start received in CAL_WAIT state - starting calibration");
        // Turn off timeout
       // k_timer_stop(&tmr_bpt_cal_timeout);

        sens_decode_ppg_fi_op_mode = PPG_FI_OP_MODE_BPT_CAL;
        smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_BPT_CAL]);
    }

    if (hpi_evt_consume(&fi_evt, EVT_BPT_EXIT_CAL))
    {
        LOG_INF("sem_bpt_exit_mode_cal received - exiting BPT calibration mode");
        hpi_hw_fi_sensor_off();  // Power off sensor when exiting calibration mode
        hpi_bpt_stop(); // Ensure sensor algorithms are stopped
        /* persistence removed — health store will ingest this (H1) */
        hpi_load_scr_spl(SCR_SPL_BPT_EST_COMPLETE, SCROLL_NONE, m_cal_sys, m_cal_dia, m_cal_hr,1); // Show last calibration results when exiting cal mode
        smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_IDLE]);
        return SMF_EVENT_HANDLED;
    }
    return SMF_EVENT_HANDLED;
}

static void st_ppg_fing_bpt_cal_entry(void *o)
{
    LOG_INF("PPG Finger SM BPT Calibration Entry");
      // RESET progress to 0%
      struct hpi_bpt_t bpt_data = {
        .progress = 0,
        .status = 0,
     };
    zbus_chan_pub(&bpt_chan, &bpt_data, K_NO_WAIT);

    LOG_INF("Step 1: Enabling finger sensor power");
    hpi_hw_fi_sensor_on();    // Power ON
    k_msleep(1000);           // Stabilize
    LOG_INF("Step 2: Starting BPT calibration with index=%d sys=%d dia=%d", m_cal_index, m_cal_sys, m_cal_dia);
    hw_bpt_start_cal(m_cal_index, m_cal_sys, m_cal_dia);
    k_msleep(100); // Short delay to ensure sensor is processing the start command before we begin sampling
    LOG_INF("Step 3: Signaling to start sampling");
    k_sem_give(&sem_start_fi_sampling);
    LOG_INF("BPT Calibration Entry complete");
}

static enum smf_state_result st_ppg_fing_bpt_cal_run(void *o)
{
    if (k_sem_take(&sem_bpt_cal_complete, K_NO_WAIT) == 0)
    {
        k_sem_give(&sem_stop_fi_sampling);
        k_sleep(K_MSEC(1000)); // Wait for the sampling to stop
        hpi_bpt_fetch_cal_vector(bpt_cal_vector_buf, m_cal_index);
        smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_BPT_CAL_DONE]);
        return SMF_EVENT_HANDLED;
    }
    if(hpi_evt_consume(&fi_evt, EVT_BPT_EXIT_CAL))
    {
        LOG_INF("sem_bpt_exit_mode_cal received - exiting BPT calibration mode");
        hpi_load_scr_spl(SCR_BPT, SCROLL_NONE, SCR_BPT, 0, 0, 0); // Show default BPT screen when exiting cal mode through cancel button
        hpi_bpt_abort(); // Ensure sensor algorithms are stopped and sensor is powered off
        return SMF_EVENT_HANDLED;
    }

     if(hpi_evt_consume(&fi_evt, EVT_FI_BPT_CAL_CANCEL))
    {
        LOG_DBG("BPT Calibration Cancelled by user");
        hpi_bpt_abort();
        return SMF_EVENT_HANDLED;
    }
    
    return SMF_EVENT_HANDLED;
}

static void st_ppg_fing_bpt_cal_done_entry(void *o)
{
    LOG_DBG("PPG Finger SM BPT Calibration Done Entry");
    /* Backstop: reaching this state means the point is over by every route, so
     * the next hpi_bpt_cal_point() must not be able to see a stale -EBUSY. */
    atomic_set(&s_cal_run, 0);
    hpi_load_scr_spl(SCR_SPL_BPT_CAL_COMPLETE, SCROLL_NONE, SCR_BPT, 0, 0, 0);
    hpi_hw_fi_sensor_off();
}

static enum smf_state_result st_ppg_fing_bpt_cal_done_run(void *o)
{
    k_msleep(2000);
    if (hpi_evt_consume(&fi_evt, EVT_BPT_EXIT_CAL))
    {
        LOG_INF("sem_bpt_exit_mode_cal received - exiting BPT calibration mode");
        hpi_hw_fi_sensor_off();  // Power off sensor when exiting calibration mode
        hpi_bpt_stop(); // Ensure sensor algorithms are stopped
        /* persistence removed — health store will ingest this (H1) */
        hpi_load_scr_spl(SCR_SPL_BPT_EST_COMPLETE, SCROLL_NONE, m_cal_sys, m_cal_dia, m_cal_hr,1); // Show last calibration results when exiting cal mode
        smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_IDLE]);
        return SMF_EVENT_HANDLED;
    }
    smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_BPT_CAL_WAIT]);
    return SMF_EVENT_HANDLED;
}

static void st_ppg_fing_bpt_est_entry(void *o)
{
    LOG_DBG("PPG Finger SM BPT Estimation Entry");
    sens_decode_ppg_fi_op_mode = PPG_FI_OP_MODE_BPT_EST;
    /* H-REC: BPT_EST is the active BP measurement window (finger confirmed on the
     * sensor, raw IR PPG streaming). Bracket it here and in the state exit — the
     * exit fires on every leave path (done / contact-timeout / cancel via
     * hpi_bpt_abort()->smf_set_state(IDLE)), so the record always finalizes. */
    hpi_data_set_ppg_finger_record_active(true);
    // hpi_load_scr_spl(SCR_SPL_BPT_MEASURE, SCROLL_NONE, SCR_SPL_FI_SENS_CHECK, 0, 0, 0);
    // hpi_hw_fi_sensor_on();
    // hw_bpt_start_est();
    // LOG_INF("Signaling to start sampling for BPT estimation from entry");
    // k_sem_give(&sem_start_fi_sampling);
}

static enum smf_state_result st_ppg_fing_bpt_est_run(void *o)
{
  
    if (k_sem_take(&sem_bpt_est_complete, K_NO_WAIT) == 0)
    {
        k_sem_give(&sem_stop_fi_sampling);
        k_sleep(K_MSEC(1000)); // Wait for the sampling to stop
        smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_BPT_EST_DONE]);
    }


    if (atomic_get(&finger_contact_ok) == 0)
    {
        if (contact_lost_start_ts == 0)
        {
            // First time contact is lost
            contact_lost_start_ts = k_uptime_get();
            LOG_DBG("Contact lost, starting %ds timeout", FINGER_SENSOR_CONTACT_TIMEOUT_MS/1000);
        } else if ((k_uptime_get() - contact_lost_start_ts) >= FINGER_SENSOR_CONTACT_TIMEOUT_MS) {
            LOG_INF("Contact lost for %ds during measurement - aborting", FINGER_SENSOR_CONTACT_TIMEOUT_MS/1000);
            hpi_bpt_abort();
            k_event_post(&fi_evt, EVT_FI_CONTACT_TIMEOUT);
            contact_lost_start_ts = 0;
            return SMF_EVENT_HANDLED;
        }
    } 
    else
    {
            // Contact restored, reset the timer
            contact_lost_start_ts = 0;
    }
    if(hpi_evt_consume(&fi_evt, EVT_FI_BPT_EST_CANCEL))
    {
        LOG_DBG("BPT Estimation Cancelled by user");
        hpi_bpt_abort();
    }
    return SMF_EVENT_HANDLED;
}

static void st_ppg_fing_bpt_est_exit(void *o)
{
    /* H-REC: finalize the BP capture on every exit path (done / cancel / abort). */
    hpi_data_set_ppg_finger_record_active(false);
}

static void st_ppg_fing_bpt_est_done_entry(void *o)
{
    LOG_DBG("PPG Finger SM BPT Estimation Done Entry");
    hpi_load_scr_spl(SCR_SPL_BPT_EST_COMPLETE, SCROLL_NONE, m_est_sys, m_est_dia, m_est_hr,0);
    // FIX: Power off sensor when estimation is complete
    hpi_hw_fi_sensor_off();
}

static enum smf_state_result st_ppg_fing_bpt_est_done_run(void *o)
{
    LOG_DBG("PPG Finger SM BPT Estimation Done Running");
    // k_msleep(2000);
    // hpi_load_screen(SCR_BPT, SCROLL_NONE);
    smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_IDLE]);
    return SMF_EVENT_HANDLED;
}

static void st_ppg_fing_bpt_est_fail_entry(void *o)
{
    LOG_DBG("PPG Finger SM BPT Estimation Fail Entry");
    hpi_load_scr_spl(SCR_SPL_BPT_FAILED, SCROLL_NONE, SCR_BPT, BPT_FAIL_EST, 0, 0);
    hpi_hw_fi_sensor_off();
}

static enum smf_state_result st_ppg_fing_bpt_est_fail_run(void *o)
{
    // LOG_DBG("PPG Finger SM BPT Estimation Fail Running");
    return SMF_EVENT_HANDLED;
}

static void st_ppg_fing_bpt_cal_fail_entry(void *o)
{
    LOG_DBG("PPG Finger SM BPT Calibration Fail Entry");
    hpi_load_scr_spl(SCR_SPL_BPT_FAILED, SCROLL_NONE, SCR_BPT, BPT_FAIL_CAL, 0, 0);
    hpi_hw_fi_sensor_off();
}

static enum smf_state_result st_ppg_fing_bpt_cal_fail_run(void *o)
{
    // LOG_DBG("PPG Finger SM BPT Calibration Fail Running");
    return SMF_EVENT_HANDLED;
}

#define SENSOR_CHECK_TIMEOUT_MS 15000

static void sensor_check_timeout_work_handler(struct k_work *work)
{
    struct s_ppg_fi_object *s = (struct s_ppg_fi_object *)&sf_obj;
    LOG_ERR("Sensor check timeout: Sensor not found, op_mode=%d", s->ppg_fi_op_mode);

    // Power off sensor first
    hpi_hw_fi_sensor_off();

    // Show appropriate timeout screen based on operation mode
    if (s->ppg_fi_op_mode == PPG_FI_OP_MODE_SPO2_EST)
    {
        LOG_DBG("SpO2 Estimation timed out waiting for sensor");
        hpi_load_scr_spl(SCR_SPL_SPO2_BPT_TIMEOUT, SCROLL_NONE, SCR_SPO2, 0, 0, 0);
    }
    else if (s->ppg_fi_op_mode == PPG_FI_OP_MODE_BPT_EST)
    {
        LOG_DBG("BPT Estimation timed out waiting for sensor");
        hpi_load_scr_spl(SCR_SPL_SPO2_BPT_TIMEOUT, SCROLL_NONE, SCR_BPT, 0, 0, 0);
    }
    else if (s->ppg_fi_op_mode == PPG_FI_OP_MODE_BPT_CAL)
    {
        LOG_DBG("BPT Calibration timed out waiting for sensor");
        hpi_load_scr_spl(SCR_SPL_SPO2_BPT_TIMEOUT, SCROLL_NONE, SCR_BPT, SCR_BPT, 0, 0);
    }

    smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_IDLE]);
}

K_WORK_DEFINE(sensor_check_timeout_work, sensor_check_timeout_work_handler);
static void sensor_check_timeout_handler(struct k_timer *timer_id)
{
    LOG_DBG("Sensor check timeout handler called, submitting work item");
    k_work_submit(&sensor_check_timeout_work);
}
K_TIMER_DEFINE(tmr_sensor_check_timeout, sensor_check_timeout_handler, NULL);


static void st_ppg_fi_check_sensor_entry(void *o)
{
    struct s_ppg_fi_object *s = (struct s_ppg_fi_object *)o;
    LOG_DBG("PPG Finger SM Check Sensor Entry, op_mode=%d", s->ppg_fi_op_mode);

    // FIX: Use correct enum type (PPG_FI_OP_MODE_* not PPG_FI_STATE_*)
    if (s->ppg_fi_op_mode == PPG_FI_OP_MODE_SPO2_EST)
    {
        hpi_load_scr_spl(SCR_SPL_FI_SENS_CHECK, SCROLL_NONE, SCR_SPO2, s->ppg_fi_op_mode, 0, 0);
    }
    else
    {
        hpi_load_scr_spl(SCR_SPL_FI_SENS_CHECK, SCROLL_NONE, SCR_BPT, s->ppg_fi_op_mode, 0, 0);
    }

    /* Ensure FI sensor rail is powered on for detection */
    hpi_hw_fi_sensor_on();
    /* Allow AFE time to power up and enumerate */
    k_msleep(150);

    k_timer_start(&tmr_sensor_check_timeout, K_MSEC(SENSOR_CHECK_TIMEOUT_MS), K_NO_WAIT);
}

static enum smf_state_result st_ppg_fi_check_sensor_run(void *o)
{
    struct s_ppg_fi_object *s = (struct s_ppg_fi_object *)o;

    // Check for cancellation FIRST, before doing sensor work
    if ((hpi_evt_consume(&fi_evt, EVT_FI_SPO2_CANCEL)) || (hpi_evt_consume(&fi_evt, EVT_FI_BPT_EST_CANCEL)) || (hpi_evt_consume(&fi_evt, EVT_FI_BPT_CAL_CANCEL)))
    {
       // LOG_DBG("SpO2 Estimation Cancelled in CHECK_SENSOR");
        k_timer_stop(&tmr_sensor_check_timeout);
        hpi_hw_fi_sensor_off();  // FIX: Power off sensor on cancel
        smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_IDLE]);
        return SMF_EVENT_HANDLED;
    }

    struct sensor_value sensor_id_get;
    sensor_id_get.val1 = 0x00;

    /* Try multiple times to read the AFE sensor ID, power-cycling the FI rail if needed */
    const int max_attempts = 3;
    int attempt;
    for (attempt = 1; attempt <= max_attempts; attempt++) {
        sensor_attr_get(max32664d_dev, SENSOR_CHAN_ALL, MAX32664D_ATTR_SENSOR_ID, &sensor_id_get);
        LOG_DBG("AFE Sensor ID (attempt %d): 0x%02X", attempt, sensor_id_get.val1);
        if (sensor_id_get.val1 == MAX30101_SENSOR_ID) {
            break;  // Found valid sensor
        }

        if (attempt < max_attempts) {
            LOG_WRN("Sensor ID read returned 0x%02X, attempt %d/%d - power-cycling FI rail",
                    sensor_id_get.val1, attempt, max_attempts);
            hpi_hw_fi_sensor_off();
            k_msleep(50);
            hpi_hw_fi_sensor_on();
            k_msleep(200);
        }
    }

    if (sensor_id_get.val1 != MAX30101_SENSOR_ID)
    {
        // Sensor not found after all retries - add a short delay and return to retry
        // The main state machine loop has 1s delay, but we want faster sensor detection
        // so we add a 200ms delay here to allow user to connect sensor
        LOG_DBG("MAX30101 AFE sensor not found, will retry in 200ms");
        k_msleep(200);
        return SMF_EVENT_HANDLED;
    }

    // Sensor found successfully
    LOG_INF("MAX30101 sensor found!");

    // Stop the timeout timer since sensor is found
    k_timer_stop(&tmr_sensor_check_timeout);

    // Route to appropriate state based on operation mode
    if (s->ppg_fi_op_mode == PPG_FI_OP_MODE_BPT_EST)
    {
        // smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_BPT_EST]);
        hpi_load_scr_spl(SCR_SPL_BPT_MEASURE, SCROLL_NONE, SCR_SPL_FI_SENS_CHECK, 0, 0, 0);
        smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_WAIT_FOR_CONTACT]);
    }
    else if (s->ppg_fi_op_mode == PPG_FI_OP_MODE_BPT_CAL)
    {
       LOG_DBG("Transitioning to BPT_CAL_WAIT state");
       hpi_load_scr_spl(SCR_SPL_BPT_CAL_PROGRESS, SCROLL_NONE, SCR_BPT, 0, 0, 0);
       smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_BPT_CAL_WAIT]);
    }
    else if (s->ppg_fi_op_mode == PPG_FI_OP_MODE_SPO2_EST)
    {
       hpi_load_scr_spl(SCR_SPL_SPO2_MEASURE, SCROLL_NONE, SCR_SPO2, SPO2_SOURCE_PPG_FI, 0, 0);
       smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_WAIT_FOR_CONTACT]);
    }
    else
    {
        LOG_ERR("Unknown PPG Finger operation mode: %d", s->ppg_fi_op_mode);
        hpi_hw_fi_sensor_off();
        smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_SENSOR_FAIL]);
    }
    return SMF_EVENT_HANDLED;
}

static void st_ppg_fi_sensor_fail_entry(void *o)
{
    LOG_DBG("PPG Finger SM Sensor Fail Entry");
    hpi_hw_fi_sensor_off();
    // Show error screen
    hpi_load_scr_spl(SCR_SPL_BPT_FAILED, SCROLL_NONE, SCR_BPT, BPT_FAIL_SENSOR, 0, 0);
}

static enum smf_state_result st_ppg_fi_sensor_fail_run(void *o)
{
    // Transition back to IDLE after showing the error
    smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_IDLE]);
    return SMF_EVENT_HANDLED;
}

static void st_ppg_fi_spo2_est_entry(void *o)
{
    LOG_DBG("PPG Finger SM SpO2 Estimation Entry");
    sens_decode_ppg_fi_op_mode = PPG_FI_OP_MODE_SPO2_EST;
    atomic_set(&spo2_process_done, 0);  // Reset completion flag for new measurement
    /* H-REC: SPO2_EST is the active finger SpO2 measurement window. Bracket here
     * and in the state exit (fires on done / contact-timeout / cancel via
     * hpi_bpt_abort()->smf_set_state(IDLE)), so the record always finalizes. */
    hpi_data_set_ppg_finger_record_active(true);
   // hpi_load_scr_spl(SCR_SPL_SPO2_MEASURE, SCROLL_NONE, SCR_SPO2, SPO2_SOURCE_PPG_FI, 0, 0);
  //  hpi_hw_fi_sensor_on();
  //  hw_bpt_start_est();                 // Start the BPT estimation for SpO2
   // k_sem_give(&sem_start_fi_sampling); // Give the semaphore to start sampling
}

static enum smf_state_result st_ppg_fi_spo2_est_run(void *o)
{
    LOG_DBG("PPG Finger SM SpO2 Estimation Running");
    if (atomic_get(&finger_contact_ok) == 0)
    {
        if (contact_lost_start_ts == 0)
        {
            // First time contact is lost
            contact_lost_start_ts = k_uptime_get();
            LOG_DBG("Contact lost, starting %ds timeout", FINGER_SENSOR_CONTACT_TIMEOUT_MS/1000);
        } else if ((k_uptime_get() - contact_lost_start_ts) >= FINGER_SENSOR_CONTACT_TIMEOUT_MS) {
            LOG_INF("Contact lost for %ds during measurement - aborting", FINGER_SENSOR_CONTACT_TIMEOUT_MS/1000);
            hpi_bpt_abort();
            k_event_post(&fi_evt, EVT_FI_CONTACT_TIMEOUT);
            contact_lost_start_ts = 0;
            return SMF_EVENT_HANDLED;
        }
    } 
    else
    {
            // Contact restored, reset the timer
            contact_lost_start_ts = 0;
    }
    /* Check for completion */
    if (k_sem_take(&sem_spo2_est_complete, K_NO_WAIT) == 0)
    {
        k_sem_give(&sem_stop_fi_sampling); // Stop the sampling
        k_sleep(K_MSEC(1000));             // Wait for the sampling to stop
        smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_SPO2_EST_DONE]);
        return SMF_EVENT_HANDLED;
    }

    /* Honor user cancel during active measurement */
    if (hpi_evt_consume(&fi_evt, EVT_FI_SPO2_CANCEL))
    {
        LOG_DBG("SpO2 Estimation Cancelled (during measurement)");
        /* Use application abort helper to stop sampling, stop algorithm and go IDLE */
        hpi_bpt_abort();
        return SMF_EVENT_HANDLED;
    }
    return SMF_EVENT_HANDLED;
}

static void st_ppg_fi_spo2_est_exit(void *o)
{
    /* H-REC: finalize the SpO2 capture on every exit path (done / cancel / abort). */
    hpi_data_set_ppg_finger_record_active(false);
}

static void st_ppg_fi_spo2_est_done_entry(void *o)
{
    LOG_DBG("PPG Finger SM SpO2 Estimation Done Entry");
    hpi_load_scr_spl(SCR_SPL_SPO2_RESULT, SCROLL_NONE, SCR_SPO2, HPI_SPO2_RESULT_SUCCESS, m_est_spo2, 0);
    hpi_hw_fi_sensor_off();
}
static enum smf_state_result st_ppg_fi_spo2_est_done_run(void *o)
{
    LOG_DBG("PPG Finger SM SpO2 Estimation Done Running");
    // k_msleep(2000);
    // hpi_load_screen(SCR_BPT, SCROLL_NONE);
    smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_IDLE]);
    return SMF_EVENT_HANDLED;
}

static void st_ppg_fi_wait_for_contact_entry(void *o)
{
    struct s_ppg_fi_object *s = (struct s_ppg_fi_object *)o;
    LOG_DBG("PPG Finger SM Wait for Contact Entry");
    wait_start_ts = k_uptime_get();
    atomic_set(&finger_contact_ok, 0);
    contact_debounce_ts = 0;
    hpi_hw_fi_sensor_on();
    sens_decode_ppg_fi_op_mode = s->ppg_fi_op_mode;
    /* Loads BPT cal + starts estimation (also arms contact detection for SpO2).
     * Stash how many cal vectors actually loaded so the run handler can refuse a
     * silent uncalibrated BP estimate. */
    m_bpt_cal_loaded = hw_bpt_start_est();
    k_sem_give(&sem_start_fi_sampling);

}
static enum smf_state_result st_ppg_fi_wait_for_contact_run(void *o)
{
    if(hpi_evt_consume(&fi_evt, EVT_FI_SPO2_CANCEL) || hpi_evt_consume(&fi_evt, EVT_FI_BPT_EST_CANCEL))
    {
        LOG_INF("Finger measurement cancelled while waiting for contact");
        hpi_bpt_abort();
        hpi_hw_fi_sensor_off();
        smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_IDLE]);
        return SMF_EVENT_HANDLED;
    }

    struct s_ppg_fi_object *s = (struct s_ppg_fi_object *)o;

    if (k_sem_take(&sem_finger_contact_on, K_NO_WAIT) == 0) 
    {
        LOG_INF("*** FINGER CONFIRMED → MEASUREMENT START ***");
        if (s->ppg_fi_op_mode == PPG_FI_OP_MODE_BPT_EST) {
            /* Fail loud: never produce a BPT estimate with no calibration loaded.
             * The idle gate checks cal-file existence; this catches present-but-
             * unloadable/corrupt vectors (all loads failed) before estimating. */
            if (m_bpt_cal_loaded == 0) {
                LOG_ERR("BPT cal vectors unloadable - refusing uncalibrated estimate");
                hpi_bpt_abort(); /* stops sampling + algorithm, powers rail off, -> IDLE */
                hpi_load_scr_spl(SCR_SPL_BPT_CAL_REQUIRED, SCROLL_NONE, SCR_BPT, 0, 0, 0);
                return SMF_EVENT_HANDLED;
            }
            smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_BPT_EST]);
        } else if (s->ppg_fi_op_mode == PPG_FI_OP_MODE_SPO2_EST) {
            smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_SPO2_EST]);
        }
        return SMF_EVENT_HANDLED;
    }
 
   if ((wait_start_ts != 0 && (k_uptime_get() - wait_start_ts) >= FINGER_SENSOR_CONTACT_TIMEOUT_MS)) {
            LOG_INF("FINGER CONTACT TIMEOUT");
            hpi_bpt_abort();
            k_event_post(&fi_evt, EVT_FI_CONTACT_TIMEOUT);
            wait_start_ts = 0;
            smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_IDLE]);
            return SMF_EVENT_HANDLED;
        }
    return SMF_EVENT_HANDLED;
}
static void st_ppg_fi_wait_for_contact_exit(void *o)
{
    LOG_DBG("PPG Finger SM Wait for Contact Exit");
}
static const struct smf_state ppg_fi_states[] = {
    [PPG_FI_STATE_IDLE] = SMF_CREATE_STATE(st_ppg_fing_idle_entry, st_ppg_fing_idle_run, NULL, NULL, NULL),
    [PPG_FI_STATE_CHECK_SENSOR] = SMF_CREATE_STATE(st_ppg_fi_check_sensor_entry, st_ppg_fi_check_sensor_run, NULL, NULL, NULL),
    [PPG_FI_STATE_SENSOR_FAIL] = SMF_CREATE_STATE(st_ppg_fi_sensor_fail_entry, st_ppg_fi_sensor_fail_run, NULL, NULL, NULL),

    [PPG_FI_STATE_BPT_EST] = SMF_CREATE_STATE(st_ppg_fing_bpt_est_entry, st_ppg_fing_bpt_est_run, st_ppg_fing_bpt_est_exit, NULL, NULL),
    [PPG_FI_STATE_BPT_EST_DONE] = SMF_CREATE_STATE(st_ppg_fing_bpt_est_done_entry, st_ppg_fing_bpt_est_done_run, NULL, NULL, NULL),
    [PPG_FI_STATE_BPT_EST_FAIL] = SMF_CREATE_STATE(st_ppg_fing_bpt_est_fail_entry, st_ppg_fing_bpt_est_fail_run, NULL, NULL, NULL),

    [PPG_FI_STATE_BPT_CAL_WAIT] = SMF_CREATE_STATE(st_ppg_fi_cal_wait_entry, st_ppg_fi_cal_wait_run, NULL, NULL, NULL),
    [PPG_FI_STATE_BPT_CAL] = SMF_CREATE_STATE(st_ppg_fing_bpt_cal_entry, st_ppg_fing_bpt_cal_run, NULL, NULL, NULL),
    [PPG_FI_STATE_BPT_CAL_DONE] = SMF_CREATE_STATE(st_ppg_fing_bpt_cal_done_entry, st_ppg_fing_bpt_cal_done_run, NULL, NULL, NULL),
    [PPG_FI_STATE_BPT_CAL_FAIL] = SMF_CREATE_STATE(st_ppg_fing_bpt_cal_fail_entry, st_ppg_fing_bpt_cal_fail_run, NULL, NULL, NULL),

    [PPG_FI_STATE_SPO2_EST] = SMF_CREATE_STATE(st_ppg_fi_spo2_est_entry, st_ppg_fi_spo2_est_run, st_ppg_fi_spo2_est_exit, NULL, NULL),
    [PPG_FI_STATE_SPO2_EST_DONE] = SMF_CREATE_STATE(st_ppg_fi_spo2_est_done_entry, st_ppg_fi_spo2_est_done_run, NULL, NULL, NULL),

    [PPG_FI_STATE_WAIT_FOR_CONTACT] = SMF_CREATE_STATE(st_ppg_fi_wait_for_contact_entry, st_ppg_fi_wait_for_contact_run,st_ppg_fi_wait_for_contact_exit, NULL, NULL),
};

static void smf_ppg_finger_thread(void)
{
    int32_t ret;

    // Wait for HW module to init the finger sensor (init handshake -> EVT_FI_SM_START)
    k_event_wait(&fi_evt, EVT_FI_SM_START, false, K_FOREVER);
    k_event_clear(&fi_evt, EVT_FI_SM_START);
    smf_set_initial(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_IDLE]);
    // k_timer_start(&tmr_ppg_fi_sampling, K_MSEC(PPG_FI_SAMPLING_INTERVAL_MS), K_MSEC(PPG_FI_SAMPLING_INTERVAL_MS));

    LOG_INF("PPG Finger SMF Thread starting");

    /* P2: task-watchdog coverage. Tick kept (finger has time-based contact/BPT
     * states); worst-case in-handler settle sleep is ~2 s, under the 10 s WDT. */
    int wdt_ch = -1;

    for (;;)
    {
        if (wdt_ch < 0)
        {
            wdt_ch = hpi_watchdog_register("smf_ppg_finger", 10000);
        }

        ret = smf_run_state(SMF_CTX(&sf_obj));
        if (ret)
        {
            LOG_ERR("Error in PPG Finger State Machine");
            break;
        }
        hpi_watchdog_feed(wdt_ch);
        // k_msleep(1000);
         k_msleep(100);
    }
}

static void ppg_fi_ctrl_thread(void)
{
    LOG_INF("PPG Finger Control Thread starting");

    int wdt_ch = -1;

    for (;;)
    {
        if (wdt_ch < 0)
        {
            wdt_ch = hpi_watchdog_register("ppg_fi_ctrl", 10000);
        }
        hpi_watchdog_feed(wdt_ch);

        if (k_sem_take(&sem_start_fi_sampling, K_NO_WAIT) == 0)
        {
            LOG_INF("Start sampling signal received");
            LOG_INF("Waiting 1 second for sensor stabilization...");
            k_msleep(1000);
            LOG_INF("Starting sampling timer (interval: %d ms)", PPG_FI_SAMPLING_INTERVAL_MS);
            k_timer_start(&tmr_ppg_fi_sampling, K_MSEC(PPG_FI_SAMPLING_INTERVAL_MS), K_MSEC(PPG_FI_SAMPLING_INTERVAL_MS));
            LOG_INF("Sampling timer started successfully");
        }

        if (k_sem_take(&sem_stop_fi_sampling, K_NO_WAIT) == 0)
        {
            LOG_INF("Stop sampling");
            k_timer_stop(&tmr_ppg_fi_sampling);
        }

        k_msleep(100);
    }
}

#define SMF_PPG_FINGER_THREAD_STACKSIZE 4096
#define SMF_PPG_FINGER_THREAD_PRIORITY 7

#define PPG_FI_CTRL_THREAD_STACKSIZE 4096
#define PPG_FI_CTRL_THREAD_PRIORITY 7

K_THREAD_DEFINE(ppg_finger_smf_thread, SMF_PPG_FINGER_THREAD_STACKSIZE, smf_ppg_finger_thread, NULL, NULL, NULL, SMF_PPG_FINGER_THREAD_PRIORITY, 0, 500);
K_THREAD_DEFINE(ppg_finger_ctrl_thread, PPG_FI_CTRL_THREAD_STACKSIZE, ppg_fi_ctrl_thread, NULL, NULL, NULL, PPG_FI_CTRL_THREAD_PRIORITY, 0, 500);