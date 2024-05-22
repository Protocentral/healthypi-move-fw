#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/rtio/rtio.h>

#include "max30001.h"
#include "max32664.h"

#include "sampling_module.h"
#include "sys_sm_module.h"

#include "display_module.h"

extern const struct device *const max30001_dev;
extern const struct device *const max32664_dev;

#define SAMPLING_INTERVAL_MS 8
#define PPG_SAMPLING_INTERVAL_MS 1

K_MSGQ_DEFINE(q_ecg_bioz_sample, sizeof(struct hpi_ecg_bioz_sensor_data_t), 100, 1);
K_MSGQ_DEFINE(q_ppg_sample, sizeof(struct hpi_ppg_sensor_data_t), 256, 1);

K_SEM_DEFINE(sem_sampling_start, 0, 1);

// ASync sensor RTIO defines

SENSOR_DT_READ_IODEV(max32664_iodev, DT_ALIAS(max32664), SENSOR_CHAN_VOLTAGE);
static struct sensor_decoder_api *max32664_decoder = SENSOR_DECODER_DT_GET(DT_ALIAS(max32664));

#define PPG_SAMPLING_THREAD_STACKSIZE 2048
#define PPG_SAMPLING_THREAD_PRIORITY 7

#define ECG_SAMPLING_THREAD_STACKSIZE 2048
#define ECG_SAMPLING_THREAD_PRIORITY 7

K_THREAD_STACK_DEFINE(ppg_sampling_stack_area, PPG_SAMPLING_THREAD_STACKSIZE);
struct k_thread ppg_sampling_thread_data;

k_tid_t global_ppg_sampling_thread_id;

RTIO_DEFINE_WITH_MEMPOOL(max32664_read_rtio_ctx,
                         32,  /* submission queue size */
                         32,  /* completion queue size */
                         128, /* number of memory blocks */
                         64,  /* size of each memory block */
                         4    /* memory alignment */
);

static void sensor_processing_callback(int result, uint8_t *buf,
                                       uint32_t buf_len, void *userdata)
{

        const struct max32664_encoded_data *edata = (const struct max32664_encoded_data *)buf;

        struct hpi_ppg_sensor_data_t sensor_sample;
        // printk("NS: %d ", edata->num_samples);
        if (edata->num_samples > 0)
        {
                int n_samples = edata->num_samples;

                for (int i = 0; i < n_samples; i++) // edata->num_samples; i++)
                {
                        sensor_sample.raw_red = edata->red_samples[i];
                        sensor_sample.raw_ir = edata->ir_samples[i];

                        sensor_sample.hr = edata->hr;
                        sensor_sample.spo2 = edata->spo2;
                        sensor_sample.bp_sys = edata->bpt_sys;
                        sensor_sample.bp_dia = edata->bpt_dia;
                        sensor_sample.bpt_status = edata->bpt_status;
                        sensor_sample.bpt_progress = edata->bpt_progress;

                        k_msgq_put(&q_ppg_sample, &sensor_sample, K_MSEC(1));
                }
                //printk("Status: %d Progress: %d\n", edata->bpt_status, edata->bpt_progress);
        }
}

void ppg_sampling_trigger_thread(void)
{
        k_sem_take(&sem_sampling_start, K_FOREVER);

        printk("PPG Sampling Trigger Thread starting\n");
        for (;;)
        {
                sensor_read(&max32664_iodev, &max32664_read_rtio_ctx, NULL);
                sensor_processing_with_callback(&max32664_read_rtio_ctx, sensor_processing_callback);

                k_sleep(K_MSEC(100));                                                                                                                                                                                                  
        }
}

void ppg_thread_create(void)
{
        printk("Creating PPG Sampling Thread\n");
        global_ppg_sampling_thread_id = k_thread_create(&ppg_sampling_thread_data, ppg_sampling_stack_area,
                                                        K_THREAD_STACK_SIZEOF(ppg_sampling_stack_area),
                                                        ppg_sampling_trigger_thread,
                                                        NULL, NULL, NULL,
                                                        PPG_SAMPLING_THREAD_PRIORITY, 0, K_NO_WAIT);
        k_thread_suspend(global_ppg_sampling_thread_id);
}

void ppg_data_stop(void)
{
        k_thread_suspend(global_ppg_sampling_thread_id);
}

void ppg_data_start(void)
{
        k_thread_resume(global_ppg_sampling_thread_id);
}

