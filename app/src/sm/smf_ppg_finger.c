#include <zephyr/kernel.h>
#include <zephyr/smf.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(smf_ppg_finger, LOG_LEVEL_DBG);

#include "hw_module.h"
#include "max32664d.h"
#include "hpi_common_types.h"
#include "fs_module.h"

#define PPG_FINGER_SAMPLING_INTERVAL_MS 40

#define DEFAULT_DATE 240428 // YYMMDD 28th April 2024
#define DEFAULT_TIME 121212 // HHMMSS 12:12:12

#define DEFAULT_FUTURE_DATE 240429 // YYMMDD 29th April 2024
#define DEFAULT_FUTURE_TIME 121213 // HHMMSS 12:12:13

#define MAX30101_SENSOR_ID 0x15

static uint8_t test_bpt_cal_vector[CALIBVECTOR_SIZE] = {0x50, 0x04, 0x03, 0, 0, 175, 63, 3, 33, 75, 0, 0, 0, 0, 15, 198, 2, 100, 3, 32, 0, 0, 3, 207, 0, // calib vector sample
                                                        4, 0, 3, 175, 170, 3, 33, 134, 0, 0, 0, 0, 15, 199, 2, 100, 3, 32, 0, 0, 3,
                                                        207, 0, 4, 0, 3, 176, 22, 3, 33, 165, 0, 0, 0, 0, 15, 200, 2, 100, 3, 32, 0,
                                                        0, 3, 207, 0, 4, 0, 3, 176, 102, 3, 33, 203, 0, 0, 0, 0, 15, 201, 2, 100, 3,
                                                        32, 0, 0, 3, 207, 0, 4, 0, 3, 176, 178, 3, 33, 236, 0, 0, 0, 0, 15, 202, 2,
                                                        100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3, 176, 255, 3, 34, 16, 0, 0, 0, 0, 15,
                                                        203, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3, 177, 64, 3, 34, 41, 0, 0, 0, 0,
                                                        15, 204, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3, 177, 130, 3, 34, 76, 0, 0,
                                                        0, 0, 15, 205, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3, 177, 189, 3, 34, 90,
                                                        0, 0, 0, 0, 15, 206, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3, 177, 248, 3, 34,
                                                        120, 0, 0, 0, 0, 15, 207, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3, 178, 69, 3,
                                                        34, 137, 0, 0, 0, 0, 15, 208, 2, 100, 3, 32, 0, 0, 3, 0, 0, 175, 63, 3, 33,
                                                        75, 0, 0, 0, 0, 15, 198, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3, 175, 170, 3,
                                                        33, 134, 0, 0, 0, 0, 15, 199, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3, 176,
                                                        22, 3, 33, 165, 0, 0, 0, 0, 15, 200, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3,
                                                        176, 102, 3, 33, 203, 0, 0, 0, 0, 15, 201, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4,
                                                        0, 3, 176, 178, 3, 33, 236, 0, 0, 0, 0, 15, 202, 2, 100, 3, 32, 0, 0, 3, 207,
                                                        0, 4, 0, 3, 176, 255, 3, 34, 16, 0, 0, 0, 0, 15, 203, 2, 100, 3, 32, 0, 0, 3,
                                                        207, 0, 4, 0, 3, 177, 64, 3, 34, 41, 0, 0, 0, 0, 15, 204, 2, 100, 3, 32, 0, 0,
                                                        3, 207, 0, 4, 0, 3, 177, 130, 3, 34, 76, 0, 0, 0, 0, 15, 205, 2, 100, 3, 32,
                                                        0, 0, 3, 207, 0, 4, 0, 3, 177, 189, 3, 34, 90, 0, 0, 0, 0, 15, 206, 2, 100, 3,
                                                        32, 0, 0, 3, 207, 0, 4, 0, 3, 177, 248, 3, 34, 120, 0, 0, 0, 0, 15, 207, 2,
                                                        100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3, 178, 69, 3, 34, 137, 0, 0, 0, 0, 15,
                                                        208, 2, 100, 3, 32, 0, 0, 3, 0, 0, 175, 63, 3, 33, 75, 0, 0, 0, 0, 15, 198, 2,
                                                        100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3, 175, 170, 3, 33, 134, 0, 0, 0, 0, 15,
                                                        199, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3, 176, 22, 3, 33, 165, 0, 0, 0, 0,
                                                        15, 200, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3, 176, 102, 3, 33, 203, 0, 0,
                                                        0, 0, 15, 201, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3, 176, 178, 3, 33, 236,
                                                        0, 0, 0, 0, 15, 202, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3, 176, 255, 3, 34,
                                                        16, 0, 0, 0, 0, 15, 203, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3, 177, 64, 3,
                                                        34, 41, 0, 0, 0, 0, 15, 204, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3, 177,
                                                        130, 3, 34, 76, 0, 0, 0, 0, 15, 205, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3,
                                                        177, 189, 3, 34, 90, 0, 0, 0, 0, 15, 206, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4,
                                                        0, 3, 177, 248, 3, 34, 120, 0, 0, 0, 0, 15, 207, 2, 100, 3, 32, 0, 0, 3, 207,
                                                        0, 4, 0, 3, 178, 69, 3, 34, 137, 0, 0, 0, 0, 15, 208, 2, 100, 3, 32, 0, 0, 3,
                                                        0, 0, 175, 63, 3, 33, 75, 0, 0, 0, 0, 15, 198, 2, 100, 3, 32, 0, 0, 3, 207, 0,
                                                        4, 0, 3, 175, 170, 3, 33, 134, 0, 0, 0, 0, 15, 199, 2, 100, 3, 32, 0, 0, 3,
                                                        207, 0, 4, 0, 3, 176, 22, 3, 33, 165, 0, 0, 0, 0, 15, 200, 2, 100, 3, 32, 0,
                                                        0, 3, 207, 0, 4, 0, 3, 176, 102, 3};

