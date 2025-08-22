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
/* sensor definitions (sensor_read_config, sensor_stream_trigger) - only when sensors enabled */
#ifdef CONFIG_SENSOR
#include <zephyr/drivers/sensor.h>
#endif
#include <stddef.h>
#include <string.h>

LOG_MODULE_REGISTER(smf_ppg_wrist, LOG_LEVEL_DBG);

/* Thread configuration for streaming */
#define PPG_STREAMING_THREAD_STACK_SIZE 2048
#define PPG_STREAMING_THREAD_PRIORITY 5

#include "hw_module.h"
#include "max32664c_sensor.h"
#include "hpi_common_types.h"
#include "hpi_sys.h"
#include "ui/move_ui.h"
#include "sm/hpi_ppg_wrist.h"

/* Local compile-time flag to enable driver->app forwarding wrapper. This is a
 * file-local flag (not Kconfig). Default: enabled. */
#ifndef APP_PPG_WRIST_FORWARD
#define APP_PPG_WRIST_FORWARD 1
#endif

#define PPG_WRIST_SAMPLING_INTERVAL_MS 40
#define HPI_OFFSKIN_THRESHOLD_S 5
#define HPI_PROBE_DURATION_S 15

static const struct smf_state ppg_samp_states[];

K_SEM_DEFINE(sem_ppg_wrist_thread_start, 0, 1);
K_SEM_DEFINE(sem_ppg_wrist_on_skin, 0, 1);
K_SEM_DEFINE(sem_ppg_wrist_off_skin, 0, 1);
K_SEM_DEFINE(sem_ppg_wrist_motion_detected, 0, 1);

K_SEM_DEFINE(sem_start_one_shot_spo2, 0, 1);
K_SEM_DEFINE(sem_stop_one_shot_spo2, 0, 1);
K_SEM_DEFINE(sem_spo2_cancel, 0, 1);

K_MSGQ_DEFINE(q_ppg_wrist_sample, sizeof(struct hpi_ppg_wr_data_t), 64, 1);

/* The driver provides a per-instance RTIO context (e.g. `max32664c_rtio_0`) which
 * the application consumes for streaming CQEs. We rely on the driver's RTIO
 * instance instead of defining a separate local mempool here. */

/* The driver defines a per-instance RTIO context (max32664c_rtio_0). The
 * application must consume CQEs from the same RTIO context the driver
 * submits into. Declare it extern so we can use it here. */
extern struct rtio max32664c_rtio_0;

/* Polling work item for sensor data acquisition every 100ms */
static struct k_work_delayable ppg_poll_work;

/* Forward declarations */
static void ppg_poll_work_handler(struct k_work *work);
static void ppg_wrist_init_polling(void);

/* Use a stream iodev so the sensor framework provides mempool-backed
 * multishot RX buffers and the iodev->data already contains a
 * sensor_read_config with is_streaming == true for the driver. */
SENSOR_DT_STREAM_IODEV(max32664c_iodev, DT_ALIAS(max32664c), {SENSOR_TRIG_FIFO_WATERMARK, SENSOR_STREAM_DATA_INCLUDE});

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

/* Atomic flag to control polling activity */
static atomic_t streaming_active = ATOMIC_INIT(0);

/* Global flag to completely disable polling during device initialization */
static atomic_t polling_globally_disabled = ATOMIC_INIT(1);  /* Start disabled */

/* Track number of outstanding mempool-backed async reads to avoid exhausting the RTIO mempool */
/* Match this to the RTIO mempool num_blks (defined below in RTIO_DEFINE_WITH_MEMPOOL) so
 * we can consume the FIFO quickly without falsely hitting the pending limit. */
#define MAX_PENDING_MEMPOOL_READS 32
static atomic_t pending_mempool_reads = ATOMIC_INIT(0);
/* Count how many times the driver-forwarding application handler was invoked */
static atomic_t app_ppg_handler_calls = ATOMIC_INIT(0);
/* Poll handler telemetry */
static atomic_t poll_handler_calls = ATOMIC_INIT(0);
static int64_t poll_telemetry_last_ms = 0;
/* Adaptive backoff state for RTIO mempool exhaustion handling */
/* (no adaptive backoff) */

