#include <zephyr/kernel.h>
#include <zephyr/smf.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/drivers/sensor.h>
#include <string.h>

#include <time.h>
#include <zephyr/posix/time.h>
#include <zephyr/sys/timeutil.h>

#include "max30001.h"
#include "hpi_common_types.h"
#include "hw_module.h"
#include "ui/move_ui.h"
#include "hpi_sys.h"
#include "hpi_user_settings_api.h"

LOG_MODULE_REGISTER(smf_ecg_bioz, LOG_LEVEL_DBG);

SENSOR_DT_READ_IODEV(max30001_iodev, DT_ALIAS(max30001), SENSOR_CHAN_VOLTAGE);

K_MSGQ_DEFINE(q_ecg_bioz_sample, sizeof(struct hpi_ecg_bioz_sensor_data_t), 64, 1);  // Reduced from 128 to 64

K_SEM_DEFINE(sem_ecg_start, 0, 1);
K_SEM_DEFINE(sem_ecg_lon, 0, 1);
K_SEM_DEFINE(sem_ecg_loff, 0, 1);
K_SEM_DEFINE(sem_ecg_cancel, 0, 1);

K_SEM_DEFINE(sem_ecg_lead_on, 0, 1);
K_SEM_DEFINE(sem_ecg_lead_off, 0, 1);

K_SEM_DEFINE(sem_ecg_lead_on_local, 0, 1);
K_SEM_DEFINE(sem_ecg_lead_off_local, 0, 1);

// Mutex for protecting shared state variables
K_MUTEX_DEFINE(ecg_state_mutex);

// Mutex for protecting timer and countdown variables
K_MUTEX_DEFINE(ecg_timer_mutex);

// Mutex for ECG smoothing filter (always defined for simplicity)
K_MUTEX_DEFINE(ecg_filter_mutex);

ZBUS_CHAN_DECLARE(ecg_stat_chan);
ZBUS_CHAN_DECLARE(ecg_lead_on_off_chan);

#define ECG_SAMPLING_INTERVAL_MS 125
#define ECG_RECORD_DURATION_S 30
#define ECG_STABILIZATION_DURATION_S 5  // Wait 5 seconds for signal to stabilize

// Define maximum sample limits for validation
#define MAX_ECG_SAMPLES 32
#define MAX_BIOZ_SAMPLES 32
#define ECG_STATS_LOG_INTERVAL_SAMPLES 2000
#define ECG_DEBUG_LOG_INTERVAL_SAMPLES 500

// ECG Signal Smoothing Configuration
#define ECG_FILTER_WINDOW_SIZE 5  // Moving average window size
#define ECG_FILTER_BUFFER_SIZE ECG_FILTER_WINDOW_SIZE

// Allow runtime configuration of smoothing (optional)
#ifndef CONFIG_ECG_SMOOTHING_ENABLED
#define CONFIG_ECG_SMOOTHING_ENABLED 1  // Enable smoothing by default
#endif

#if CONFIG_ECG_SMOOTHING_ENABLED
// ECG smoothing filter state
static int32_t ecg_filter_buffer[ECG_FILTER_BUFFER_SIZE];
static uint8_t ecg_filter_index = 0;
static bool ecg_filter_initialized = false;

/**
 * @brief Apply moving average filter to smooth ECG sample
 * @param raw_sample Raw ECG sample value
 * @return Smoothed ECG sample value
 */
static int32_t ecg_smooth_sample(int32_t raw_sample)
{
    k_mutex_lock(&ecg_filter_mutex, K_FOREVER);
    
    // Add new sample to circular buffer
    ecg_filter_buffer[ecg_filter_index] = raw_sample;
    ecg_filter_index = (ecg_filter_index + 1) % ECG_FILTER_BUFFER_SIZE;
    
    // Calculate moving average
    int64_t sum = 0;
    uint8_t valid_samples = 0;
    
    if (!ecg_filter_initialized) {
        // During initialization, use only available samples
        for (int i = 0; i <= ecg_filter_index; i++) {
            sum += ecg_filter_buffer[i];
            valid_samples++;
        }
        
        if (ecg_filter_index == ECG_FILTER_BUFFER_SIZE - 1) {
            ecg_filter_initialized = true;
        }
    } else {
        // Use full window
        for (int i = 0; i < ECG_FILTER_BUFFER_SIZE; i++) {
            sum += ecg_filter_buffer[i];
        }
        valid_samples = ECG_FILTER_BUFFER_SIZE;
    }
    
    int32_t smoothed_sample = (int32_t)(sum / valid_samples);
    
    k_mutex_unlock(&ecg_filter_mutex);
    return smoothed_sample;
}

