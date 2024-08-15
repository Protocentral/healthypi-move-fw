//#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/rtio/rtio.h>

#include "max30001.h"
//#include "max32664.h"

#include "maxm86146.h"

#include "sampling_module.h"


#include "display_module.h"

extern const struct device *const max30001_dev;
extern const struct device *const maxm86146_dev;
extern const struct device *const max32664d_dev;

#define SAMPLING_INTERVAL_MS 8
#define PPG_SAMPLING_INTERVAL_MS 1

K_MSGQ_DEFINE(q_ecg_bioz_sample, sizeof(struct hpi_ecg_bioz_sensor_data_t), 100, 1);
K_MSGQ_DEFINE(q_ppg_sample, sizeof(struct hpi_ppg_sensor_data_t), 256, 1);

K_SEM_DEFINE(sem_sampling_start, 0, 1);

// ASync sensor RTIO defines

SENSOR_DT_READ_IODEV(maxm86146_iodev, DT_ALIAS(maxm86146), SENSOR_CHAN_VOLTAGE);
//static struct sensor_decoder_api *maxm86146_decoder = SENSOR_DECODER_DT_GET(DT_ALIAS(maxm86146));


#define PPG_SAMPLING_THREAD_STACKSIZE 2048
#define PPG_SAMPLING_THREAD_PRIORITY 7

K_THREAD_STACK_DEFINE(ppg_sampling_stack_area, PPG_SAMPLING_THREAD_STACKSIZE);
struct k_thread ppg_sampling_thread_data;

k_tid_t global_ppg_sampling_thread_id;

bool ppg_wrist_sampling_on=false;
bool ppg_finger_sampling_on=false;

RTIO_DEFINE_WITH_MEMPOOL(maxm86146_read_rtio_ctx,
                         32,  /* submission queue size */
                         32,  /* completion queue size */
                         128, /* number of memory blocks */
                         64,  /* size of each memory block */
                         4    /* memory alignment */
);

static void sensor_ppg_wrist_processing_callback(int result, uint8_t *buf,
                                       uint32_t buf_len, void *userdata)
{

        // const struct maxm86146_encoded_data *edata = (const struct maxm86146_encoded_data *)buf;

        const struct maxm86146_encoded_data *edata = (const struct maxm86146_encoded_data *)buf;

        struct hpi_ppg_sensor_data_t ppg_sensor_sample;
        // printk("NS: %d ", edata->num_samples);
        if (edata->num_samples > 0)
        {
                int n_samples = edata->num_samples;

                for (int i = 0; i < n_samples; i++) // edata->num_samples; i++)
                {
                        ppg_sensor_sample.raw_red = edata->red_samples[i];
                        ppg_sensor_sample.raw_ir = edata->ir_samples[i];
                        ppg_sensor_sample.raw_green = edata->green_samples[i];

                        ppg_sensor_sample.hr = edata->hr;
                        ppg_sensor_sample.spo2 = edata->spo2;
                        ppg_sensor_sample.rtor = edata->rtor;
                        ppg_sensor_sample.scd_state = edata->scd_state;

                        ppg_sensor_sample.steps_run = edata->steps_run;
                        ppg_sensor_sample.steps_walk = edata->steps_walk;
                        
                        //printk("Steps Run: %d Steps Walk: %d\n", edata->steps_run, edata->steps_walk);
                        // ppg_sensor_sample.bp_sys = edata->bpt_sys;
                        // ppg_sensor_sample.bp_dia = edata->bpt_dia;
                        // ppg_sensor_sample.bpt_status = edata->bpt_status;
                        // ppg_sensor_sample.bpt_progress = edata->bpt_progress;

                        k_msgq_put(&q_ppg_sample, &ppg_sensor_sample, K_MSEC(1));
                }
                // printk("Status: %d Progress: %d\n", edata->bpt_status, edata->bpt_progress);
        }
}

void ppg_sampling_trigger_thread(void)
{
        k_sem_take(&sem_sampling_start, K_FOREVER);

        printk("PPG Sampling Trigger Thread starting\n");
        for (;;)
        {
                sensor_read(&maxm86146_iodev, &maxm86146_read_rtio_ctx, NULL);
                sensor_processing_with_callback(&maxm86146_read_rtio_ctx, sensor_ppg_wrist_processing_callback);

                k_sleep(K_MSEC(40));
        }
}

void ppg_data_stop(void)
{
        k_thread_suspend(global_ppg_sampling_thread_id);
}

void ppg_data_start(void)
{
        k_thread_resume(global_ppg_sampling_thread_id);
}

void ecg_sampling_thread(void)
{
        printk("ECG/ BioZ Sampling Thread starting\n");

        for (;;)
        {
                k_sleep(K_MSEC(SAMPLING_INTERVAL_MS));

                struct sensor_value ecg_sample;
                struct sensor_value bioz_sample;
                struct sensor_value rtor_sample;
                struct sensor_value hr_sample;
                struct sensor_value ecg_lead_off;

#ifdef CONFIG_SENSOR_MAX30001
                sensor_sample_fetch(max30001_dev);
                sensor_channel_get(max30001_dev, SENSOR_CHAN_ECG_UV, &ecg_sample);
                sensor_channel_get(max30001_dev, SENSOR_CHAN_BIOZ_UV, &bioz_sample);
                sensor_channel_get(max30001_dev, SENSOR_CHAN_RTOR, &rtor_sample);
                sensor_channel_get(max30001_dev, SENSOR_CHAN_HR, &hr_sample);
                sensor_channel_get(max30001_dev, SENSOR_CHAN_LDOFF, &ecg_lead_off);

#endif
                struct hpi_ecg_bioz_sensor_data_t ecg_bioz_sensor_sample;

                ecg_bioz_sensor_sample.ecg_sample = ecg_sample.val1;
                ecg_bioz_sensor_sample.bioz_sample = bioz_sample.val1;

                ecg_bioz_sensor_sample.rtor_sample = rtor_sample.val1;
                ecg_bioz_sensor_sample.hr_sample = hr_sample.val1;

                ecg_bioz_sensor_sample.ecg_lead_off = false; // ecg_lead_off.val1;

                ecg_bioz_sensor_sample._bioZSkipSample = false;

                k_msgq_put(&q_ecg_bioz_sample, &ecg_bioz_sensor_sample, K_NO_WAIT);
        }
}

#define ECG_SAMPLING_THREAD_STACKSIZE 2048
#define ECG_SAMPLING_THREAD_PRIORITY 7

//K_THREAD_DEFINE(ppg_sampling_trigger_thread_id, 8192, ppg_sampling_trigger_thread, NULL, NULL, NULL, PPG_SAMPLING_THREAD_PRIORITY, 0, 1000);
//K_THREAD_DEFINE(ecg_sampling_thread_id, ECG_SAMPLING_THREAD_STACKSIZE, ecg_sampling_thread, NULL, NULL, NULL, ECG_SAMPLING_THREAD_PRIORITY, 0, 1000);

// K_THREAD_DEFINE(ppg_sampling_thread_id, SAMPLING_THREAD_STACKSIZE, ppg_sampling_thread, NULL, NULL, NULL, SAMPLING_THREAD_PRIORITY, 0, 1000);