// Local variables for measured SPO2 and status
static uint16_t measured_spo2 = 0;
static enum spo2_meas_state measured_spo2_status = SPO2_MEAS_UNK;

// Mutex for thread-safe access to measured SPO2 variables
K_MUTEX_DEFINE(mutex_measured_spo2);

static int m_curr_state;
int sig_wake_on_motion_count = 0;
static bool spo2_measurement_in_progress = false;
static enum max32664c_scd_states m_curr_scd_state;

// Externs
extern struct k_sem sem_ppg_wrist_sm_start;

static void work_off_skin_wait_handler(struct k_work *work)
{
    if (m_curr_scd_state == MAX32664C_SCD_STATE_OFF_SKIN)
    {
        LOG_DBG("Still OFF SKIN");
        hpi_sys_set_device_on_skin(false);
        // smf_set_state(SMF_CTX(&sm_ctx_ppg_wr), &ppg_samp_states[PPG_SAMP_STATE_MOTION_DETECT]);
    }
}
K_WORK_DELAYABLE_DEFINE(work_off_skin, work_off_skin_wait_handler);

static void work_on_skin_wait_handler(struct k_work *work)
{
    if (m_curr_scd_state == MAX32664C_SCD_STATE_ON_SKIN)
    {
        LOG_DBG("Still ON SKIN");
        hpi_sys_set_device_on_skin(true);
        // smf_set_state(SMF_CTX(&sm_ctx_ppg_wr), &ppg_samp_states[PPG_SAMP_STATE_ACTIVE]);
    }
}
K_WORK_DELAYABLE_DEFINE(work_on_skin, work_on_skin_wait_handler);

static void set_measured_spo2(uint16_t spo2_value, enum spo2_meas_state status)
{
    if (k_mutex_lock(&mutex_measured_spo2, K_MSEC(100)) == 0) {
        measured_spo2 = spo2_value;
        measured_spo2_status = status;
        k_mutex_unlock(&mutex_measured_spo2);
    } else {
        LOG_WRN("Failed to acquire mutex for setting measured SPO2");
    }
}


static void sensor_ppg_wrist_decode(uint8_t *buf, uint32_t buf_len)
{
    const struct max32664c_encoded_data *edata = (const struct max32664c_encoded_data *)buf;
    struct hpi_ppg_wr_data_t ppg_sensor_sample;

    uint16_t _n_samples = edata->num_samples;

    LOG_DBG("Decode: op_mode=%d, num_samples=%d, buf_len=%u", 
            edata->chip_op_mode, _n_samples, buf_len);

    if (edata->chip_op_mode == MAX32664C_OP_MODE_SCD)
    {
        // printk("SCD: ", edata->scd_state);
        if (edata->scd_state == MAX32664C_SCD_STATE_ON_SKIN)
        {
            LOG_DBG("ON SKIN | state: %d", m_curr_state);
            k_work_schedule(&work_on_skin, K_SECONDS(HPI_PROBE_DURATION_S));
            // k_sem_give(&sem_ppg_wrist_on_skin);
        }
        return;
    }
    else if (edata->chip_op_mode == MAX32664C_OP_MODE_WAKE_ON_MOTION && sig_wake_on_motion_count <= 1)
    {
        LOG_DBG("WOKEN ON MOTION | state: %d", m_curr_state);
        sig_wake_on_motion_count++;
        hw_max32664c_set_op_mode(MAX32664C_OP_MODE_EXIT_WAKE_ON_MOTION, MAX32664C_ALGO_MODE_NONE);
        k_sem_give(&sem_ppg_wrist_motion_detected);
        // return;
    }
    else if (edata->chip_op_mode == MAX32664C_OP_MODE_ALGO_AEC || edata->chip_op_mode == MAX32664C_OP_MODE_ALGO_AGC || edata->chip_op_mode == MAX32664C_OP_MODE_ALGO_EXTENDED)
    {
        LOG_DBG("ALGO mode: %d, samples: %d, HR: %d, SpO2: %d", 
                edata->chip_op_mode, _n_samples, edata->hr, edata->spo2);
        
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

            LOG_INF("PPG Data: HR=%d (conf=%d), SpO2=%d (conf=%d), samples=%d", 
                    ppg_sensor_sample.hr, ppg_sensor_sample.hr_confidence,
                    ppg_sensor_sample.spo2, ppg_sensor_sample.spo2_confidence,
                    _n_samples);

            

            if (ppg_sensor_sample.scd_state != MAX32664C_SCD_STATE_ON_SKIN && (m_curr_scd_state == MAX32664C_SCD_STATE_ON_SKIN))
            {
                LOG_DBG("OFF SKIN");
                k_work_schedule(&work_off_skin, K_SECONDS(HPI_OFFSKIN_THRESHOLD_S));
            }
            else if (ppg_sensor_sample.scd_state == MAX32664C_SCD_STATE_ON_SKIN && (m_curr_scd_state != MAX32664C_SCD_STATE_ON_SKIN))
            {
                LOG_DBG("ON SKIN");
                k_work_schedule(&work_on_skin, K_SECONDS(HPI_PROBE_DURATION_S));
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
                LOG_DBG("SPO2 MEAS TIMEOUT");
                k_sem_give(&sem_stop_one_shot_spo2);
                set_measured_spo2(0, SPO2_MEAS_TIMEOUT);
                spo2_measurement_in_progress = false;
            }

            m_curr_scd_state = ppg_sensor_sample.scd_state;

            // LOG_DBG("SCD: %d", ppg_sensor_sample.scd_state);
            if (ppg_sensor_sample.scd_state == MAX32664C_SCD_STATE_ON_SKIN)
            {
                k_msgq_put(&q_ppg_wrist_sample, &ppg_sensor_sample, K_MSEC(1));
            }
        } else {
            LOG_WRN("ALGO mode but 0 samples - sensor algorithm may not be running properly");
        }
    } else {
        LOG_WRN("Unknown or unexpected chip operation mode: %d", edata->chip_op_mode);
    }
}

