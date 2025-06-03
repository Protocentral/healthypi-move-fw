#include <zephyr/kernel.h>
#include <zephyr/smf.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/drivers/sensor.h>

#include <time.h>
#include <zephyr/posix/time.h>
#include <zephyr/sys/timeutil.h>

#include "max30001.h"
#include "hpi_common_types.h"
#include "hw_module.h"
#include "ui/move_ui.h"
#include "hpi_sys.h"

LOG_MODULE_REGISTER(smf_ecg_bioz, LOG_LEVEL_DBG);

SENSOR_DT_READ_IODEV(max30001_iodev, DT_ALIAS(max30001), SENSOR_CHAN_VOLTAGE);

K_MSGQ_DEFINE(q_ecg_bioz_sample, sizeof(struct hpi_ecg_bioz_sensor_data_t), 64, 1);

K_SEM_DEFINE(sem_ecg_start, 0, 1);
K_SEM_DEFINE(sem_ecg_lon, 0, 1);
K_SEM_DEFINE(sem_ecg_loff, 0, 1);
K_SEM_DEFINE(sem_ecg_cancel, 0, 1);

K_SEM_DEFINE(sem_ecg_lead_on, 0, 1);
K_SEM_DEFINE(sem_ecg_lead_off, 0, 1);

K_SEM_DEFINE(sem_ecg_lead_on_local, 0, 1);
K_SEM_DEFINE(sem_ecg_lead_off_local, 0, 1);

ZBUS_CHAN_DECLARE(ecg_stat_chan);
ZBUS_CHAN_DECLARE(ecg_lead_on_off_chan);

#define ECG_SAMPLING_INTERVAL_MS 125
#define ECG_RECORD_DURATION_S 30

static int ecg_last_timer_val = 0;
static int ecg_countdown_val = 0;

static const struct smf_state ecg_bioz_states[];
struct s_ecg_bioz_object
{
    struct smf_ctx ctx;
} s_ecg_bioz_obj;

enum ecg_bioz_state
{
    HPI_ECG_BIOZ_STATE_IDLE,
    HPI_ECG_BIOZ_STATE_STREAM,
    HPI_ECG_BIOZ_STATE_LEADOFF,
    HPI_ECG_BIOZ_STATE_COMPLETE,
};

RTIO_DEFINE(max30001_read_rtio_poll_ctx, 1, 1);

static bool ecg_active = false;
static bool m_ecg_lead_on_off = true;

static uint16_t m_ecg_hr = 0;

static int hw_max30001_ecg_enable(void);
static int hw_max30001_ecg_disable(void);

// EXTERNS
extern const struct device *const max30001_dev;
extern struct k_sem sem_ecg_bioz_smf_start;
extern struct k_sem sem_ecg_bioz_sm_start;
extern struct k_sem sem_ecg_complete;
extern struct k_sem sem_ecg_complete_reset;

static void work_ecg_lon_handler(struct k_work *work)
{
    LOG_DBG("ECG LON Work");
    hw_max30001_ecg_disable();
    hw_max30001_ecg_enable();
}
K_WORK_DEFINE(work_ecg_lon, work_ecg_lon_handler);

static void work_ecg_loff_handler(struct k_work *work)
{
    LOG_DBG("ECG LOFF Work");

}
K_WORK_DEFINE(work_ecg_loff, work_ecg_loff_handler);

static void sensor_ecg_bioz_process_decode(uint8_t *buf, uint32_t buf_len)
{
    const struct max30001_encoded_data *edata = (const struct max30001_encoded_data *)buf;
    struct hpi_ecg_bioz_sensor_data_t ecg_bioz_sensor_sample;

    uint8_t ecg_num_samples = edata->num_samples_ecg;
    uint8_t bioz_samples = edata->num_samples_bioz;

    // printk("ECG NS: %d ", ecg_samples);
    // printk("BioZ NS: %d ", bioz_samples);

    if ((ecg_num_samples < 32 && ecg_num_samples > 0) || (bioz_samples < 32 && bioz_samples > 0))
    {
        ecg_bioz_sensor_sample.ecg_num_samples = edata->num_samples_ecg;
        ecg_bioz_sensor_sample.bioz_num_samples = edata->num_samples_bioz;

        for (int i = 0; i < edata->num_samples_ecg; i++)
        {
            ecg_bioz_sensor_sample.ecg_samples[i] = edata->ecg_samples[i];
        }

        for (int i = 0; i < edata->num_samples_bioz; i++)
        {
            ecg_bioz_sensor_sample.bioz_sample[i] = edata->bioz_samples[i];
        }

        ecg_bioz_sensor_sample.hr = edata->hr;
        ecg_bioz_sensor_sample.rtor = edata->rri;

        m_ecg_hr = edata->hr;
        // ecg_bioz_sensor_sample.rrint = edata->rri;

        //LOG_DBG("RRI: %d", edata->rri);

        ecg_bioz_sensor_sample.ecg_lead_off = edata->ecg_lead_off;

        if (edata->ecg_lead_off == 1 && m_ecg_lead_on_off == false)
        {
            m_ecg_lead_on_off = true;
            LOG_DBG("ECG LOFF");
            k_work_submit(&work_ecg_loff);
 
            // k_sem_give(&sem_ecg_lead_off);
            //  smf_set_state(SMF_CTX(&s_ecg_bioz_obj), &ecg_bioz_states[HPI_ECG_BIOZ_STATE_LEADOFF]);
        }
        else if (edata->ecg_lead_off == 0 && m_ecg_lead_on_off == true)
        {
            m_ecg_lead_on_off = false;
            LOG_DBG("ECG LON");
            k_work_submit(&work_ecg_lon);
            // k_sem_give(&sem_ecg_lead_on);
            // k_sem_give(&sem_ecg_lead_on_local);
            //  smf_set_state(SMF_CTX(&s_ecg_bioz_obj), &ecg_bioz_states[HPI_ECG_BIOZ_STATE_STREAM]);
        }

        if (ecg_active)
        {
            k_msgq_put(&q_ecg_bioz_sample, &ecg_bioz_sensor_sample, K_MSEC(1));
        }
    }
}