/**
 * @brief Reset ECG smoothing filter state
 */
static void ecg_smooth_reset(void)
{
    k_mutex_lock(&ecg_filter_mutex, K_FOREVER);
    memset(ecg_filter_buffer, 0, sizeof(ecg_filter_buffer));
    ecg_filter_index = 0;
    ecg_filter_initialized = false;
    k_mutex_unlock(&ecg_filter_mutex);
}
#else
// ECG smoothing disabled - no-op functions
static int32_t ecg_smooth_sample(int32_t raw_sample)
{
    return raw_sample;
}

static void ecg_smooth_reset(void)
{
    // No-op when smoothing is disabled
}
#endif

static int ecg_last_timer_val = 0;
static int ecg_countdown_val = 0;
static int ecg_stabilization_countdown = 0;
static bool ecg_stabilization_complete = false;

static const struct smf_state ecg_bioz_states[];
struct s_ecg_bioz_object
{
    struct smf_ctx ctx;
} s_ecg_bioz_obj;

enum ecg_bioz_state
{
    HPI_ECG_BIOZ_STATE_IDLE,
    HPI_ECG_BIOZ_STATE_STABILIZING,
    HPI_ECG_BIOZ_STATE_STREAM,
    HPI_ECG_BIOZ_STATE_LEADOFF,
    HPI_ECG_BIOZ_STATE_COMPLETE,
};

RTIO_DEFINE(max30001_read_rtio_poll_ctx, 1, 1);

static bool ecg_active = false;
static bool m_ecg_lead_on_off = true;

static uint16_t m_ecg_hr = 0;

static int hw_max30001_ecg_enable(void);
static int hw_max30001_ecg_disable(void);

// EXTERNS
extern const struct device *const max30001_dev;
extern struct k_sem sem_ecg_bioz_smf_start;
extern struct k_sem sem_ecg_bioz_sm_start;
extern struct k_sem sem_ecg_complete;
extern struct k_sem sem_ecg_complete_reset;

/**
 * @brief Configure ECG leads based on hand worn setting
 * @return 0 on success, negative error code on failure
 */
static int hw_max30001_configure_leads(void)
{
    struct sensor_value lead_config;
    uint8_t hand_worn = hpi_user_settings_get_hand_worn();
    
    // Set lead configuration: 0 = Left hand, 1 = Right hand
    lead_config.val1 = hand_worn;
    lead_config.val2 = 0;
    
    int ret = sensor_attr_set(max30001_dev, SENSOR_CHAN_ALL, MAX30001_ATTR_LEAD_CONFIG, &lead_config);
    if (ret != 0) {
        LOG_ERR("Failed to configure ECG leads for %s hand: %d", 
                hand_worn ? "right" : "left", ret);
        return ret;
    }
    
    LOG_INF("ECG leads configured for %s hand", hand_worn ? "right" : "left");
    return 0;
}

/**
 * @brief Reconfigure ECG leads when hand worn setting changes
 * This function can be called from UI when the hand worn setting is changed
 */
void reconfigure_ecg_leads_for_hand_worn(void)
{
    // Only reconfigure if ECG is currently active
    if (ecg_active && max30001_dev != NULL) {
        hw_max30001_configure_leads();
    }
}



// Thread-safe accessors for shared variables
static bool get_ecg_active(void)
{
    k_mutex_lock(&ecg_state_mutex, K_FOREVER);
    bool active = ecg_active;
    k_mutex_unlock(&ecg_state_mutex);
    return active;
}

static void set_ecg_active(bool active)
{
    k_mutex_lock(&ecg_state_mutex, K_FOREVER);
    ecg_active = active;
    k_mutex_unlock(&ecg_state_mutex);
}

