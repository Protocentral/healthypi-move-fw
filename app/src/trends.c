#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <stdio.h>

#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/settings/settings.h>
#include <zephyr/zbus/zbus.h>

#include <time.h>
#include <zephyr/posix/time.h>
#include <zephyr/sys/timeutil.h>

#include "hpi_common_types.h"
#include "fs_module.h"
#include "trends.h"
#include "log_module.h"

LOG_MODULE_REGISTER(trends_module, LOG_LEVEL_DBG);

// Size of one trend point = 12 bytes
// Size of one hour = 12 * 60 = 720 bytes
// Size of one day = 720 * 24 = 17280 bytes
// Size of one week = 17280 * 7 = 120960 bytes
// Size of one month = 120960 * 4 = 483840 bytes

#define HR_TREND_POINT_SIZE 12
#define HR_TREND_MINUTE_PTS 60
#define HR_TREND_DAY_MAX_PTS 1440 // 60 * 24

#define TEMP_TREND_MINUTE_PTS 12

#define NUM_HOURS 24
#define MAX_POINTS_PER_HOUR 60
#define MAX_POINTS_SPO2_PER_HOUR 10

// Store raw HR values for the current minute
static uint16_t m_hr_curr_minute[60] = {0};   // Assumed max 60 points per minute
static uint16_t m_temp_curr_minute[13] = {0}; // Assumed max 10 points per minute

static uint16_t m_spo2 = 0;
static int64_t m_spo2_last_ts = 0;
K_SEM_DEFINE(sem_spo2_updated, 0, 1);

static uint8_t m_trends_curr_minute_counter = 0;

static uint8_t m_trends_temp_minute_sample_counter = 0;
static uint8_t m_trends_hr_minute_sample_counter = 0;

// Time variables
static struct tm m_trend_sys_time_tm;
static int64_t m_trend_time_ts;

K_MSGQ_DEFINE(q_hr_trend, sizeof(struct hpi_hr_trend_point_t), 8, 1);
K_MSGQ_DEFINE(q_spo2_trend, sizeof(struct hpi_spo2_point_t), 8, 4);
K_MSGQ_DEFINE(q_temp_trend, sizeof(struct hpi_temp_trend_point_t), 8, 1);
K_MSGQ_DEFINE(q_steps_trend, sizeof(struct hpi_steps_t), 8, 1);

static int hpi_trend_process_points()
{
    struct hpi_hr_trend_point_t hr_trend_point;
    hr_trend_point.timestamp = m_trend_time_ts;
    uint16_t hr_sum = 0;
    hr_trend_point.max = 0;
    hr_trend_point.min = 65535;

    struct hpi_temp_trend_point_t temp_trend_point;
    temp_trend_point.timestamp = m_trend_time_ts;
    uint32_t temp_sum = 0;
    temp_trend_point.max = 0;
    temp_trend_point.min = 65535;

    LOG_DBG("HR points: %d", m_trends_hr_minute_sample_counter);

    for (int i = 0; i < m_trends_hr_minute_sample_counter; i++)
    {
        if (m_hr_curr_minute[i] > hr_trend_point.max)
        {
            hr_trend_point.max = m_hr_curr_minute[i];
        }
        if ((m_hr_curr_minute[i] < hr_trend_point.min) && (m_hr_curr_minute[i] != 0))
        {
            hr_trend_point.min = m_hr_curr_minute[i];
        }
        if ((m_hr_curr_minute[i] != 0) && (m_hr_curr_minute[i] != 65535))
        {
            hr_sum += m_hr_curr_minute[i];
        }
    }

    LOG_DBG("Temp points: %d", m_trends_temp_minute_sample_counter);

    for (int i = 0; i < m_trends_temp_minute_sample_counter; i++)
    {

        if (m_temp_curr_minute[i] > temp_trend_point.max)
        {
            temp_trend_point.max = m_temp_curr_minute[i];
        }
        if ((m_temp_curr_minute[i] < temp_trend_point.min) && (m_temp_curr_minute[i] != 0))
        {
            temp_trend_point.min = m_temp_curr_minute[i];
        }
        if ((m_temp_curr_minute[i] != 0) && (m_temp_curr_minute[i] != 65535))
        {
            temp_sum += m_temp_curr_minute[i];
        }
    }

    if (temp_sum > 0)
    {
        temp_trend_point.avg = temp_sum / m_trends_temp_minute_sample_counter;
        temp_trend_point.latest = m_temp_curr_minute[m_trends_temp_minute_sample_counter - 1];
        k_msgq_put(&q_temp_trend, &temp_trend_point, K_NO_WAIT);
    }

    m_trends_temp_minute_sample_counter = 0;

    if (hr_sum > 0)
    {
        hr_trend_point.avg = hr_sum / m_trends_hr_minute_sample_counter;
        hr_trend_point.latest = m_hr_curr_minute[HR_TREND_MINUTE_PTS - 1];
        k_msgq_put(&q_hr_trend, &hr_trend_point, K_NO_WAIT);
    }

    if (k_sem_take(&sem_spo2_updated, K_NO_WAIT) == 0)
    {
        struct hpi_spo2_point_t spo2_trend_point;
        spo2_trend_point.timestamp = m_trend_time_ts;
        spo2_trend_point.spo2 = m_spo2;
        k_msgq_put(&q_spo2_trend, &spo2_trend_point, K_NO_WAIT);
    }

    m_trends_hr_minute_sample_counter = 0;
}

