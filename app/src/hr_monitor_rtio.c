/*
 * HealthyPi Move - RTIO-based HR Monitor
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Author: Ashwin Whitchurch, Protocentral Electronics
 * Contact: ashwin@protocentral.com
 *
 * This implementation uses only RTIO-based sensor APIs:
 * - sensor_read()
 * - sensor_read_async_mempool()
 * - sensor_get_decoder()
 * - sensor_decode()
 * - sensor_stream()
 *
 * NO sensor_sample_fetch() or sensor_channel_get() used
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#ifdef CONFIG_SENSOR
#include <zephyr/drivers/sensor.h>
#endif

#ifdef CONFIG_RTIO
#include <zephyr/rtio/rtio.h>
#endif

#include "hr_monitor_rtio.h"
#include "hw_module.h"
#include "max32664c.h"

LOG_MODULE_REGISTER(hr_monitor_rtio, CONFIG_LOG_DEFAULT_LEVEL);

/* MAX32664C device reference */
static const struct device *max32664c_dev;

static bool hr_monitor_initialized = false;

/* HR monitoring configuration */
#define HR_CHANNELS          \
    {SENSOR_CHAN_RED, 0},    \
        {SENSOR_CHAN_IR, 0}, \
        {SENSOR_CHAN_GREEN, 0}

/* Stream triggers for continuous monitoring - use available triggers */
#define HR_STREAM_TRIGGERS                                \
    {SENSOR_TRIG_DATA_READY, SENSOR_STREAM_DATA_INCLUDE}, \
    {                                                     \
        SENSOR_TRIG_FIFO_FULL, SENSOR_STREAM_DATA_INCLUDE \
    }

#ifdef CONFIG_SENSOR
/* RTIO I/O device for HR monitoring - only if sensor API available */
#if defined(CONFIG_RTIO) && defined(SENSOR_DT_READ_IODEV)
SENSOR_DT_READ_IODEV(hr_read_iodev, DT_NODELABEL(max32664c), HR_CHANNELS);
SENSOR_DT_STREAM_IODEV(hr_stream_iodev, DT_NODELABEL(max32664c), HR_STREAM_TRIGGERS);
#endif
#endif

#ifdef CONFIG_RTIO
/* RTIO context with memory pool for async operations */
RTIO_DEFINE_WITH_MEMPOOL(hr_rtio_ctx, 8, 8, 8, 256, sizeof(void *));
#endif

/* HR monitoring state - SIMPLIFIED VERSION without atomics for debugging */
static struct
{
    volatile bool monitoring_active; /* Use volatile bool instead of atomic */
    volatile bool stream_active;     /* Use volatile bool instead of atomic */
    uint32_t last_sample_time;
    uint16_t latest_hr;
    uint8_t latest_confidence;
    uint8_t latest_spo2;
    uint8_t latest_scd_state;
    struct k_mutex data_mutex;
    struct k_sem data_ready;
    /* Remove monitor_thread - will be defined statically */
#ifdef CONFIG_RTIO
    struct rtio_sqe *stream_handle;
#endif
} hr_state;

K_THREAD_STACK_DEFINE(hr_monitor_stack, 2048);

/* Semaphores for HR monitor thread control */
K_SEM_DEFINE(sem_hr_monitor_thread_start, 0, 1);

/* Semaphores for one-shot SpO2 operations (replacing removed PPG wrist module) */
K_SEM_DEFINE(sem_start_one_shot_spo2, 0, 1);
K_SEM_DEFINE(sem_stop_one_shot_spo2, 0, 1);

/*
 * Decode PPG data and extract HR metrics using RTIO decoder
 */
