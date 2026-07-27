/*
 * HealthyPi Move
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Author: Ashwin Whitchurch, Protocentral Electronics
 * Contact: ashwin@protocentral.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <zephyr/kernel.h>
#include "hpi_evt.h"
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <stdio.h>
#include <math.h>
#include <arm_math.h>

#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <time.h>

LOG_MODULE_REGISTER(data_module, LOG_LEVEL_DBG);

#include "max30001.h"

#include "hw_module.h"
#include "hpi_common_types.h"
#include "hpi_dfu.h"
#include "fs_module.h"
#include "ble_module.h"
#include "health/hpi_hs_record.h"
#include "health/hpi_hs_hrv.h"
#include "ui/move_ui.h"
#include "hpi_sys.h"

#include "gsr_algos.h"

#if defined(CONFIG_HPI_GSR_STRESS_INDEX)
ZBUS_CHAN_DECLARE(gsr_stress_chan);
#endif

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

/* ecg_record_buffer removed 2026-07-18 (−15 KB RAM): its samples were written but
 * never read — H-REC streams each batch to the record via hpi_hs_rec_append, and
 * only the counter was consumed. ecg_record_counter stays as the 30 s sample-count
 * target that ends a capture (compared against ECG_RECORD_BUFFER_SAMPLES). */
static volatile uint16_t ecg_record_counter = 0;
/* H-REC: the active ECG capture session (>0 = open). Started when recording
 * begins, streamed to as batches drain, finalized on stop. */
static int s_ecg_rid = 0;
#define HS_ECG_RATE_HZ 128

/* H-REC Stage 2b: BioZ/GSR capture. Streams into a SIG_BIOZ record the same way
 * ECG does. `s_gsr_cancelled` is separate from the SMF's `ecg_cancellation`,
 * which is also set on a *successful* GSR completion and so cannot distinguish
 * "user cancelled" from "buffer full". */
static int s_gsr_rid = 0;
static bool s_gsr_cancelled = false;
#define HS_GSR_RATE_HZ 32

K_MUTEX_DEFINE(mutex_is_ecg_record_active);

static bool is_gsr_record_active = false;
static int32_t gsr_record_buffer[GSR_RECORD_BUFFER_SAMPLES]; // e.g., 32Hz * 30s = 960 samples
static volatile uint16_t gsr_record_counter = 0;
K_MUTEX_DEFINE(mutex_is_gsr_record_active);

/* H-REC: episodic PPG capture (wrist SpO2 spot-check, finger BP/SpO2). Mirrors
 * the ECG/GSR pattern: an open record (rid>0) is streamed to directly from the
 * drained FIFO batch — no extra RAM buffer (RAM is at ~97-99%). raw_green /
 * raw_ir are uint32_t; stored as HPI_HS_SFMT_I32 (4-byte little-endian, byte-
 * identical to the u32 the BLE notify path forwards — there is no U32 sfmt). */
static bool is_ppg_wrist_record_active = false;
static int s_ppg_wrist_rid = 0;
K_MUTEX_DEFINE(mutex_is_ppg_wrist_record_active);
#define HS_PPG_WRIST_RATE_HZ 25   /* MAX32664C wrist SpO2 spot-check nominal rate */

static bool is_ppg_finger_record_active = false;
static int s_ppg_finger_rid = 0;
K_MUTEX_DEFINE(mutex_is_ppg_finger_record_active);
#define HS_PPG_FINGER_RATE_HZ 100 /* MAX32664D finger BP/SpO2 nominal rate */

static int g_last_scr_count = 0;

static bool is_gsr_measurement_active = false;
K_MUTEX_DEFINE(mutex_is_gsr_measurement_active);

static uint32_t last_hr_update_time = 0;

K_MUTEX_DEFINE(mutex_hr_change);

// Externs
extern bool ecg_cancellation;

ZBUS_CHAN_DECLARE(hr_chan);

ZBUS_CHAN_DECLARE(ecg_stat_chan);

extern struct k_msgq q_ecg_sample;
extern struct k_msgq q_bioz_sample;
extern struct k_msgq q_ppg_wrist_sample;
extern struct k_msgq q_ppg_fi_sample;

extern struct k_msgq q_plot_ecg;
extern struct k_msgq q_plot_ppg_wrist;
extern struct k_msgq q_plot_ppg_fi;
extern struct k_msgq q_plot_gsr;

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

