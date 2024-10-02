// #include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/rtio/rtio.h>
#include <zephyr/zbus/zbus.h>

#include "max30001.h"
#include "max32664.h"
#include "maxm86146.h"
#include "sampling_module.h"

LOG_MODULE_REGISTER(sampling_module, CONFIG_SENSOR_LOG_LEVEL);

extern const struct device *const max30001_dev;
extern const struct device *const maxm86146_dev;
extern const struct device *const max32664d_dev;

#define SAMPLING_INTERVAL_MS 8

#define PPG_FINGER_SAMPLING_INTERVAL_MS 40
#define PPG_WRIST_SAMPLING_INTERVAL_MS 8
#define ECG_SAMPLING_INTERVAL_MS 65

K_MSGQ_DEFINE(q_ecg_bioz_sample, sizeof(struct hpi_ecg_bioz_sensor_data_t), 100, 1);
K_MSGQ_DEFINE(q_ppg_sample, sizeof(struct hpi_ppg_sensor_data_t), 256, 1);

// ASync sensor RTIO defines

SENSOR_DT_READ_IODEV(maxm86146_iodev, DT_ALIAS(maxm86146), SENSOR_CHAN_VOLTAGE);
SENSOR_DT_READ_IODEV(max32664d_iodev, DT_ALIAS(max32664d), SENSOR_CHAN_VOLTAGE);
SENSOR_DT_READ_IODEV(max30001_iodev, DT_ALIAS(max30001), SENSOR_CHAN_VOLTAGE);

#define PPG_SAMPLING_THREAD_STACKSIZE 2048
#define PPG_SAMPLING_THREAD_PRIORITY 7

K_THREAD_STACK_DEFINE(ppg_sampling_stack_area, PPG_SAMPLING_THREAD_STACKSIZE);
struct k_thread ppg_sampling_thread_data;

k_tid_t global_ppg_sampling_thread_id;

bool ppg_wrist_sampling_on = false;
bool ppg_finger_sampling_on = false;

// static lv_timer_t *ecg_sampling_timer;

// *** Externs ***
ZBUS_CHAN_DECLARE(hr_chan);

extern struct k_sem sem_ecg_intb_recd;

// static const struct gpio_dt_spec max30001_intb = GPIO_DT_SPEC_GET(DT_NODELABEL(max30001_intb), gpios);
//  static const struct gpio_dt_spec button1 = GPIO_DT_SPEC_GET(DT_ALIAS(gpio_button0), gpios);

RTIO_DEFINE_WITH_MEMPOOL(maxm86146_read_rtio_ctx,
                         32,  /* submission queue size */
                         32,  /* completion queue size */
                         128, /* number of memory blocks */
                         64,  /* size of each memory block */
                         4    /* memory alignment */
);

RTIO_DEFINE_WITH_MEMPOOL(max32664d_read_rtio_ctx,
                         32,  /* submission queue size */
                         32,  /* completion queue size */
                         128, /* number of memory blocks */
                         64,  /* size of each memory block */
                         4    /* memory alignment */
);

RTIO_DEFINE_WITH_MEMPOOL(max30001_read_rtio_ctx,
                         32,  /* submission queue size */
                         32,  /* completion queue size */
                         128, /* number of memory blocks */
                         64,  /* size of each memory block */
                         4    /* memory alignment */
);

