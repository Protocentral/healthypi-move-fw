#include <zephyr/kernel.h>
#include <zephyr/smf.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/drivers/sensor.h>

#include "hpi_common_types.h"
#include "hw_module.h"
#include "ui/move_ui.h"

#include "max30001.h"

LOG_MODULE_REGISTER(smf_ecg_bioz, LOG_LEVEL_INF);

SENSOR_DT_READ_IODEV(max30001_iodev, DT_ALIAS(max30001), SENSOR_CHAN_VOLTAGE);
K_MSGQ_DEFINE(q_ecg_bioz_sample, sizeof(struct hpi_ecg_bioz_sensor_data_t), 64, 1);
K_SEM_DEFINE(sem_ecg_bioz_thread_start, 0, 1);

#define ECG_SAMPLING_INTERVAL_MS 65

/*
RTIO_DEFINE_WITH_MEMPOOL(max30001_read_rtio_ctx,
                         32,
                         32,
                         128,
                         64,
                         4
);
*/

RTIO_DEFINE(max30001_read_rtio_poll_ctx, 1, 1);

// Externs
extern struct k_sem sem_ecg_bioz_sm_start;

/*
static void sensor_ecg_bioz_process_cb(int result, uint8_t *buf,
                                       uint32_t buf_len, void *userdata)
{
        const struct max30001_encoded_data *edata = (const struct max30001_encoded_data *)buf;
        struct hpi_ecg_bioz_sensor_data_t ecg_bioz_sensor_sample;

        // printk("ECG NS: %d ", edata->num_samples_ecg);
        // printk("BioZ NS: %d ", edata->num_samples_bioz);

        if ((edata->num_samples_ecg > 0) || (edata->num_samples_bioz > 0))
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

                k_msgq_put(&q_ecg_bioz_sample, &ecg_bioz_sensor_sample, K_MSEC(1));
        }
}*/

static void sensor_ecg_bioz_process_decode(uint8_t *buf, uint32_t buf_len)
{
    const struct max30001_encoded_data *edata = (const struct max30001_encoded_data *)buf;
    struct hpi_ecg_bioz_sensor_data_t ecg_bioz_sensor_sample;

    // printk("ECG NS: %d ", edata->num_samples_ecg);
    // printk("BioZ NS: %d ", edata->num_samples_bioz);

    if ((edata->num_samples_ecg > 0) || (edata->num_samples_bioz > 0))
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

        k_msgq_put(&q_ecg_bioz_sample, &ecg_bioz_sensor_sample, K_MSEC(1));
        
    }
}

void ecg_bioz_sampling_trigger_thread(void)
{
    // Wait until sem is received
    k_sem_take(&sem_ecg_bioz_thread_start, K_FOREVER);

    uint8_t ecg_bioz_buf[512];
    int ret;

    LOG_INF("ECG/ BioZ Sampling starting");
    for (;;)
    {
        ret = sensor_read(&max30001_iodev, &max30001_read_rtio_poll_ctx, ecg_bioz_buf, sizeof(ecg_bioz_buf));
        if(ret < 0)
        {
            LOG_ERR("Error reading sensor data");
            continue;
        }
        sensor_ecg_bioz_process_decode(ecg_bioz_buf, sizeof(ecg_bioz_buf));
        

        //sensor_read_async_mempool(&max30001_iodev, &max30001_read_rtio_poll_ctx, NULL);
        //sensor_processing_with_callback(&max30001_read_rtio_poll_ctx, sensor_ecg_bioz_process_cb);

        k_sleep(K_MSEC(ECG_SAMPLING_INTERVAL_MS));
    }
}

static const struct smf_state ecg_bioz_states[];

struct s_ecg_bioz_object
{
    struct smf_ctx ctx;
} s_ecg_bioz_obj;

enum ecg_bioz_state
{
    HPI_ECG_BIOZ_STATE_INIT,
    HPI_ECG_BIOZ_STATE_IDLE,
    HPI_ECG_BIOZ_STATE_STREAM,
};

// EXTERNS
extern const struct device *const max30001_dev;
extern struct k_sem sem_ecg_bioz_smf_start;

static void st_ecg_bioz_init_entry(void *o)
{
    LOG_DBG("ECG/BioZ SM Init Entry");
    // Disable ECG and BIOZ by default
    hw_max30001_ecg_enable(false);
    hw_max30001_bioz_enable(false);
}

static void st_ecg_bioz_idle_entry(void *o)
{
    LOG_DBG("ECG/BioZ SM Idle Entry");
}

static void st_ecg_bioz_idle_run(void *o)
{
    // LOG_DBG("ECG/BioZ SM Idle Run");
}

static void st_ecg_bioz_idle_exit(void *o)
{
    LOG_DBG("ECG/BioZ SM Idle Exit");
}

static void st_ecg_bioz_stream_entry(void *o)
{
    LOG_DBG("ECG/BioZ SM Stream Entry");
}

static void st_ecg_bioz_stream_run(void *o)
{
    // LOG_DBG("ECG/BioZ SM Stream Run");
}

static void st_ecg_bioz_stream_exit(void *o)
{
    LOG_DBG("ECG/BioZ SM Stream Exit");
}

static const struct smf_state ecg_bioz_states[] = {
    [HPI_ECG_BIOZ_STATE_INIT] = SMF_CREATE_STATE(st_ecg_bioz_init_entry, NULL, NULL, NULL, NULL),
    [HPI_ECG_BIOZ_STATE_IDLE] = SMF_CREATE_STATE(st_ecg_bioz_idle_entry, st_ecg_bioz_idle_run, st_ecg_bioz_idle_exit, NULL, NULL),
    [HPI_ECG_BIOZ_STATE_STREAM] = SMF_CREATE_STATE(st_ecg_bioz_stream_entry, st_ecg_bioz_stream_run, st_ecg_bioz_stream_exit, NULL, NULL),
};

void smf_ecg_bioz_thread(void)
{
    int ret;
    
    // Wait for HW module to init ECG/BioZ
    k_sem_take(&sem_ecg_bioz_sm_start, K_FOREVER);

    LOG_INF("ECG/BioZ SMF Thread Started");

    smf_set_initial(SMF_CTX(&s_ecg_bioz_obj), &ecg_bioz_states[HPI_ECG_BIOZ_STATE_INIT]);

    k_sem_give(&sem_ecg_bioz_thread_start);
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

#define ECG_BIOZ_SAMPLING_THREAD_STACKSIZE 4096
#define ECG_BIOZ_SAMPLING_THREAD_PRIORITY 7

K_THREAD_DEFINE(smf_ecg_bioz_thread_id, 1024, smf_ecg_bioz_thread, NULL, NULL, NULL, 10, 0, 0);
K_THREAD_DEFINE(ecg_bioz_sampling_trigger_thread_id, ECG_BIOZ_SAMPLING_THREAD_STACKSIZE, ecg_bioz_sampling_trigger_thread, NULL, NULL, NULL, ECG_BIOZ_SAMPLING_THREAD_PRIORITY, 0, 700);