/* Application-facing handler called by the driver when a streaming buffer
 * is populated and ready for decoding. This function is intended to be
 * called from the driver's completion path when forwarding is enabled via
 * CONFIG_APP_PPG_WRIST_FORWARD. It will call the existing decode
 * implementation and then release the mempool buffer. */
void hpi_ppg_wrist_handle_stream_buffer(uint8_t *buf, uint32_t buf_len)
{
    if (buf == NULL || buf_len == 0) {
        return;
    }
    /* Record that the driver forwarded a buffer to the app handler */
    atomic_inc(&app_ppg_handler_calls);
    LOG_INF("Driver->app PPG stream buffer handler invoked (count=%ld)", (long)atomic_get(&app_ppg_handler_calls));
    /* Mark buffer as forwarded to prevent the CQE consumer from decoding it
     * again. We use the high bit of the timestamp as an in-buffer flag. */
    if (buf_len >= sizeof(struct max32664c_encoded_data)) {
        struct max32664c_encoded_data *edata = (struct max32664c_encoded_data *)buf;
        const uint64_t FORWARDED_FLAG = (1ULL << 63);
        edata->header.timestamp |= FORWARDED_FLAG;
    }

    /* Decode buffer contents. Do NOT release the mempool buffer here; leave
     * release and pending counter bookkeeping to the CQE consumer so ownership
     * is handled in a single place and double-release is avoided. */
    sensor_ppg_wrist_decode(buf, buf_len);
}


/* ppg_wrist_start_polling removed - polling is controlled by state machine and
 * explicit start/stop helpers. Use ppg_wrist_init_polling() and set
 * `streaming_active`/`polling_globally_disabled` as needed. */

static void ppg_wrist_stop_polling(void)
{
    atomic_set(&streaming_active, 0);
    atomic_set(&polling_globally_disabled, 1);  /* Globally disable polling */
    k_work_cancel_delayable(&ppg_poll_work);
    LOG_DBG("Polling stopped and globally disabled");
}