/*
void ppg_sampling_thread(void)
{
        printk("PPG Sampling Thread starting\n");

        for (;;)
        {
                k_sleep(K_MSEC(PPG_SAMPLING_INTERVAL_MS));

                struct sensor_value red_sample;
                struct sensor_value ir_sample;
                struct sensor_value hr_sample;
                struct sensor_value spo2_sample;
                struct sensor_value bp_sys_sample;
                struct sensor_value bp_dia_sample;
                struct sensor_value bpt_status_sample;
                struct sensor_value bpt_progress_sample;

                sensor_sample_fetch(max32664_dev);

                int num_samples;
                sensor_channel_get(max32664_dev, SENSOR_PPG_NUM_SAMPLES, &num_samples);

                if (num_samples > 0)
                {
                        // Get 1st sample

                        sensor_channel_get(max32664_dev, SENSOR_CHAN_PPG_RED_1, &red_sample);
                        sensor_channel_get(max32664_dev, SENSOR_CHAN_PPG_IR_1, &ir_sample);
                        sensor_channel_get(max32664_dev, SENSOR_CHAN_PPG_HR, &hr_sample);
                        sensor_channel_get(max32664_dev, SENSOR_CHAN_PPG_SPO2, &spo2_sample);
                        sensor_channel_get(max32664_dev, SENSOR_CHAN_PPG_BP_SYS, &bp_sys_sample);
                        sensor_channel_get(max32664_dev, SENSOR_CHAN_PPG_BP_DIA, &bp_dia_sample);
                        sensor_channel_get(max32664_dev, SENSOR_CHAN_PPG_BPT_STATUS, &bpt_status_sample);
                        sensor_channel_get(max32664_dev, SENSOR_CHAN_PPG_BPT_PROGRESS, &bpt_progress_sample);

                        struct hpi_ppg_sensor_data_t sensor_sample;

                        sensor_sample.raw_red = red_sample.val1;
                        sensor_sample.raw_ir = ir_sample.val1;

                        sensor_sample.hr = hr_sample.val1;
                        sensor_sample.spo2 = spo2_sample.val1;
                        sensor_sample.bp_sys = bp_sys_sample.val1;
                        sensor_sample.bp_dia = bp_dia_sample.val1;
                        sensor_sample.bpt_status = bpt_status_sample.val1;
                        sensor_sample.bpt_progress = bpt_progress_sample.val1;

                        // if(sensor_sample.raw_ir > 1)
                        //{
                        k_msgq_put(&q_ppg_sample, &sensor_sample, K_NO_WAIT);
                        //}
                        // k_msgq_put(&q_ppg_sample, &sensor_sample, K_NO_WAIT);

                        if (num_samples > 4)
                                num_samples = 4;

                        if (num_samples > 1)
                        {
                                int start_channel = SENSOR_CHAN_PPG_IR_1 + 1;

                                // Get remaining samples
                                for (int i = 1; i < num_samples; i++)
                                {

                                        sensor_channel_get(max32664_dev, (start_channel++), &red_sample);
                                        sensor_channel_get(max32664_dev, (start_channel++), &ir_sample);

                                        sensor_channel_get(max32664_dev, SENSOR_CHAN_PPG_HR, &hr_sample);
                                        sensor_channel_get(max32664_dev, SENSOR_CHAN_PPG_SPO2, &spo2_sample);
                                        sensor_channel_get(max32664_dev, SENSOR_CHAN_PPG_BP_SYS, &bp_sys_sample);
                                        sensor_channel_get(max32664_dev, SENSOR_CHAN_PPG_BP_DIA, &bp_dia_sample);
                                        sensor_channel_get(max32664_dev, SENSOR_CHAN_PPG_BPT_STATUS, &bpt_status_sample);
                                        sensor_channel_get(max32664_dev, SENSOR_CHAN_PPG_BPT_PROGRESS, &bpt_progress_sample);

                                        struct hpi_ppg_sensor_data_t sensor_sample;

                                        sensor_sample.raw_red = red_sample.val1;
                                        sensor_sample.raw_ir = ir_sample.val1;

                                        sensor_sample.hr = hr_sample.val1;
                                        sensor_sample.spo2 = spo2_sample.val1;
                                        sensor_sample.bp_sys = bp_sys_sample.val1;
                                        sensor_sample.bp_dia = bp_dia_sample.val1;
                                        sensor_sample.bpt_status = bpt_status_sample.val1;
                                        sensor_sample.bpt_progress = bpt_progress_sample.val1;

                                        k_msgq_put(&q_ppg_sample, &sensor_sample, K_NO_WAIT);
                                }
                        }
                }
        }
}*/

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

                ecg_bioz_sensor_sample.ecg_lead_off = ecg_lead_off.val1;

                ecg_bioz_sensor_sample._bioZSkipSample = false;

                k_msgq_put(&q_ecg_bioz_sample, &ecg_bioz_sensor_sample, K_NO_WAIT);
        }
}

// K_THREAD_DEFINE(ppg_sampling_trigger_thread_id, 8192, ppg_sampling_trigger_thread, NULL, NULL, NULL, SAMPLING_THREAD_PRIORITY, 0, 1000);
K_THREAD_DEFINE(sampling_thread_id, ECG_SAMPLING_THREAD_STACKSIZE, ecg_sampling_thread, NULL, NULL, NULL, ECG_SAMPLING_THREAD_PRIORITY, 0, 1000);

// K_THREAD_DEFINE(ppg_sampling_thread_id, SAMPLING_THREAD_STACKSIZE, ppg_sampling_thread, NULL, NULL, NULL, SAMPLING_THREAD_PRIORITY, 0, 1000);