void work_process_points_handler(struct k_work *work)
{
    if (m_trends_curr_minute_counter < HR_TREND_MINUTE_PTS)
    {
        m_trends_curr_minute_counter++;
    }
    else
    {
        m_trends_curr_minute_counter = 0;
        hpi_trend_process_points();
        memset(m_hr_curr_minute, 0, sizeof(m_hr_curr_minute));
    }
}
K_WORK_DEFINE(work_process_points, work_process_points_handler);

void trend_process_handler(struct k_timer *dummy)
{
    k_work_submit(&work_process_points);
}
K_TIMER_DEFINE(tmr_trend_process, trend_process_handler, NULL);

static int64_t hpi_trend_get_day_start_ts(int64_t *today_time_ts)
{
    struct tm today_time_tm = *gmtime(today_time_ts);
    today_time_tm.tm_hour = 0;
    today_time_tm.tm_min = 0;
    today_time_tm.tm_sec = 0;
    return timeutil_timegm64(&today_time_tm);
}

void hpi_trend_record_thread(void)
{
    struct hpi_hr_trend_point_t trend_hr_minute;
    struct hpi_spo2_point_t trend_spo2;
    struct hpi_temp_trend_point_t trend_temp;
    struct hpi_steps_t trend_steps;

    /* start a periodic timer that expires once every second */
    k_timer_start(&tmr_trend_process, K_SECONDS(1), K_SECONDS(1));

    for (;;)
    {
        if (k_msgq_get(&q_hr_trend, &trend_hr_minute, K_NO_WAIT) == 0)
        {
            int64_t today_ts = hpi_trend_get_day_start_ts(&trend_hr_minute.timestamp);
            LOG_DBG("Recd HR point: %" PRId64 "| %d | %d | %d", trend_hr_minute.timestamp, trend_hr_minute.max, trend_hr_minute.min, trend_hr_minute.avg);
            hpi_hr_trend_wr_point_to_file(trend_hr_minute, today_ts);
        }

        if (k_msgq_get(&q_temp_trend, &trend_temp, K_NO_WAIT) == 0)
        {
            int64_t today_ts = hpi_trend_get_day_start_ts(&trend_temp.timestamp);
            LOG_DBG("Recd Temp point: %" PRId64 "| %d | %d | %d", trend_temp.timestamp, trend_temp.max, trend_temp.min, trend_temp.avg);
            hpi_temp_trend_wr_point_to_file(trend_temp, today_ts);
        }

        if (k_msgq_get(&q_spo2_trend, &trend_spo2, K_NO_WAIT) == 0)
        {
            int64_t today_ts = hpi_trend_get_day_start_ts(&trend_spo2.timestamp);
            LOG_DBG("Recd SpO2 point: %" PRId64 "| %d ", trend_spo2.timestamp, trend_spo2.spo2);
            hpi_spo2_trend_wr_point_to_file(trend_spo2, today_ts);
        }

        if (k_msgq_get(&q_steps_trend, &trend_steps, K_NO_WAIT) == 0)
        {
            int64_t today_ts = hpi_trend_get_day_start_ts(&trend_steps.timestamp);
            LOG_DBG("Recd Steps point: %" PRId64 "| %d ", trend_steps.timestamp, trend_steps.steps);
            hpi_steps_trend_wr_point_to_file(trend_steps, today_ts);
        }

        k_sleep(K_SECONDS(2));
    }
}

