#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <stdio.h>

#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/settings/settings.h>
#include <zephyr/zbus/zbus.h>

#include <zephyr/drivers/rtc.h>

#include <time.h>
#include <zephyr/posix/time.h>
#include <zephyr/sys/timeutil.h>

#include "hpi_common_types.h"
#include "fs_module.h"
#include "trends.h"

LOG_MODULE_REGISTER(trends_module, LOG_LEVEL_DBG);

// Size of one trend point = 12 bytes
// Size of one hour = 12 * 60 = 720 bytes
// Size of one day = 720 * 24 = 17280 bytes
// Size of one week = 17280 * 7 = 120960 bytes
// Size of one month = 120960 * 4 = 483840 bytes

#define HR_TREND_POINT_SIZE 12
#define HR_TREND_MINUTE_PTS 60
#define HR_TREND_WEEK_PTS   10080 // 60 * 24 * 7 

//Store raw HR values for the current minute
static uint16_t m_hr_curr_minute[60];  // Assumed max 60 points per minute
static uint8_t m_hr_curr_minute_counter = 0;

// Time variables
static int64_t m_trend_time_ts;
static struct tm m_today_time_tm;

K_MSGQ_DEFINE(q_hr_trend, sizeof(struct hpi_hr_trend_point_t), 8, 1);

// Trend buffers
static struct hpi_hr_trend_point_t m_hr_trend_point_buffer[10];


void hpi_trend_sample_thread(void)
{
    for(;;)
    {
        if(m_hr_curr_minute_counter<HR_TREND_MINUTE_PTS)
        {
            m_hr_curr_minute_counter++;
        }
        else
        {
            m_hr_curr_minute_counter = 0;

            struct hpi_hr_trend_point_t hr_trend_point;
            hr_trend_point.timestamp = m_trend_time_ts;
            uint16_t hr_sum = 0;
            hr_trend_point.hr_max = 0;
            hr_trend_point.hr_min = 255;
            for(int i = 0; i < HR_TREND_MINUTE_PTS; i++)
            {
                if(m_hr_curr_minute[i] > hr_trend_point.hr_max)
                {
                    hr_trend_point.hr_max = m_hr_curr_minute[i];
                }
                if((m_hr_curr_minute[i] < hr_trend_point.hr_min) && (m_hr_curr_minute[i] != 0))
                {
                    hr_trend_point.hr_min = m_hr_curr_minute[i];
                }
                hr_sum += m_hr_curr_minute[i];
            }

            hr_trend_point.hr_avg = hr_sum / HR_TREND_MINUTE_PTS;
            hr_trend_point.hr_latest = m_hr_curr_minute[HR_TREND_MINUTE_PTS-1];

            k_msgq_put(&q_hr_trend, &hr_trend_point, K_NO_WAIT);
        }
        k_msleep(1000);
    }
}

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
    struct hpi_hr_trend_point_t _hr_trend_minute;
    for (;;)
    {
        if(k_msgq_get(&q_hr_trend, &_hr_trend_minute, K_NO_WAIT) == 0)
        {
            LOG_DBG("Recd HR point: %" PRIx64 "| %d | %d | %d", _hr_trend_minute.timestamp, _hr_trend_minute.hr_max, _hr_trend_minute.hr_min, _hr_trend_minute.hr_avg);
            int64_t today_ts = hpi_trend_get_day_start_ts(&_hr_trend_minute.timestamp);
            LOG_DBG("Today TS: %" PRIx64, today_ts);
            hpi_trend_wr_hr_point_to_file(_hr_trend_minute, today_ts);
        }

        k_sleep(K_SECONDS(2));
    }
}

void hpi_trend_wr_hr_point_to_file(struct hpi_hr_trend_point_t m_hr_trend_point, int64_t day_ts)
{
    struct fs_file_t file;
    int ret = 0;

    fs_file_t_init(&file);

    // fs_mkdir("/lfs/hr");

    char fname[30];
    sprintf(fname, "/lfs/trhr/%" PRIx64, day_ts);

    LOG_DBG("Write to file... %s | Size: %d", fname, sizeof(m_hr_trend_point));

    ret = fs_open(&file, fname, FS_O_CREATE | FS_O_RDWR | FS_O_APPEND);

    if (ret < 0)
    {
        LOG_ERR("FAIL: open %s: %d", fname, ret);
    }

    m_hr_trend_point_buffer[0] = m_hr_trend_point;

    ret = fs_write(&file, m_hr_trend_point_buffer, sizeof(m_hr_trend_point_buffer));
    // ret = fs_write(&file, test_array, sizeof(test_array));

    ret = fs_close(&file);
    ret = fs_sync(&file);
}

