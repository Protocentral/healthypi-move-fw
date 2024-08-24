#include <zephyr/kernel.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <stdio.h>

#include <arm_math.h>

#include "max30001.h"

#include "data_module.h"
#include "hw_module.h"
#include "sampling_module.h"
#include "fs_module.h"
#include "ble_module.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(data_module, CONFIG_SENSOR_LOG_LEVEL);

#include "algos.h"

// ProtoCentral data formats
#define CES_CMDIF_PKT_START_1 0x0A
#define CES_CMDIF_PKT_START_2 0xFA
#define CES_CMDIF_TYPE_DATA 0x02
#define CES_CMDIF_PKT_STOP 0x0B
#define DATA_LEN 22

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

extern struct k_msgq q_ecg_bioz_sample;
extern struct k_msgq q_ppg_sample;
extern struct k_msgq q_plot_ecg_bioz;
extern struct k_msgq q_plot_ppg;
extern struct k_msgq q_plot_hrv;

#define SAMPLING_FREQ 104 // in Hz.

#define LOG_SAMPLE_RATE_SPS 125
#define LOG_WRITE_INTERVAL 10      // Write to file every 10 seconds
#define LOG_BUFFER_LENGTH 1250 + 1 // 125Hz * 10 seconds

#define SAMPLE_BUFF_WATERMARK 8

struct hpi_ecg_bioz_sensor_data_t log_buffer[LOG_BUFFER_LENGTH];

uint16_t current_session_log_counter = 0;
uint16_t current_session_log_id = 0;
char session_id_str[5];

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

void send_data_text(int32_t ecg_sample, int32_t bioz_sample, int32_t raw_red)
{
    char data[100];
    float f_ecg_sample = (float)ecg_sample / 1000;
    float f_bioz_sample = (float)bioz_sample / 1000;
    float f_raw_red = (float)raw_red / 1000;

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

/*
function y = iir_filter(x, filter_instance)
 y = 0;
 N = filter_instance.filter_taps;
 c = filter_instance.filter_cycle;
 filter_instance.input_history[mod(c,N)] = x;
 for i = 0 to N-1
y = y + filter_instance.b[i] * filter_instance.input_history[(mod(c-i+N,N)];
 for i = 1 to N-1
y = y - filter_instance.a[i] * filter_instance.output_history[mod(c-i+N,N)];
 y = y / filter_instance.a[0];
 filter_instance.output_history[mod(c,N)] = y;
 filter_instance.filter_cycle = mod(c+1,N);
 */
#define IIR_FILT_TAPS 5

static const float32_t filt_low_b[IIR_FILT_TAPS] = {0.418163345761899, 0.836326691523798, 0.418163345761899, 0.0, 0.0}; //{ 1.0, 2.803860444771638, 3.571057889147946, 2.271508164463490, 0.659389877319096};
static const float32_t filt_low_a[IIR_FILT_TAPS] = {1.0, 0.462938025291041, 0.209715357756555, 0.0, 0.0};

static const float32_t filt_notch_b[IIR_FILT_TAPS] = {0.811831745907865, 2.537684304617564, 3.606784274651312, 2.537684304617564, 0.811831745907865};
static const float32_t filt_notch_a[IIR_FILT_TAPS] = {1.0, 2.803860444771638, 3.571057889147946, 2.271508164463490, 0.659389877319096};

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
}