static void work_ecg_sample_handler(struct k_work *work)
{
    uint8_t ecg_bioz_buf[512];
    int ret;
    ret = sensor_read(&max30001_iodev, &max30001_read_rtio_poll_ctx, ecg_bioz_buf, sizeof(ecg_bioz_buf));
    if (ret < 0)
    {
        LOG_ERR("Error reading sensor data");
        return;
    }
    sensor_ecg_bioz_process_decode(ecg_bioz_buf, sizeof(ecg_bioz_buf));
}

K_WORK_DEFINE(work_ecg_sample, work_ecg_sample_handler);

static void ecg_bioz_sampling_handler(struct k_timer *dummy)
{
    k_work_submit(&work_ecg_sample);
}

K_TIMER_DEFINE(tmr_ecg_bioz_sampling, ecg_bioz_sampling_handler, NULL);

static int hw_max30001_bioz_enable(void)
{
    struct sensor_value bioz_mode_set;
    bioz_mode_set.val1 = 1;
    return sensor_attr_set(max30001_dev, SENSOR_CHAN_ALL, MAX30001_ATTR_BIOZ_ENABLED, &bioz_mode_set);
}

static int hw_max30001_ecg_enable(void)
{
    struct sensor_value ecg_mode_set;
    ecg_mode_set.val1 = 1;
    sensor_attr_set(max30001_dev, SENSOR_CHAN_ALL, MAX30001_ATTR_ECG_ENABLED, &ecg_mode_set);
    ecg_active = true;
    return 0;
}

static int hw_max30001_bioz_disable(void)
{
    struct sensor_value bioz_mode_set;
    bioz_mode_set.val1 = 0;
    return sensor_attr_set(max30001_dev, SENSOR_CHAN_ALL, MAX30001_ATTR_BIOZ_ENABLED, &bioz_mode_set);
}

static int hw_max30001_ecg_disable(void)
{
    struct sensor_value ecg_mode_set;
    ecg_mode_set.val1 = 0;
    return sensor_attr_set(max30001_dev, SENSOR_CHAN_ALL, MAX30001_ATTR_ECG_ENABLED, &ecg_mode_set);
}

static void st_ecg_bioz_idle_entry(void *o)
{
    LOG_DBG("ECG/BioZ SM Idle Entry");

    hw_max30001_ecg_disable();
    k_timer_stop(&tmr_ecg_bioz_sampling);
    hw_max30001_bioz_disable();

   
}

static void st_ecg_bioz_idle_run(void *o)
{
    // LOG_DBG("ECG/BioZ SM Idle Run");
    if (k_sem_take(&sem_ecg_start, K_NO_WAIT) == 0)
    {
        smf_set_state(SMF_CTX(&s_ecg_bioz_obj), &ecg_bioz_states[HPI_ECG_BIOZ_STATE_STREAM]);
    }
}

static void st_ecg_bioz_stream_entry(void *o)
{
    LOG_DBG("ECG/BioZ SM Stream Entry");

    hw_max30001_ecg_enable();
    k_timer_start(&tmr_ecg_bioz_sampling, K_MSEC(ECG_SAMPLING_INTERVAL_MS), K_MSEC(ECG_SAMPLING_INTERVAL_MS));
    hpi_data_set_ecg_record_active(true);
    // hpi_max30001_lon_detect_enable();

    ecg_countdown_val = ECG_RECORD_DURATION_S;
}