static bool get_ecg_lead_on_off(void)
{
    k_mutex_lock(&ecg_state_mutex, K_FOREVER);
    bool lead_state = m_ecg_lead_on_off;
    k_mutex_unlock(&ecg_state_mutex);
    return lead_state;
}

static void set_ecg_lead_on_off(bool state)
{
    k_mutex_lock(&ecg_state_mutex, K_FOREVER);
    m_ecg_lead_on_off = state;
    k_mutex_unlock(&ecg_state_mutex);
}

static uint16_t get_ecg_hr(void)
{
    k_mutex_lock(&ecg_state_mutex, K_FOREVER);
    uint16_t hr = m_ecg_hr;
    k_mutex_unlock(&ecg_state_mutex);
    return hr;
}

static void set_ecg_hr(uint16_t hr)
{
    k_mutex_lock(&ecg_state_mutex, K_FOREVER);
    m_ecg_hr = hr;
    k_mutex_unlock(&ecg_state_mutex);
}

// Thread-safe accessors for timer variables
static void set_ecg_timer_values(uint32_t last_timer, int countdown)
{
    k_mutex_lock(&ecg_timer_mutex, K_FOREVER);
    ecg_last_timer_val = last_timer;
    ecg_countdown_val = countdown;
    k_mutex_unlock(&ecg_timer_mutex);
}

static void get_ecg_timer_values(uint32_t *last_timer, int *countdown)
{
    k_mutex_lock(&ecg_timer_mutex, K_FOREVER);
    if (last_timer) *last_timer = ecg_last_timer_val;
    if (countdown) *countdown = ecg_countdown_val;
    k_mutex_unlock(&ecg_timer_mutex);
}

static void set_ecg_stabilization_values(int stabilization_countdown, bool complete)
{
    k_mutex_lock(&ecg_timer_mutex, K_FOREVER);
    ecg_stabilization_countdown = stabilization_countdown;
    ecg_stabilization_complete = complete;
    k_mutex_unlock(&ecg_timer_mutex);
}

static void get_ecg_stabilization_values(int *stabilization_countdown, bool *complete)
{
    k_mutex_lock(&ecg_timer_mutex, K_FOREVER);
    if (stabilization_countdown) *stabilization_countdown = ecg_stabilization_countdown;
    if (complete) *complete = ecg_stabilization_complete;
    k_mutex_unlock(&ecg_timer_mutex);
}

static void work_ecg_lon_handler(struct k_work *work)
{
    LOG_DBG("ECG LON Work");
    // Don't disable/enable ECG during recording to avoid sample loss
    // Just log the lead-on event
    LOG_INF("ECG leads connected");
}
K_WORK_DEFINE(work_ecg_lon, work_ecg_lon_handler);

static void work_ecg_loff_handler(struct k_work *work)
{
    LOG_DBG("ECG LOFF Work");
}
K_WORK_DEFINE(work_ecg_loff, work_ecg_loff_handler);

