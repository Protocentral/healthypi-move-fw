#include <zephyr/kernel.h>
#include <zephyr/smf.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(smf_ppg_finger, LOG_LEVEL_INF);

#include "hw_module.h"
#include "max32664d.h"
#include "hpi_common_types.h"

#define PPG_FINGER_SAMPLING_INTERVAL_MS 40

#define DEFAULT_DATE 240428 // YYMMDD 28th April 2024
#define DEFAULT_TIME 121212 // HHMMSS 12:12:12

#define DEFAULT_FUTURE_DATE 240429 // YYMMDD 29th April 2024
#define DEFAULT_FUTURE_TIME 121213 // HHMMSS 12:12:13

SENSOR_DT_READ_IODEV(max32664d_iodev, DT_ALIAS(max32664d), {SENSOR_CHAN_VOLTAGE});

K_SEM_DEFINE(sem_ppg_finger_thread_start, 0, 1);
K_MSGQ_DEFINE(q_ppg_fi_sample, sizeof(struct hpi_ppg_fi_data_t), 64, 1);

RTIO_DEFINE(max32664d_read_rtio_poll_ctx, 8, 8);

enum ppg_samp_state
{
    PPG_FING_STATE_IDLE,
    PPG_SAMP_STATE_ACTIVE,
    PPG_SAMP_STATE_PROBING,
    PPG_SAMP_STATE_OFF_SKIN,
};

struct s_object
{
    struct smf_ctx ctx;
} sf_obj;

extern const struct device *const max32664d_dev;
extern struct k_sem sem_ppg_finger_sm_start;

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
    //printk("FNS: %d ", edata->num_samples);
    if(_n_samples>16)
    {
        _n_samples=16;
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

void ppg_finger_sampling_thread(void)
{
    int ret;
    uint8_t fing_data_buf[768];

    k_sem_take(&sem_ppg_finger_thread_start, K_FOREVER);

    // k_sem_give(&sem_ppg_finger_sample_trigger);

    LOG_INF("PPG Finger Sampling starting");
    for (;;)
    {
        ret= sensor_read(&max32664d_iodev, &max32664d_read_rtio_poll_ctx, fing_data_buf, sizeof(fing_data_buf));
        if (ret < 0)
        {
            LOG_ERR("Error reading sensor data");
            continue;
        }
        sensor_ppg_finger_decode(fing_data_buf, sizeof(fing_data_buf));        
        // k_sem_take(&sem_ppg_finger_sample_trigger, K_FOREVER);

        //sensor_read_async_mempool(&max32664d_iodev, &max32664d_read_rtio_ctx, NULL);
        //sensor_processing_with_callback(&max32664d_read_rtio_ctx, sensor_ppg_finger_processing_callback);

        k_sleep(K_MSEC(PPG_FINGER_SAMPLING_INTERVAL_MS));
    }
}

/**
 * @brief Starts the Blood Pressure Trend (BPT) estimation process.
 *
 * This function sets the date and time for the BPT estimation, then sets the 
 * operation mode to BPT and starts the estimation process.
 */
void hw_bpt_start_est(void)
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
    // ppg_data_start();
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

static void st_ppg_fing_idle_entry(void *o)
{
    LOG_DBG("PPG Finger SM Idle Entry");

    hw_bpt_start_est();
}

static void st_ppg_fing_idle_run(void *o)
{
    LOG_DBG("PPG Finger SM Idle Running");
}

static void st_ppg_fing_idle_exit(void *o)
{
    LOG_DBG("PPG Finger SM Idle Exit");
}

static void st_ppg_fing_active_entry(void *o)
{
    LOG_DBG("PPG Finger SM Active Entry");
}

static void st_ppg_fing_active_run(void *o)
{
    LOG_DBG("PPG Finger SM Active Running");
}

static void st_ppg_fing_active_exit(void *o)
{
    LOG_DBG("PPG Finger SM Active Exit");
}

static void st_ppg_fing_probing_entry(void *o)
{
    LOG_DBG("PPG Finger SM Probing Entry");
}

static void st_ppg_fing_probing_run(void *o)
{
    LOG_DBG("PPG Finger SM Probing Running");
}

static void st_ppg_fing_probing_exit(void *o)
{
    LOG_DBG("PPG Finger SM Probing Exit");
}

static const struct smf_state ppg_fing_states[] = {
    [PPG_FING_STATE_IDLE] = SMF_CREATE_STATE(st_ppg_fing_idle_entry, st_ppg_fing_idle_run, st_ppg_fing_idle_exit, NULL, NULL),
    [PPG_SAMP_STATE_ACTIVE] = SMF_CREATE_STATE(st_ppg_fing_active_entry, st_ppg_fing_active_run, st_ppg_fing_active_exit, NULL, NULL),
    [PPG_SAMP_STATE_PROBING] = SMF_CREATE_STATE(st_ppg_fing_probing_entry, st_ppg_fing_probing_run, st_ppg_fing_probing_exit, NULL, NULL),
};

static void smf_ppg_finger_thread(void)
{
    int32_t ret;

    k_sem_take(&sem_ppg_finger_sm_start, K_FOREVER);

    /*if (hw_is_max32664d_present() == false)
    {
            LOG_ERR("MAX32664D device not present. Not starting PPG Finger SMF");
            return;
    }*/

    smf_set_initial(SMF_CTX(&sf_obj), &ppg_fing_states[PPG_FING_STATE_IDLE]);

    k_sem_give(&sem_ppg_finger_thread_start);

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

#define SMF_PPG_FINGER_THREAD_STACKSIZE 4096
#define SMF_PPG_FINGER_THREAD_PRIORITY 7

#define PPG_FINGER_SAMPLING_THREAD_STACKSIZE 4096
#define PPG_FINGER_SAMPLING_THREAD_PRIORITY 7


K_THREAD_DEFINE(ppg_finger_thread_id, SMF_PPG_FINGER_THREAD_STACKSIZE, smf_ppg_finger_thread, NULL, NULL, NULL, SMF_PPG_FINGER_THREAD_PRIORITY, 0, 500);
K_THREAD_DEFINE(ppg_finger_sampling_trigger_thread_id, PPG_FINGER_SAMPLING_THREAD_STACKSIZE, ppg_finger_sampling_thread, NULL, NULL, NULL, PPG_FINGER_SAMPLING_THREAD_PRIORITY, 0, 500);