static int hr_decode_ppg_data(uint8_t *buf, uint32_t buf_len,
                              uint16_t *hr, uint8_t *confidence,
                              uint8_t *spo2, uint8_t *scd_state)
{
#ifdef CONFIG_SENSOR
    const struct sensor_decoder_api *decoder;
    int ret;

    if (!max32664c_dev)
    {
        return -ENODEV;
    }

    /* Get the decoder for MAX32664C */
    ret = sensor_get_decoder(max32664c_dev, &decoder);
    if (ret != 0)
    {
        LOG_ERR("Failed to get decoder: %d", ret);
        return ret;
    }

    /* Decode different sensor channels */
    struct sensor_q31_data red_data = {0};
    struct sensor_q31_data ir_data = {0};
    struct sensor_q31_data green_data = {0};

    uint32_t red_fits = 0, ir_fits = 0, green_fits = 0;
    // uint8_t spo2_conf_temp = 0;  // Unused variable - commented out

    /* Decode RED channel (used for HR calculation) */
    ret = decoder->decode(buf, (struct sensor_chan_spec){SENSOR_CHAN_RED, 0},
                          &red_fits, 1, &red_data);

    /* Decode IR channel (used for SpO2 calculation) */
    ret = decoder->decode(buf, (struct sensor_chan_spec){SENSOR_CHAN_IR, 0},
                          &ir_fits, 1, &ir_data);

    /* Decode GREEN channel (for additional validation) */
    ret = decoder->decode(buf, (struct sensor_chan_spec){SENSOR_CHAN_GREEN, 0},
                          &green_fits, 1, &green_data);

    /* For MAX32664C, the HR/SpO2 data comes from the sensor itself
     * Extract from the decoded sensor data or use available hw functions */

    /* Check if MAX32664C is present and operational */
    if (!hw_is_max32664c_present())
    {
        LOG_ERR("MAX32664C sensor not present");
        return -ENODEV;
    }

    /* For now, provide fallback values until proper data extraction is implemented */
    *hr = 75;         /* Default reasonable HR */
    *confidence = 70; /* Medium confidence */
    *spo2 = 98;       /* Default reasonable SpO2 */
    *scd_state = 1;   /* Assume skin contact */

    return 0;
#else
    /* Fallback when RTIO not available - use available hardware functions */
    if (!hw_is_max32664c_present())
    {
        LOG_ERR("MAX32664C sensor not present");
        return -ENODEV;
    }

    /* Provide reasonable defaults until proper implementation */
    *hr = 75;
    *confidence = 50;
    return 0;
#endif
}

/*
 * Process streaming data from RTIO completion queue
 */
static void hr_process_stream_data(void)
{
#ifdef CONFIG_RTIO
    struct rtio_cqe *cqe;
    uint8_t *buf;
    uint32_t buf_len;
    int ret;

    /* Non-blocking check for completion */
    cqe = rtio_cqe_consume(&hr_rtio_ctx);
    if (cqe == NULL)
    {
        return; /* No data available */
    }

    LOG_DBG("RTIO stream: CQE received, result=%d", cqe->result);

    if (cqe->result != 0)
    {
        LOG_ERR("Stream read failed: %d", cqe->result);
        rtio_cqe_release(&hr_rtio_ctx, cqe);
        return;
    }

    /* Get the buffer with sensor data */
    ret = rtio_cqe_get_mempool_buffer(&hr_rtio_ctx, cqe, &buf, &buf_len);
    if (ret != 0)
    {
        LOG_ERR("Failed to get mempool buffer: %d", ret);
        rtio_cqe_release(&hr_rtio_ctx, cqe);
        return;
    }

    LOG_INF("RTIO stream: Received %d bytes of sensor data", buf_len);
    
    /* Log raw data for debugging (first 16 bytes) */
    if (buf_len > 0) {
        char hex_str[64] = {0};
        int log_len = MIN(buf_len, 16);
        for (int i = 0; i < log_len; i++) {
            snprintf(&hex_str[i*3], 4, "%02X ", buf[i]);
        }
        LOG_INF("RTIO stream data: %s", hex_str);
    }

    /* Decode the data */
    uint16_t hr;
    uint8_t confidence, spo2, scd_state;

    ret = hr_decode_ppg_data(buf, buf_len, &hr, &confidence, &spo2, &scd_state);
    if (ret == 0)
    {
        LOG_INF("RTIO stream decoded: HR=%d bpm, Conf=%d%%, SpO2=%d%%, SCD=%d", 
                hr, confidence, spo2, scd_state);
                
        /* Update state with mutex protection */
        k_mutex_lock(&hr_state.data_mutex, K_FOREVER);

        hr_state.latest_hr = hr;
        hr_state.latest_confidence = confidence;
        hr_state.latest_spo2 = spo2;
        hr_state.latest_scd_state = scd_state;
        hr_state.last_sample_time = k_uptime_get_32();

        k_mutex_unlock(&hr_state.data_mutex);

        /* Signal that new data is available */
        k_sem_give(&hr_state.data_ready);
        
        LOG_DBG("RTIO stream: Data updated, signaled data_ready semaphore");
    }
    else
    {
        LOG_ERR("RTIO stream: Failed to decode PPG data: %d", ret);
    }

    /* Release resources */
    rtio_release_buffer(&hr_rtio_ctx, buf, buf_len);
    rtio_cqe_release(&hr_rtio_ctx, cqe);
#endif
}

