#include <zephyr/kernel.h>
#include <zephyr/smf.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

LOG_MODULE_REGISTER(smf_ppg_finger, LOG_LEVEL_DBG);

#include "hw_module.h"
#include "max32664d.h"
#include "hpi_common_types.h"
#include "fs_module.h"
#include "ui/move_ui.h"
#include "cmd_module.h"
#include "hpi_sys.h"

#define PPG_FI_SAMPLING_INTERVAL_MS 20
#define MAX30101_SENSOR_ID 0x15
#define BPT_CAL_TIMEOUT_MS 60000

K_SEM_DEFINE(sem_bpt_est_start, 0, 1);
K_SEM_DEFINE(sem_bpt_cal_start, 0, 1);
K_SEM_DEFINE(sem_fi_spo2_est_start, 0, 1);

K_SEM_DEFINE(sem_bpt_sensor_found, 0, 1);
K_SEM_DEFINE(sem_spo2_sensor_found, 0, 1);

K_SEM_DEFINE(sem_start_fi_sampling, 0, 1);
K_SEM_DEFINE(sem_stop_fi_sampling, 0, 1);

K_SEM_DEFINE(sem_bpt_est_complete, 0, 1);
K_SEM_DEFINE(sem_bpt_cal_complete, 0, 1);
K_SEM_DEFINE(sem_spo2_est_complete, 0, 1);

K_SEM_DEFINE(sem_bpt_enter_mode_cal, 0, 1);
K_SEM_DEFINE(sem_bpt_exit_mode_cal, 0, 1);

ZBUS_CHAN_DECLARE(bpt_chan);

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

static bool bpt_process_done = false;

static uint8_t m_cal_index;
static uint8_t m_cal_sys;
static uint8_t m_cal_dia;

static uint8_t m_est_sys;
static uint8_t m_est_dia;
static uint8_t m_est_hr;
static uint8_t m_est_spo2;
static uint8_t m_est_spo2_conf;

K_MUTEX_DEFINE(mutex_bpt_cal_set);

// Externs
extern const struct device *const max32664d_dev;
extern struct k_sem sem_ppg_finger_sm_start;

void hpi_bpt_set_cal_vals(uint8_t cal_index, uint8_t cal_sys, uint8_t cal_dia)
{
    k_mutex_lock(&mutex_bpt_cal_set, K_FOREVER);
    m_cal_index = cal_index;
    m_cal_sys = cal_sys;
    m_cal_dia = cal_dia;
    k_mutex_unlock(&mutex_bpt_cal_set);
}

static void sensor_ppg_finger_decode(uint8_t *buf, uint32_t buf_len, uint8_t m_ppg_op_mode)
{
    const struct max32664d_encoded_data *edata = (const struct max32664d_encoded_data *)buf;
    struct hpi_ppg_fi_data_t ppg_sensor_sample;

    uint16_t _n_samples = edata->num_samples;
    // printk("FNS: %d ", edata->num_samples);
    if (_n_samples > 16)
    {
        _n_samples = 16;
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
            else
            {
                ppg_sensor_sample.spo2_state = SPO2_MEAS_SUCCESS;
                LOG_DBG("SpO2 Measurement Done");
                k_sem_give(&sem_spo2_est_complete);
            }
        }

        k_msgq_put(&q_ppg_fi_sample, &ppg_sensor_sample, K_MSEC(1));
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

            if (edata->bpt_progress == 100 && bpt_process_done == false)
            {
                hpi_bpt_stop();
                if (m_ppg_op_mode == PPG_FI_OP_MODE_BPT_CAL)
                {
                    // BPT Calibration done
                    LOG_INF("BPT Calibration Done");
                    k_sem_give(&sem_bpt_cal_complete);
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
                bpt_process_done = true;
            }
        }
        else if (m_ppg_op_mode == PPG_FI_OP_MODE_SPO2_EST)
        {
            // SpO2 Estimation done
            m_est_spo2 = edata->spo2;
            m_est_spo2_conf = edata->spo2_conf;

            LOG_DBG("SpO2: %d | Confidence: %d", edata->spo2, edata->spo2_conf);

            // k_sem_give(&sem_bpt_est_complete);
        }
    }
}

