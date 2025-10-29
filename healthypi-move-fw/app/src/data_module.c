#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <stdio.h>
#include <arm_math.h>

#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <time.h>

LOG_MODULE_REGISTER(data_module, CONFIG_SENSOR_LOG_LEVEL);

#include "max30001.h"

#include "hw_module.h"
#include "hpi_common_types.h"
#include "fs_module.h"
#include "ble_module.h"
#include "ui/move_ui.h"
#include "hpi_sys.h"

#include "log_module.h"

// ProtoCentral data formats
#define CES_CMDIF_PKT_START_1 0x0A
#define CES_CMDIF_PKT_START_2 0xFA
#define CES_CMDIF_TYPE_DATA 0x02
#define CES_CMDIF_PKT_STOP 0x0B
#define DATA_LEN 22

#define LOG_SAMPLE_RATE_SPS 125
#define SAMPLE_BUFF_WATERMARK 8

char DataPacket[DATA_LEN];
const char DataPacketFooter[2] = {0, CES_CMDIF_PKT_STOP};
const char DataPacketHeader[5] = {CES_CMDIF_PKT_START_1, CES_CMDIF_PKT_START_2, DATA_LEN, 0, CES_CMDIF_TYPE_DATA};

extern const struct device *const max30001_dev;
extern const struct device *const max32664d_dev;

static bool settings_send_usb_enabled = false;
static bool settings_send_ble_enabled = true;
static bool settings_plot_enabled = true;

enum hpi5_data_format
{
    DATA_FMT_OPENVIEW,
    DATA_FMT_PLAIN_TEXT,
} hpi5_data_format_t;

static bool settings_log_data_enabled = true; // true;
static int settings_data_format = DATA_FMT_OPENVIEW;

// struct hpi_ecg_bioz_sensor_data_t log_buffer[LOG_BUFFER_LENGTH];

uint16_t current_session_log_counter = 0;
uint16_t current_session_log_id = 0;
char session_id_str[5];

static bool is_ecg_record_active = false;
static int32_t ecg_record_buffer[ECG_RECORD_BUFFER_SAMPLES]; // 128*30 = 3840
static volatile uint16_t ecg_record_counter = 0;
K_MUTEX_DEFINE(mutex_is_ecg_record_active);

static uint32_t last_hr_update_time = 0;

K_MUTEX_DEFINE(mutex_hr_change);

// Externs

ZBUS_CHAN_DECLARE(hr_chan);

ZBUS_CHAN_DECLARE(ecg_stat_chan);

extern struct k_msgq q_ecg_bioz_sample;
extern struct k_msgq q_ppg_wrist_sample;
extern struct k_msgq q_ppg_fi_sample;

extern struct k_msgq q_plot_ecg_bioz;
extern struct k_msgq q_plot_ppg_wrist;
extern struct k_msgq q_plot_ppg_fi;
extern struct k_msgq q_plot_hrv;

void sendData(int32_t ecg_sample, int32_t bioz_sample, uint32_t raw_red, uint32_t raw_ir, int32_t temp, uint8_t hr,
              uint8_t bpt_status, uint8_t spo2, bool _bioZSkipSample)
{
    DataPacket[0] = ecg_sample;
    DataPacket[1] = ecg_sample >> 8;
    DataPacket[2] = ecg_sample >> 16;
    DataPacket[3] = ecg_sample >> 24;

    DataPacket[4] = bioz_sample;
    DataPacket[5] = bioz_sample >> 8;
    DataPacket[6] = bioz_sample >> 16;
    DataPacket[7] = bioz_sample >> 24;

    if (_bioZSkipSample == false)
    {
        DataPacket[8] = 0x00;
    }
    else
    {
        DataPacket[8] = 0xFF;
    }

    DataPacket[9] = raw_red;
    DataPacket[10] = raw_red >> 8;
    DataPacket[11] = raw_red >> 16;
    DataPacket[12] = raw_red >> 24;

    DataPacket[13] = raw_ir;
    DataPacket[14] = raw_ir >> 8;
    DataPacket[15] = raw_ir >> 16;
    DataPacket[16] = raw_ir >> 24;

    DataPacket[17] = temp;
    DataPacket[18] = temp >> 8;

    DataPacket[19] = spo2;
    DataPacket[20] = hr;
    DataPacket[21] = bpt_status;

    if (settings_send_usb_enabled)
    {
        send_usb_cdc(DataPacketHeader, 5);
        send_usb_cdc(DataPacket, DATA_LEN);
        send_usb_cdc(DataPacketFooter, 2);
    }
}

