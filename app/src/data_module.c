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
#include "algos.h"
#include "ui/move_ui.h"
#include "hpi_sys.h"

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

    /*if (settings_send_ble_enabled)
    {
        cmdif_send_ble_data(data, strlen(data));
    }*/
}

void send_data_text_1(int32_t in_sample)
{
    char data[100];
    float f_in_sample = (float)in_sample / 1000;

    sprintf(data, "%.3f\r\n", f_in_sample);
    send_usb_cdc(data, strlen(data));
}

#define IIR_FILT_TAPS 5

// static const float32_t filt_low_b[IIR_FILT_TAPS] = {0.418163345761899, 0.836326691523798, 0.418163345761899, 0.0, 0.0}; //{ 1.0, 2.803860444771638, 3.571057889147946, 2.271508164463490, 0.659389877319096};
// static const float32_t filt_low_a[IIR_FILT_TAPS] = {1.0, 0.462938025291041, 0.209715357756555, 0.0, 0.0};

// static const float32_t filt_notch_b[IIR_FILT_TAPS] = {0.811831745907865, 2.537684304617564, 3.606784274651312, 2.537684304617564, 0.811831745907865};
// static const float32_t filt_notch_a[IIR_FILT_TAPS] = {1.0, 2.803860444771638, 3.571057889147946, 2.271508164463490, 0.659389877319096};

/*
struct iir_filter_t
{
    float32_t input_history[IIR_FILT_TAPS];
    float32_t output_history[IIR_FILT_TAPS];

    float32_t coeff_b[IIR_FILT_TAPS];
    float32_t coeff_a[IIR_FILT_TAPS];

    int filter_cycle;
};

float32_t iir_filt(float32_t x, struct iir_filter_t *filter_instance)
{
    float32_t y = 0;
    int N = IIR_FILT_TAPS;
    int c = filter_instance->filter_cycle;
    filter_instance->input_history[c] = x;

    for (int i = 0; i < N; i++)
    {
        y = y + filter_instance->coeff_b[i] * filter_instance->input_history[(c - i + N) % N];
    }
    for (int i = 1; i < N; i++)
    {
        y = y - filter_instance->coeff_a[i] * filter_instance->output_history[(c - i + N) % N];
    }
    y = y / filter_instance->coeff_a[0];
    filter_instance->output_history[(c % N)] = y;
    filter_instance->filter_cycle = (c + 1) % N;
    return y;
}*/

static uint32_t last_hr_update_time = 0;

void data_thread(void)
{
    struct hpi_ecg_bioz_sensor_data_t ecg_bioz_sensor_sample;
    struct hpi_ppg_wr_data_t ppg_wr_sensor_sample;
    struct hpi_ppg_fi_data_t ppg_fi_sensor_sample;

    static uint32_t hr_zbus_last_pub_time = 0;

    LOG_INF("Data Thread starting");

    for (;;)
    {
        if (k_msgq_get(&q_ecg_bioz_sample, &ecg_bioz_sensor_sample, K_NO_WAIT) == 0)
        {
            /*
            // Stage 1 50 Hz Notch filter
            for(int i = 0; i < BLOCK_SIZE; i++)
            {
                input[i] = (ecg_bioz_sensor_sample.ecg_samples[i])/1000.00;
            }

            arm_fir_f32(&sFIR, input, output, BLOCK_SIZE);

            for(int i = 0; i < BLOCK_SIZE; i++)
            {
                ecg_bioz_sensor_sample.ecg_samples[i] = output[i]*1000.00;
            }
            // End of Stage 1
            */

            if (settings_send_ble_enabled)
            {

                ble_ecg_notify(ecg_bioz_sensor_sample.ecg_samples, ecg_bioz_sensor_sample.ecg_num_samples);
                ble_gsr_notify(ecg_bioz_sensor_sample.ecg_samples, ecg_bioz_sensor_sample.ecg_num_samples);
                // ble_bioz_notify(ecg_bioz_sensor_sample.bioz_sample, ecg_bioz_sensor_sample.bioz_num_samples);
                //  b_notify(ecg_bioz_sensor_sample.bioz_sample);

                /*resp_sample_buffer[resp_sample_buffer_count++] = ecg_bioz_sensor_sample.bioz_sample;
                if (resp_sample_buffer_count >= SAMPLE_BUFF_WATERMARK)
                {
                    ble_bioz_notify(resp_sample_buffer, resp_sample_buffer_count);
                    resp_sample_buffer_count = 0;

                }*/
            }
            if (settings_plot_enabled)
            {
                k_msgq_put(&q_plot_ecg_bioz, &ecg_bioz_sensor_sample, K_NO_WAIT);
            }

            /*uint16_t ecg_hr = ecg_bioz_sensor_sample.hr;
            zbus_chan_pub(&ecg_stat_chan, &ecg_hr, K_SECONDS(1));

            if (ecg_bioz_sensor_sample.rrint == 1)
            {
                LOG_DBG("RRINT !");
            }*/
        }

        if (k_msgq_get(&q_ppg_fi_sample, &ppg_fi_sensor_sample, K_NO_WAIT) == 0)
        {
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

            // LOG_DBG("SpO2: %d | Confidence: %d", ppg_wr_sensor_sample.spo2, ppg_wr_sensor_sample.spo2_confidence);
        }

        k_sleep(K_MSEC(40));
    }
}

#define DATA_THREAD_STACKSIZE 4096
#define DATA_THREAD_PRIORITY 7

K_THREAD_DEFINE(data_thread_id, DATA_THREAD_STACKSIZE, data_thread, NULL, NULL, NULL, DATA_THREAD_PRIORITY, 0, 1000);