/* Streaming thread that handles CQE consumption only - driver handles resubmission */
/* Polling work function - polls sensor every 100ms and reads data if available */
static void ppg_poll_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    /* Telemetry: increment call count and emit summary once per second */
    atomic_inc(&poll_handler_calls);
    int64_t now_ms = k_uptime_get();
    if ((now_ms - poll_telemetry_last_ms) >= 1000) {
    poll_telemetry_last_ms = now_ms;
    int pending = atomic_get(&pending_mempool_reads);
    int app_calls = atomic_get(&app_ppg_handler_calls);
    int poll_calls = atomic_get(&poll_handler_calls);
    int available = MAX_PENDING_MEMPOOL_READS - pending;
    LOG_INF("PPG poll telemetry: pending=%d available=%d app_handler_calls=%d poll_calls=%d",
        pending, available, app_calls, poll_calls);
    }
    
    /* Global safety check - do not poll if globally disabled or streaming not active */
    if (atomic_get(&polling_globally_disabled) || !atomic_get(&streaming_active)) {
        return;
    }

    const struct device *dev = DEVICE_DT_GET(DT_ALIAS(max32664c));
    if (!dev || !device_is_ready(dev)) {
        LOG_ERR("MAX32664C device not ready");
        k_work_schedule(&ppg_poll_work, K_MSEC(100));
        return;
    }

    /* Check DRDY via hub status */
    uint8_t status = max32664c_read_hub_status(dev);
    if (status == 0xFF || status == 0x00) {
        /* Sensor not ready yet; try again later */
        k_work_schedule(&ppg_poll_work, K_SECONDS(1));
        return;
    }

    if (!(status & MAX32664C_HUB_STAT_DRDY_MASK)) {
        /* No data ready; reschedule and exit */
        k_work_schedule(&ppg_poll_work, K_MSEC(100));
        return;
    }

    /* DRDY set - read FIFO count */
    int fifo_count = max32664c_get_fifo_count(dev);
    if (fifo_count <= 0) {
        /* Nothing to read or error */
        k_work_schedule(&ppg_poll_work, K_MSEC(50));
        return;
    }

    /* First, process any pending completions from previous async operations so
     * the pending counter is up-to-date before deciding to submit a new read.
     */
    struct rtio_cqe *cqe;
    while ((cqe = rtio_cqe_consume(&max32664c_rtio_0)) != NULL) {
        if (cqe->result >= 0) {
            uint8_t *buf;
            uint32_t buf_len;
            int rc = rtio_cqe_get_mempool_buffer(&max32664c_rtio_0, cqe, &buf, &buf_len);

            if (rc == 0 && buf_len > 0) {
                bool already_forwarded = false;
                if (buf_len >= sizeof(struct max32664c_encoded_data)) {
                    const struct max32664c_encoded_data *edata = (const struct max32664c_encoded_data *)buf;
                    const uint64_t FORWARDED_FLAG = (1ULL << 63);
                    if (edata->header.timestamp & FORWARDED_FLAG) {
                        already_forwarded = true;
                    }
                }

                if (!already_forwarded) {
                    sensor_ppg_wrist_decode(buf, buf_len);
                } else {
                    LOG_DBG("Skipping decode for driver-forwarded buffer");
                }

        rtio_release_buffer(&max32664c_rtio_0, buf, buf_len);
                if (atomic_get(&pending_mempool_reads) > 0) {
                    atomic_dec(&pending_mempool_reads);
                }
            }
        } else {
            LOG_ERR("Async RTIO operation failed: %d", cqe->result);
        }

    rtio_cqe_release(&max32664c_rtio_0, cqe);
    }

    /* Enforce single outstanding mempool-backed read: if one is already
     * outstanding, defer submission so completions can free buffers and be
     * processed in the next poll handler run. */
    if (atomic_get(&pending_mempool_reads) > 0) {
        k_work_schedule(&ppg_poll_work, K_MSEC(20));
        return;
    }

    /* Attempt a single mempool-backed async read; on ENOMEM schedule a short retry. */
    int rc = sensor_read_async_mempool(&max32664c_iodev, &max32664c_rtio_0, NULL);
    if (rc == 0) {
        atomic_inc(&pending_mempool_reads);
        LOG_DBG("Submitted single mempool read (fifo=%d) pending=%ld", fifo_count, (long)atomic_get(&pending_mempool_reads));
    } else if (rc == -12) {
        /* mempool exhausted - try again shortly */
        LOG_WRN("RTIO mempool exhausted during submit (rc=%d)", rc);
        k_work_schedule(&ppg_poll_work, K_MSEC(20));
    } else {
        LOG_ERR("sensor_read_async_mempool failed: %d", rc);
        k_work_schedule(&ppg_poll_work, K_MSEC(50));
    }

    /* Schedule next poll in 100ms */
    k_work_schedule(&ppg_poll_work, K_MSEC(100));
}