static void sensor_ecg_bioz_process_decode(uint8_t *buf, uint32_t buf_len)
{
    // Input validation
    if (!buf || buf_len < sizeof(struct max30001_encoded_data)) {
        LOG_ERR("Invalid buffer parameters: buf=%p, len=%u, required=%u", 
                buf, buf_len, (uint32_t)sizeof(struct max30001_encoded_data));
        return;
    }

    const struct max30001_encoded_data *edata = (const struct max30001_encoded_data *)buf;
    struct hpi_ecg_bioz_sensor_data_t ecg_bioz_sensor_sample;

    uint8_t ecg_num_samples = edata->num_samples_ecg;
    uint8_t bioz_samples = edata->num_samples_bioz;

    // Validate sample counts to prevent buffer overflows
    if (ecg_num_samples > MAX_ECG_SAMPLES || bioz_samples > MAX_BIOZ_SAMPLES) {
        LOG_ERR("Sample count exceeds limits: ECG=%u (max %u), BioZ=%u (max %u)", 
                ecg_num_samples, MAX_ECG_SAMPLES, bioz_samples, MAX_BIOZ_SAMPLES);
        return;
    }

    // printk("ECG NS: %d ", ecg_samples);
    // printk("BioZ NS: %d ", bioz_samples);

    if ((ecg_num_samples < MAX_ECG_SAMPLES && ecg_num_samples > 0) || (bioz_samples < MAX_BIOZ_SAMPLES && bioz_samples > 0))
    {
        // Log sample counts for debugging
        static uint32_t total_ecg_samples_processed = 0;
        total_ecg_samples_processed += ecg_num_samples;
        
        if ((total_ecg_samples_processed % ECG_DEBUG_LOG_INTERVAL_SAMPLES) == 0) {
            LOG_DBG("ECG samples processed: %u (current batch: %u)", total_ecg_samples_processed, ecg_num_samples);
        }

        ecg_bioz_sensor_sample.ecg_num_samples = edata->num_samples_ecg;
        ecg_bioz_sensor_sample.bioz_num_samples = edata->num_samples_bioz;

        // Apply smoothing filter to ECG samples if enabled
        for (int i = 0; i < edata->num_samples_ecg; i++)
        {
            int32_t raw_sample = edata->ecg_samples[i];
            ecg_bioz_sensor_sample.ecg_samples[i] = ecg_smooth_sample(raw_sample);
        }

        for (int i = 0; i < edata->num_samples_bioz; i++)
        {
            ecg_bioz_sensor_sample.bioz_sample[i] = edata->bioz_samples[i];
        }

        ecg_bioz_sensor_sample.hr = edata->hr;
        ecg_bioz_sensor_sample.rtor = edata->rri;

        set_ecg_hr(edata->hr);
        // ecg_bioz_sensor_sample.rrint = edata->rri;

        // LOG_DBG("RRI: %d", edata->rri);

        ecg_bioz_sensor_sample.ecg_lead_off = edata->ecg_lead_off;

        // Thread-safe lead detection logic
        bool current_lead_state = get_ecg_lead_on_off();
        if (edata->ecg_lead_off == 1 && current_lead_state == false)
        {
            set_ecg_lead_on_off(true);
            LOG_DBG("ECG LOFF");
            k_work_submit(&work_ecg_loff);

            // Only transition to leadoff state if actively recording (not during stabilization)
            if (hpi_data_is_ecg_record_active()) {
                // k_sem_give(&sem_ecg_lead_off);
                // smf_set_state(SMF_CTX(&s_ecg_bioz_obj), &ecg_bioz_states[HPI_ECG_BIOZ_STATE_LEADOFF]);
            }
        }
        else if (edata->ecg_lead_off == 0 && current_lead_state == true)
        {
            set_ecg_lead_on_off(false);
            LOG_DBG("ECG LON");
            k_work_submit(&work_ecg_lon);
            // k_sem_give(&sem_ecg_lead_on);
            // k_sem_give(&sem_ecg_lead_on_local);
            //  smf_set_state(SMF_CTX(&s_ecg_bioz_obj), &ecg_bioz_states[HPI_ECG_BIOZ_STATE_STREAM]);
        }

        if (get_ecg_active())
        {
            static uint32_t total_samples_queued = 0;
            static uint32_t total_samples_dropped = 0;
            
            int ret = k_msgq_put(&q_ecg_bioz_sample, &ecg_bioz_sensor_sample, K_NO_WAIT);
            if (ret != 0) {
                total_samples_dropped += ecg_num_samples;
                LOG_WRN("ECG sample dropped - queue full (ret=%d), dropped: %u, queued: %u", 
                        ret, total_samples_dropped, total_samples_queued);
            } else {
                total_samples_queued += ecg_num_samples;
            }
            
            // Log statistics every interval
            if (((total_samples_queued + total_samples_dropped) % ECG_STATS_LOG_INTERVAL_SAMPLES) == 0) {
                uint32_t total_samples = total_samples_queued + total_samples_dropped;
                if (total_samples > 0) {  // Prevent division by zero
                    LOG_INF("ECG sample stats - Queued: %u, Dropped: %u, Drop rate: %u%%", 
                            total_samples_queued, total_samples_dropped,
                            (total_samples_dropped * 100) / total_samples);
                }
            }
        }
    }
}

static void work_ecg_sample_handler(struct k_work *work)
{
    uint8_t ecg_bioz_buf[512];
    int ret;
    
    ret = sensor_read(&max30001_iodev, &max30001_read_rtio_poll_ctx, ecg_bioz_buf, sizeof(ecg_bioz_buf));
    if (ret < 0) {
        LOG_ERR("Error reading sensor data: %d", ret);
        return;
    }
    sensor_ecg_bioz_process_decode(ecg_bioz_buf, sizeof(ecg_bioz_buf));
}