void work_fi_sample_handler(struct k_work *work)
{
    uint8_t data_buf[384];

    int ret = 0;
    ret = sensor_read(&max32664d_iodev, &max32664d_read_rtio_poll_ctx, data_buf, sizeof(data_buf));
    if (ret < 0)
    {
        LOG_ERR("Error reading sensor data");
        return;
    }
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

static void hw_bpt_start_est(void)
{
    LOG_INF("Starting BPT Estimation");
    // ppg_fi_op_mode = PPG_FI_OP_MODE_BPT_EST;
    bpt_process_done = false;

    struct tm curr_time = hpi_sys_get_sys_time();

    uint32_t date, time;
    hw_bpt_encode_date_time(&curr_time, &date, &time);

    struct sensor_value data_time_val;
    data_time_val.val1 = date; // Date
    data_time_val.val2 = time; // Time
    sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664D_ATTR_SET_DATE_TIME, &data_time_val);

    char m_file_name[32];

    for (int i = 0; i < 5; i++)
    {
        snprintf(m_file_name, sizeof(m_file_name), "/lfs/sys/bpt_cal_%d", i);
        // Load calibration vector 0
        if (fs_load_file_to_buffer(m_file_name, bpt_cal_vector_buf, CAL_VECTOR_SIZE) == 0)
        {
            max32664d_set_bpt_cal_vector(max32664d_dev, i, bpt_cal_vector_buf);
            LOG_INF("Loaded calibration vector %d", i);
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
}

static void hw_bpt_start_cal(int cal_index, int cal_sys, int cal_dia)
{
    LOG_INF("Starting BPT Calibration");
    // ppg_fi_op_mode = PPG_FI_OP_MODE_BPT_CAL;
    bpt_process_done = false;
    // Set the date and time for the BPT calibration
    struct tm curr_time = hpi_sys_get_sys_time();

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
    smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_IDLE]);
}

static void st_ppg_fing_idle_entry(void *o)
{
    LOG_DBG("PPG Finger SM Idle Entry");

    // k_thread_suspend(ppg_finger_sampling_thread_id);
}

static void st_ppg_fing_idle_run(void *o)
{
    // LOG_DBG("PPG Finger SM Idle Running");
    struct s_ppg_fi_object *s = (struct s_ppg_fi_object *)o;

    if (k_sem_take(&sem_bpt_est_start, K_NO_WAIT) == 0)
    {
        s->ppg_fi_op_mode = PPG_FI_OP_MODE_BPT_EST;
        smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_CHECK_SENSOR]);
    }

    if (k_sem_take(&sem_bpt_enter_mode_cal, K_NO_WAIT) == 0)
    {
        s->ppg_fi_op_mode = PPG_FI_OP_MODE_BPT_CAL;
        smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_BPT_CAL_WAIT]);
    }

    if (k_sem_take(&sem_fi_spo2_est_start, K_NO_WAIT) == 0)
    {
        s->ppg_fi_op_mode = PPG_FI_OP_MODE_SPO2_EST;
        smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_CHECK_SENSOR]);
    }

    // smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_CHECK_SENSOR]);

    /*if (k_sem_take(&sem_bpt_cal_start, K_NO_WAIT) == 0)
    {
        // k_msleep(1000);
        s->ppg_fi_op_mode = PPG_FI_OP_MODE_BPT_CAL;
        smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_CHECK_SENSOR]);
    }*/
}

static void bpt_cal_timeout_handler(struct k_timer *timer_id)
{
    LOG_ERR("BPT Calibration Timeout");
    k_sem_give(&sem_stop_fi_sampling);                                          // Stop sampling if needed
    smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_BPT_CAL_FAIL]); // Transition to failure state
}

// Declare a timer for the timeout
K_TIMER_DEFINE(tmr_bpt_cal_timeout, bpt_cal_timeout_handler, NULL);

static void st_ppg_fi_cal_wait_entry(void *o)
{
    LOG_DBG("PPG Finger SM BPT Calibration Wait Entry");
    hpi_load_scr_spl(SCR_SPL_BPT_CAL_PROGRESS, SCROLL_NONE, SCR_BPT, 0, 0, 0);

    // Start the timeout timer
    // k_timer_start(&tmr_bpt_cal_timeout, K_MSEC(BPT_CAL_TIMEOUT_MS), K_NO_WAIT);
}