void data_thread(void)
{
    

    struct hpi_ecg_bioz_sensor_data_t ecg_bioz_sensor_sample;
    struct hpi_ppg_sensor_data_t ppg_sensor_sample;

    int32_t ecg_sample_buffer[64];
    int sample_buffer_count = 0;

    int16_t ppg_sample_buffer[64];
    int ppg_sample_buffer_count = 0;

    int32_t resp_sample_buffer[64];
    int resp_sample_buffer_count = 0;

    /* Filter coeffcients from Maxim AN6906

    b[] = {b0, b1, b2, b3, b4}
    a[] = {a0, a1, a2, a3, a4}

    CMSIS DSP Library expects coefficient in second order section digital filter manner:

    {b10, b11, b12, a11, a12, b20, b21, b22, a21, a22, ...}

    50Hz notch, 128 samples/sec
    b[] = { 0.811831745907865, 2.537684304617564, 3.606784274651312, 2.537684304617564,0.811831745907865 }
    a[] = { 1.0, 2.803860444771638, 3.571057889147946, 2.271508164463490, 0.659389877319096 }

    */

    // Initialize IIR filter (2nd order, 5 tap, 50 Hz Notch)
    // arm_biquad_casd_df1_inst_f32 iir_filt_inst;
    // arm_biquad_cascade_df2T_instance_f32 iir_filt_inst;
    // arm_biquad_cascade_df1_init_f32(&iir_filt_inst, 2, iir_coeff, iir_state);
    // arm_biquad_cascade_df2T_init_f32(&iir_filt_inst, 2, iir_coeff, iir_state);
    // record_init_session_log();

    static struct iir_filter_t iir_filt_notch_inst;
    static struct iir_filter_t iir_filt_low_inst;

    for (int i = 0; i < IIR_FILT_TAPS; i++)
    {
        iir_filt_notch_inst.input_history[i] = 0;
        iir_filt_notch_inst.output_history[i] = 0;
        iir_filt_notch_inst.coeff_b[i] = filt_notch_b[i];
        iir_filt_notch_inst.coeff_a[i] = filt_notch_a[i];
    }
    iir_filt_notch_inst.filter_cycle = 0;

    for (int i = 0; i < IIR_FILT_TAPS; i++)
    {
        iir_filt_low_inst.input_history[i] = 0;
        iir_filt_low_inst.output_history[i] = 0;
        iir_filt_low_inst.coeff_b[i] = filt_low_b[i];
        iir_filt_low_inst.coeff_a[i] = filt_low_a[i];
    }
    iir_filt_low_inst.filter_cycle = 0;

    float32_t ecg_input = 0;
    float32_t ecg_output = 0;
    float32_t ecg_output2 = 0;

    int32_t hrv_max;
    int32_t hrv_min;
    float hrv_mean;
    float hrv_sdnn;
    float hrv_pnn;
    float hrv_rmssd;
    bool hrv_ready_flag = false;

    struct hpi_computed_hrv_t hrv_calculated;

    LOG_INF("Data Thread starting\n");

    for (;;)
    {
        k_sleep(K_USEC(50));

        if (k_msgq_get(&q_ecg_bioz_sample, &ecg_bioz_sensor_sample, K_NO_WAIT) == 0)
        {
            //ecg_input = (float32_t)(ecg_bioz_sensor_sample.ecg_sample / 100000.0000);
            //ecg_output2 = iir_filt(ecg_input, &iir_filt_notch_inst);
            //ecg_output = iir_filt(ecg_output2, &iir_filt_low_inst);

            // arm_biquad_cascade_df1_f32(&iir_filt_inst, ecg_input, ecg_output, 1);
            // arm_biquad_cascade_df1_f32(&iir_filt_inst, ecg_input, ecg_output, 1);
            int32_t ecg_output_int = (int32_t)(ecg_output * 1000); // ecg_input[0]*1000;
            // printk("ECG: %f, ECG_F: %f, ECGI: %d\n", ecg_input, ecg_output, ecg_output_int);

            //ecg_bioz_sensor_sample.ecg_sample = ecg_input;// ecg_output_int;

            if (settings_send_ble_enabled)
            {

                ecg_sample_buffer[sample_buffer_count++] = ecg_bioz_sensor_sample.ecg_sample; //ecg_output_int; // ecg_bioz_sensor_sample.ecg_sample;
                if (sample_buffer_count >= SAMPLE_BUFF_WATERMARK)
                {
                    ble_ecg_notify(ecg_sample_buffer, sample_buffer_count);
                    sample_buffer_count = 0;
                }

                resp_sample_buffer[resp_sample_buffer_count++] = ecg_bioz_sensor_sample.bioz_sample;
                if (resp_sample_buffer_count >= SAMPLE_BUFF_WATERMARK)
                {
                    ble_resp_notify(resp_sample_buffer, resp_sample_buffer_count);
                    resp_sample_buffer_count = 0;
                }
            }
            if (settings_plot_enabled)
            {
                k_msgq_put(&q_plot_ecg_bioz, &ecg_bioz_sensor_sample, K_NO_WAIT);
            }
        }

        // Check if PPG data is available
        if (k_msgq_get(&q_ppg_sample, &ppg_sensor_sample, K_NO_WAIT) == 0)
        {
            if (settings_send_ble_enabled)
            {
                ppg_sample_buffer[ppg_sample_buffer_count++] = ppg_sensor_sample.raw_green;
                if (ppg_sample_buffer_count >= SAMPLE_BUFF_WATERMARK)
                {
                    ble_ppg_notify(ppg_sample_buffer, ppg_sample_buffer_count);
                    ppg_sample_buffer_count = 0;
                }
            }
            if (settings_plot_enabled)
            {
                k_msgq_put(&q_plot_ppg, &ppg_sensor_sample, K_NO_WAIT);
            }

            if (settings_send_usb_enabled)
            {
                if (settings_data_format == DATA_FMT_OPENVIEW)
                {
                    sendData(ecg_bioz_sensor_sample.ecg_sample, ecg_bioz_sensor_sample.bioz_sample,
                             ppg_sensor_sample.raw_red, ppg_sensor_sample.raw_ir,
                             0, ppg_sensor_sample.hr, ppg_sensor_sample.bpt_status, ppg_sensor_sample.spo2, ecg_bioz_sensor_sample._bioZSkipSample);
                }
                /*else if (settings_data_format == DATA_FMT_PLAIN_TEXT)
                {
                    send_data_text(ecg_bioz_sensor_sample.ecg_sample, ecg_bioz_sensor_sample.bioz_sample, ecg_bioz_sensor_sample.raw_red);
                    // printk("ECG: %d, BIOZ: %d, RED: %d\n", sensor_sample.ecg_sample, sensor_sample.bioz_sample,
                    //        sensor_sample.raw_red);
                }*/
            }

            if (ppg_sensor_sample.rtor != 0)
            {
                calculate_hrv(ppg_sensor_sample.rtor, &hrv_max, &hrv_min, &hrv_mean, &hrv_sdnn, &hrv_pnn, &hrv_rmssd, &hrv_ready_flag);
                if (hrv_ready_flag == true)
                {
                    hrv_calculated.hrv_ready_flag = hrv_ready_flag;
                    hrv_calculated.hrv_max = hrv_max;
                    hrv_calculated.hrv_min = hrv_min;
                    hrv_calculated.mean = hrv_mean;
                    hrv_calculated.sdnn = hrv_sdnn;
                    hrv_calculated.pnn = hrv_pnn;
                    hrv_calculated.rmssd = hrv_rmssd;

                    k_msgq_put(&q_plot_hrv, &hrv_calculated, K_NO_WAIT);
                    // printk("mean: %f, max: %d, min: %d, sdnn: %f, pnn: %f, rmssd:%f\n", hrv_calculated.mean, hrv_calculated.hrv_max, hrv_calculated.hrv_min, hrv_calculated.sdnn, hrv_calculated.pnn, hrv_calculated.rmssd);
                }
            }
        }

        // Data is now available in sensor_sample

        // Send, store or process data here

        // printk("%d\n", sensor_sample.raw_ir);

        /***** Send to USB if enabled *****/

        //

        if (settings_log_data_enabled)
        {
            // log_data(sensor_sample.ecg_sample, sensor_sample.bioz_sample, sensor_sample.raw_red, sensor_sample.raw_ir,
            //          sensor_sample.temp, 0, 0, 0, sensor_sample._bioZSkipSample);
            // record_session_add_point(ecg_bioz_sensor_sample.ecg_sample, ecg_bioz_sensor_sample.bioz_sample);
        }
    }
}

#define DATA_THREAD_STACKSIZE 4096
#define DATA_THREAD_PRIORITY 7

K_THREAD_DEFINE(data_thread_id, DATA_THREAD_STACKSIZE, data_thread, NULL, NULL, NULL, DATA_THREAD_PRIORITY, 0, 1000);