uint8_t bpt_cal_vector[CALIBVECTOR_SIZE] = {0};

static const struct smf_state ppg_fi_states[];

SENSOR_DT_READ_IODEV(max32664d_iodev, DT_ALIAS(max32664d), {SENSOR_CHAN_VOLTAGE});

// K_SEM_DEFINE(sem_ppg_fi_bpt_est_start, 0, 1);

K_SEM_DEFINE(sem_ppg_fi_show_loading, 0, 1);
K_SEM_DEFINE(sem_ppg_fi_hide_loading, 0, 1);

K_SEM_DEFINE(sem_bpt_est_abort, 0, 1);

// New Sems
K_SEM_DEFINE(sem_bpt_est_start, 0, 1);

K_SEM_DEFINE(sem_bpt_check_sensor, 0, 1);

K_MSGQ_DEFINE(q_ppg_fi_sample, sizeof(struct hpi_ppg_fi_data_t), 64, 1);
RTIO_DEFINE(max32664d_read_rtio_poll_ctx, 8, 8);

enum ppg_fi_sm_state
{
    PPG_FI_STATE_IDLE,
    PPG_FI_STATE_CHECK_SENSOR,
    PPG_FI_STATE_SENSOR_FAIL,

    PPG_FI_STATE_BPT_EST,
    PPG_FI_STATE_BPT_EST_DONE,
    PPG_FI_STATE_BPT_EST_FAIL,

    PPG_FI_STATE_BPT_CAL,
    PPG_FI_STATE_BPT_CAL_DONE,
    PPG_FI_STATE_BPT_CAL_FAIL,
};

struct s_object
{
    struct smf_ctx ctx;
} sf_obj;

extern const struct device *const max32664d_dev;
extern struct k_sem sem_ppg_finger_sm_start;

#define SMF_PPG_FINGER_THREAD_STACKSIZE 4096
#define SMF_PPG_FINGER_THREAD_PRIORITY 7

K_THREAD_STACK_DEFINE(ppg_finger_sampling_thread_stack, SMF_PPG_FINGER_THREAD_STACKSIZE);

struct k_thread ppg_finger_sampling_thread;
k_tid_t ppg_finger_sampling_thread_id;

/*
static void sensor_ppg_finger_processing_callback(int result, uint8_t *buf,
                                                  uint32_t buf_len, void *userdata)
{
        const struct max32664d_encoded_data *edata = (const struct max32664d_encoded_data *)buf;

        struct hpi_ppg_fi_data_t ppg_sensor_sample;
        // printk("NS: %d ", edata->num_samples);
        if (edata->num_samples > 0)
        {
                ppg_sensor_sample.ppg_num_samples = edata->num_samples;

                for (int i = 0; i < edata->num_samples; i++)
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

                // printk("Status: %d Progress: %d\n", edata->bpt_status, edata->bpt_progress);
 */

static void sensor_ppg_finger_decode(uint8_t *buf, uint32_t buf_len)
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

        // printk("Status: %d Progress: %d\n", edata->bpt_status, edata->bpt_progress);
    }
}