/* Initialize the polling work queue */
static void ppg_wrist_init_polling(void)
{
    k_work_init_delayable(&ppg_poll_work, ppg_poll_work_handler);
    LOG_DBG("PPG polling work initialized");
}

/* Emergency function to disable all polling during device initialization */
void ppg_wrist_emergency_disable_polling(void)
{
    atomic_set(&polling_globally_disabled, 1);
    atomic_set(&streaming_active, 0);
    k_work_cancel_delayable(&ppg_poll_work);
    LOG_INF("Emergency polling disable - all PPG polling stopped");
}

static void st_ppg_samp_active_entry(void *o)
{
    LOG_DBG("PPG SM Active Entry");
    m_curr_state = PPG_SAMP_STATE_ACTIVE;

    /* Initialize sensor into continuous HR algorithm mode.
     * The driver exposes a single helper `hw_max32664c_set_op_mode()` which
     * performs the required MAX32664C command sequence (output mode, thresholds,
     * report rate, continuous mode and enabling algorithm/AEC). Call that helper
     * to configure the chip into ALGO_AEC / CONT_HRM mode.
     */
    LOG_INF("Configuring MAX32664C for Continuous HRM (ALGO_AEC + CONT_HRM)");
    int mode_result = hw_max32664c_set_op_mode(MAX32664C_OP_MODE_ALGO_AEC, MAX32664C_ALGO_MODE_CONT_HRM);
    if (mode_result < 0) {
        LOG_ERR("Failed to set MAX32664C op mode: %d", mode_result);
    } else {
        LOG_DBG("MAX32664C op mode set successfully");
    }

    /* Give the sensor time to apply configuration and stabilize. Keep this
     * short but long enough for the driver to complete internal command
     * sequences. */
    k_msleep(500);

    /* Initialize local polling work (CQE consumer) and enable streaming. The
     * module uses the driver's RTIO streaming path: the application must
     * submit one mempool-backed streaming read (via sensor_read_async_mempool)
     * and then resubmit after each completion to keep streaming continuous.
     */
    ppg_wrist_init_polling();
    LOG_DBG("PPG polling initialized");
}

static void st_ppg_samp_active_run(void *o)
{
    static int run_count = 0;
    static bool polling_started = false;
    
    /* Emit a debug line every 10 runs while debugging to confirm state execution */
    if (++run_count % 10 == 0) {
        LOG_DBG("PPG SM Active Run #%d", run_count);
    }
    
    /* Start polling immediately on first run (remove delayed start) */
    if (!polling_started) {
        LOG_INF("Enabling global polling and starting sensor polling");
        atomic_set(&polling_globally_disabled, 0);  /* Enable polling globally */
        atomic_set(&streaming_active, 1);
        k_work_schedule(&ppg_poll_work, K_MSEC(100));
        polling_started = true;
    }
    
    if (k_sem_take(&sem_ppg_wrist_off_skin, K_NO_WAIT) == 0)
    {
        LOG_DBG("Switching to Off Skin");
        smf_set_state(SMF_CTX(&sm_ctx_ppg_wr), &ppg_samp_states[PPG_SAMP_STATE_OFF_SKIN]);
    }
    
    /* Add a longer delay since we don't need to process completions here anymore */
    k_msleep(50);
}

static void st_ppg_samp_probing_entry(void *o)
{
    LOG_DBG("PPG SM Probing Entry");
    m_curr_state = PPG_SAMP_STATE_PROBING;

    // Enter SCD mode
    hw_max32664c_set_op_mode(MAX32664C_OP_MODE_SCD, MAX32664C_ALGO_MODE_CONT_HRM);
    
    /* Start polling if not already active for SCD detection - but defer to avoid I2C conflicts */
    if (!atomic_get(&streaming_active)) {
        LOG_INF("SCD mode set - polling will start when device is fully stable");
        /* Don't start polling immediately - let the run state handle it */
    }
}