void hpi_data_set_ecg_record_active(bool active)
{
    k_mutex_lock(&mutex_is_ecg_record_active, K_FOREVER);
    is_ecg_record_active = active;

    if (active)
    {
        // Starting new recording - reset the sample counter
        ecg_record_counter = 0;
        ecg_cancellation = false;  // reset cancellation flag for new recording session
        // H-REC: open a record session now (captures the real start_ts); batches
        // are streamed in as they drain, so an interrupted session recovers as
        // PARTIAL at next boot rather than being lost.
        s_ecg_rid = hpi_hs_rec_start(HPI_HS_SIG_ECG, HPI_HS_SFMT_I32, 1, HS_ECG_RATE_HZ);
        if (s_ecg_rid <= 0) {
            LOG_ERR("ECG rec_start failed: %d", s_ecg_rid);
            s_ecg_rid = 0;
        } else {
            LOG_INF("ECG recording started - record %d", s_ecg_rid);
        }
    }
    else if (s_ecg_rid > 0)
    {
        // Stopping recording - finalize (data already streamed). Mutex is held,
        // so no new session can start until this one is closed.
        hpi_hs_rec_stop((uint32_t)s_ecg_rid);
        if (ecg_cancellation) {
            // user cancelled / lead-off abort → drop the capture, don't keep it
            hpi_hs_rec_ack((uint32_t)s_ecg_rid);
            LOG_INF("ECG record %d cancelled + dropped", s_ecg_rid);
        } else {
            LOG_INF("ECG record %d stored (%u samples, %.1fs @ %dHz)", s_ecg_rid,
                    ecg_record_counter, (float)ecg_record_counter / (float)HS_ECG_RATE_HZ,
                    HS_ECG_RATE_HZ);
        }
        s_ecg_rid = 0;
    }
    k_mutex_unlock(&mutex_is_ecg_record_active);
}

void hpi_data_set_gsr_record_active(bool active)
{
    k_mutex_lock(&mutex_is_gsr_record_active, K_FOREVER);
    is_gsr_record_active = active;

    if (active)
    {
        // Starting new recording
        gsr_record_counter = 0;
        memset(gsr_record_buffer, 0, sizeof(gsr_record_buffer));
        s_gsr_cancelled = false;
        // H-REC: open a record session now (captures the real start_ts); batches
        // are streamed in as they drain, so an interrupted session recovers as
        // PARTIAL at next boot rather than being lost.
        s_gsr_rid = hpi_hs_rec_start(HPI_HS_SIG_BIOZ, HPI_HS_SFMT_I32, 1, HS_GSR_RATE_HZ);
        if (s_gsr_rid <= 0) {
            LOG_ERR("GSR rec_start failed: %d", s_gsr_rid);
            s_gsr_rid = 0;
        } else {
            LOG_INF("GSR recording started - record %d", s_gsr_rid);
        }
    }
    else
    {
        if (s_gsr_rid > 0)
        {
            // Finalize (data already streamed). Mutex is held, so no new session
            // can start until this one is closed.
            hpi_hs_rec_stop((uint32_t)s_gsr_rid);
            if (s_gsr_cancelled) {
                hpi_hs_rec_ack((uint32_t)s_gsr_rid);   // user cancelled → drop it
                LOG_INF("GSR record %d cancelled + dropped", s_gsr_rid);
            } else {
                LOG_INF("GSR record %d stored (%d samples, %.1fs @ %dHz)", s_gsr_rid,
                        gsr_record_counter, (float)gsr_record_counter / (float)HS_GSR_RATE_HZ,
                        HS_GSR_RATE_HZ);
            }
            s_gsr_rid = 0;
        }

        // Stopping recording - write file synchronously
        if (gsr_record_counter > 0)
        {
            if (gsr_record_counter > GSR_RECORD_BUFFER_SAMPLES) {
                LOG_ERR("GSR counter overflow detected: %d > %d - clamping to max",
                        gsr_record_counter, GSR_RECORD_BUFFER_SAMPLES);
                gsr_record_counter = GSR_RECORD_BUFFER_SAMPLES;
            }

            int64_t log_time = hw_get_synced_system_time();

            LOG_INF("GSR recording stopped - writing %d samples to file (%.1f seconds @ 32Hz)",
                    gsr_record_counter, (float)gsr_record_counter / 32.0f);

            /* record write removed — health store Record tier (H-REC) */
            LOG_INF("GSR file write completed");

            if (!is_gsr_record_active && gsr_record_counter > 0)
            {
                // Calculate duration in seconds
                int duration_sec = gsr_record_counter / 32;  // 32 Hz sample rate
                if (duration_sec < 1) {
                    duration_sec = 1;
                }

#if defined(CONFIG_HPI_GSR_STRESS_INDEX)
                // Calculate comprehensive stress index from buffered samples
                static struct hpi_gsr_stress_index_t stress_data = {0};
                calculate_gsr_stress_index(gsr_record_buffer, gsr_record_counter,
                                           duration_sec, &stress_data);

                if (stress_data.stress_data_ready) {
                    // Publish stress data via ZBus
                    zbus_chan_pub(&gsr_stress_chan, &stress_data, K_NO_WAIT);
                    LOG_INF("GSR stress published: level=%u, tonic=%u.%02u uS, SCR=%u/30s",
                            stress_data.stress_level,
                            stress_data.tonic_level_x100 / 100,
                            stress_data.tonic_level_x100 % 100,
                            stress_data.peaks_per_minute);

                    g_last_scr_count = stress_data.peaks_per_minute;
                    /* persistence removed — health store will ingest this (H1) */
                }
#else
                int scr_count = calculate_scr_count(gsr_record_buffer, gsr_record_counter);
                LOG_INF("SCR count: %d", scr_count);
                g_last_scr_count = scr_count;
                /* persistence removed — health store will ingest this (H1) */
#endif
            }
        }
        else
        {
            LOG_WRN("GSR recording stopped but no samples collected");
        }
    }

    k_mutex_unlock(&mutex_is_gsr_record_active);
}