void hpi_trend_load_day_trend(int64_t day_ts, struct hpi_hr_trend_point_t *hr_trend_points, int *num_points)
{
    struct fs_file_t file;
    int ret = 0;

    fs_file_t_init(&file);

    struct fs_dirent trend_file_ent;

    char fname[30];
    sprintf(fname, "/lfs/trhr/%" PRIx64, day_ts);

    ret = fs_stat(fname, &trend_file_ent);
    if (ret < 0)
    {
        LOG_ERR("FAIL: stat %s: %d", fname, ret);
        return;
    }

    LOG_DBG("Read from file %s | Size: %d", fname, trend_file_ent.size);

    uint16_t trend_points = trend_file_ent.size / sizeof(struct hpi_hr_trend_point_t);
    LOG_DBG("Num HR Points: %d", trend_points);

    ret = fs_open(&file, fname, FS_O_READ);

    if (ret < 0)
    {
        LOG_ERR("FAIL: open %s: %d", fname, ret);
    }

    struct hpi_hr_trend_point_t hr_trend_point;
   
    for(int i=0;i<trend_points;i++)
    {
        ret = fs_read(&file, &hr_trend_point, sizeof(hr_trend_point));
        if(ret < 0)
        {
            LOG_ERR("FAIL: read %s: %d", fname, ret);
        }
        LOG_DBG("Read HR point: %" PRIx64 "| %d | %d | %d", hr_trend_point.timestamp, hr_trend_point.hr_max, hr_trend_point.hr_min, hr_trend_point.hr_avg);
        hr_trend_points[i] = hr_trend_point;
    }
    /*while (fs_read(&file, &hr_trend_point, sizeof(hr_trend_point)) == sizeof(hr_trend_point))
    {
        LOG_DBG("Read HR point: %" PRIx64 "| %d | %d | %d", hr_trend_point.timestamp, hr_trend_point.hr_max, hr_trend_point.hr_min, hr_trend_point.hr_avg);
        hr_trend_points[*num_points] = hr_trend_point;
        i++;
    }*/

    ret = fs_close(&file);
    ret = fs_sync(&file);
}

static void trend_hr_listener(const struct zbus_channel *chan)
{
    const struct hpi_hr_t *hpi_hr = zbus_chan_const_msg(chan);
    m_hr_curr_minute[m_hr_curr_minute_counter] = hpi_hr->hr;
}
ZBUS_LISTENER_DEFINE(trend_hr_lis, trend_hr_listener);


static void trend_sys_time_listener(const struct zbus_channel *chan)
{
    const struct tm *sys_time = zbus_chan_const_msg(chan);
    m_trend_time_ts = timeutil_timegm64(sys_time);

    //LOG_DBG("Time: %d-%d-%d %d:%d:%d", m_trend_time->tm_year, m_trend_time->tm_mon, m_trend_time->tm_mday, m_trend_time->tm_hour, m_trend_time->tm_min, m_trend_time->tm_sec);
    //LOG_DBG("Sys TS: %" PRIx64, m_trend_time_ts);    
}
ZBUS_LISTENER_DEFINE(trend_sys_time_lis, trend_sys_time_listener);

#define THREAD_SAMPLE_THREAD_STACK_SIZE 2048
#define THREAD_SAMPLE_THREAD_PRIORITY 5

#define TREND_RECORD_THREAD_STACK_SIZE 2048
#define TREND_RECORD_THREAD_PRIORITY 5

K_THREAD_DEFINE(trend_record_thread_id, TREND_RECORD_THREAD_STACK_SIZE, hpi_trend_record_thread, NULL, NULL, NULL, TREND_RECORD_THREAD_PRIORITY, 0, 2000);
K_THREAD_DEFINE(trend_sample_thread_id, THREAD_SAMPLE_THREAD_STACK_SIZE, hpi_trend_sample_thread, NULL, NULL, NULL, THREAD_SAMPLE_THREAD_PRIORITY, 0, 2000);