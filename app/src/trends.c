#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <stdio.h>

#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/settings/settings.h>
#include <zephyr/zbus/zbus.h>

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

struct hpi_hr_trend_point_t hr_trend_minute[HR_TREND_MINUTE_PTS]; // 60 points max per minute

static uint16_t m_hr_curr_minute[60];  // Assumed max 60 points per minute
static uint8_t m_hr_curr_minute_counter = 0;


K_MSGQ_DEFINE(q_hr_trend, sizeof(struct hpi_hr_trend_point_t), 8, 1);

void hpi_rec_write_hour(uint32_t filenumber, struct hpi_hr_trend_day_t hr_data)
{
    struct fs_file_t file;
    int ret = 0;

    fs_file_t_init(&file);
    fs_mkdir("/lfs/hr");

    char fname[30];
    sprintf(fname, "/lfs/hr/hr_%d", filenumber);

    LOG_DBG("Write to file... %s | Size: %d", fname, sizeof(hr_data));

    ret = fs_open(&file, fname, FS_O_CREATE | FS_O_RDWR | FS_O_APPEND);

    if (ret < 0)
    {
        printk("FAIL: open %s: %d", fname, ret);
    }

    /*for (int i = 0; i < current_session_log_counter; i++)
    {
        ret = fs_write(&file, &current_session_log_points[i], sizeof(struct hpi_ecg_bioz_sensor_data_t));
    }*/

    ret = fs_write(&file, &hr_data, sizeof(struct hpi_hr_trend_day_t));

    ret = fs_close(&file);
    ret = fs_sync(&file);

    /*ret = fs_statvfs(mp->mnt_point, &sbuf);
    if (ret < 0)
    {
        printk("FAIL: statvfs: %d\n", ret);
        // goto out;
    }*/
}

void hpi_rec_reset_day(void)
{
    struct fs_file_t file;
    int ret = 0;

    fs_file_t_init(&file);
    fs_mkdir("/lfs/hr");

    char fname[30] = "/lfs/hr/hr_7";

    // Open file in WR mode to wipe contents
    ret = fs_open(&file, fname, FS_O_CREATE | FS_O_WRITE);
}

void hpi_rec_add_hr_point(struct hpi_hr_trend_point_t m_hr_trend_point, int day)
{
    struct hpi_hr_trend_day_t hr_data;

    struct fs_file_t file;
    int ret = 0;

    fs_file_t_init(&file);
    fs_mkdir("/lfs/hr");

    char fname[30];
    sprintf(fname, "/lfs/hr/hr%d", day);

    LOG_DBG("Write to file... %s | Size: %d", fname, sizeof(hr_data));

    ret = fs_open(&file, fname, FS_O_CREATE | FS_O_RDWR | FS_O_APPEND);

    if (ret < 0)
    {
        printk("FAIL: open %s: %d", fname, ret);
    }

    ret = fs_write(&file, &m_hr_trend_point, sizeof(struct hpi_hr_trend_point_t));

    ret = fs_close(&file);
    ret = fs_sync(&file);
}

void write_test_data(void)
{
    struct hpi_hr_trend_day_t hr_data;

    for (int i = 0; i < 60; i++)
    {
        // hr_data.hr_points->hr = 80;
        hr_data.hr_points->timestamp = sys_clock_cycle_get_32();
    }

    hpi_rec_write_hour(sys_clock_cycle_get_32(), hr_data);
}

static void trend_hr_listener(const struct zbus_channel *chan)
{
    const struct hpi_hr_t *hpi_hr = zbus_chan_const_msg(chan);
    m_hr_curr_minute[m_hr_curr_minute_counter] = hpi_hr->hr;
}
ZBUS_LISTENER_DEFINE(trend_hr_lis, trend_hr_listener);

void trend_sample_thread(void)
{
    for(;;)
    {
        if(m_hr_curr_minute_counter<60)
        {
            m_hr_curr_minute_counter++;
        }
        else
        {
            m_hr_curr_minute_counter = 0;

            struct hpi_hr_trend_point_t hr_trend_point;
            hr_trend_point.timestamp = sys_clock_cycle_get_32();
            uint16_t hr_sum = 0;
            for(int i = 0; i < 60; i++)
            {
                if(m_hr_curr_minute[i] > hr_trend_point.hr_max)
                {
                    hr_trend_point.hr_max = m_hr_curr_minute[i];
                }
                if(m_hr_curr_minute[i] < hr_trend_point.hr_min)
                {
                    hr_trend_point.hr_min = m_hr_curr_minute[i];
                }
                hr_sum += m_hr_curr_minute[i];
            }

            hr_trend_point.hr_avg = hr_sum / 60;
            hr_trend_point.hr_latest = m_hr_curr_minute[59];

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
            //hpi_rec_add_hr_point(hr_trend_minute, 7);
        }
        LOG_DBG("Trend thread running");
        k_sleep(K_SECONDS(30));
    }
}

#define TREND_RECORD_THREAD_STACK_SIZE 1024
#define TREND_RECORD_THREAD_PRIORITY 5

K_THREAD_DEFINE(trend_record_thread_id, TREND_RECORD_THREAD_STACK_SIZE, trend_record_thread, NULL, NULL, NULL, TREND_RECORD_THREAD_PRIORITY, 0, 0);
