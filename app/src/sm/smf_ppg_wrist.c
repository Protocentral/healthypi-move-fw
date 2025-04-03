#include <zephyr/kernel.h>
#include <zephyr/smf.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(smf_ppg_wrist, LOG_LEVEL_DBG);

#include "hw_module.h"
#include "max32664c.h"
#include "hpi_common_types.h"

#define PPG_WRIST_SAMPLING_INTERVAL_MS 40

#define HPI_OFFSKIN_THRESHOLD_S 5
#define HPI_PROBE_DURATION_S 15

static const struct smf_state ppg_samp_states[];

K_SEM_DEFINE(sem_ppg_wrist_thread_start, 0, 1);
K_MSGQ_DEFINE(q_ppg_wrist_sample, sizeof(struct hpi_ppg_wr_data_t), 64, 1);

K_SEM_DEFINE(sem_ppg_wrist_on_skin, 0, 1);
K_SEM_DEFINE(sem_ppg_wrist_off_skin, 0, 1);
K_SEM_DEFINE(sem_ppg_wrist_motion_detected, 0, 1);

K_SEM_DEFINE(sem_start_one_shot_spo2, 0, 1);

RTIO_DEFINE(max32664c_read_rtio_poll_ctx, 1, 1);

SENSOR_DT_READ_IODEV(max32664c_iodev, DT_ALIAS(max32664c), SENSOR_CHAN_VOLTAGE);

extern struct k_sem sem_ppg_wrist_sm_start;

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

static int m_curr_state;

static enum max32664c_scd_states m_curr_scd_state;

void work_off_skin_handler(struct k_work *work)
{
    if (m_curr_scd_state == MAX32664C_SCD_STATE_OFF_SKIN)
    {
        LOG_DBG("Still OFF SKIN");
        smf_set_state(SMF_CTX(&sm_ctx_ppg_wr), &ppg_samp_states[PPG_SAMP_STATE_OFF_SKIN]);
    }
}

K_WORK_DELAYABLE_DEFINE(work_off_skin, work_off_skin_handler);

void work_on_skin_handler(struct k_work *work)
{
    if (m_curr_scd_state == MAX32664C_SCD_STATE_ON_SKIN)
    {
        LOG_DBG("Still ON SKIN");
        // smf_set_state(SMF_CTX(&sm_ctx_ppg_wr), &ppg_samp_states[PPG_SAMP_STATE_ACTIVE]);
    }
}

K_WORK_DELAYABLE_DEFINE(work_on_skin, work_on_skin_handler);

static void sensor_ppg_wrist_decode(uint8_t *buf, uint32_t buf_len)
{
    const struct max32664c_encoded_data *edata = (const struct max32664c_encoded_data *)buf;
    struct hpi_ppg_wr_data_t ppg_sensor_sample;

    // static uint8_t prev_hr_val = 0;
    //  uint8_t hr_chan_value=0;
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
    else if (edata->chip_op_mode == MAX32664C_OP_MODE_WAKE_ON_MOTION)
    {
        LOG_DBG("WAKE ON MOTION | state: %d", m_curr_state);
        k_sem_give(&sem_ppg_wrist_motion_detected);
        return;
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

            //LOG_DBG("SPO2: %d | Spo2 Prog: %d | Low PI: %d | Motion: %d | State: %d", ppg_sensor_sample.spo2, ppg_sensor_sample.spo2_valid_percent_complete,
            //        ppg_sensor_sample.spo2_low_pi, ppg_sensor_sample.spo2_excessive_motion, ppg_sensor_sample.spo2_state);

            // LOG_DBG("HR Conf: %d", ppg_sensor_sample.hr_confidence);

            if ((ppg_sensor_sample.scd_state == MAX32664C_SCD_STATE_OFF_SKIN) && (m_curr_scd_state != MAX32664C_SCD_STATE_OFF_SKIN))
            {
                LOG_DBG("OFF SKIN");
                k_work_schedule(&work_off_skin, K_SECONDS(HPI_OFFSKIN_THRESHOLD_S));
            }

            if(ppg_sensor_sample.spo2_valid_percent_complete==100)
            {
                hw_max32664c_stop_algo();
                k_msleep(600);
            }

            m_curr_scd_state = edata->scd_state;

            // LOG_DBG("SCD: %d", ppg_sensor_sample.scd_state);
            if (ppg_sensor_sample.scd_state == MAX32664C_SCD_STATE_ON_SKIN)
            {
                k_msgq_put(&q_ppg_wrist_sample, &ppg_sensor_sample, K_MSEC(1));
            }
        }
    }
}

/*
void ppg_wrist_sampling_trigger_thread(void)
{
    k_sem_take(&sem_ppg_wrist_thread_start, K_FOREVER);

    int ret;
    uint8_t wrist_buf[512];

    LOG_INF("PPG Wrist Sampling starting");
    for (;;)
    {
        ret = sensor_read(&max32664c_iodev, &max32664c_read_rtio_poll_ctx, wrist_buf, sizeof(wrist_buf));
        if (ret < 0)
        {
            LOG_ERR("Error reading sensor data");
            continue;
        }
        sensor_ppg_wrist_decode(wrist_buf, sizeof(wrist_buf));

        // sensor_read_async_mempool(&maxm86146_iodev, &maxm86146_read_rtio_ctx, NULL);
        // sensor_processing_with_callback(&maxm86146_read_rtio_ctx, sensor_ppg_wrist_processing_callback);

        // sensor_read_async_mempool(&max32664c_iodev, &max32664c_read_rtio_ctx, NULL);
        // sensor_processing_with_callback(&max32664c_read_rtio_ctx, sensor_ppg_wrist_processing_callback);

        k_sleep(K_MSEC(PPG_WRIST_SAMPLING_INTERVAL_MS));
    }
}
*/

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

