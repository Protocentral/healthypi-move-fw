#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <stdio.h>

#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/settings/settings.h>

#include "hpi_common_types.h"
#include "fs_module.h"
#include "ui/move_ui.h"

LOG_MODULE_REGISTER(record_module, LOG_LEVEL_DBG);

void hpi_rec_write_hour(uint32_t filenumber, struct hpi_hr_trend_day_t hr_data)
{
    struct fs_file_t file;
    struct fs_statvfs sbuf;
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