static void st_ecg_bioz_stream_run(void *o)
{
    // LOG_DBG("ECG/BioZ SM Stream Run");
    // Stream for ECG duration (30s)
    if (hpi_data_is_ecg_record_active() == true)
    {
        if ((k_uptime_get_32() - ecg_last_timer_val) >= 1000)
        {
            ecg_countdown_val--;
            LOG_DBG("ECG timer: %d", ecg_countdown_val);
            ecg_last_timer_val = k_uptime_get_32();

            struct hpi_ecg_status_t ecg_stat = {
                .ts_complete = 0,
                .status = HPI_ECG_STATUS_STREAMING,
                .hr = m_ecg_hr,
                .progress_timer = ecg_countdown_val};
            zbus_chan_pub(&ecg_stat_chan, &ecg_stat, K_NO_WAIT);

            if (ecg_countdown_val <= 0)
            {
                smf_set_state(SMF_CTX(&s_ecg_bioz_obj), &ecg_bioz_states[HPI_ECG_BIOZ_STATE_COMPLETE]);
            }
        }
    }

    if (k_sem_take(&sem_ecg_cancel, K_NO_WAIT) == 0)
    {
        LOG_DBG("ECG cancelled");
        smf_set_state(SMF_CTX(&s_ecg_bioz_obj), &ecg_bioz_states[HPI_ECG_BIOZ_STATE_IDLE]);
    }
}

static void st_ecg_bioz_stream_exit(void *o)
{
    LOG_DBG("ECG/BioZ SM Stream Exit");
    
    hpi_data_set_ecg_record_active(false);

    ecg_last_timer_val = 0;
    ecg_countdown_val = 0;
}

static void st_ecg_bioz_complete_entry(void *o)
{
    LOG_DBG("ECG/BioZ SM Complete Entry");
    k_timer_stop(&tmr_ecg_bioz_sampling);
    hw_max30001_ecg_disable();

    k_sem_give(&sem_ecg_complete);
}

static void st_ecg_bioz_complete_run(void *o)
{
    smf_set_state(SMF_CTX(&s_ecg_bioz_obj), &ecg_bioz_states[HPI_ECG_BIOZ_STATE_IDLE]);
}

static void st_ecg_bioz_complete_exit(void *o)
{
    LOG_DBG("ECG/BioZ SM Complete Exit");
}

static void st_ecg_bioz_leadoff_entry(void *o)
{
    LOG_DBG("ECG/BioZ SM Leadoff Entry");
    // hw_max30001_ecg_disable();
    // hw_max30001_bioz_disable();
    // k_timer_stop(&tmr_ecg_bioz_sampling);
    // k_timer_stop()
}

static void st_ecg_bioz_leadoff_run(void *o)
{

    // LOG_DBG("ECG/BioZ SM Leadoff Run");
    if (k_sem_take(&sem_ecg_lead_on_local, K_NO_WAIT) == 0)
    {
        smf_set_state(SMF_CTX(&s_ecg_bioz_obj), &ecg_bioz_states[HPI_ECG_BIOZ_STATE_STREAM]);
    }
}

static const struct smf_state ecg_bioz_states[] = {
    [HPI_ECG_BIOZ_STATE_IDLE] = SMF_CREATE_STATE(st_ecg_bioz_idle_entry, st_ecg_bioz_idle_run, NULL, NULL, NULL),
    [HPI_ECG_BIOZ_STATE_STREAM] = SMF_CREATE_STATE(st_ecg_bioz_stream_entry, st_ecg_bioz_stream_run, st_ecg_bioz_stream_exit, NULL, NULL),
    [HPI_ECG_BIOZ_STATE_LEADOFF] = SMF_CREATE_STATE(st_ecg_bioz_leadoff_entry, st_ecg_bioz_leadoff_run, NULL, NULL, NULL),
    [HPI_ECG_BIOZ_STATE_COMPLETE] = SMF_CREATE_STATE(st_ecg_bioz_complete_entry, st_ecg_bioz_complete_run, st_ecg_bioz_complete_exit, NULL, NULL),
};

void smf_ecg_bioz_thread(void)
{
    int ret;

    // Wait for HW module to init ECG/BioZ
    k_sem_take(&sem_ecg_bioz_sm_start, K_FOREVER);

    LOG_INF("ECG/BioZ SMF Thread Started");

    smf_set_initial(SMF_CTX(&s_ecg_bioz_obj), &ecg_bioz_states[HPI_ECG_BIOZ_STATE_IDLE]);

    // k_timer_start(&tmr_ecg_bioz_sampling, K_MSEC(ECG_SAMPLING_INTERVAL_MS), K_MSEC(ECG_SAMPLING_INTERVAL_MS));

    for (;;)
    {
        ret = smf_run_state(SMF_CTX(&s_ecg_bioz_obj));
        if (ret != 0)
        {
            LOG_ERR("SMF Run error: %d", ret);
            break;
        }
        k_msleep(100);
    }
}

K_THREAD_DEFINE(smf_ecg_bioz_thread_id, 1024, smf_ecg_bioz_thread, NULL, NULL, NULL, 10, 0, 0);