K_WORK_DEFINE(work_ecg_sample, work_ecg_sample_handler);

static void ecg_bioz_sampling_handler(struct k_timer *dummy)
{
    k_work_submit(&work_ecg_sample);
}

K_TIMER_DEFINE(tmr_ecg_bioz_sampling, ecg_bioz_sampling_handler, NULL);

static int hw_max30001_bioz_enable(void) __attribute__((unused));
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
    int ret = sensor_attr_set(max30001_dev, SENSOR_CHAN_ALL, MAX30001_ATTR_ECG_ENABLED, &ecg_mode_set);
    if (ret == 0) {
        set_ecg_active(true);
        LOG_DBG("ECG enabled successfully");
        
        // Configure leads based on hand worn setting
        int lead_ret = hw_max30001_configure_leads();
        if (lead_ret != 0) {
            LOG_WRN("Failed to configure ECG leads, using default configuration");
        }
    } else {
        LOG_ERR("Failed to enable ECG: %d", ret);
    }
    return ret;
}

static int hw_max30001_bioz_disable(void)
{
    struct sensor_value bioz_mode_set;
    bioz_mode_set.val1 = 0;
    int ret = sensor_attr_set(max30001_dev, SENSOR_CHAN_ALL, MAX30001_ATTR_BIOZ_ENABLED, &bioz_mode_set);
    if (ret != 0) {
        LOG_ERR("Failed to disable BioZ: %d", ret);
    }
    return ret;
}

static int hw_max30001_ecg_disable(void)
{
    struct sensor_value ecg_mode_set;
    ecg_mode_set.val1 = 0;
    int ret = sensor_attr_set(max30001_dev, SENSOR_CHAN_ALL, MAX30001_ATTR_ECG_ENABLED, &ecg_mode_set);
    if (ret == 0) {
        set_ecg_active(false);
        LOG_DBG("ECG disabled successfully");
    } else {
        LOG_ERR("Failed to disable ECG: %d", ret);
    }
    return ret;
}

static void st_ecg_bioz_idle_entry(void *o)
{
    LOG_DBG("ECG/BioZ SM Idle Entry");

    int ret;
    
    ret = hw_max30001_ecg_disable();
    if (ret != 0) {
        LOG_ERR("Failed to disable ECG in idle entry: %d", ret);
    }
    
    k_timer_stop(&tmr_ecg_bioz_sampling);
    
    ret = hw_max30001_bioz_disable();
    if (ret != 0) {
        LOG_ERR("Failed to disable BioZ in idle entry: %d", ret);
    }
}

static void st_ecg_bioz_idle_run(void *o)
{
    // LOG_DBG("ECG/BioZ SM Idle Run");
    if (k_sem_take(&sem_ecg_start, K_NO_WAIT) == 0)
    {
        smf_set_state(SMF_CTX(&s_ecg_bioz_obj), &ecg_bioz_states[HPI_ECG_BIOZ_STATE_STABILIZING]);
    }
}

static void st_ecg_bioz_stream_entry(void *o)
{
    LOG_DBG("ECG/BioZ SM Stream Entry");
    int ret;
    bool stabilization_complete;

    // Check if coming from stabilization 
    get_ecg_stabilization_values(NULL, &stabilization_complete);
    if (!stabilization_complete) {
        ret = hw_max30001_ecg_enable();
        if (ret != 0) {
            LOG_ERR("Failed to enable ECG in stream entry: %d", ret);
            // Consider transitioning to error state or retry logic
            return;
        }
        k_timer_start(&tmr_ecg_bioz_sampling, K_MSEC(ECG_SAMPLING_INTERVAL_MS), K_MSEC(ECG_SAMPLING_INTERVAL_MS));
    }
    
    // Start actual recording
    hpi_data_set_ecg_record_active(true);
    
    // Timer initialization
    set_ecg_timer_values(k_uptime_get_32(), ECG_RECORD_DURATION_S);
    
    LOG_INF("ECG recording started - %d seconds", ECG_RECORD_DURATION_S);
}

