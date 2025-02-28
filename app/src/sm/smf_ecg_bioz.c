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

LOG_MODULE_REGISTER(smf_ecg_bioz, LOG_LEVEL_DBG);

SENSOR_DT_READ_IODEV(max30001_iodev, DT_ALIAS(max30001), SENSOR_CHAN_VOLTAGE);

K_MSGQ_DEFINE(q_ecg_bioz_sample, sizeof(struct hpi_ecg_bioz_sensor_data_t), 64, 1);

K_SEM_DEFINE(sem_ecg_start, 0, 1);
K_SEM_DEFINE(sem_ecg_complete_ok, 0, 1);
K_SEM_DEFINE(sem_ecg_lon, 0, 1);
K_SEM_DEFINE(sem_ecg_loff, 0, 1);

ZBUS_CHAN_DECLARE(ecg_timer_chan);

#define ECG_SAMPLING_INTERVAL_MS 65

#define ECG_RECORD_DURATION_S 30

static int ecg_last_timer_val;
static int ecg_countdown_val;

static const struct smf_state ecg_bioz_states[];

struct s_ecg_bioz_object
{
    struct smf_ctx ctx;
} s_ecg_bioz_obj;

struct k_thread ecg_bioz_sampling_thread;
k_tid_t ecg_bioz_sampling_thread_id;

#define ECG_RECORD_BUFFER_SIZE 75 


int32_t ecg_data_buffer[3840]; //128*30 = 3840 

#define ECG_BIOZ_SAMPLING_THREAD_STACKSIZE 4096
#define ECG_BIOZ_SAMPLING_THREAD_PRIORITY 7

K_THREAD_STACK_DEFINE(ecg_bioz_sampling_thread_stack, ECG_BIOZ_SAMPLING_THREAD_STACKSIZE);

enum ecg_bioz_state
{
    HPI_ECG_BIOZ_STATE_IDLE,
    HPI_ECG_BIOZ_STATE_LEADON_DETECT,
    HPI_ECG_BIOZ_STATE_STREAM,
    HPI_ECG_BIOZ_STATE_COMPLETE,
};

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

static bool ecg_active = false;

// EXTERNS
extern const struct device *const max30001_dev;
extern struct k_sem sem_ecg_bioz_smf_start;
extern struct k_sem sem_ecg_bioz_sm_start;
extern struct k_sem sem_ecg_complete;
extern struct k_sem sem_ecg_complete_reset;

static void sensor_ecg_bioz_process_decode(uint8_t *buf, uint32_t buf_len)
{
    const struct max30001_encoded_data *edata = (const struct max30001_encoded_data *)buf;
    struct hpi_ecg_bioz_sensor_data_t ecg_bioz_sensor_sample;

    uint8_t ecg_samples = edata->num_samples_ecg;
    uint8_t bioz_samples = edata->num_samples_bioz;

    // printk("ECG NS: %d ", ecg_samples);
    // printk("BioZ NS: %d ", bioz_samples);

    if (edata->chip_op_mode == MAX30001_OP_MODE_LON_DETECT)
    {
        if (edata->lon_state == 1)
        {
            LOG_DBG("LeadOn");
            // k_sem_give(&sem_ecg_start);
        }

        return;
    }

    if ((ecg_samples < 32 && ecg_samples > 0) || (bioz_samples < 32 && bioz_samples > 0))
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

        if (ecg_active)
        {
            k_msgq_put(&q_ecg_bioz_sample, &ecg_bioz_sensor_sample, K_MSEC(1));
        }
    }
}

void ecg_bioz_sampling_thread_runner(void *, void *, void *)
{

    uint8_t ecg_bioz_buf[512];
    int ret;

    LOG_INF("ECG/ BioZ Sampling starting");

    for (;;)
    {
        ret = sensor_read(&max30001_iodev, &max30001_read_rtio_poll_ctx, ecg_bioz_buf, sizeof(ecg_bioz_buf));
        if (ret < 0)
        {
            LOG_ERR("Error reading sensor data");
            continue;
        }
        sensor_ecg_bioz_process_decode(ecg_bioz_buf, sizeof(ecg_bioz_buf));

        // sensor_read_async_mempool(&max30001_iodev, &max30001_read_rtio_poll_ctx, NULL);
        // sensor_processing_with_callback(&max30001_read_rtio_poll_ctx, sensor_ecg_bioz_process_cb);

        k_sleep(K_MSEC(ECG_SAMPLING_INTERVAL_MS));
    }
}

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

static int hpi_max30001_lon_detect_enable(void)
{
    struct sensor_value mode_set;
    mode_set.val1 = MAX30001_OP_MODE_LON_DETECT;
    return sensor_attr_set(max30001_dev, SENSOR_CHAN_ALL, MAX30001_ATTR_OP_MODE, &mode_set);
}

static void st_ecg_bioz_idle_entry(void *o)
{
    LOG_DBG("ECG/BioZ SM Idle Entry");

    hw_max30001_ecg_disable();
    hw_max30001_bioz_disable();
}

static void st_ecg_bioz_idle_run(void *o)
{
    // LOG_DBG("ECG/BioZ SM Idle Run");
    if (k_sem_take(&sem_ecg_start, K_NO_WAIT) == 0)
    {
        smf_set_state(SMF_CTX(&s_ecg_bioz_obj), &ecg_bioz_states[HPI_ECG_BIOZ_STATE_LEADON_DETECT]);
    }
}

