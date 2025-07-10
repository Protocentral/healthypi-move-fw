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

RTIO_DEFINE(max32664c_read_rtio_poll_ctx, 1, 1);
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
        // printk("WR NS: %d ", _n_samples);
        // LOG_DBG("Chip Mode: %d", edata->chip_op_mode);
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

            // LOG_DBG("SPO2: %d | Spo2 Prog: %d | Low PI: %d | Motion: %d | State: %d", ppg_sensor_sample.spo2, ppg_sensor_sample.spo2_valid_percent_complete,
            //         ppg_sensor_sample.spo2_low_pi, ppg_sensor_sample.spo2_excessive_motion, ppg_sensor_sample.spo2_state);

            // LOG_DBG("HR Conf: %d", ppg_sensor_sample.hr_confidence);

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
        }
    }
}

void work_sample_handler(struct k_work *work)
{
    uint8_t wrist_buf[512];
    int ret;
    ret = sensor_read(&max32664c_iodev, &max32664c_read_rtio_poll_ctx, wrist_buf, sizeof(wrist_buf));
    if (ret < 0)
    {
        LOG_ERR("Error reading sensor data");
        return;
    }
    sensor_ppg_wrist_decode(wrist_buf, sizeof(wrist_buf));
}

K_WORK_DEFINE(work_sample, work_sample_handler);

void ppg_wrist_sampling_handler(struct k_timer *dummy)
{
    k_work_submit(&work_sample);
}

K_TIMER_DEFINE(tmr_ppg_wrist_sampling, ppg_wrist_sampling_handler, NULL);

static void st_ppg_samp_active_entry(void *o)
{
    LOG_DBG("PPG SM Active Entry");
    m_curr_state = PPG_SAMP_STATE_ACTIVE;

    hw_max32664c_set_op_mode(MAX32664C_OP_MODE_ALGO_AEC, MAX32664C_ALGO_MODE_CONT_HRM);

    // hw_max32664c_set_op_mode(MAX32664C_OP_MODE_RAW, MAX32664C_ALGO_MODE_CONT_HR_CONT_SPO2);
    // hw_max32664c_set_op_mode(MAX32664C_OP_MODE_ALGO_AEC, MAX32664C_ALGO_MODE_CONT_HRM);
    // hw_max32664c_set_op_mode(MAX32664C_OP_MODE_SCD, MAX32664C_ALGO_MODE_CONT_HR_CONT_SPO2);
}

static void st_ppg_samp_active_run(void *o)
{
    LOG_DBG("PPG SM Active Run");
    if (k_sem_take(&sem_ppg_wrist_off_skin, K_FOREVER) == 0)
    {
        LOG_DBG("Switching to Off Skin");
        smf_set_state(SMF_CTX(&sm_ctx_ppg_wr), &ppg_samp_states[PPG_SAMP_STATE_OFF_SKIN]);
    }
}

static void st_ppg_samp_probing_entry(void *o)
{
    LOG_DBG("PPG SM Probing Entry");
    m_curr_state = PPG_SAMP_STATE_PROBING;

    // Enter SCD mode
    hw_max32664c_set_op_mode(MAX32664C_OP_MODE_SCD, MAX32664C_ALGO_MODE_CONT_HRM);
}

static void st_ppg_samp_probing_run(void *o)
{
    // LOG_DBG("PPG SM Probing Run");
    if (k_sem_take(&sem_ppg_wrist_on_skin, K_FOREVER) == 0)
    {
        LOG_DBG("Switching to Active");
        smf_set_state(SMF_CTX(&sm_ctx_ppg_wr), &ppg_samp_states[PPG_SAMP_STATE_ACTIVE]);
    }
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

static const struct smf_state ppg_samp_states[] = {
    [PPG_SAMP_STATE_ACTIVE] = SMF_CREATE_STATE(st_ppg_samp_active_entry, st_ppg_samp_active_run, NULL, NULL, NULL),
    [PPG_SAMP_STATE_PROBING] = SMF_CREATE_STATE(st_ppg_samp_probing_entry, st_ppg_samp_probing_run, NULL, NULL, NULL),
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