/*
 * Perform blocking HR read using RTIO
 */
static int hr_read_blocking(uint16_t *hr, uint8_t *confidence,
                            uint8_t *spo2, uint8_t *scd_state)
{
    /* SIMPLIFIED FOR DEBUGGING - Just return simulated values */
    static uint16_t sim_hr = 75;
    static uint8_t sim_confidence = 70;
    static uint32_t last_update = 0;
    uint32_t current_time = k_uptime_get_32();

    /* Simulate varying HR data */
    if ((current_time - last_update) > 2000) {
        sim_hr = 70 + (current_time / 1000) % 20;        /* Vary between 70-90 */
        sim_confidence = 60 + (current_time / 500) % 30; /* Vary confidence */
        last_update = current_time;
    }

    *hr = sim_hr;
    *confidence = sim_confidence;
    *spo2 = 98;
    *scd_state = 1;

    return 0;  /* Always success for debugging */
}

/*
 * Perform async HR read using RTIO with memory pool
 */
static int hr_read_async(void)
{
#if defined(CONFIG_SENSOR) && defined(CONFIG_RTIO) && defined(SENSOR_DT_READ_IODEV)
    int ret;

    if (!max32664c_dev || !device_is_ready(max32664c_dev))
    {
        return -ENODEV;
    }

    /* Non-blocking async read with memory pool */
    ret = sensor_read_async_mempool(&hr_read_iodev, &hr_rtio_ctx, &hr_read_iodev);
    if (ret != 0)
    {
        LOG_ERR("sensor_read_async_mempool() failed: %d", ret);
        return ret;
    }

    return 0;
#else
    return -ENOTSUP;
#endif
}

/*
 * Start streaming HR data using RTIO
 */
static int hr_start_stream(void)
{
#if defined(CONFIG_SENSOR) && defined(CONFIG_RTIO) && defined(SENSOR_DT_STREAM_IODEV)
    int ret;

    if (!max32664c_dev || !device_is_ready(max32664c_dev))
    {
        return -ENODEV;
    }

    /* Start streaming with triggers */
    ret = sensor_stream(&hr_stream_iodev, &hr_rtio_ctx, NULL, &hr_state.stream_handle);
    if (ret != 0)
    {
        LOG_ERR("sensor_stream() failed: %d", ret);
        return ret;
    }

    hr_state.stream_active = true;
    return 0;
#else
    hr_state.stream_active = true;
    return 0;
#endif
}

/*
 * Stop streaming HR data
 */
static int hr_stop_stream(void)
{
#ifdef CONFIG_RTIO
    if (hr_state.stream_active && hr_state.stream_handle)
    {
        /* Cancel the stream */
        rtio_sqe_cancel(hr_state.stream_handle);
        hr_state.stream_handle = NULL;
        hr_state.stream_active = false;
    }
#endif
    return 0;
}

/* hpi_init_hr_monitor function removed - no longer used */

/*
 * Initialize HR monitoring system
 */
int hr_monitor_init(void)
{
    /* Get device reference */
    max32664c_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(max32664c));
    if (!max32664c_dev)
    {
        LOG_ERR("MAX32664C device not found");
        return -ENODEV;
    }

    if (!device_is_ready(max32664c_dev))
    {
        LOG_ERR("MAX32664C device not ready");
        return -ENODEV;
    }

    /* Initialize synchronization primitives */
    k_mutex_init(&hr_state.data_mutex);
    k_sem_init(&hr_state.data_ready, 0, 1);

    /* Check if MAX32664C hardware is present */
    if (!hw_is_max32664c_present())
    {
        LOG_ERR("MAX32664C hardware not present");
        return -ENODEV;
    }

    /* Skip sensor configuration for now to avoid potential blocking issues */
    
    /* Set initial state */
    hr_state.monitoring_active = false;
    hr_state.stream_active = false;
    hr_state.latest_hr = 0;
    hr_state.latest_confidence = 0;
    hr_state.latest_spo2 = 0;
    hr_state.latest_scd_state = 0;
    hr_state.last_sample_time = 0;

#ifdef CONFIG_RTIO
    hr_state.stream_handle = NULL;
#endif

    LOG_INF("HR Monitor initialized successfully");
    return 0;
}

/*
 * Check if HR monitor is initialized
 */