static int hpi_get_trend_stats(uint16_t *in_array, uint16_t in_array_len, uint16_t *out_max, uint16_t *out_min, uint16_t *out_mean)
{
    if (in_array_len == 0)
    {
        return -1;
    }

    uint16_t max = in_array[0];
    uint16_t min = in_array[0];
    uint32_t sum = 0;

    for (int i = 0; i < in_array_len; i++)
    {
        if (in_array[i] > max)
        {
            max = in_array[i];
        }
        if (in_array[i] < min)
        {
            min = in_array[i];
        }
        sum += in_array[i];
    }

    *out_max = max;
    *out_min = min;
    *out_mean = sum / in_array_len;

    return 0;
}

void send_data_text(int32_t ecg_sample, int32_t bioz_sample, int32_t raw_red)
{
    char data[100];
    double f_ecg_sample = (double)ecg_sample / 1000;
    double f_bioz_sample = (double)bioz_sample / 1000;
    double f_raw_red = (double)raw_red / 1000;

    sprintf(data, "%.3f\t%.3f\t%.3f\r\n", f_ecg_sample, f_bioz_sample, f_raw_red);

    if (settings_send_usb_enabled)
    {
        send_usb_cdc(data, strlen(data));
    }
}

void send_data_text_1(int32_t in_sample)
{
    char data[100];
    float f_in_sample = (float)in_sample / 1000;

    sprintf(data, "%.3f\r\n", f_in_sample);
    send_usb_cdc(data, strlen(data));
}

static void work_ecg_write_file_handler(struct k_work *work)
{
    struct tm tm_sys_time = hpi_sys_get_sys_time();
    int64_t log_time = timeutil_timegm64(&tm_sys_time);

    LOG_DBG("ECG/BioZ SM Write File: %" PRId64, log_time);
    // Write ECG data to file
    hpi_write_ecg_record_file(ecg_record_buffer, ECG_RECORD_BUFFER_SAMPLES, log_time);
}

K_WORK_DEFINE(work_ecg_write_file, work_ecg_write_file_handler);

void hpi_data_set_ecg_record_active(bool active)
{
    k_mutex_lock(&mutex_is_ecg_record_active, K_FOREVER);
    is_ecg_record_active = active;

    if (active)
    {
        ecg_record_counter = 0;
        memset(ecg_record_buffer, 0, sizeof(ecg_record_buffer));
    } 
    else
    {
        // If recording is stopped, write the file
        if (ecg_record_counter > 0)
        {
            k_work_submit(&work_ecg_write_file);
        }
    }
    k_mutex_unlock(&mutex_is_ecg_record_active);
}

bool hpi_data_is_ecg_record_active(void)
{
    bool active;
    k_mutex_lock(&mutex_is_ecg_record_active, K_FOREVER);
    active = is_ecg_record_active;
    k_mutex_unlock(&mutex_is_ecg_record_active);
    return active;
}