static void ppg_wr_start_oneshot_spo2(void)
{
    LOG_DBG("PPG Wrist starting one-shot SpO2");
    hw_max32664c_set_op_mode(MAX32664C_OP_MODE_STOP_ALGO, MAX32664C_ALGO_MODE_NONE);
    k_msleep(600);
    hw_max32664c_set_op_mode(MAX32664C_OP_MODE_ALGO_AEC, MAX32664C_ALGO_MODE_CONT_HR_SHOT_SPO2);
    k_msleep(600);
    k_timer_start(&tmr_ppg_wrist_sampling, K_MSEC(PPG_WRIST_SAMPLING_INTERVAL_MS), K_MSEC(PPG_WRIST_SAMPLING_INTERVAL_MS));
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

static void st_ppg_samp_off_skin_entry(void *o)
{
    LOG_DBG("PPG SM Off Skin Entry");
    m_curr_state = PPG_SAMP_STATE_OFF_SKIN;

    // hw_max32664c_set_op_mode(MAX32664C_OP_MODE_WAKE_ON_MOTION, MAX32664C_ALGO_MODE_NONE);
    hw_max32664c_set_op_mode(MAX32664C_OP_MODE_ALGO_AGC, MAX32664C_ALGO_MODE_CONT_HRM);
    k_msleep(1000);
}

static void st_ppg_samp_off_skin_run(void *o)
{
    LOG_DBG("PPG SM Off Skin Running");
    // smf_set_state(SMF_CTX(&sm_ctx_ppg_wr), &ppg_samp_states[PPG_SAMP_STATE_MOTION_DETECT]);
}

static void st_ppg_samp_motion_detect_entry(void *o)
{
    LOG_DBG("PPG SM Motion Detect Entry");
    m_curr_state = PPG_SAMP_STATE_MOTION_DETECT;
}

static void st_ppg_samp_motion_detect_run(void *o)
{
    LOG_DBG("PPG SM Motion Detect Running");

    if (k_sem_take(&sem_ppg_wrist_motion_detected, K_FOREVER) == 0)
    {
        hw_max32664c_set_op_mode(MAX32664C_OP_MODE_EXIT_WAKE_ON_MOTION, MAX32664C_ALGO_MODE_NONE);
        k_msleep(1000);
        LOG_DBG("Switching to Probing");
        smf_set_state(SMF_CTX(&sm_ctx_ppg_wr), &ppg_samp_states[PPG_SAMP_STATE_PROBING]);
    }
}

static const struct smf_state ppg_samp_states[] = {
    [PPG_SAMP_STATE_ACTIVE] = SMF_CREATE_STATE(st_ppg_samp_active_entry, st_ppg_samp_active_run, NULL, NULL, NULL),
    [PPG_SAMP_STATE_PROBING] = SMF_CREATE_STATE(st_ppg_samp_probing_entry, st_ppg_samp_probing_run, NULL, NULL, NULL),
    [PPG_SAMP_STATE_MOTION_DETECT] = SMF_CREATE_STATE(st_ppg_samp_motion_detect_entry, st_ppg_samp_motion_detect_run, NULL, NULL, NULL),
    [PPG_SAMP_STATE_OFF_SKIN] = SMF_CREATE_STATE(st_ppg_samp_off_skin_entry, st_ppg_samp_off_skin_run, NULL, NULL, NULL),
};

static void smf_ppg_wrist_thread(void)
{
    int32_t ret;

    bool is_smf_active = true;

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
        // if (is_smf_active == true)
        //{
        ret = smf_run_state(SMF_CTX(&sm_ctx_ppg_wr));
        //}
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
    for(;;)
    {
        if (k_sem_take(&sem_start_one_shot_spo2, K_NO_WAIT) == 0)
        {
            // smf_set_terminate(SMF_CTX(&sm_ctx_ppg_wr);
            LOG_DBG("Stopping PPG Sampling");
            k_timer_stop(&tmr_ppg_wrist_sampling);           
            LOG_DBG("Starting One Shot SpO2");
            ppg_wr_start_oneshot_spo2();
        }

        //LOG_DBG("PPG WR control Running");

        k_msleep(1000);
    }
}

#define PPG_CTRL_THREAD_STACKSIZE 1024
#define PPG_CTRL_THREAD_PRIORITY 7

#define SMF_PPG_THREAD_STACKSIZE 4096
#define SMF_PPG_THREAD_PRIORITY 7

K_THREAD_DEFINE(smf_ppg_thread_id, SMF_PPG_THREAD_STACKSIZE, smf_ppg_wrist_thread, NULL, NULL, NULL, SMF_PPG_THREAD_PRIORITY, 0, 1000);
K_THREAD_DEFINE(ppg_ctrl_thread_id, PPG_CTRL_THREAD_STACKSIZE, ppg_wrist_ctrl_thread, NULL, NULL, NULL, PPG_CTRL_THREAD_PRIORITY, 0, 0);