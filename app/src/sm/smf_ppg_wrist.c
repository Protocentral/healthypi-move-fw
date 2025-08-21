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

/* Use RTIO with mempool for streaming - this is required for proper streaming support */
/* Parameters: name, sq_sz, cq_sz, num_blks, blk_size, balign */
/* Increased queue sizes to handle concurrent operations and prevent resource exhaustion */
RTIO_DEFINE_WITH_MEMPOOL(max32664c_read_rtio_poll_ctx, 4, 4, 16, 512, 8);

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

void work_off_skin_wait_handler(struct k_work *work)
{
    if (m_curr_scd_state == MAX32664C_SCD_STATE_OFF_SKIN)
    {
        LOG_DBG("Still OFF SKIN");
        hpi_sys_set_device_on_skin(false);
        // smf_set_state(SMF_CTX(&sm_ctx_ppg_wr), &ppg_samp_states[PPG_SAMP_STATE_MOTION_DETECT]);
    }
}
K_WORK_DELAYABLE_DEFINE(work_off_skin, work_off_skin_wait_handler);

void work_on_skin_wait_handler(struct k_work *work)
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

static int get_measured_spo2(uint16_t *spo2_value, enum spo2_meas_state *status)
{
    if (spo2_value == NULL || status == NULL) {
        return -EINVAL;
    }
    
    if (k_mutex_lock(&mutex_measured_spo2, K_MSEC(100)) == 0) {
        *spo2_value = measured_spo2;
        *status = measured_spo2_status;
        k_mutex_unlock(&mutex_measured_spo2);
        return 0;
    } else {
        LOG_WRN("Failed to acquire mutex for getting measured SPO2");
        return -EBUSY;
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

            /*if ((ppg_sensor_sample.scd_state == MAX32664C_SCD_STATE_OFF_SKIN) && (m_curr_scd_state != MAX32664C_SCD_STATE_OFF_SKIN))
            {
                LOG_DBG("OFF SKIN");
                k_work_schedule(&work_off_skin, K_SECONDS(HPI_OFFSKIN_THRESHOLD_S));
            }*/

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

static int ppg_wrist_init_rtio(void)
{
    /* Get device reference for polling */
    const struct device *dev = DEVICE_DT_GET(DT_ALIAS(max32664c));
    if (!dev) {
        LOG_ERR("Cannot get MAX32664C device");
        return -ENODEV;
    }
    
    LOG_DBG("RTIO context initialized for polling");
    return 0;
}

static int ppg_wrist_start_polling(void)
{
    LOG_DBG("Starting MAX32664C polling...");
    atomic_set(&streaming_active, 1);
    
    /* Initialize the work queue if not already done */
    ppg_wrist_init_polling();
    
    /* Start the first poll cycle */
    k_work_schedule(&ppg_poll_work, K_MSEC(100));
    
    LOG_INF("Polling started successfully");
    return 0;
}

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
    
    /* Global safety check - do not poll if globally disabled */
    if (atomic_get(&polling_globally_disabled)) {
        LOG_DBG("Polling globally disabled - skipping poll cycle");
        /* Do not reschedule if globally disabled */
        return;
    }
    
    if (!atomic_get(&streaming_active)) {
        /* Not active, reschedule for next check */
        k_work_schedule(&ppg_poll_work, K_MSEC(100));
        return;
    }
    
    const struct device *dev = DEVICE_DT_GET(DT_ALIAS(max32664c));
    if (!dev) {
        LOG_ERR("Cannot get MAX32664C device");
        k_work_schedule(&ppg_poll_work, K_MSEC(100));
        return;
    }
    
    /* Check if sensor has data ready */
    uint8_t status = max32664c_read_hub_status(dev);
    bool data_ready = (status & MAX32664C_HUB_STAT_DRDY_MASK) != 0;
    
    /* If status read failed (returns 0xFF or 0x00), sensor may not be ready yet */
    if (status == 0xFF || status == 0x00) {
        LOG_WRN("Sensor status read failed (0x%02x), device may not be ready - retrying in 1s", status);
        k_work_schedule(&ppg_poll_work, K_SECONDS(1));  /* Retry after longer delay */
        return;
    }
    
    LOG_DBG("Poll: status=0x%02x, DRDY=%s", status, data_ready ? "YES" : "NO");
    
    if (data_ready) {
        /* Check FIFO count */
        int fifo_count = max32664c_get_fifo_count(dev);
        
        /* Negative value indicates I2C read error */
        if (fifo_count < 0) {
            LOG_WRN("FIFO count read failed (%d), I2C error - retrying in 1s", fifo_count);
            k_work_schedule(&ppg_poll_work, K_SECONDS(1));  /* Retry after longer delay */
            return;
        }
        
        LOG_DBG("FIFO count: %d", fifo_count);
        
        if (fifo_count > 0) {
            LOG_INF("Data available (FIFO=%d), reading sensor data", fifo_count);
            
            /* Use the device's direct read function to get the data */
            const struct device *dev = DEVICE_DT_GET(DT_ALIAS(max32664c));
            if (dev && device_is_ready(dev)) {
                /* Call the driver's sample fetch which will read and decode the FIFO */
                int ret = sensor_sample_fetch(dev);
                if (ret == 0) {
                    LOG_DBG("Successfully fetched sensor data from FIFO (%d samples)", fifo_count);
                } else {
                    LOG_ERR("Failed to fetch sensor data: %d", ret);
                }
            } else {
                LOG_ERR("Sensor device not ready");
            }
        } else {
            LOG_DBG("DRDY set but FIFO empty");
        }
    } else {
        LOG_DBG("No data ready");
    }
    
    /* Process any pending completions from previous async operations */
    struct rtio_cqe *cqe;
    while ((cqe = rtio_cqe_consume(&max32664c_read_rtio_poll_ctx)) != NULL) {
        LOG_DBG("Processing async completion with result: %d", cqe->result);
        
        if (cqe->result >= 0) {
            uint8_t *buf;
            uint32_t buf_len;
            int rc = rtio_cqe_get_mempool_buffer(&max32664c_read_rtio_poll_ctx, cqe, &buf, &buf_len);
            
            if (rc == 0 && buf_len > 0) {
                LOG_DBG("Processing async %u bytes of sensor data", buf_len);
                sensor_ppg_wrist_decode(buf, buf_len);
                rtio_release_buffer(&max32664c_read_rtio_poll_ctx, buf, buf_len);
            }
        } else {
            LOG_ERR("Async RTIO operation failed: %d", cqe->result);
        }
        
        rtio_cqe_release(&max32664c_read_rtio_poll_ctx, cqe);
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

    LOG_INF("Setting MAX32664C to ALGO_AEC + CONT_HRM mode for polling-based data acquisition");
    int mode_result = hw_max32664c_set_op_mode(MAX32664C_OP_MODE_ALGO_AEC, MAX32664C_ALGO_MODE_CONT_HRM);
    LOG_DBG("Mode set result: %d", mode_result);
    
    /* Give sensor time to switch modes and stabilize */
    LOG_DBG("Waiting for sensor to stabilize after mode switch...");
    k_msleep(2000);  /* Extended delay to ensure proper initialization */

    /* Verify sensor is ready by checking status */
    const struct device *max32664c_dev = DEVICE_DT_GET(DT_ALIAS(max32664c));
    if (max32664c_dev) {
        uint8_t status = max32664c_read_hub_status(max32664c_dev);
        LOG_DBG("Sensor hub status after mode switch: 0x%02x (DRDY=%s)", 
                status, (status & MAX32664C_HUB_STAT_DRDY_MASK) ? "YES" : "NO");
        
        int fifo_count = max32664c_get_fifo_count(max32664c_dev);
        LOG_DBG("Initial FIFO count after mode switch: %d", fifo_count);
    }

    /* Initialize RTIO context for polling-based reads */
    int rc = ppg_wrist_init_rtio();
    if (rc < 0) {
        LOG_ERR("Failed to initialize RTIO context: %d", rc);
        return;
    }

    /* Initialize polling work but DO NOT start it yet - defer to run state */
    ppg_wrist_init_polling();
    LOG_INF("Polling infrastructure initialized - will start polling in run state");
}

static void st_ppg_samp_active_run(void *o)
{
    static int run_count = 0;
    static bool polling_started = false;
    
    /* Only log every 500th run to reduce spam */
    if (++run_count % 500 == 0) {
        LOG_DBG("PPG SM Active Run #%d", run_count);
    }
    
    /* Start polling only once, after we've been running for a while to ensure I2C is stable */
    if (!polling_started && run_count > 50) {  /* After ~2.5 seconds (50 * 50ms) */
        LOG_INF("Enabling global polling and starting sensor polling (run #%d)", run_count);
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
            LOG_ERR("Error in PPG State Machine");
            break;
        }

        k_msleep(1000);
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
            ppg_wrist_stop_polling();

            LOG_DBG("Starting One Shot SpO2");

            hpi_load_scr_spl(SCR_SPL_SPO2_MEASURE, SCROLL_UP, (uint8_t)SCR_SPO2, SPO2_SOURCE_PPG_WR, 0, 0);

            hw_max32664c_set_op_mode(MAX32664C_OP_MODE_STOP_ALGO, MAX32664C_ALGO_MODE_NONE);
            k_msleep(600);
            hw_max32664c_set_op_mode(MAX32664C_OP_MODE_ALGO_AEC, MAX32664C_ALGO_MODE_CONT_HR_SHOT_SPO2);
            k_msleep(600);
            LOG_INF("SpO2 mode set - polling will be managed by active state");

            spo2_measurement_in_progress = true;
        }

        if (k_sem_take(&sem_stop_one_shot_spo2, K_NO_WAIT) == 0)
        {
            LOG_DBG("Stopping One Shot SpO2");
            ppg_wrist_stop_polling();
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
            LOG_INF("Continuous HR mode set - polling will be managed by active state");
        }

        k_msleep(100);
    }
}

#define PPG_CTRL_THREAD_STACKSIZE 1024
#define PPG_CTRL_THREAD_PRIORITY 5

#define SMF_PPG_THREAD_STACKSIZE 4096
#define SMF_PPG_THREAD_PRIORITY 5

K_THREAD_DEFINE(smf_ppg_wrist_thread_id, SMF_PPG_THREAD_STACKSIZE, smf_ppg_wrist_thread, NULL, NULL, NULL, SMF_PPG_THREAD_PRIORITY, 0, 1000);
//K_THREAD_DEFINE(ppg_ctrl_thread_id, PPG_CTRL_THREAD_STACKSIZE, ppg_wrist_ctrl_thread, NULL, NULL, NULL, PPG_CTRL_THREAD_PRIORITY, 0, 0);