void data_thread(void)
{
    struct hpi_ecg_bioz_sensor_data_t ecg_bioz_sensor_sample;
    struct hpi_ppg_wr_data_t ppg_wr_sensor_sample;
    struct hpi_ppg_fi_data_t ppg_fi_sensor_sample;

    static uint32_t hr_zbus_last_pub_time = 0;

    LOG_INF("Data Thread starting");

    for (;;)
    {
        bool processed_data = false;
        
        // Process all available ECG samples
        while (k_msgq_get(&q_ecg_bioz_sample, &ecg_bioz_sensor_sample, K_NO_WAIT) == 0)
        {
            processed_data = true;
            if (settings_send_ble_enabled)
            {

                ble_ecg_notify(ecg_bioz_sensor_sample.ecg_samples, ecg_bioz_sensor_sample.ecg_num_samples);
                ble_gsr_notify(ecg_bioz_sensor_sample.ecg_samples, ecg_bioz_sensor_sample.ecg_num_samples);
            }
            if (settings_plot_enabled)
            {
                k_msgq_put(&q_plot_ecg_bioz, &ecg_bioz_sensor_sample, K_NO_WAIT);
            }

            if (is_ecg_record_active == true)
            {
                int samples_to_copy = ecg_bioz_sensor_sample.ecg_num_samples;
                int space_left = ECG_RECORD_BUFFER_SAMPLES - ecg_record_counter;

                // Log sample count for debugging
                static uint32_t total_samples_recorded = 0;
                total_samples_recorded += samples_to_copy;
                
                if (samples_to_copy <= space_left)
                {
                    memcpy(&ecg_record_buffer[ecg_record_counter], ecg_bioz_sensor_sample.ecg_samples, samples_to_copy * sizeof(int32_t));
                    ecg_record_counter += samples_to_copy;
                    if (ecg_record_counter >= ECG_RECORD_BUFFER_SAMPLES)
                    {
                        ecg_record_counter = 0;
                    }
                }
                else
                {
                    // Copy in two parts: to end of buffer, then wrap around
                    memcpy(&ecg_record_buffer[ecg_record_counter], ecg_bioz_sensor_sample.ecg_samples, space_left * sizeof(int32_t));
                    memcpy(ecg_record_buffer, &ecg_bioz_sensor_sample.ecg_samples[space_left], (samples_to_copy - space_left) * sizeof(int32_t));
                    ecg_record_counter = samples_to_copy - space_left;
                }
                
                // Log every 1000 samples for debugging
                if ((total_samples_recorded % 1000) == 0) {
                    LOG_DBG("ECG samples recorded: %u, buffer pos: %u", total_samples_recorded, ecg_record_counter);
                }
            }
        }

        if (k_msgq_get(&q_ppg_fi_sample, &ppg_fi_sensor_sample, K_NO_WAIT) == 0)
        {
            processed_data = true;
            if (settings_send_ble_enabled)
            {
                ble_ppg_notify_fi(ppg_fi_sensor_sample.raw_ir, ppg_fi_sensor_sample.ppg_num_samples);
            }
            if (settings_plot_enabled)
            {
                k_msgq_put(&q_plot_ppg_fi, &ppg_fi_sensor_sample, K_NO_WAIT);
            }
        }

        // Check if PPG data is available
        if (k_msgq_get(&q_ppg_wrist_sample, &ppg_wr_sensor_sample, K_NO_WAIT) == 0)
        {
            processed_data = true;
            if (settings_send_ble_enabled)
            {
                ble_ppg_notify_wr(ppg_wr_sensor_sample.raw_green, ppg_wr_sensor_sample.ppg_num_samples);
            }
            if (settings_plot_enabled)
            {
                k_msgq_put(&q_plot_ppg_wrist, &ppg_wr_sensor_sample, K_NO_WAIT);
            }

            if (settings_send_usb_enabled)
            {
            }

            if (ppg_wr_sensor_sample.scd_state == HPI_PPG_SCD_ON_SKIN)
            {
                if (ppg_wr_sensor_sample.hr_confidence > 75)
                {
                    if (hr_zbus_last_pub_time == 0)
                    {
                        hr_zbus_last_pub_time = k_uptime_seconds();
                    }
                    if ((k_uptime_seconds() - hr_zbus_last_pub_time) > 2)
                    {
                        struct hpi_hr_t hr_chan_value = {
                            .timestamp = hw_get_sys_time_ts(),
                            .hr = ppg_wr_sensor_sample.hr,
                            .hr_ready_flag = true,
                        };
                        zbus_chan_pub(&hr_chan, &hr_chan_value, K_SECONDS(1));
                        hr_zbus_last_pub_time = k_uptime_seconds();
                    }
                }
            }
        }

        // Sleep longer if no data was processed to reduce CPU usage
        if (processed_data) {
            k_yield(); // Give other threads a chance to run
        } else {
            k_sleep(K_MSEC(1));  // Reduced sleep time to process samples faster
        }
    }
}

#define DATA_THREAD_STACKSIZE 4096
#define DATA_THREAD_PRIORITY 5  // Higher priority to process samples faster

K_THREAD_DEFINE(data_thread_id, DATA_THREAD_STACKSIZE, data_thread, NULL, NULL, NULL, DATA_THREAD_PRIORITY, 0, 1000);