static void st_ecg_bioz_stream_run(void *o)
{
    // LOG_DBG("ECG/BioZ SM Stream Run");
    // Stream for ECG duration (30s)
    if (hpi_data_is_ecg_record_active() == true)
    {
        uint32_t last_timer;
        int countdown;
        get_ecg_timer_values(&last_timer, &countdown);
        
        if ((k_uptime_get_32() - last_timer) >= 1000)
        {
            countdown--;
            LOG_DBG("ECG timer: %d", countdown);
            set_ecg_timer_values(k_uptime_get_32(), countdown);

            struct hpi_ecg_status_t ecg_stat = {
                .ts_complete = 0,
                .status = HPI_ECG_STATUS_STREAMING,
                .hr = get_ecg_hr(),
                .progress_timer = countdown};
            zbus_chan_pub(&ecg_stat_chan, &ecg_stat, K_NO_WAIT);

            if (countdown <= 0)
            {
                smf_set_state(SMF_CTX(&s_ecg_bioz_obj), &ecg_bioz_states[HPI_ECG_BIOZ_STATE_COMPLETE]);
            }
        }
    }

    if (k_sem_take(&sem_ecg_cancel, K_NO_WAIT) == 0)
    {
        LOG_DBG("ECG cancelled");
        smf_set_state(SMF_CTX(&s_ecg_bioz_obj), &ecg_bioz_states[HPI_ECG_BIOZ_STATE_IDLE]);
    }
}

static void st_ecg_bioz_stream_exit(void *o)
{
    LOG_DBG("ECG/BioZ SM Stream Exit");

    hpi_data_set_ecg_record_active(false);

    // Reset timer
    set_ecg_timer_values(0, 0);
    set_ecg_stabilization_values(0, false); 
}

static void st_ecg_bioz_complete_entry(void *o)
{
    LOG_DBG("ECG/BioZ SM Complete Entry");
    int ret;
    
    k_timer_stop(&tmr_ecg_bioz_sampling);
    
    ret = hw_max30001_ecg_disable();
    if (ret != 0) {
        LOG_ERR("Failed to disable ECG in complete entry: %d", ret);
    }

    k_sem_give(&sem_ecg_complete);
}

static void st_ecg_bioz_complete_run(void *o)
{
    smf_set_state(SMF_CTX(&s_ecg_bioz_obj), &ecg_bioz_states[HPI_ECG_BIOZ_STATE_IDLE]);
}

static void st_ecg_bioz_complete_exit(void *o)
{
    LOG_DBG("ECG/BioZ SM Complete Exit");
}

static void st_ecg_bioz_leadoff_entry(void *o)
{
    LOG_DBG("ECG/BioZ SM Leadoff Entry");
    // hw_max30001_ecg_disable();
    // hw_max30001_bioz_disable();
    // k_timer_stop(&tmr_ecg_bioz_sampling);
    // k_timer_stop()
}

static void st_ecg_bioz_leadoff_run(void *o)
{

    // LOG_DBG("ECG/BioZ SM Leadoff Run");
    if (k_sem_take(&sem_ecg_lead_on_local, K_NO_WAIT) == 0)
    {
        smf_set_state(SMF_CTX(&s_ecg_bioz_obj), &ecg_bioz_states[HPI_ECG_BIOZ_STATE_STREAM]);
    }
}

static void st_ecg_bioz_stabilizing_entry(void *o)
{
    LOG_DBG("ECG/BioZ SM Stabilizing Entry");
    int ret;

    // Enable ECG
    // Reset ECG smoothing filter for clean start
    ecg_smooth_reset();

    // Enable ECG but don't start recording yet
    ret = hw_max30001_ecg_enable();
    if (ret != 0) {
        LOG_ERR("Failed to enable ECG in stabilizing entry: %d", ret);
       
        return;
    }
    
    k_timer_start(&tmr_ecg_bioz_sampling, K_MSEC(ECG_SAMPLING_INTERVAL_MS), K_MSEC(ECG_SAMPLING_INTERVAL_MS));
    
    // Init stabilization values
    set_ecg_stabilization_values(ECG_STABILIZATION_DURATION_S, false);
    set_ecg_timer_values(k_uptime_get_32(), 0);
    
    // Publish status indicating stabilization phase
    struct hpi_ecg_status_t ecg_stat = {
        .ts_complete = 0,
        .status = HPI_ECG_STATUS_STREAMING, // add HPI_ECG_STATUS_STABILIZING
        .hr = 0,
        .progress_timer = ECG_RECORD_DURATION_S + ECG_STABILIZATION_DURATION_S};
    zbus_chan_pub(&ecg_stat_chan, &ecg_stat, K_NO_WAIT);
    
    LOG_INF("ECG stabilization started - waiting %d seconds", ECG_STABILIZATION_DURATION_S);
#if CONFIG_ECG_SMOOTHING_ENABLED
    LOG_INF("ECG smoothing enabled with window size %d", ECG_FILTER_WINDOW_SIZE);
#else
    LOG_INF("ECG smoothing disabled - using raw samples");
#endif
}

