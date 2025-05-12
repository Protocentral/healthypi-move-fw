#include <zephyr/kernel.h>
#include <zephyr/smf.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(smf_ppg_finger, LOG_LEVEL_DBG);

#include "hw_module.h"
#include "max32664d.h"
#include "hpi_common_types.h"
#include "fs_module.h"
#include "ui/move_ui.h"
#include "cmd_module.h"

#define PPG_FI_SAMPLING_INTERVAL_MS 60

#define DEFAULT_DATE 240428 // YYMMDD 28th April 2024
#define DEFAULT_TIME 121212 // HHMMSS 12:12:12

#define DEFAULT_FUTURE_DATE 240429 // YYMMDD 29th April 2024
#define DEFAULT_FUTURE_TIME 121213 // HHMMSS 12:12:13

#define MAX30101_SENSOR_ID 0x15

static const struct smf_state ppg_fi_states[];

SENSOR_DT_READ_IODEV(max32664d_iodev, DT_ALIAS(max32664d), {SENSOR_CHAN_VOLTAGE});

// K_SEM_DEFINE(sem_ppg_fi_bpt_est_start, 0, 1);

K_SEM_DEFINE(sem_ppg_fi_show_loading, 0, 1);
K_SEM_DEFINE(sem_ppg_fi_hide_loading, 0, 1);

K_SEM_DEFINE(sem_bpt_est_abort, 0, 1);

// New Sems
K_SEM_DEFINE(sem_bpt_est_start, 0, 1);
K_SEM_DEFINE(sem_bpt_cal_start, 0, 1);

K_SEM_DEFINE(sem_bpt_sensor_found, 0, 1);

K_SEM_DEFINE(sem_start_bpt_sampling, 0, 1);
K_SEM_DEFINE(sem_stop_bpt_sampling, 0, 1);

K_SEM_DEFINE(sem_bpt_cal_complete, 0, 1);
K_SEM_DEFINE(sem_bpt_set_mode_cal, 0, 1);
// K_SEM_DEFINE(sem_bpt)

K_MSGQ_DEFINE(q_ppg_fi_sample, sizeof(struct hpi_ppg_fi_data_t), 64, 1);
RTIO_DEFINE(max32664d_read_rtio_poll_ctx, 8, 8);

enum ppg_fi_op_modes
{
    PPG_FI_OP_MODE_IDLE,
    PPG_FI_OP_MODE_BPT_EST,
    PPG_FI_OP_MODE_BPT_CAL,
};

// static uint8_t ppg_fi_op_mode = PPG_FI_OP_MODE_IDLE;

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
};

struct s_ppg_fi_object
{
    struct smf_ctx ctx;
    uint8_t ppg_fi_op_mode;
    uint8_t bpt_cal_curr_index;

} sf_obj;

static uint8_t sens_decode_ppg_fi_op_mode = PPG_FI_OP_MODE_IDLE;

static uint8_t bpt_cal_vector_buf[CAL_VECTOR_SIZE] = {0};

// Forward declaration
static void hw_bpt_start_cal(int cal_index, int cal_sys, int cal_dia);
static void hpi_bpt_fetch_cal_vector(uint8_t *bpt_cal_vector_buf);

static bool bpt_process_done = false;

static uint8_t m_cal_index;
static uint8_t m_cal_sys;
static uint8_t m_cal_dia;
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

        ppg_sensor_sample.bp_sys = edata->bpt_sys;
        ppg_sensor_sample.bp_dia = edata->bpt_dia;
        ppg_sensor_sample.bpt_status = edata->bpt_status;
        ppg_sensor_sample.bpt_progress = edata->bpt_progress;

        k_msgq_put(&q_ppg_fi_sample, &ppg_sensor_sample, K_MSEC(1));
        // k_sem_give(&sem_ppg_finger_sample_trigger);

        LOG_DBG("Status: %d Progress: %d", edata->bpt_status, edata->bpt_progress);

        if (edata->bpt_progress == 100 && bpt_process_done == false)
        {
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
                // k_sem_give(&sem_bpt_est_abort);
            }
            bpt_process_done = true;
        }
    }
}