static void sensor_ppg_finger_processing_callback(int result, uint8_t *buf,
                                                  uint32_t buf_len, void *userdata)
{
        const struct max32664_encoded_data *edata = (const struct max32664_encoded_data *)buf;

        struct hpi_ppg_sensor_data_t ppg_sensor_sample;
        // printk("NS: %d ", edata->num_samples);
        if (edata->num_samples > 0)
        {
                int n_samples = edata->num_samples;

                for (int i = 0; i < n_samples; i++) // edata->num_samples; i++)
                {
                        ppg_sensor_sample.raw_red = edata->red_samples[i];
                        ppg_sensor_sample.raw_ir = edata->ir_samples[i];
                        // ppg_sensor_sample.raw_green = edata->green_samples[i];

                        ppg_sensor_sample.hr = edata->hr;
                        ppg_sensor_sample.spo2 = edata->spo2;

                        ppg_sensor_sample.bp_sys = edata->bpt_sys;
                        ppg_sensor_sample.bp_dia = edata->bpt_dia;
                        ppg_sensor_sample.bpt_status = edata->bpt_status;
                        ppg_sensor_sample.bpt_progress = edata->bpt_progress;

                        k_msgq_put(&q_ppg_sample, &ppg_sensor_sample, K_MSEC(1));
                }
                // printk("Status: %d Progress: %d\n", edata->bpt_status, edata->bpt_progress);
        }
}
static void sensor_ppg_wrist_processing_callback(int result, uint8_t *buf,
                                                 uint32_t buf_len, void *userdata)
{
        const struct maxm86146_encoded_data *edata = (const struct maxm86146_encoded_data *)buf;
        struct hpi_ppg_sensor_data_t ppg_sensor_sample;

        static uint8_t prev_hr_val=0;
        // uint8_t hr_chan_value=0;
        // printk("WR NS: %d ", edata->num_samples);
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

                        // printk("Steps Run: %d Steps Walk: %d\n", edata->steps_run, edata->steps_walk);

                        k_msgq_put(&q_ppg_sample, &ppg_sensor_sample, K_MSEC(1));

                        struct hpi_hr_t hr_chan_value = {
                            .hr = edata->hr,
                            .hr_ready_flag = true,
                        };
                        zbus_chan_pub(&hr_chan, &hr_chan_value, K_SECONDS(1));
                }
        }
}

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
}

void ppg_wrist_sampling_trigger_thread(void)
{

        LOG_INF("PPG Wrist Sampling Trigger Thread starting\n");
        for (;;)
        {
                sensor_read(&maxm86146_iodev, &maxm86146_read_rtio_ctx, NULL);
                sensor_processing_with_callback(&maxm86146_read_rtio_ctx, sensor_ppg_wrist_processing_callback);

                k_sleep(K_MSEC(PPG_WRIST_SAMPLING_INTERVAL_MS));
        }
}

void ecg_bioz_sampling_trigger_thread(void)
{
        LOG_INF("ECG/ BioZ Sampling Trigger Thread starting\n");

        for (;;)
        {
                // k_sem_take(&sem_ecg_intb_recd, K_FOREVER);

                sensor_read(&max30001_iodev, &max30001_read_rtio_ctx, NULL);
                sensor_processing_with_callback(&max30001_read_rtio_ctx, sensor_ecg_bioz_process_cb);

                k_sleep(K_MSEC(ECG_SAMPLING_INTERVAL_MS));
        }
}

void ppg_finger_sampling_trigger_thread(void)
{
        LOG_INF("PPG Finger Sampling Trigger Thread starting");
        for (;;)
        {
                sensor_read(&max32664d_iodev, &max32664d_read_rtio_ctx, NULL);
                sensor_processing_with_callback(&max32664d_read_rtio_ctx, sensor_ppg_finger_processing_callback);

                k_sleep(K_MSEC(PPG_FINGER_SAMPLING_INTERVAL_MS));
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

void ecg_sampling_timer_start(void)
{
        // ecg_sampling_timer = lv_timer_create(ecg_sampling_timer_cb, ECG_SAMPLING_INTERVAL_MS, NULL);
}

#define ECG_SAMPLING_THREAD_STACKSIZE 2048
#define ECG_SAMPLING_THREAD_PRIORITY 7

// K_THREAD_DEFINE(ppg_finger_sampling_trigger_thread_id, 8192, ppg_finger_sampling_trigger_thread, NULL, NULL, NULL, PPG_SAMPLING_THREAD_PRIORITY, 0, 1000);

K_THREAD_DEFINE(ppg_wrist_sampling_trigger_thread_id, 8192, ppg_wrist_sampling_trigger_thread, NULL, NULL, NULL, PPG_SAMPLING_THREAD_PRIORITY, 0, 1000);
K_THREAD_DEFINE(ecg_bioz_sampling_trigger_thread_id, 2048, ecg_bioz_sampling_trigger_thread, NULL, NULL, NULL, ECG_SAMPLING_THREAD_PRIORITY, 0, 1000);