static void st_ecg_bioz_stabilizing_run(void *o)
{
    // Count down stabilization timer
    uint32_t last_timer;
    int stabilization_countdown;
    get_ecg_timer_values(&last_timer, NULL);
    get_ecg_stabilization_values(&stabilization_countdown, NULL);
    
    if ((k_uptime_get_32() - last_timer) >= 1000)
    {
        stabilization_countdown--;
        LOG_DBG("ECG stabilization timer: %d", stabilization_countdown);
        
        // Update timer values atomically
        set_ecg_timer_values(k_uptime_get_32(), 0);
        set_ecg_stabilization_values(stabilization_countdown, false);

        // Update progress - total time includes stabilization + recording
        struct hpi_ecg_status_t ecg_stat = {
            .ts_complete = 0,
            .status = HPI_ECG_STATUS_STREAMING,
            .hr = get_ecg_hr(),
            .progress_timer = ECG_RECORD_DURATION_S + stabilization_countdown};
        zbus_chan_pub(&ecg_stat_chan, &ecg_stat, K_NO_WAIT);

        if (stabilization_countdown <= 0)
        {
            LOG_INF("ECG stabilization complete - starting recording");
            smf_set_state(SMF_CTX(&s_ecg_bioz_obj), &ecg_bioz_states[HPI_ECG_BIOZ_STATE_STREAM]);
        }
    }

    // Allow cancellation during stabilization
    if (k_sem_take(&sem_ecg_cancel, K_NO_WAIT) == 0)
    {
        LOG_DBG("ECG cancelled during stabilization");
        smf_set_state(SMF_CTX(&s_ecg_bioz_obj), &ecg_bioz_states[HPI_ECG_BIOZ_STATE_IDLE]);
    }
}

static void st_ecg_bioz_stabilizing_exit(void *o)
{
    LOG_DBG("ECG/BioZ SM Stabilizing Exit");
    set_ecg_stabilization_values(0, true);
}

static const struct smf_state ecg_bioz_states[] = {
    [HPI_ECG_BIOZ_STATE_IDLE] = SMF_CREATE_STATE(st_ecg_bioz_idle_entry, st_ecg_bioz_idle_run, NULL, NULL, NULL),
    [HPI_ECG_BIOZ_STATE_STABILIZING] = SMF_CREATE_STATE(st_ecg_bioz_stabilizing_entry, st_ecg_bioz_stabilizing_run, st_ecg_bioz_stabilizing_exit, NULL, NULL),
    [HPI_ECG_BIOZ_STATE_STREAM] = SMF_CREATE_STATE(st_ecg_bioz_stream_entry, st_ecg_bioz_stream_run, st_ecg_bioz_stream_exit, NULL, NULL),
    [HPI_ECG_BIOZ_STATE_LEADOFF] = SMF_CREATE_STATE(st_ecg_bioz_leadoff_entry, st_ecg_bioz_leadoff_run, NULL, NULL, NULL),
    [HPI_ECG_BIOZ_STATE_COMPLETE] = SMF_CREATE_STATE(st_ecg_bioz_complete_entry, st_ecg_bioz_complete_run, st_ecg_bioz_complete_exit, NULL, NULL),
};

void smf_ecg_bioz_thread(void)
{
    int ret;

    // Wait for HW module to init ECG/BioZ
    k_sem_take(&sem_ecg_bioz_sm_start, K_FOREVER);

    LOG_INF("ECG/BioZ SMF Thread Started");

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