static void st_ppg_fi_cal_wait_run(void *o)
{
    // LOG_DBG("PPG Finger SM BPT Calibration Wait Running");

    // LOG_DBG("PPG Finger SM BPT Calibration Running");
    if (k_sem_take(&sem_bpt_cal_start, K_NO_WAIT) == 0)
    {
        // Turn off timeout
        k_timer_stop(&tmr_bpt_cal_timeout);

        sens_decode_ppg_fi_op_mode = PPG_FI_OP_MODE_BPT_CAL;
        smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_BPT_CAL]);
    }

    if (k_sem_take(&sem_bpt_exit_mode_cal, K_NO_WAIT) == 0)
    {
        hpi_load_screen(SCR_BPT, SCROLL_UP);
        smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_IDLE]);
    }
}

static void st_ppg_fing_bpt_cal_entry(void *o)
{
    LOG_DBG("PPG Finger SM BPT Calibration Entry");
    hw_bpt_start_cal(m_cal_index, m_cal_sys, m_cal_dia);
    k_sem_give(&sem_start_fi_sampling);
}

static void st_ppg_fing_bpt_cal_run(void *o)
{
    if (k_sem_take(&sem_bpt_cal_complete, K_NO_WAIT) == 0)
    {
        k_sem_give(&sem_stop_fi_sampling);
        k_sleep(K_MSEC(1000)); // Wait for the sampling to stop
        hpi_bpt_fetch_cal_vector(bpt_cal_vector_buf, m_cal_index);
        smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_BPT_CAL_DONE]);
    }
}

static void st_ppg_fing_bpt_cal_done_entry(void *o)
{
    LOG_DBG("PPG Finger SM BPT Calibration Done Entry");
    hpi_load_scr_spl(SCR_SPL_BPT_CAL_COMPLETE, SCROLL_NONE, SCR_BPT, 0, 0, 0);
}

static void st_ppg_fing_bpt_cal_done_run(void *o)
{
    k_msleep(2000);
    smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_BPT_CAL_WAIT]);
}

static void st_ppg_fing_bpt_est_entry(void *o)
{
    LOG_DBG("PPG Finger SM BPT Estimation Entry");
    sens_decode_ppg_fi_op_mode = PPG_FI_OP_MODE_BPT_EST;
    hpi_load_scr_spl(SCR_SPL_BPT_MEASURE, SCROLL_NONE, SCR_SPL_FI_SENS_CHECK, 0, 0, 0);
    hw_bpt_start_est();
    k_sem_give(&sem_start_fi_sampling);
}

static void st_ppg_fing_bpt_est_run(void *o)
{
    if (k_sem_take(&sem_bpt_est_complete, K_NO_WAIT) == 0)
    {
        k_sem_give(&sem_stop_fi_sampling);
        k_sleep(K_MSEC(1000)); // Wait for the sampling to stop
        smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_BPT_EST_DONE]);
    }
}

static void st_ppg_fing_bpt_est_done_entry(void *o)
{
    LOG_DBG("PPG Finger SM BPT Estimation Done Entry");
    hpi_load_scr_spl(SCR_SPL_BPT_EST_COMPLETE, SCROLL_NONE, m_est_sys, m_est_dia, m_est_hr, m_est_spo2);
    // hpi_hw_ldsw2_off();
}

static void st_ppg_fing_bpt_est_done_run(void *o)
{
    LOG_DBG("PPG Finger SM BPT Estimation Done Running");
    // k_msleep(2000);
    // hpi_load_screen(SCR_BPT, SCROLL_NONE);
    // smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_IDLE]);
}

static void st_ppg_fing_bpt_est_fail_entry(void *o)
{
    LOG_DBG("PPG Finger SM BPT Estimation Fail Entry");
}

static void st_ppg_fing_bpt_est_fail_run(void *o)
{
    // LOG_DBG("PPG Finger SM BPT Estimation Fail Running");
}

static void st_ppg_fing_bpt_cal_fail_entry(void *o)
{
    LOG_DBG("PPG Finger SM BPT Calibration Fail Entry");
    hpi_load_scr_spl(SCR_SPL_BPT_FAILED, SCROLL_NONE, SCR_BPT, 0, 0, 0);
}

static void st_ppg_fing_bpt_cal_fail_run(void *o)
{
    // LOG_DBG("PPG Finger SM BPT Calibration Fail Running");
}