static void st_ecg_bioz_idle_exit(void *o)
{
    LOG_DBG("ECG/BioZ SM Idle Exit");
}

static void st_ecg_bioz_leadon_detect_entry(void *o)
{
    LOG_DBG("ECG/BioZ SM LeadOn Detect Entry");
    // TODO: Implement LeadOn detection
}

static void st_ecg_bioz_leadon_detect_run(void *o)
{
    smf_set_state(SMF_CTX(&s_ecg_bioz_obj), &ecg_bioz_states[HPI_ECG_BIOZ_STATE_STREAM]);
}

static void st_ecg_bioz_leadon_detect_exit(void *o)
{
    LOG_DBG("ECG/BioZ SM LeadOn Detect Exit");
}

static void st_ecg_bioz_stream_entry(void *o)
{
    LOG_DBG("ECG/BioZ SM Stream Entry");

    k_thread_resume(ecg_bioz_sampling_thread_id);

    hw_max30001_ecg_enable();
    //hpi_max30001_lon_detect_enable();

    ecg_countdown_val = ECG_RECORD_DURATION_S;
}

static void st_ecg_bioz_stream_run(void *o)
{
    // LOG_DBG("ECG/BioZ SM Stream Run");
    // Stream for ECG duration (30s)
    if ((k_uptime_get_32() - ecg_last_timer_val) >= 1000)
    {
        ecg_countdown_val--;
        LOG_DBG("ECG timer: %d", ecg_countdown_val);
        ecg_last_timer_val = k_uptime_get_32();
        if (ecg_countdown_val <= 0)
        {
            smf_set_state(SMF_CTX(&s_ecg_bioz_obj), &ecg_bioz_states[HPI_ECG_BIOZ_STATE_COMPLETE]);
        }

        struct hpi_ecg_timer_t ecg_timer = {
            .timer_val = ecg_countdown_val,
        };
        zbus_chan_pub(&ecg_timer_chan, &ecg_timer, K_NO_WAIT);
    }
}

static void st_ecg_bioz_stream_exit(void *o)
{
    LOG_DBG("ECG/BioZ SM Stream Exit");
}

static void st_ecg_bioz_complete_entry(void *o)
{
    LOG_DBG("ECG/BioZ SM Complete Entry");
    hw_max30001_ecg_disable();
    k_thread_suspend(ecg_bioz_sampling_thread_id);
    k_sem_give(&sem_ecg_complete);
}

static void st_ecg_bioz_complete_run(void *o)
{
    if (k_sem_take(&sem_ecg_complete_ok, K_NO_WAIT) == 0)
    {
        smf_set_state(SMF_CTX(&s_ecg_bioz_obj), &ecg_bioz_states[HPI_ECG_BIOZ_STATE_IDLE]);
    }
}

static void st_ecg_bioz_complete_exit(void *o)
{
    LOG_DBG("ECG/BioZ SM Complete Exit");
}

static const struct smf_state ecg_bioz_states[] = {
    [HPI_ECG_BIOZ_STATE_IDLE] = SMF_CREATE_STATE(st_ecg_bioz_idle_entry, st_ecg_bioz_idle_run, st_ecg_bioz_idle_exit, NULL, NULL),
    [HPI_ECG_BIOZ_STATE_LEADON_DETECT] = SMF_CREATE_STATE(st_ecg_bioz_leadon_detect_entry, st_ecg_bioz_leadon_detect_run, st_ecg_bioz_leadon_detect_exit, NULL, NULL),
    [HPI_ECG_BIOZ_STATE_STREAM] = SMF_CREATE_STATE(st_ecg_bioz_stream_entry, st_ecg_bioz_stream_run, st_ecg_bioz_stream_exit, NULL, NULL),
    [HPI_ECG_BIOZ_STATE_COMPLETE] = SMF_CREATE_STATE(st_ecg_bioz_complete_entry, st_ecg_bioz_complete_run, st_ecg_bioz_complete_exit, NULL, NULL),
};

void smf_ecg_bioz_thread(void)
{
    int ret;

    // Wait for HW module to init ECG/BioZ
    k_sem_take(&sem_ecg_bioz_sm_start, K_FOREVER);

    LOG_INF("ECG/BioZ SMF Thread Started");

    ecg_bioz_sampling_thread_id = k_thread_create(&ecg_bioz_sampling_thread, ecg_bioz_sampling_thread_stack,
                                                  ECG_BIOZ_SAMPLING_THREAD_STACKSIZE,
                                                  ecg_bioz_sampling_thread_runner,
                                                  NULL, NULL, NULL,
                                                  ECG_BIOZ_SAMPLING_THREAD_PRIORITY, 0, K_NO_WAIT);

    k_thread_suspend(ecg_bioz_sampling_thread_id);

    smf_set_initial(SMF_CTX(&s_ecg_bioz_obj), &ecg_bioz_states[HPI_ECG_BIOZ_STATE_IDLE]);

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
// K_THREAD_DEFINE(ecg_bioz_sampling_trigger_thread_id, ECG_BIOZ_SAMPLING_THREAD_STACKSIZE, ecg_bioz_sampling_trigger_thread, NULL, NULL, NULL, ECG_BIOZ_SAMPLING_THREAD_PRIORITY, 0, 700);