int hpi_trend_load_trend(struct hpi_hourly_trend_point_t *hourly_trend_points, struct hpi_minutely_trend_point_t *minutely_trend_points, int *num_points, enum trend_type m_trend_type)
{

    // Trend buffers
    struct hpi_hr_trend_point_t trend_day_points[NUM_HOURS][MAX_POINTS_PER_HOUR];
    struct hpi_hr_trend_point_t trend_point_all[1440];

    struct fs_file_t file;
    int ret = 0;

    struct fs_dirent trend_file_ent;
    char fname[30];

    uint16_t num_trend_points;

    int64_t day_ts = hpi_trend_get_day_start_ts(&m_trend_time_ts);

    fs_file_t_init(&file);

    if (m_trend_type == TREND_HR)
    {
        sprintf(fname, "/lfs/trhr/%" PRId64, day_ts);
    }
    else if (m_trend_type == TREND_SPO2)
    {
        sprintf(fname, "/lfs/trspo2/%" PRId64, day_ts);
    }
    else
    {
        LOG_ERR("Invalid trend type");
        return -1;
    }

    ret = fs_stat(fname, &trend_file_ent);
    if (ret < 0)
    {
        LOG_ERR("FAIL: stat %s: %d", fname, ret);
        if (ret == -ENOENT)
        {
            *num_points = 0;
            LOG_ERR("File not found: %s", fname);
        }
        return ret;
    }

    LOG_DBG("Read from file %s | Size: %d", fname, trend_file_ent.size);

    num_trend_points = trend_file_ent.size / sizeof(struct hpi_hr_trend_point_t);
    *num_points = num_trend_points;

    LOG_DBG("Num Trend Points: %d", num_trend_points);

    ret = fs_open(&file, fname, FS_O_READ);

    if (ret < 0)
    {
        LOG_ERR("FAIL: open %s: %d", fname, ret);
    }

    struct hpi_hr_trend_point_t hr_trend_point;

    int bucket_counts[NUM_HOURS] = {0};

    for (int i = 0; i < num_trend_points; i++)
    {
        ret = fs_read(&file, &hr_trend_point, sizeof(hr_trend_point));
        if (ret < 0)
        {
            LOG_ERR("FAIL: read %s: %d", fname, ret);
        }
        trend_point_all[i] = hr_trend_point;
    }

    ret = fs_close(&file);
    ret = fs_sync(&file);

    for (int i = 0; i < num_trend_points; i++)
    {
        struct tm *time_info = gmtime(&trend_point_all[i].timestamp);
        int hour = time_info->tm_hour;

        if (bucket_counts[hour] < MAX_POINTS_PER_HOUR)
        {
            trend_day_points[hour][bucket_counts[hour]] = trend_point_all[i];
            bucket_counts[hour]++;
        }
        else
        {
            LOG_DBG("Bucket overflow for hour %d\n", hour);
        }
    }

    int8_t minute_counter = 0;
    for (int i = 0; i < num_trend_points; i++)
    {
        if (trend_point_all[i].timestamp > m_trend_time_ts - 3600)
        {
            if (minute_counter >= 60)
            {
                LOG_ERR("Minute counter overflow");
                break;
            }
            minutely_trend_points[minute_counter++].avg = trend_point_all[i].avg;
        }
    }

    for (int i = 0; i < NUM_HOURS; i++)
    {
        // printf("Hour %d: %d points\n", i, bucket_counts[i]);

        hourly_trend_points[i].hour_no = i;
        hourly_trend_points[i].avg = 0;
        hourly_trend_points[i].max = 0;
        hourly_trend_points[i].min = 0;

        uint16_t max = 0;
        uint16_t min = 255;

        for (int j = 0; j < bucket_counts[i]; j++)
        {
            if (trend_day_points[i][j].max > max)
            {
                hourly_trend_points[i].max = trend_day_points[i][j].max;
            }
            if (trend_day_points[i][j].min < min)
            {
                hourly_trend_points[i].min = trend_day_points[i][j].min;
            }
            hourly_trend_points[i].avg += trend_day_points[i][j].avg;
            // printf("  Timestamp: %" PRIx64 "\n", trend_day_points[i][j].timestamp);
        }
        if (bucket_counts[i] > 0)
        {
            hourly_trend_points[i].avg /= bucket_counts[i];
        }

        // LOG_DBG("Hour %d: | %d | %d | %d", hr_hourly_trend_points[i].hour_no, hr_hourly_trend_points[i].max, hr_hourly_trend_points[i].min, hr_hourly_trend_points[i].avg);
    }
    return 0;
}