void work_fi_sample_handler(struct k_work *work)
{
    uint8_t data_buf[512];
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

    uint32_t cal_year = (timeinfo.tm_year + 1900) * 10000;
    uint32_t cal_month = (timeinfo.tm_mon + 1) * 100; // tm_mon is 0-11
    uint32_t cal_day = timeinfo.tm_mday;

    uint32_t cal_hour = timeinfo.tm_hour * 10000;
    uint32_t cal_min = timeinfo.tm_min * 100;
    uint32_t cal_sec = timeinfo.tm_sec;

    LOG_DBG("Current Date: %d-%d-%d", cal_year, cal_month, cal_day);
    LOG_DBG("Current Time: %d:%d:%d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    //*date = (timeinfo.tm_year << 16) | (timeinfo.tm_mon << 8) | timeinfo.tm_mday;
    //*time = (timeinfo.tm_hour << 16) | (timeinfo.tm_min << 8) | timeinfo.tm_sec;

    uint32_t encoded_date = (cal_year + cal_month + cal_day);
    uint32_t encoded_time = (cal_hour + cal_min + cal_sec);

    LOG_DBG("Encoded Date: %d, Time: %d", encoded_date, encoded_time);

    *date = encoded_date;
    *time = encoded_time;
}

/**
 * @brief Starts the Blood Pressure Trend (BPT) estimation process.
 *
 * This function sets the date and time for the BPT estimation, then sets the
 * operation mode to BPT and starts the estimation process.
 */
static void hw_bpt_start_est(void)
{
    LOG_INF("Starting BPT Estimation");
    // ppg_fi_op_mode = PPG_FI_OP_MODE_BPT_EST;
    bpt_process_done = false;

    struct tm curr_time = hw_get_sys_time();

    uint32_t date, time;
    hw_bpt_encode_date_time(&curr_time, &date, &time);

    struct sensor_value data_time_val;
    data_time_val.val1 = date; // Date
    data_time_val.val2 = time; // Time
    sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664D_ATTR_SET_DATE_TIME, &data_time_val);

    // Load calibration vector 0
    // struct sensor_value cal_idx_val;
    // cal_idx_val.val1 = 0;
    // sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664D_ATTR_CAL_SET_CURR_INDEX, &cal_idx_val);
    fs_load_file_to_buffer("/lfs/sys/bpt_cal_0", bpt_cal_vector_buf, CAL_VECTOR_SIZE);
    //max32664d_set_bpt_cal_vector(max32664d_dev, 0, bpt_cal_vector_buf);

    // Load calibration vector 1
    // cal_idx_val.val1 = 1;
    // sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664D_ATTR_CAL_SET_CURR_INDEX, &cal_idx_val);
    fs_load_file_to_buffer("/lfs/sys/bpt_cal_1", bpt_cal_vector_buf, CAL_VECTOR_SIZE);
   // max32664d_set_bpt_cal_vector(max32664d_dev, 1, bpt_cal_vector_buf);

    // Load calibration vector 2
    // cal_idx_val.val1 = 2;
    // sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664D_ATTR_CAL_SET_CURR_INDEX, &cal_idx_val);
    fs_load_file_to_buffer("/lfs/sys/bpt_cal_2", bpt_cal_vector_buf, CAL_VECTOR_SIZE);
   // max32664d_set_bpt_cal_vector(max32664d_dev, 2, bpt_cal_vector_buf);

    // Load calibration vector 3
    // cal_idx_val.val1 = 3;
    // sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664D_ATTR_CAL_SET_CURR_INDEX, &cal_idx_val);
    fs_load_file_to_buffer("/lfs/sys/bpt_cal_3", bpt_cal_vector_buf, CAL_VECTOR_SIZE);
    //max32664d_set_bpt_cal_vector(max32664d_dev, 3, bpt_cal_vector_buf);

    // Load calibration vector 4
    // cal_idx_val.val1 = 4;
    // sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664D_ATTR_CAL_SET_CURR_INDEX, &cal_idx_val);
    fs_load_file_to_buffer("/lfs/sys/bpt_cal_4", bpt_cal_vector_buf, CAL_VECTOR_SIZE);
    //max32664d_set_bpt_cal_vector(max32664d_dev, 4, bpt_cal_vector_buf);

    // TODO: load cal vectors 1-4 if needed

    struct sensor_value mode_val;
    mode_val.val1 = MAX32664D_OP_MODE_BPT;
    sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664D_ATTR_OP_MODE, &mode_val);

    /*struct sensor_value load_cal;
    load_cal.val1 = 0x00000000;
    sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664D_ATTR_LOAD_CALIB, &load_cal);


    struct sensor_value data_time_val;
    data_time_val.val1 = DEFAULT_FUTURE_DATE; // Date // TODO: Update to local time
    data_time_val.val2 = DEFAULT_FUTURE_TIME; // Time
    sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664D_ATTR_DATE_TIME, &data_time_val);
    k_sleep(K_MSEC(100));

    struct sensor_value mode_set;
    mode_set.val1 = MAX32664D_OP_MODE_BPT;
    sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664D_ATTR_OP_MODE, &mode_set);
    */

    k_sleep(K_MSEC(1000));
}