static void st_ppg_samp_probing_run(void *o)
{
    // LOG_DBG("PPG SM Probing Run");
    
    if (k_sem_take(&sem_ppg_wrist_on_skin, K_NO_WAIT) == 0)
    {
        LOG_DBG("Switching to Active");
        smf_set_state(SMF_CTX(&sm_ctx_ppg_wr), &ppg_samp_states[PPG_SAMP_STATE_ACTIVE]);
    }
    
    /* Add a longer delay since we don't need to process completions here anymore */
    k_msleep(50);
}

static void st_ppg_samp_motion_detect_entry(void *o)
{
    LOG_DBG("PPG SM Motion Detect Entry");
    sig_wake_on_motion_count = 0;
    hw_max32664c_set_op_mode(MAX32664C_OP_MODE_WAKE_ON_MOTION, MAX32664C_ALGO_MODE_NONE);
    m_curr_state = PPG_SAMP_STATE_MOTION_DETECT;
}

static void st_ppg_samp_motion_detect_run(void *o)
{
    LOG_DBG("PPG SM Motion Detect Running");

    if (k_sem_take(&sem_ppg_wrist_motion_detected, K_FOREVER) == 0)
    {
        k_msleep(1000);
        LOG_DBG("Switching to Probing");
        smf_set_state(SMF_CTX(&sm_ctx_ppg_wr), &ppg_samp_states[PPG_SAMP_STATE_PROBING]);
    }
}

static void st_ppg_samp_active_exit(void *o)
{
    LOG_DBG("PPG SM Active Exit");
    /* Stop polling when leaving active state and globally disable */
    atomic_set(&polling_globally_disabled, 1);  /* Globally disable first */
    ppg_wrist_stop_polling();
}

static void st_ppg_samp_probing_exit(void *o)
{
    LOG_DBG("PPG SM Probing Exit");
    /* Stop polling when leaving probing state and globally disable */
    atomic_set(&polling_globally_disabled, 1);  /* Globally disable first */
    ppg_wrist_stop_polling();
}

static const struct smf_state ppg_samp_states[] = {
    [PPG_SAMP_STATE_ACTIVE] = SMF_CREATE_STATE(st_ppg_samp_active_entry, st_ppg_samp_active_run, st_ppg_samp_active_exit, NULL, NULL),
    [PPG_SAMP_STATE_PROBING] = SMF_CREATE_STATE(st_ppg_samp_probing_entry, st_ppg_samp_probing_run, st_ppg_samp_probing_exit, NULL, NULL),
    [PPG_SAMP_STATE_MOTION_DETECT] = SMF_CREATE_STATE(st_ppg_samp_motion_detect_entry, st_ppg_samp_motion_detect_run, NULL, NULL, NULL),
    //[PPG_SAMP_STATE_OFF_SKIN] = SMF_CREATE_STATE(st_ppg_samp_off_skin_entry, st_ppg_samp_off_skin_run, NULL, NULL, NULL),
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

    // legacy timer-based polling removed; streaming is active

    LOG_INF("PPG State Machine Thread starting");
    for (;;)
    {
        ret = smf_run_state(SMF_CTX(&sm_ctx_ppg_wr));

        if (ret)
        {
            LOG_ERR("Error in PPG State Machine: %d", ret);
            break;
        }

        static int _smf_loop_hb = 0;
        if ((++_smf_loop_hb % 5) == 0) {
            LOG_DBG("PPG SMF loop heartbeat #%d", _smf_loop_hb);
        }

        k_msleep(1000);
    }
}

#define SMF_PPG_THREAD_STACKSIZE 4096
#define SMF_PPG_THREAD_PRIORITY 5

K_THREAD_DEFINE(smf_ppg_wrist_thread_id, SMF_PPG_THREAD_STACKSIZE, smf_ppg_wrist_thread, NULL, NULL, NULL, SMF_PPG_THREAD_PRIORITY, 0, 1000);