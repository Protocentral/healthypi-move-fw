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
#define HR_TREND_MINUTE_PTS 3
#define HR_TREND_WEEK_PTS   10080 // 60 * 24 * 7 

//Store raw HR values for the current minute
static uint16_t m_hr_curr_minute[60];  // Assumed max 60 points per minute
static uint8_t m_hr_curr_minute_counter = 0;

// Time variables
static int64_t m_trend_time_ts;
static struct tm m_today_time_tm;



K_MSGQ_DEFINE(q_hr_trend, sizeof(struct hpi_hr_trend_point_t), 8, 1);

static void hpi_rec_write_hr_point_to_file(struct hpi_hr_trend_point_t m_hr_trend_point)
{
    struct fs_file_t file;
    int ret = 0;

    fs_file_t_init(&file);
    //fs_mkdir("/lfs/hr");

    char fname[30];
    sprintf(fname, "/lfs/trend");

    LOG_DBG("Write to file... %s | Size: %d", fname, sizeof(m_hr_trend_point));

    ret = fs_open(&file, fname, FS_O_CREATE | FS_O_RDWR | FS_O_APPEND);

    if (ret < 0)
    {
        printk("FAIL: open %s: %d", fname, ret);
    }

    ret = fs_write(&file, &m_hr_trend_point, sizeof(struct hpi_hr_trend_point_t));

    ret = fs_close(&file);
    ret = fs_sync(&file);
}

void trend_sample_thread(void)
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

void trend_record_thread(void)
{
    struct hpi_hr_trend_point_t _hr_trend_minute;
    for (;;)
    {
        if(k_msgq_get(&q_hr_trend, &_hr_trend_minute, K_NO_WAIT) == 0)
        {
            LOG_DBG("Recd HR point: %" PRIx64 "| %d | %d | %d", _hr_trend_minute.timestamp, _hr_trend_minute.hr_max, _hr_trend_minute.hr_min, _hr_trend_minute.hr_avg);
            hpi_rec_write_hr_point_to_file(_hr_trend_minute);
        }

        k_sleep(K_SECONDS(2));
    }
}

static void trend_hr_listener(const struct zbus_channel *chan)
{
    const struct hpi_hr_t *hpi_hr = zbus_chan_const_msg(chan);
    m_hr_curr_minute[m_hr_curr_minute_counter] = hpi_hr->hr;
}
ZBUS_LISTENER_DEFINE(trend_hr_lis, trend_hr_listener);

static void trend_sys_time_listener(const struct zbus_channel *chan)
{
    const struct rtc_time *sys_time = zbus_chan_const_msg(chan);
    struct tm* _sys_tm_time = rtc_time_to_tm(sys_time);
    m_trend_time_ts = timeutil_timegm64(_sys_tm_time);

    //LOG_DBG("Time: %d-%d-%d %d:%d:%d", m_trend_time->tm_year, m_trend_time->tm_mon, m_trend_time->tm_mday, m_trend_time->tm_hour, m_trend_time->tm_min, m_trend_time->tm_sec);
    //LOG_DBG("Sys TS: %" PRIx64, m_trend_time_ts);    
}
ZBUS_LISTENER_DEFINE(trend_sys_time_lis, trend_sys_time_listener);

#define THREAD_SAMPLE_THREAD_STACK_SIZE 1024
#define THREAD_SAMPLE_THREAD_PRIORITY 5

#define TREND_RECORD_THREAD_STACK_SIZE 1024
#define TREND_RECORD_THREAD_PRIORITY 5

K_THREAD_DEFINE(trend_record_thread_id, TREND_RECORD_THREAD_STACK_SIZE, trend_record_thread, NULL, NULL, NULL, TREND_RECORD_THREAD_PRIORITY, 0, 0);
K_THREAD_DEFINE(trend_sample_thread_id, THREAD_SAMPLE_THREAD_STACK_SIZE, trend_sample_thread, NULL, NULL, NULL, THREAD_SAMPLE_THREAD_PRIORITY, 0, 0);