static void hw_bpt_start_cal(int cal_index, int cal_sys, int cal_dia)
{
    LOG_INF("Starting BPT Calibration");
    // ppg_fi_op_mode = PPG_FI_OP_MODE_BPT_CAL;
    bpt_process_done = false;
    // Set the date and time for the BPT calibration
    struct tm curr_time = hw_get_sys_time();

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

void hpi_bpt_stop(void)
{
    struct sensor_value mode_val;
    mode_val.val1 = MAX32664D_ATTR_STOP_EST;
    sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664D_ATTR_STOP_EST, &mode_val);
}

static void hpi_bpt_fetch_cal_vector(uint8_t *bpt_cal_vector_buf)
{
    struct sensor_value fetch_cal;
    fetch_cal.val1 = 0;
    sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664D_ATTR_CAL_FETCH_VECTOR, &fetch_cal);

    max32664d_get_bpt_cal_vector(max32664d_dev, bpt_cal_vector_buf);
    LOG_HEXDUMP_INF(bpt_cal_vector_buf, CAL_VECTOR_SIZE, "BPT Cal Vector 1");
    char cal_file_name[32];
    snprintf(cal_file_name, sizeof(cal_file_name), "/lfs/sys/bpt_cal_%d", m_cal_index);

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

    if (k_sem_take(&sem_bpt_set_mode_cal, K_NO_WAIT) == 0)
    {
        s->ppg_fi_op_mode = PPG_FI_OP_MODE_BPT_CAL;
        smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_BPT_CAL_WAIT]);
    }
    // smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_CHECK_SENSOR]);

    /*if (k_sem_take(&sem_bpt_cal_start, K_NO_WAIT) == 0)
    {
        // k_msleep(1000);
        s->ppg_fi_op_mode = PPG_FI_OP_MODE_BPT_CAL;
        smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_CHECK_SENSOR]);
    }*/
}

// Define a timeout duration (e.g., 10 seconds)
#define BPT_CAL_TIMEOUT_MS 60000

// Timeout handler function
static void bpt_cal_timeout_handler(struct k_timer *timer_id)
{
    LOG_ERR("BPT Calibration Timeout");
    k_sem_give(&sem_stop_bpt_sampling);                                         // Stop sampling if needed
    smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_BPT_CAL_FAIL]); // Transition to failure state
}

// Declare a timer for the timeout
K_TIMER_DEFINE(tmr_bpt_cal_timeout, bpt_cal_timeout_handler, NULL);

static void st_ppg_fi_cal_wait_entry(void *o)
{
    LOG_DBG("PPG Finger SM BPT Calibration Wait Entry");
    hpi_load_scr_spl(SCR_SPL_BPT_CAL_PROGRESS, SCROLL_NONE, SCR_BPT);

    // Start the timeout timer
    k_timer_start(&tmr_bpt_cal_timeout, K_MSEC(BPT_CAL_TIMEOUT_MS), K_NO_WAIT);
}

static void st_ppg_fi_cal_wait_run(void *o)
{
    LOG_DBG("PPG Finger SM BPT Calibration Wait Running");

    // LOG_DBG("PPG Finger SM BPT Calibration Running");
    if (k_sem_take(&sem_bpt_cal_start, K_NO_WAIT) == 0)
    {
        // Turn off timeout
        k_timer_stop(&tmr_bpt_cal_timeout);

        sens_decode_ppg_fi_op_mode = PPG_FI_OP_MODE_BPT_CAL;
        smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_BPT_CAL]);
    }
}

static void st_ppg_fing_bpt_cal_entry(void *o)
{
    LOG_DBG("PPG Finger SM BPT Calibration Entry");
    hw_bpt_start_cal(m_cal_index, m_cal_sys, m_cal_dia);
    k_sem_give(&sem_start_bpt_sampling);
}