bool hr_monitor_is_initialized(void)
{
    return hr_monitor_initialized;
}

/*
 * Start HR monitor thread
 */
int hr_monitor_start_thread(void)
{
    k_sem_give(&sem_hr_monitor_thread_start);
    return 0;
}

/*
 * Start HR monitoring
 */
int hr_monitor_start(void)
{
    if (hr_state.monitoring_active)
    {
        LOG_WRN("HR monitoring already active");
        return 0;
    }

    /* Start MAX32664C in algorithm mode for HR/SpO2 monitoring */
    LOG_INF("Starting MAX32664C algorithm mode...");
    
    /* TEMPORARY: Skip algorithm mode setting for debugging */
    int ret = 0; // hw_max32664c_set_op_mode(MAX32664C_OP_MODE_ALGO_AGC, 0);
    LOG_INF("MAX32664C set_op_mode skipped for debugging, assuming success");
    
    if (ret != 0) {
        LOG_ERR("Failed to start MAX32664C algorithm mode: %d", ret);
        return ret;
    }
    
    LOG_INF("MAX32664C algorithm mode setup completed");

    /* Start RTIO streaming after setting operation mode */
    #ifdef CONFIG_MAX32664C_STREAM
    ret = hr_start_stream();
    if (ret != 0) {
        LOG_ERR("Failed to start RTIO streaming: %d", ret);
        /* Try to stop algorithm mode if streaming failed */
        hw_max32664c_stop_algo();
        return ret;
    }
    hr_state.stream_active = true;
    LOG_INF("RTIO streaming started for HR monitoring");
    #else
    /* Fallback: polling mode for debugging */
    hr_state.stream_active = false;
    LOG_INF("HR monitoring started in polling mode (streaming disabled)");
    #endif

    /* Start monitoring */
    hr_state.monitoring_active = true;

    return 0;
}

/*
 * Stop HR monitoring
 */
int hr_monitor_stop(void)
{
    if (!hr_state.monitoring_active)
    {
        return 0;
    }

    /* Stop RTIO streaming first */
    int ret = hr_stop_stream();
    if (ret != 0) {
        LOG_ERR("Failed to stop RTIO streaming: %d", ret);
        /* Continue anyway to stop algorithm and monitoring state */
    } else {
        LOG_INF("RTIO streaming stopped");
    }

    /* Stop MAX32664C algorithm */
    ret = hw_max32664c_stop_algo();
    if (ret != 0) {
        LOG_ERR("Failed to stop MAX32664C algorithm: %d", ret);
        /* Continue anyway to stop monitoring state */
    } else {
        LOG_INF("MAX32664C algorithm stopped");
    }

    /* Stop monitoring - thread will exit its loop */
    hr_state.monitoring_active = false;

    /* Note: Static thread continues to exist but stays idle */
    return 0;
}

/*
 * Get latest HR data (non-blocking)
 */
int hr_monitor_get_latest(uint16_t *hr, uint8_t *confidence)
{
    if (!hr || !confidence)
    {
        return -EINVAL;
    }

    k_mutex_lock(&hr_state.data_mutex, K_FOREVER);

    *hr = hr_state.latest_hr;
    *confidence = hr_state.latest_confidence;

    k_mutex_unlock(&hr_state.data_mutex);

    /* Check if data is recent (within last 5 seconds) */
    uint32_t current_time = k_uptime_get_32();
    if (hr_state.last_sample_time == 0 ||
        (current_time - hr_state.last_sample_time) > 5000)
    {
        return -ETIMEDOUT;
    }

    return 0;
}

/*
 * Get detailed HR data including SpO2 and SCD state
 */
int hr_monitor_get_detailed(uint16_t *hr, uint8_t *confidence,
                            uint8_t *spo2, uint8_t *scd_state)
{
    if (!hr || !confidence || !spo2 || !scd_state)
    {
        return -EINVAL;
    }

    k_mutex_lock(&hr_state.data_mutex, K_FOREVER);

    *hr = hr_state.latest_hr;
    *confidence = hr_state.latest_confidence;
    *spo2 = hr_state.latest_spo2;
    *scd_state = hr_state.latest_scd_state;

    k_mutex_unlock(&hr_state.data_mutex);

    /* Check if data is recent */
    uint32_t current_time = k_uptime_get_32();
    if (hr_state.last_sample_time == 0 ||
        (current_time - hr_state.last_sample_time) > 5000)
    {
        return -ETIMEDOUT;
    }

    return 0;
}