void ppg_finger_sampling_thread_runner(void *, void *, void *)
{
    int ret;
    uint8_t fing_data_buf[768];

    LOG_INF("PPG Finger Sampling starting");
    for (;;)
    {
        ret = sensor_read(&max32664d_iodev, &max32664d_read_rtio_poll_ctx, fing_data_buf, sizeof(fing_data_buf));
        if (ret < 0)
        {
            LOG_ERR("Error reading sensor data");
            continue;
        }
        sensor_ppg_finger_decode(fing_data_buf, sizeof(fing_data_buf));

        // sensor_read_async_mempool(&max32664d_iodev, &max32664d_read_rtio_ctx, NULL);
        // sensor_processing_with_callback(&max32664d_read_rtio_ctx, sensor_ppg_finger_processing_callback);

        k_sleep(K_MSEC(PPG_FINGER_SAMPLING_INTERVAL_MS));
    }
}

/**
 * @brief Starts the Blood Pressure Trend (BPT) estimation process.
 *
 * This function sets the date and time for the BPT estimation, then sets the
 * operation mode to BPT and starts the estimation process.
 */
static void hw_bpt_start_est(void)
{
    struct sensor_value load_cal;
    load_cal.val1 = 0x00000000;
    sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664_ATTR_LOAD_CALIB, &load_cal);

    LOG_INF("Starting BPT Estimation");
    struct sensor_value data_time_val;
    data_time_val.val1 = DEFAULT_FUTURE_DATE; // Date // TODO: Update to local time
    data_time_val.val2 = DEFAULT_FUTURE_TIME; // Time
    sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664_ATTR_DATE_TIME, &data_time_val);
    k_sleep(K_MSEC(100));

    struct sensor_value mode_set;
    mode_set.val1 = MAX32664D_OP_MODE_BPT;
    sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664_ATTR_OP_MODE, &mode_set);

    k_sleep(K_MSEC(1000));
}

static int hpi_bpt_load_cal_data(uint8_t *bpt_cal_vector)
{
    fs_load_file_to_buffer("/sys/bpt_cal_vector", bpt_cal_vector, CALIBVECTOR_SIZE);
    max32664d_set_bpt_cal_vector(max32664d_dev, bpt_cal_vector);
}

static int hpi_bpt_save_cal_data(uint8_t *bpt_cal_vector)
{
    max32664d_get_bpt_cal_vector(max32664d_dev, bpt_cal_vector);
    fs_save_buffer_to_file("/sys/bpt_cal_vector", bpt_cal_vector, CALIBVECTOR_SIZE);
}

void hpi_bpt_stop(void)
{
    struct sensor_value mode_val;
    mode_val.val1 = MAX32664_ATTR_STOP_EST;
    sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664_ATTR_STOP_EST, &mode_val);
}

void hpi_bpt_get_calib(void)
{
    struct sensor_value mode_val;
    mode_val.val1 = MAX32664D_OP_MODE_BPT_CAL_GET_VECTOR;
    sensor_attr_set(max32664d_dev, SENSOR_CHAN_ALL, MAX32664_ATTR_OP_MODE, &mode_val);
}

void hpi_bpt_pause_thread(void)
{
    k_thread_suspend(ppg_finger_sampling_thread_id);
}

void hpi_bpt_abort(void)
{
    hpi_bpt_pause_thread();
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

    if (k_sem_take(&sem_bpt_check_sensor, K_NO_WAIT) == 0)
    {
        smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_CHECK_SENSOR]);
    }

    if (k_sem_take(&sem_bpt_est_start, K_NO_WAIT) == 0)
    {
        k_msleep(1000);

        smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_BPT_EST]);
    }
}

static void st_ppg_fing_idle_exit(void *o)
{
    LOG_DBG("PPG Finger SM Idle Exit");
    // k_sem_give(&sem_ppg_fi_show_loading);
}

static void st_ppg_fing_bpt_est_entry(void *o)
{
    LOG_DBG("PPG Finger SM BPT Estimation Entry");
}

static void st_ppg_fing_bpt_est_run(void *o)
{
    if (k_sem_take(&sem_bpt_est_abort, K_NO_WAIT) == 0)
    {
        hpi_bpt_stop();
        smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_IDLE]);
    }

    // LOG_DBG("PPG Finger SM BPT Estimation Running");

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

static void st_ppg_fing_bpt_cal_entry(void *o)
{
    LOG_DBG("PPG Finger SM BPT Calibration Entry");

    // hpi_bpt_get_calib();
}

static void st_ppg_fing_bpt_cal_run(void *o)
{
    LOG_DBG("PPG Finger SM BPT Calibration Running");

    struct hpi_ppg_fi_data_t ppg_fi_sensor_sample;

    /*if (k_msgq_get(&q_ppg_fi_sample, &ppg_fi_sensor_sample, K_NO_WAIT) == 0)
    {
        if (ppg_fi_sensor_sample.bpt_status == 1)
        {
            smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FING_STATE_BPT_CAL_DONE]);
        }
        else if (ppg_fi_sensor_sample.bpt_status == 2)
        {
            smf_set_state(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FING_STATE_BPT_CAL_FAIL]);
        }
    }*/
}