static void st_ppg_fing_bpt_cal_run(void *o)
{
    if (k_sem_take(&sem_bpt_cal_complete, K_NO_WAIT) == 0)
    {
        k_sem_give(&sem_stop_bpt_sampling);
        hpi_bpt_fetch_cal_vector(bpt_cal_vector_buf);
        smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_BPT_CAL_DONE]);
    }
}

static void st_ppg_fing_bpt_cal_done_entry(void *o)
{
    LOG_DBG("PPG Finger SM BPT Calibration Done Entry");
    hpi_load_scr_spl(SCR_SPL_BPT_CAL_COMPLETE, SCROLL_NONE, SCR_BPT);
}

static void st_ppg_fing_bpt_cal_done_run(void *o)
{
    k_msleep(2000);
    smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_IDLE]);
}

static void st_ppg_fing_bpt_est_entry(void *o)
{
    LOG_DBG("PPG Finger SM BPT Estimation Entry");
    hw_bpt_start_est();
    sens_decode_ppg_fi_op_mode = PPG_FI_OP_MODE_BPT_EST;
    k_sem_give(&sem_start_bpt_sampling);
}

static void st_ppg_fing_bpt_est_run(void *o)
{

    // LOG_DBG("PPG Finger SM BPT Estimation Running");

    // hw_bpt_start_est();
    //   k_thread_resume(ppg_finger_sampling_thread_id);
    //   }

    if (k_sem_take(&sem_bpt_est_abort, K_NO_WAIT) == 0)
    {
        hpi_bpt_stop();
        smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_IDLE]);
    }

    // struct hpi_ppg_fi_data_t ppg_fi_sensor_sample;

    // k_msleep(1000);
    // smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_IDLE]);

    /*if (k_msgq_get(&q_ppg_fi_sample, &ppg_fi_sensor_sample, K_NO_WAIT) == 0)
    {
        if (ppg_fi_sensor_sample.bpt_status == 1)
        {
            smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FING_STATE_BPT_EST_DONE]);
        }
        else if (ppg_fi_sensor_sample.bpt_status == 2)
        {
            smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FING_STATE_BPT_EST_FAIL]);
        }
    }*/
}

static void st_ppg_fing_bpt_est_done_entry(void *o)
{
    LOG_DBG("PPG Finger SM BPT Estimation Done Entry");
}

static void st_ppg_fing_bpt_est_done_run(void *o)
{
    LOG_DBG("PPG Finger SM BPT Estimation Done Running");
}

static void st_ppg_fing_bpt_est_fail_entry(void *o)
{
    LOG_DBG("PPG Finger SM BPT Estimation Fail Entry");
}

static void st_ppg_fing_bpt_est_fail_run(void *o)
{
    LOG_DBG("PPG Finger SM BPT Estimation Fail Running");
}

static void st_ppg_fing_bpt_cal_fail_entry(void *o)
{
    LOG_DBG("PPG Finger SM BPT Calibration Fail Entry");
    hpi_load_scr_spl(SCR_SPL_BPT_FAILED, SCROLL_NONE, SCR_BPT);
}

static void st_ppg_fing_bpt_cal_fail_run(void *o)
{
    // LOG_DBG("PPG Finger SM BPT Calibration Fail Running");
}

static void st_ppg_fi_check_sensor_entry(void *o)
{
    LOG_DBG("PPG Finger SM Check Sensor Entry");
    hpi_load_scr_spl(SCR_SPL_BPT_SCR3, SCROLL_NONE, SCR_BPT);
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
        k_sem_give(&sem_bpt_sensor_found);
        if (s->ppg_fi_op_mode == PPG_FI_OP_MODE_BPT_EST)
        {
            smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_BPT_EST]);
        }
        else if (s->ppg_fi_op_mode == PPG_FI_OP_MODE_BPT_CAL)
        {
            smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_BPT_CAL]);
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
        if (k_sem_take(&sem_start_bpt_sampling, K_NO_WAIT) == 0)
        {
            LOG_INF("Start sampling");
            k_msleep(1000);
            k_timer_start(&tmr_ppg_fi_sampling, K_MSEC(PPG_FI_SAMPLING_INTERVAL_MS), K_MSEC(PPG_FI_SAMPLING_INTERVAL_MS));
        }

        if (k_sem_take(&sem_stop_bpt_sampling, K_NO_WAIT) == 0)
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