#define SENSOR_CHECK_TIMEOUT_MS 15000

static void sensor_check_timeout_handler(struct k_timer *timer_id)
{
    LOG_ERR("Sensor check timeout: Sensor not found");
    smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_IDLE]);
}

K_TIMER_DEFINE(tmr_sensor_check_timeout, sensor_check_timeout_handler, NULL);

static void st_ppg_fi_check_sensor_entry(void *o)
{
    struct s_ppg_fi_object *s = (struct s_ppg_fi_object *)o;
    LOG_DBG("PPG Finger SM Check Sensor Entry");
    hpi_load_scr_spl(SCR_SPL_FI_SENS_CHECK, SCROLL_NONE, SCR_BPT, s->ppg_fi_op_mode, 0, 0);

    k_timer_start(&tmr_sensor_check_timeout, K_MSEC(SENSOR_CHECK_TIMEOUT_MS), K_NO_WAIT);
}

static void st_ppg_fi_check_sensor_run(void *o)
{
    // LOG_DBG("PPG Finger SM Check Sensor Running");
    struct s_ppg_fi_object *s = (struct s_ppg_fi_object *)o;

    struct sensor_value sensor_id_get;
    sensor_id_get.val1 = 0x00;
    sensor_attr_get(max32664d_dev, SENSOR_CHAN_ALL, MAX32664D_ATTR_SENSOR_ID, &sensor_id_get);
    LOG_DBG("AFE Sensor ID: %d", sensor_id_get.val1);

    // k_sem_give(&sem_ppg_fi_hide_loading);

    if (sensor_id_get.val1 != MAX30101_SENSOR_ID)
    {
        LOG_ERR("MAX30101 AFE sensor not found");
        // return;
    }
    else
    {
        LOG_DBG("MAX30101 sensor found !");

        // Stop the timeout timer since sensor is found
        k_timer_stop(&tmr_sensor_check_timeout);

        if (s->ppg_fi_op_mode == PPG_FI_OP_MODE_BPT_EST)
        {
            k_sem_give(&sem_bpt_sensor_found);
            smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_BPT_EST]);
        }
        else if (s->ppg_fi_op_mode == PPG_FI_OP_MODE_BPT_CAL)
        {
            k_sem_give(&sem_bpt_sensor_found);
            smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_BPT_CAL]);
        }
        else if (s->ppg_fi_op_mode == PPG_FI_OP_MODE_SPO2_EST)
        {
            k_sem_give(&sem_spo2_sensor_found);
            smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_SPO2_EST]);
        }
        else
        {
            LOG_ERR("Unknown PPG Finger operation mode");
            smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_SENSOR_FAIL]);
        }
    }
}

static void st_ppg_fi_sensor_fail_entry(void *o)
{
    LOG_DBG("PPG Finger SM Sensor Fail Entry");
}

static void st_ppg_fi_sensor_fail_run(void *o)
{
    LOG_DBG("PPG Finger SM Sensor Fail Running");
}

static void st_ppg_fi_spo2_est_entry(void *o)
{
    LOG_DBG("PPG Finger SM SpO2 Estimation Entry");
    sens_decode_ppg_fi_op_mode = PPG_FI_OP_MODE_SPO2_EST;
    hpi_load_scr_spl(SCR_SPL_SPO2_MEASURE, SCROLL_NONE, 0, 0, 0, 0);
    hw_bpt_start_est();                 // Start the BPT estimation for SpO2
    k_sem_give(&sem_start_fi_sampling); // Give the semaphore to start sampling
}

static void st_ppg_fi_spo2_est_run(void *o)
{
    LOG_DBG("PPG Finger SM SpO2 Estimation Running");
    if (k_sem_take(&sem_spo2_est_complete, K_NO_WAIT) == 0)
    {
        k_sem_give(&sem_stop_fi_sampling); // Stop the sampling
        k_sleep(K_MSEC(1000));             // Wait for the sampling to stop
        smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_SPO2_EST_DONE]);
    }
}