static void st_ppg_fing_bpt_cal_done_entry(void *o)
{
    LOG_DBG("PPG Finger SM BPT Calibration Done Entry");
}

static void st_ppg_fing_bpt_cal_done_run(void *o)
{
    // LOG_DBG("PPG Finger SM B
}

static void st_ppg_fing_bpt_cal_fail_entry(void *o)
{
    LOG_DBG("PPG Finger SM BPT Calibration Fail Entry");
}

static void st_ppg_fing_bpt_cal_fail_run(void *o)
{
    LOG_DBG("PPG Finger SM BPT Calibration Fail Running");
}

static void st_ppg_fi_check_sensor_entry(void *o)
{
    LOG_DBG("PPG Finger SM Check Sensor Entry");
}

static void st_ppg_fi_check_sensor_run(void *o)
{
    // LOG_DBG("PPG Finger SM Check Sensor Running");

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
        // hw_bpt_start_est();
        //  k_thread_resume(ppg_finger_sampling_thread_id);
        //  }
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
    [PPG_FI_STATE_IDLE] = SMF_CREATE_STATE(st_ppg_fing_idle_entry, st_ppg_fing_idle_run, st_ppg_fing_idle_exit, NULL, NULL),
    [PPG_FI_STATE_CHECK_SENSOR] = SMF_CREATE_STATE(st_ppg_fi_check_sensor_entry, st_ppg_fi_check_sensor_run, NULL, NULL, NULL),
    [PPG_FI_STATE_SENSOR_FAIL] = SMF_CREATE_STATE(st_ppg_fi_sensor_fail_entry, st_ppg_fi_sensor_fail_run, NULL, NULL, NULL),

    [PPG_FI_STATE_BPT_EST] = SMF_CREATE_STATE(st_ppg_fing_bpt_est_entry, st_ppg_fing_bpt_est_run, NULL, NULL, NULL),
    [PPG_FI_STATE_BPT_EST_DONE] = SMF_CREATE_STATE(st_ppg_fing_bpt_est_done_entry, st_ppg_fing_bpt_est_done_run, NULL, NULL, NULL),
    [PPG_FI_STATE_BPT_EST_FAIL] = SMF_CREATE_STATE(st_ppg_fing_bpt_est_fail_entry, st_ppg_fing_bpt_est_fail_run, NULL, NULL, NULL),

    [PPG_FI_STATE_BPT_CAL] = SMF_CREATE_STATE(st_ppg_fing_bpt_cal_entry, st_ppg_fing_bpt_cal_run, NULL, NULL, NULL),
    [PPG_FI_STATE_BPT_CAL_DONE] = SMF_CREATE_STATE(st_ppg_fing_bpt_cal_done_entry, st_ppg_fing_bpt_cal_done_run, NULL, NULL, NULL),
    [PPG_FI_STATE_BPT_CAL_FAIL] = SMF_CREATE_STATE(st_ppg_fing_bpt_cal_fail_entry, st_ppg_fing_bpt_cal_fail_run, NULL, NULL, NULL),
};

static void smf_ppg_finger_thread(void)
{
    int32_t ret;

    k_sem_take(&sem_ppg_finger_sm_start, K_FOREVER);

    ppg_finger_sampling_thread_id = k_thread_create(&ppg_finger_sampling_thread, ppg_finger_sampling_thread_stack,
                                                    SMF_PPG_FINGER_THREAD_STACKSIZE,
                                                    ppg_finger_sampling_thread_runner, NULL, NULL, NULL,
                                                    SMF_PPG_FINGER_THREAD_PRIORITY, 0, K_NO_WAIT);

    k_thread_suspend(ppg_finger_sampling_thread_id);

    smf_set_initial(SMF_CTX(&sf_obj), &ppg_fi_states[PPG_FI_STATE_IDLE]);

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

#define PPG_FINGER_SAMPLING_THREAD_STACKSIZE 4096
#define PPG_FINGER_SAMPLING_THREAD_PRIORITY 7

K_THREAD_DEFINE(ppg_finger_smf_thread, SMF_PPG_FINGER_THREAD_STACKSIZE, smf_ppg_finger_thread, NULL, NULL, NULL, SMF_PPG_FINGER_THREAD_PRIORITY, 0, 500);
// K_THREAD_DEFINE(ppg_finger_sampling_trigger_thread_id, PPG_FINGER_SAMPLING_THREAD_STACKSIZE, ppg_finger_sampling_thread_runner, NULL, NULL, NULL, PPG_FINGER_SAMPLING_THREAD_PRIORITY, 0, 500);