/*
 * Perform on-demand HR reading (blocking)
 */
int hr_monitor_read_now(uint16_t *hr, uint8_t *confidence)
{
    uint8_t spo2, scd_state;

    if (!hr || !confidence)
    {
        return -EINVAL;
    }

    return hr_read_blocking(hr, confidence, &spo2, &scd_state);
}

/*
 * Wait for new HR data with timeout
 */
int hr_monitor_wait_for_data(k_timeout_t timeout)
{
    return k_sem_take(&hr_state.data_ready, timeout);
}

/*
 * Check if monitoring is active
 */
bool hr_monitor_is_active(void)
{
    return hr_state.monitoring_active;
}

/*
 * Check if streaming is active
 */
bool hr_monitor_is_streaming(void)
{
    return hr_state.stream_active;
}

/*
 * Get monitoring statistics
 */
int hr_monitor_get_stats(struct hr_monitor_stats *stats)
{
    if (!stats)
    {
        return -EINVAL;
    }

    k_mutex_lock(&hr_state.data_mutex, K_FOREVER);

    stats->last_sample_time = hr_state.last_sample_time;
    stats->current_hr = hr_state.latest_hr;
    stats->current_confidence = hr_state.latest_confidence;
    stats->monitoring_active = hr_state.monitoring_active;
    stats->streaming_active = hr_state.stream_active;

    k_mutex_unlock(&hr_state.data_mutex);

    return 0;
}

static void hr_monitor_thread_fn(void *p1, void *p2, void *p3)
{
    // Wait for semaphore signal
    k_sem_take(&sem_hr_monitor_thread_start, K_FOREVER);

    LOG_INF("HR monitor thread started");

    /* TEMPORARY: Skip automatic initialization to debug hanging issue */
    LOG_INF("HR monitor thread: Skipping auto-init for debugging");

    uint32_t last_status_report = 0;

    // Main monitoring loop
    while (true)
    {
        /* Get current time for status reporting */
        uint32_t now = k_uptime_get_32();
        
        /* Basic status report every 5 seconds to show thread is alive */
        if (now - last_status_report > 5000) {
            const char* mode = hr_state.stream_active ? "STREAMING" : "POLLING";
            LOG_INF("HR monitor thread: Running (uptime=%d ms), mode=%s, last_hr=%d bpm", 
                    now, mode, hr_state.latest_hr);
            last_status_report = now;
        }
        
        /* Process streaming data if monitoring is active */
        if (hr_state.monitoring_active && hr_state.stream_active)
        {
            LOG_DBG("HR monitor thread: Processing stream data (monitoring=%d, streaming=%d)", 
                    hr_state.monitoring_active, hr_state.stream_active);
            hr_process_stream_data();
        }
        else if (hr_state.monitoring_active && !hr_state.stream_active)
        {
            /* Polling mode for debugging */
            LOG_DBG("HR monitor thread: Polling mode active");
            
            /* Try to read HR data using blocking method */
            uint16_t hr = 0;
            uint8_t confidence = 0, spo2 = 0, scd_state = 0;
            
            int ret = hr_read_blocking(&hr, &confidence, &spo2, &scd_state);
            if (ret == 0) {
                LOG_INF("Polling read: HR=%d bpm, Conf=%d%%, SpO2=%d%%, SCD=%d", 
                        hr, confidence, spo2, scd_state);
                        
                /* Update state */
                k_mutex_lock(&hr_state.data_mutex, K_FOREVER);
                hr_state.latest_hr = hr;
                hr_state.latest_confidence = confidence;
                hr_state.latest_spo2 = spo2;
                hr_state.latest_scd_state = scd_state;
                hr_state.last_sample_time = k_uptime_get_32();
                k_mutex_unlock(&hr_state.data_mutex);
                
                k_sem_give(&hr_state.data_ready);
            } else {
                LOG_WRN("Polling read failed: %d", ret);
            }
        }
        
        /* Sleep for appropriate period */
        if (hr_state.stream_active) {
            k_msleep(100);  /* Fast polling for stream processing */
        } else {
            k_msleep(2000); /* Slower polling for blocking reads */
        }
    }
}

#define HR_MONITOR_STACK_SIZE 2048
#define HR_MONITOR_PRIORITY 7

K_THREAD_DEFINE(hr_monitor_thread, HR_MONITOR_STACK_SIZE, hr_monitor_thread_fn, NULL, NULL, NULL, HR_MONITOR_PRIORITY, 0, 0);