bool hpi_data_is_gsr_record_active(void)
{
    bool active;
    k_mutex_lock(&mutex_is_gsr_record_active, K_FOREVER);
    active = is_gsr_record_active;
    k_mutex_unlock(&mutex_is_gsr_record_active);
    return active;
}

int hpi_data_get_last_scr_count(void)
{
    return g_last_scr_count;
}

void hpi_data_set_gsr_cancelled(bool cancelled)
{
    k_mutex_lock(&mutex_is_gsr_record_active, K_FOREVER);
    s_gsr_cancelled = cancelled;
    k_mutex_unlock(&mutex_is_gsr_record_active);
}

void hpi_data_reset_gsr_record_buffer(void)
{
    k_mutex_lock(&mutex_is_gsr_record_active, K_FOREVER);
    // Reset buffer and counter without saving (for contact lost / restart)
    gsr_record_counter = 0;
    memset(gsr_record_buffer, 0, sizeof(gsr_record_buffer));

    /* H-REC: appended bytes cannot be un-appended, so a restart needs a fresh
     * session. Drop the partial one and reopen only if the capture is still
     * running (contact lost mid-capture); on the cancel path the SMF has already
     * flagged s_gsr_cancelled, so we leave it closed for set_..._active(false). */
    if (s_gsr_rid > 0) {
        hpi_hs_rec_stop((uint32_t)s_gsr_rid);
        hpi_hs_rec_ack((uint32_t)s_gsr_rid);
        LOG_DBG("GSR record %d discarded (restart)", s_gsr_rid);
        s_gsr_rid = 0;
    }
    if (is_gsr_record_active && !s_gsr_cancelled) {
        s_gsr_rid = hpi_hs_rec_start(HPI_HS_SIG_BIOZ, HPI_HS_SFMT_I32, 1, HS_GSR_RATE_HZ);
        if (s_gsr_rid <= 0) {
            LOG_ERR("GSR rec_start (restart) failed: %d", s_gsr_rid);
            s_gsr_rid = 0;
        }
    }

    LOG_DBG("GSR recording buffer reset");
    k_mutex_unlock(&mutex_is_gsr_record_active);

}