static void trend_spo2_listener(const struct zbus_channel *chan)
{
    const struct hpi_spo2_point_t *hpi_spo2 = zbus_chan_const_msg(chan);
    LOG_DBG("ZB SpO2: %d | Time: %" PRId64, hpi_spo2->spo2, hpi_spo2->timestamp);
    m_spo2 = hpi_spo2->spo2;
    m_spo2_last_ts = hpi_spo2->timestamp;
    k_sem_give(&sem_spo2_updated);
    // k_msgq_put(&q_spo2_trend, &hpi_spo2, K_NO_WAIT);
}
ZBUS_LISTENER_DEFINE(trend_spo2_lis, trend_spo2_listener);

static void trend_hr_listener(const struct zbus_channel *chan)
{
    const struct hpi_hr_t *hpi_hr = zbus_chan_const_msg(chan);
    m_hr_curr_minute[m_trends_hr_minute_sample_counter] = hpi_hr->hr;
    m_trends_hr_minute_sample_counter++;
    LOG_DBG("ZB HR: %d", hpi_hr->hr);
}
ZBUS_LISTENER_DEFINE(trend_hr_lis, trend_hr_listener);

static void trend_temp_listener(const struct zbus_channel *chan)
{
    const struct hpi_temp_t *hpi_temp = zbus_chan_const_msg(chan);
    m_temp_curr_minute[m_trends_temp_minute_sample_counter] = hpi_temp->temp_f * 100;
    m_trends_temp_minute_sample_counter++;
    // LOG_INF("ZB Temp: %f", hpi_temp->temp_f);
}
ZBUS_LISTENER_DEFINE(trend_temp_lis, trend_temp_listener);

static void trend_steps_listener(const struct zbus_channel *chan)
{
    const struct hpi_steps_t *hpi_steps = zbus_chan_const_msg(chan);

    // m_disp_steps = hpi_steps->steps_walk;
    // m_disp_kcals = hpi_get_kcals_from_steps(m_disp_steps);
    // ui_steps_button_update(hpi_steps->steps_walk);
}
ZBUS_LISTENER_DEFINE(trend_steps_lis, trend_steps_listener);

static void trend_bpt_listener(const struct zbus_channel *chan)
{
    const struct hpi_bpt_t *hpi_bpt = zbus_chan_const_msg(chan);
    // m_disp_bp_sys = hpi_bpt->sys;
}
ZBUS_LISTENER_DEFINE(trend_bpt_lis, trend_bpt_listener);

static void trend_sys_time_listener(const struct zbus_channel *chan)
{
    const struct tm *sys_time = zbus_chan_const_msg(chan);
    m_trend_sys_time_tm = *sys_time;
    m_trend_time_ts = timeutil_timegm64(sys_time);
    // LOG_DBG("Sys TS: %" PRIx64, m_trend_time_ts);
}
ZBUS_LISTENER_DEFINE(trend_sys_time_lis, trend_sys_time_listener);

#define THREAD_SAMPLE_THREAD_STACK_SIZE 1024
#define THREAD_SAMPLE_THREAD_PRIORITY 5

#define TREND_RECORD_THREAD_STACK_SIZE 2048
#define TREND_RECORD_THREAD_PRIORITY 5

//K_THREAD_DEFINE(trend_record_thread_id, TREND_RECORD_THREAD_STACK_SIZE, hpi_trend_record_thread, NULL, NULL, NULL, TREND_RECORD_THREAD_PRIORITY, 0, 2000);