static void st_ppg_fi_spo2_est_done_entry(void *o)
{
    LOG_DBG("PPG Finger SM SpO2 Estimation Done Entry");
    hpi_load_scr_spl(SCR_SPL_SPO2_COMPLETE, SCROLL_NONE, SCR_SPO2, m_est_spo2, 0, 0);
}
static void st_ppg_fi_spo2_est_done_run(void *o)
{
    LOG_DBG("PPG Finger SM SpO2 Estimation Done Running");
    // k_msleep(2000);
    // hpi_load_screen(SCR_BPT, SCROLL_NONE);
    smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_IDLE]);
}

static const struct smf_state ppg_fi_states[] = {
    [PPG_FI_STATE_IDLE] = SMF_CREATE_STATE(st_ppg_fing_idle_entry, st_ppg_fing_idle_run, NULL, NULL, NULL),
    [PPG_FI_STATE_CHECK_SENSOR] = SMF_CREATE_STATE(st_ppg_fi_check_sensor_entry, st_ppg_fi_check_sensor_run, NULL, NULL, NULL),
    [PPG_FI_STATE_SENSOR_FAIL] = SMF_CREATE_STATE(st_ppg_fi_sensor_fail_entry, st_ppg_fi_sensor_fail_run, NULL, NULL, NULL),

    [PPG_FI_STATE_BPT_EST] = SMF_CREATE_STATE(st_ppg_fing_bpt_est_entry, st_ppg_fing_bpt_est_run, NULL, NULL, NULL),
    [PPG_FI_STATE_BPT_EST_DONE] = SMF_CREATE_STATE(st_ppg_fing_bpt_est_done_entry, st_ppg_fing_bpt_est_done_run, NULL, NULL, NULL),
    [PPG_FI_STATE_BPT_EST_FAIL] = SMF_CREATE_STATE(st_ppg_fing_bpt_est_fail_entry, st_ppg_fing_bpt_est_fail_run, NULL, NULL, NULL),

    [PPG_FI_STATE_BPT_CAL_WAIT] = SMF_CREATE_STATE(st_ppg_fi_cal_wait_entry, st_ppg_fi_cal_wait_run, NULL, NULL, NULL),
    [PPG_FI_STATE_BPT_CAL] = SMF_CREATE_STATE(st_ppg_fing_bpt_cal_entry, st_ppg_fing_bpt_cal_run, NULL, NULL, NULL),
    [PPG_FI_STATE_BPT_CAL_DONE] = SMF_CREATE_STATE(st_ppg_fing_bpt_cal_done_entry, st_ppg_fing_bpt_cal_done_run, NULL, NULL, NULL),
    [PPG_FI_STATE_BPT_CAL_FAIL] = SMF_CREATE_STATE(st_ppg_fing_bpt_cal_fail_entry, st_ppg_fing_bpt_cal_fail_run, NULL, NULL, NULL),

    [PPG_FI_STATE_SPO2_EST] = SMF_CREATE_STATE(st_ppg_fi_spo2_est_entry, st_ppg_fi_spo2_est_run, NULL, NULL, NULL),
    [PPG_FI_STATE_SPO2_EST_DONE] = SMF_CREATE_STATE(st_ppg_fi_spo2_est_done_entry, st_ppg_fi_spo2_est_done_run, NULL, NULL, NULL),
};

static void smf_ppg_finger_thread(void)
{
    int32_t ret;

    k_sem_take(&sem_ppg_finger_sm_start, K_FOREVER);
    smf_set_initial(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_IDLE]);
    // k_timer_start(&tmr_ppg_fi_sampling, K_MSEC(PPG_FI_SAMPLING_INTERVAL_MS), K_MSEC(PPG_FI_SAMPLING_INTERVAL_MS));

    LOG_INF("PPG Finger SMF Thread starting");

    for (;;)
    {
        ret = smf_run_state(SMF_CTX(&sf_obj));
        if (ret)
        {
            LOG_ERR("Error in PPG Finger State Machine");
            break;
        }
        k_msleep(1000);
    }
}

static void ppg_fi_ctrl_thread(void)
{
    LOG_INF("PPG Finger Control Thread starting");

    for (;;)
    {
        if (k_sem_take(&sem_start_fi_sampling, K_NO_WAIT) == 0)
        {
            LOG_INF("Start sampling");
            k_msleep(1000);
            k_timer_start(&tmr_ppg_fi_sampling, K_MSEC(PPG_FI_SAMPLING_INTERVAL_MS), K_MSEC(PPG_FI_SAMPLING_INTERVAL_MS));
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