void hpi_data_reset_ecg_record_buffer(void)
{
    k_mutex_lock(&mutex_is_ecg_record_active, K_FOREVER);
    // Reset the sample counter without saving (for lead-off restart)
    ecg_record_counter = 0;
    LOG_INF("ECG recording counter reset (discard incomplete data)");
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

/* Only flip the desired-state flag. The actual rec_start/rec_stop does LittleFS
 * file creation (fs_open CREATE + write + sync), far too deep for the 1024-byte
 * ppg_ctrl_thread that calls this — doing it here overflowed that stack. The
 * FS-capable data_thread reconciles the flag -> record in its loop instead. */
void hpi_data_set_ppg_wrist_record_active(bool active)
{
    k_mutex_lock(&mutex_is_ppg_wrist_record_active, K_FOREVER);
    is_ppg_wrist_record_active = active;
    k_mutex_unlock(&mutex_is_ppg_wrist_record_active);
}

bool hpi_data_is_ppg_wrist_record_active(void)
{
    bool active;
    k_mutex_lock(&mutex_is_ppg_wrist_record_active, K_FOREVER);
    active = is_ppg_wrist_record_active;
    k_mutex_unlock(&mutex_is_ppg_wrist_record_active);
    return active;
}

/* Flag-only, same as the wrist setter — the finger SMF thread must not run the
 * FS-heavy rec_start/rec_stop. The data_thread reconciles it. */
void hpi_data_set_ppg_finger_record_active(bool active)
{
    k_mutex_lock(&mutex_is_ppg_finger_record_active, K_FOREVER);
    is_ppg_finger_record_active = active;
    k_mutex_unlock(&mutex_is_ppg_finger_record_active);
}

bool hpi_data_is_ppg_finger_record_active(void)
{
    bool active;
    k_mutex_lock(&mutex_is_ppg_finger_record_active, K_FOREVER);
    active = is_ppg_finger_record_active;
    k_mutex_unlock(&mutex_is_ppg_finger_record_active);
    return active;
}

void hpi_data_set_gsr_measurement_active(bool active)
{
    k_mutex_lock(&mutex_is_gsr_measurement_active, K_FOREVER);
    is_gsr_measurement_active = active;
    k_mutex_unlock(&mutex_is_gsr_measurement_active);
}

bool hpi_data_is_gsr_measurement_active(void)
{
    bool active = false;
    k_mutex_lock(&mutex_is_gsr_measurement_active, K_FOREVER);
    active = is_gsr_measurement_active;
    k_mutex_unlock(&mutex_is_gsr_measurement_active);
    return active;
}

/* Reconcile a PPG record's desired-active flag with its open/closed state, doing
 * the FS-heavy hpi_hs_rec_start/stop here on the data_thread (which has the stack
 * for LittleFS) rather than on the shallow SMF/control threads that set the flag.
 * Runs every data_thread loop, so a stop is honoured within ~1 ms even after the
 * sample stream ends. rid/mutex are otherwise touched only by this thread. */
static void ppg_record_reconcile(struct k_mutex *m, bool *active, int *rid,
                                 uint8_t sig, uint16_t rate, const char *label)
{
    k_mutex_lock(m, K_FOREVER);
    if (*active && *rid == 0) {
        int r = hpi_hs_rec_start(sig, HPI_HS_SFMT_I32, 1, rate);
        if (r <= 0) {
            LOG_ERR("PPG %s rec_start failed: %d", label, r);
            *rid = 0;
        } else {
            *rid = r;
            LOG_INF("PPG %s recording started - record %d", label, r);
        }
    } else if (!*active && *rid > 0) {
        hpi_hs_rec_stop((uint32_t)*rid);
        LOG_INF("PPG %s record %d stored", label, *rid);
        *rid = 0;
    }
    k_mutex_unlock(m);
}

void data_thread(void)
{
    struct hpi_ecg_bioz_sensor_data_t ecg_sensor_sample;
    struct hpi_ppg_wr_data_t ppg_wr_sensor_sample;
    struct hpi_ppg_fi_data_t ppg_fi_sensor_sample;
    struct hpi_bioz_sample_t bsample;

    static uint32_t hr_zbus_last_pub_time = 0;

    LOG_INF("Data Thread starting");

    for (;;)
    {
        /* DFU quiesce: while a BLE OTA is writing the QSPI-resident secondary
         * slot, keep off /lfs (same die) entirely — drop incoming samples and
         * skip every hpi_hs_rec_append / record open-close below. Records resume
         * naturally when the flag clears (no explicit resume needed). */
        if (hpi_dfu_is_active())
        {
            k_msgq_purge(&q_ecg_sample);
            k_msgq_purge(&q_bioz_sample);
            k_msgq_purge(&q_ppg_wrist_sample);
            k_msgq_purge(&q_ppg_fi_sample);
            k_sleep(K_MSEC(50));
            continue;
        }

        bool processed_data = false;

        /* Open/close PPG records here (FS ops need this thread's stack, not the
         * 1024-byte ppg_ctrl_thread that flips the flag). */
        ppg_record_reconcile(&mutex_is_ppg_wrist_record_active, &is_ppg_wrist_record_active,
                             &s_ppg_wrist_rid, HPI_HS_SIG_PPG_WRIST, HS_PPG_WRIST_RATE_HZ, "wrist");
        ppg_record_reconcile(&mutex_is_ppg_finger_record_active, &is_ppg_finger_record_active,
                             &s_ppg_finger_rid, HPI_HS_SIG_PPG_FINGER, HS_PPG_FINGER_RATE_HZ, "finger");

        // Process all available ECG samples (unchanged)
        if (k_msgq_get(&q_ecg_sample, &ecg_sensor_sample, K_NO_WAIT) == 0)
        {
            processed_data = true;
            if (settings_send_ble_enabled)
            {
                /* ECG only. This used to ALSO push the very same ecg_samples
                 * buffer out of ble_gsr_notify(), i.e. ECG data on the BioZ
                 * characteristic — wrong payload, and it doubled ECG-rate
                 * notification traffic, so a momentarily empty BLE TX pool
                 * dropped ECG batches and the app's live plot went patchy.
                 * The BioZ characteristic has its own correct feed from
                 * q_bioz_sample below. */
                ble_ecg_notify(ecg_sensor_sample.ecg_samples, ecg_sensor_sample.ecg_num_samples);
            }
            if (settings_plot_enabled)
            {
                int ret = k_msgq_put(&q_plot_ecg, &ecg_sensor_sample, K_NO_WAIT);
                if (ret != 0)
                {
                    static uint32_t plot_drops = 0;
                    plot_drops++;
                    if ((plot_drops % 10) == 0)
                    {
                        LOG_WRN("Plot queue full - dropped %u ECG sample batches", plot_drops);
                    }
                }
            }

            

            // ECG recording buffer management with mutex protection
            // Fixed: No circular buffer - linear recording only, stop when full
            // IMPORTANT: Only record samples when leads are connected (not lead-off)
            // This prevents buffer from filling with garbage data when leads are removed

            k_mutex_lock(&mutex_is_ecg_record_active, K_FOREVER);
            /* DEBUG: Removed !ecg_sensor_sample.ecg_lead_off check to record regardless of lead state */
            if (is_ecg_record_active == true)
            {
                int samples_to_copy = ecg_sensor_sample.ecg_num_samples;
                int space_left = ECG_RECORD_BUFFER_SAMPLES - ecg_record_counter;

                /* Buffer already full: batches keep arriving for the ~1 batch period
                 * between us posting EVT_ECG_COMPLETE below and the SMF clearing
                 * is_ecg_record_active. Expected, not an overflow - drop the batch
                 * and re-post (the event is consumed once). */
                if (ecg_record_counter >= ECG_RECORD_BUFFER_SAMPLES) {
                    LOG_DBG("ECG buffer full (%d/%d) - awaiting SMF stop, dropping batch",
                            ecg_record_counter, ECG_RECORD_BUFFER_SAMPLES);
                    k_event_post(&ecg_evt, EVT_ECG_COMPLETE);
                    k_mutex_unlock(&mutex_is_ecg_record_active);
                    continue;  // Skip this sample batch
                }

                if (samples_to_copy <= space_left)
                {
                    ecg_record_counter += samples_to_copy;
                    // H-REC: stream this batch into the open record
                    if (s_ecg_rid > 0) {
                        hpi_hs_rec_append((uint32_t)s_ecg_rid, ecg_sensor_sample.ecg_samples,
                                          (size_t)samples_to_copy * sizeof(int32_t));
                    }
                    
                    // Check if buffer is exactly full
                    if (ecg_record_counter >= ECG_RECORD_BUFFER_SAMPLES)
                    {
                        LOG_INF("ECG buffer full - collected %d samples (30.0 seconds @ 128Hz)", 
                                ecg_record_counter);
                        
                       
                        
                            LOG_INF("Signaling state machine to stop recording");
                            
                            // Signal state machine that buffer is full
                            // State machine will call hpi_data_set_ecg_record_active(false)
                            // which will write the file synchronously
                            k_event_post(&ecg_evt, EVT_ECG_COMPLETE);
                        }
                       
                }
                else
                {
                    // Not enough space - copy what fits and stop
                    if (space_left > 0)
                    {
                        ecg_record_counter += space_left;
                        // H-REC: stream the tail that fit into the open record
                        if (s_ecg_rid > 0) {
                            hpi_hs_rec_append((uint32_t)s_ecg_rid, ecg_sensor_sample.ecg_samples,
                                              (size_t)space_left * sizeof(int32_t));
                        }
                    }
                    
                    LOG_WRN("ECG buffer full mid-batch - collected %d samples, discarded %d", 
                            ecg_record_counter, samples_to_copy - space_left);
                  
                    LOG_INF("Signaling state machine to stop recording");
                        
                     // Signal state machine that buffer is full
                    k_event_post(&ecg_evt, EVT_ECG_COMPLETE);
                    
                          
                }
            
            }
        
            k_mutex_unlock(&mutex_is_ecg_record_active);

        }
        if (k_msgq_get(&q_bioz_sample, &bsample, K_NO_WAIT) == 0)
        {
            processed_data = true;
            if (settings_send_ble_enabled)
            {
                ble_gsr_notify(bsample.bioz_samples, bsample.bioz_num_samples);
            }
            if (settings_plot_enabled)
            {
                int ret = k_msgq_put(&q_plot_gsr, &bsample, K_NO_WAIT);
                if (ret != 0)
                {
                    static uint32_t plot_drops = 0;
                    plot_drops++;
                    if ((plot_drops % 10) == 0)
                    {
                        LOG_WRN("Plot queue full - dropped %u GSR sample batches", plot_drops);
                    }
                }
            }

        k_mutex_lock(&mutex_is_gsr_record_active, K_FOREVER);

       // LOG_DBG("is_gsr_record_active=%d, is_measurement_active=%d, gsr_record_counter=%d",is_gsr_record_active, hpi_data_is_gsr_measurement_active(), gsr_record_counter);
        if (is_gsr_record_active == true)
        {
            int samples_to_copy = bsample.bioz_num_samples;
            int space_left = GSR_RECORD_BUFFER_SAMPLES - gsr_record_counter;

            // Defensive check: prevent overflow
            if (gsr_record_counter >= GSR_RECORD_BUFFER_SAMPLES)
            {
                LOG_ERR("GSR buffer overflow detected");
                k_event_post(&ecg_evt, EVT_GSR_COMPLETE);
                k_mutex_unlock(&mutex_is_gsr_record_active);
                continue;
            }

            if (samples_to_copy <= space_left)
            {
                memcpy(&gsr_record_buffer[gsr_record_counter],
                    bsample.bioz_samples,
                    samples_to_copy * sizeof(int32_t));

                gsr_record_counter += samples_to_copy;
                // H-REC: stream this batch into the open record
                if (s_gsr_rid > 0) {
                    hpi_hs_rec_append((uint32_t)s_gsr_rid, bsample.bioz_samples,
                                      (size_t)samples_to_copy * sizeof(int32_t));
                }

                // Completed exactly full buffer
                if (gsr_record_counter >= GSR_RECORD_BUFFER_SAMPLES)
                {
                    LOG_WRN("GSR buffer full - collected %d samples(30.0 seconds @ 32Hz)", gsr_record_counter);
                    LOG_INF("Signaling GSR state machine to stop recording");
                    
                    is_gsr_record_active = false;   // 🔴 CRITICAL
                    k_event_post(&ecg_evt, EVT_GSR_COMPLETE);
                }
            }
            else
            {
                // Copy what fits
                if (space_left > 0)
                {
                    memcpy(&gsr_record_buffer[gsr_record_counter],
                        bsample.bioz_samples,
                        space_left * sizeof(int32_t));

                    gsr_record_counter += space_left;
                    // H-REC: keep the record in step with the buffer (truncated batch)
                    if (s_gsr_rid > 0) {
                        hpi_hs_rec_append((uint32_t)s_gsr_rid, bsample.bioz_samples,
                                          (size_t)space_left * sizeof(int32_t));
                    }
                }

             //   LOG_WRN("GSR buffer full mid-batch - dropped samples");
                LOG_WRN("GSR buffer full mid-batch - collected %d samples, discarded %d",gsr_record_counter, samples_to_copy - space_left);
                LOG_INF("Signaling GSR state machine to stop recording");

                k_event_post(&ecg_evt, EVT_GSR_COMPLETE);
            }
        }

        k_mutex_unlock(&mutex_is_gsr_record_active);
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
                if (k_msgq_put(&q_plot_ppg_fi, &ppg_fi_sensor_sample, K_NO_WAIT) != 0)
                {
                    static uint32_t plot_drops = 0;
                    if ((++plot_drops % 10) == 0)
                    {
                        LOG_WRN("Plot queue full - dropped %u PPG-finger sample batches", plot_drops);
                    }
                }
            }

            // H-REC: stream raw finger IR PPG into the open record while active.
            // Append directly from the drained batch (no extra copy), mirroring GSR.
            k_mutex_lock(&mutex_is_ppg_finger_record_active, K_FOREVER);
            if (is_ppg_finger_record_active && s_ppg_finger_rid > 0)
            {
                hpi_hs_rec_append((uint32_t)s_ppg_finger_rid, ppg_fi_sensor_sample.raw_ir,
                                  (size_t)ppg_fi_sensor_sample.ppg_num_samples * sizeof(uint32_t));
            }
            k_mutex_unlock(&mutex_is_ppg_finger_record_active);
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
                if (k_msgq_put(&q_plot_ppg_wrist, &ppg_wr_sensor_sample, K_NO_WAIT) != 0)
                {
                    static uint32_t plot_drops = 0;
                    if ((++plot_drops % 10) == 0)
                    {
                        LOG_WRN("Plot queue full - dropped %u PPG-wrist sample batches", plot_drops);
                    }
                }
            }

            // H-REC: stream raw wrist green PPG into the open record while active.
            // Append directly from the drained batch (no extra copy), mirroring GSR.
            k_mutex_lock(&mutex_is_ppg_wrist_record_active, K_FOREVER);
            if (is_ppg_wrist_record_active && s_ppg_wrist_rid > 0)
            {
                hpi_hs_rec_append((uint32_t)s_ppg_wrist_rid, ppg_wr_sensor_sample.raw_green,
                                  (size_t)ppg_wr_sensor_sample.ppg_num_samples * sizeof(uint32_t));
            }
            k_mutex_unlock(&mutex_is_ppg_wrist_record_active);


            if (settings_send_usb_enabled)
            {
            }

            /* HS-2 P3: continuous PPG-derived HRV.
             *
             * The hub hands us an R-R interval with every FIFO sample and we used to
             * drop it on the floor, so HRV/stress needed a manual ECG spot check. The
             * PPG is already running -- R-R comes out of the same FIFO for free.
             *
             * Gate hard: motion destroys pulse-rate variability. `still` comes from the
             * BMI323 any-motion trigger (quiet for CONFIG_HPI_HS_HRV_QUIET_S). The
             * confidence and contact gates live in the HRV module. */
            {
                bool on_skin = (ppg_wr_sensor_sample.scd_state == HPI_PPG_SCD_ON_SKIN);
#if CONFIG_HPI_HS_HRV_QUIET_S > 0
                int64_t quiet_s = (int64_t)k_uptime_seconds() - hpi_hw_get_last_motion_s();
                bool still = (quiet_s >= CONFIG_HPI_HS_HRV_QUIET_S);
#else
                bool still = true;   /* stillness gate disabled */
#endif
                hpi_hs_hrv_feed(ppg_wr_sensor_sample.rtor,
                                ppg_wr_sensor_sample.rtor_confidence,
                                on_skin, still, hw_get_sys_time_ts());
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
        if (processed_data)
        {
            k_yield(); // Give other threads a chance to run
        }
        else
        {
            k_sleep(K_MSEC(1)); // Reduced sleep time to process samples faster
        }
    }
}

#define DATA_THREAD_STACKSIZE 4096
#define DATA_THREAD_PRIORITY 5 // Higher priority to process samples faster

K_THREAD_DEFINE(data_thread_id, DATA_THREAD_STACKSIZE, data_thread, NULL, NULL, NULL, DATA_THREAD_PRIORITY, 0, 1000);
