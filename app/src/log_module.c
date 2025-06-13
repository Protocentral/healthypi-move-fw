#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <stdio.h>

#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <time.h>

#include "log_module.h"
#include "cmd_module.h"
#include "fs_module.h"
#include "ui/move_ui.h"

LOG_MODULE_REGISTER(log_module, LOG_LEVEL_DBG);

// Externs
extern struct fs_mount_t *mp;
extern const char *hpi_sys_update_time_file;

static int hpi_log_get_path(char *m_path, uint8_t m_log_type)
{
    if (m_log_type == HPI_LOG_TYPE_TREND_HR)
    {
        strcpy(m_path, "/lfs/trhr/");
    }
    else if (m_log_type == HPI_LOG_TYPE_TREND_SPO2)
    {
        strcpy(m_path, "/lfs/trspo2/");
    }
    else if (m_log_type == HPI_LOG_TYPE_TREND_TEMP)
    {
        strcpy(m_path, "/lfs/trtemp/");
    }
    else if (m_log_type == HPI_LOG_TYPE_TREND_STEPS)
    {
        strcpy(m_path, "/lfs/trsteps/");
    }
    else if (m_log_type == HPI_LOG_TYPE_TREND_BPT)
    {
        strcpy(m_path, "/lfs/trbpt/");
    }
    else if (m_log_type == HPI_LOG_TYPE_ECG_RECORD)
    {
        strcpy(m_path, "/lfs/ecg/");
    }
    else if (m_log_type == HPI_LOG_TYPE_BIOZ_RECORD)
    {
        strcpy(m_path, "/lfs/bioz/");
    }
    else if (m_log_type == HPI_LOG_TYPE_PPG_WRIST_RECORD)
    {
        strcpy(m_path, "/lfs/ppgw/");
    }
    else if (m_log_type == HPI_LOG_TYPE_PPG_FINGER_RECORD)
    {
        strcpy(m_path, "/lfs/ppgf/");
    }
    else
    {
        strcpy(m_path, "/lfs/log/");
    }

    return 0;
}

void hpi_write_ecg_record_file(int32_t *ecg_record_buffer, uint16_t ecg_record_length, int64_t start_ts)
{
    struct fs_file_t file;
    int ret = 0;
    char fname[30];

    fs_file_t_init(&file);

    sprintf(fname, "/lfs/ecg/%" PRId64, start_ts);

    LOG_DBG("Write to file... %s | Size: %d", fname, ecg_record_length);

    ret = fs_open(&file, fname, FS_O_CREATE | FS_O_RDWR);
    if (ret < 0)
    {
        LOG_ERR("FAIL: open %s: %d", fname, ret);
    }

    ret = fs_write(&file, ecg_record_buffer, (ecg_record_length * 4));
    if (ret < 0)
    {
        LOG_ERR("FAIL: open %s: %d", fname, ret);
    }

    ret = fs_close(&file);
    ret = fs_sync(&file);
}

void hpi_hr_trend_wr_point_to_file(struct hpi_hr_trend_point_t m_trend_point, int64_t day_ts)
{
    struct fs_file_t file;
    int ret = 0;
    char fname[30];

    fs_file_t_init(&file);

    sprintf(fname, "/lfs/trhr/%" PRId64, day_ts);

    LOG_DBG("Write to file... %s | Size: %d", fname, sizeof(m_trend_point));

    ret = fs_open(&file, fname, FS_O_CREATE | FS_O_RDWR | FS_O_APPEND);
    if (ret < 0)
    {
        LOG_ERR("FAIL: open %s: %d", fname, ret);
    }
    ret = fs_write(&file, &m_trend_point, sizeof(m_trend_point));
    if (ret < 0)
    {
        LOG_ERR("FAIL: open %s: %d", fname, ret);
    }

    ret = fs_close(&file);
    ret = fs_sync(&file);
}

void hpi_spo2_trend_wr_point_to_file(struct hpi_spo2_point_t m_spo2_point, int64_t day_ts)
{
    struct fs_file_t file;
    int ret = 0;
    char fname[30];

    fs_file_t_init(&file);

    sprintf(fname, "/lfs/trspo2/%" PRId64, day_ts);

    LOG_DBG("Write to file... %s | Size: %d", fname, 10); // sizeof(m_spo2_point));

    ret = fs_open(&file, fname, FS_O_CREATE | FS_O_RDWR | FS_O_APPEND);
    if (ret < 0)
    {
        LOG_ERR("FAIL: open %s: %d", fname, ret);
    }
    ret = fs_write(&file, &m_spo2_point, sizeof(m_spo2_point));
    if (ret < 0)
    {
        LOG_ERR("FAIL: open %s: %d", fname, ret);
    }

    ret = fs_close(&file);
    ret = fs_sync(&file);
}

void hpi_bpt_trend_wr_point_to_file(struct hpi_bpt_point_t m_bpt_point, int64_t day_ts)
{
    struct fs_file_t file;
    int ret = 0;
    char fname[30];

    fs_file_t_init(&file);

    sprintf(fname, "/lfs/trbpt/%" PRId64, day_ts);

    LOG_DBG("Write to file... %s | Size: %d", fname, sizeof(m_bpt_point));

    ret = fs_open(&file, fname, FS_O_CREATE | FS_O_RDWR | FS_O_APPEND);
    if (ret < 0)
    {
        LOG_ERR("FAIL: open %s: %d", fname, ret);
    }
    ret = fs_write(&file, &m_bpt_point, sizeof(m_bpt_point));
    if (ret < 0)
    {
        LOG_ERR("FAIL: open %s: %d", fname, ret);
    }

    ret = fs_close(&file);
    ret = fs_sync(&file);
}

void hpi_temp_trend_wr_point_to_file(struct hpi_temp_trend_point_t m_temp_point, int64_t day_ts)
{
    struct fs_file_t file;
    int ret = 0;
    char fname[30];

    fs_file_t_init(&file);

    sprintf(fname, "/lfs/trtemp/%" PRId64, day_ts);

    LOG_DBG("Write to file... %s | Size: %d", fname, sizeof(m_temp_point));

    ret = fs_open(&file, fname, FS_O_CREATE | FS_O_RDWR | FS_O_APPEND);
    if (ret < 0)
    {
        LOG_ERR("FAIL: open %s: %d", fname, ret);
    }
    ret = fs_write(&file, &m_temp_point, sizeof(m_temp_point));
    if (ret < 0)
    {
        LOG_ERR("FAIL: open %s: %d", fname, ret);
    }

    ret = fs_close(&file);
    ret = fs_sync(&file);
}

void hpi_steps_trend_wr_point_to_file(struct hpi_steps_t m_steps_point, int64_t day_ts)
{
    struct fs_file_t file;
    int ret = 0;
    char fname[30];

    fs_file_t_init(&file);

    sprintf(fname, "/lfs/trsteps/%" PRId64, day_ts);

    LOG_DBG("Write to file... %s | Size: %d", fname, sizeof(m_steps_point));

    ret = fs_open(&file, fname, FS_O_CREATE | FS_O_RDWR | FS_O_APPEND);
    if (ret < 0)
    {
        LOG_ERR("FAIL: open %s: %d", fname, ret);
    }
    ret = fs_write(&file, &m_steps_point, sizeof(m_steps_point));
    if (ret < 0)
    {
        LOG_ERR("FAIL: open %s: %d", fname, ret);
    }

    ret = fs_close(&file);
    ret = fs_sync(&file);
}

uint16_t log_get_count(uint8_t m_log_type)
{
    int res;
    struct fs_dir_t dirp;
    static struct fs_dirent entry;

    uint16_t log_count = 0;
    char m_path[40] = "";

    fs_dir_t_init(&dirp);

    hpi_log_get_path(m_path, m_log_type);

    /* Verify fs_opendir() */
    res = fs_opendir(&dirp, m_path);
    if (res)
    {
        LOG_ERR("Error opening dir %s [%d]\n", m_path, res);
        return res;
    }

    LOG_DBG("Get Count CMD %s", m_path);
    for (;;)
    {
        /* Verify fs_readdir() */
        res = fs_readdir(&dirp, &entry);

        /* entry.name[0] == 0 means end-of-dir */
        if (res || entry.name[0] == 0)
        {
            if (res < 0)
            {
                LOG_ERR("Error reading dir [%d]\n", res);
            }

            LOG_DBG("Total log count: %d\n", log_count);
            break;
        }

        if (entry.type != FS_DIR_ENTRY_DIR)
        {
            log_count++;
        }
    }

    /* Verify fs_closedir() */
    fs_closedir(&dirp);

    return log_count;
}

int log_get_index(uint8_t m_log_type)
{
    int res;
    struct fs_dir_t dirp;
    static struct fs_dirent entry;

    fs_dir_t_init(&dirp);

    char m_path[40] = " ";

    hpi_log_get_path(m_path, m_log_type);

    res = fs_opendir(&dirp, m_path);
    if (res)
    {
        LOG_ERR("Error opening dir %s [%d]\n", m_path, res);
        return res;
    }

    LOG_DBG("Getting Index %s", m_path);
    for (;;)
    {
        res = fs_readdir(&dirp, &entry);

        /* entry.name[0] == 0 means end-of-dir */
        if (res || entry.name[0] == 0)
        {
            if (res < 0)
            {
                LOG_ERR("Error reading dir [%d]\n", res);
            }

            break;
        }

        if (entry.type != FS_DIR_ENTRY_DIR)
        {
            char file_name[70] = "";

            strcpy(file_name, m_path);
            strcat(file_name, entry.name);

            struct fs_file_t m_file;
            struct fs_dirent trend_file_ent;
            int ret = 0;

            fs_file_t_init(&m_file);

            ret = fs_stat(file_name, &trend_file_ent);
            if (ret < 0)
            {
                LOG_ERR("FAIL: stat %s: %d", file_name, ret);
                if (ret == -ENOENT)
                {
                    LOG_ERR("File not found: %s", file_name);
                }
                // return ret;
            }

            // LOG_DBG("Read from file %s | Size: %d", file_name, trend_file_ent.size);

            int64_t filename_int = atoi(trend_file_ent.name);

            struct hpi_log_index_t m_index;
            m_index.start_time = filename_int; // file_sessionID;
            m_index.log_file_length = trend_file_ent.size;
            m_index.log_type = m_log_type;

            LOG_DBG("Log File Start: %" PRId64 " | Size: %d | Type: %d", m_index.start_time, m_index.log_file_length, m_index.log_type);

            cmdif_send_ble_data_idx((uint8_t *)&m_index, HPI_FILE_IDX_SIZE);
        }
    }

    fs_closedir(&dirp);

    return res;
}

void log_get(uint8_t log_type, int64_t file_id)
{
    char m_file_name[40];
    char temp_file_name[60];

    LOG_DBG("Getting Log type %d , File ID %" PRId64, log_type, file_id);

    hpi_log_get_path(m_file_name, log_type);
    sprintf(temp_file_name, "%s%" PRId64, m_file_name, file_id);
    strcpy(m_file_name, temp_file_name);
    transfer_send_file(m_file_name);
}

void log_delete(uint16_t file_id)
{
    char log_file_name[30];

    snprintf(log_file_name, sizeof(log_file_name), "/lfs/log/%d", file_id);
    LOG_DBG("Deleting %s", log_file_name);
    fs_unlink(log_file_name);
}

void log_wipe_folder(char *folder_path)
{
    int err = 0;
    struct fs_dir_t dir;
    char file_name[100] = "";

    fs_dir_t_init(&dir);

    err = fs_opendir(&dir, folder_path);
    if (err)
    {  
        LOG_DBG("Directory %s not found, creating it", folder_path);
        err = fs_mkdir(folder_path);
        if (err) {
            LOG_ERR("Failed to create directory %s: %d", folder_path, err);
            return;
        }
        
        // Try to open the directory again after creating it
        err = fs_opendir(&dir, folder_path);
        if (err) {
            LOG_ERR("Failed to open directory %s after creation: %d", folder_path, err);
            return;
        }
    }

    while (1)
    {
        struct fs_dirent entry;

        err = fs_readdir(&dir, &entry);
        if (err)
        {
            LOG_ERR("Unable to read directory");
            break;
        }

        /* Check for end of directory listing */
        if (entry.name[0] == '\0')
        {
            break;
        }
        strcpy(file_name, folder_path);
        strcat(file_name, entry.name);

        LOG_DBG("Deleting %s", file_name);

        fs_unlink(file_name);
    }
    fs_closedir(&dir);
}

void log_wipe_trends(void)
{
    char log_file_name[40] = "";

    hpi_log_get_path(log_file_name, HPI_LOG_TYPE_TREND_HR);
    log_wipe_folder(log_file_name);

    hpi_log_get_path(log_file_name, HPI_LOG_TYPE_TREND_SPO2);
    log_wipe_folder(log_file_name);

    hpi_log_get_path(log_file_name, HPI_LOG_TYPE_TREND_TEMP);
    log_wipe_folder(log_file_name);

    hpi_log_get_path(log_file_name, HPI_LOG_TYPE_TREND_STEPS);
    log_wipe_folder(log_file_name);

    hpi_log_get_path(log_file_name, HPI_LOG_TYPE_ECG_RECORD);
    log_wipe_folder(log_file_name);

    hpi_log_get_path(log_file_name, HPI_LOG_TYPE_TREND_BPT);
    log_wipe_folder(log_file_name);

    fs_unlink(hpi_sys_update_time_file);

    hpi_disp_reset_all_last_updated();
    
    LOG_DBG("All trend logs wiped");
}

void log_wipe_records(void)
{
    char log_file_name[40] = "";

    LOG_DBG("Wiping all records");

    hpi_log_get_path(log_file_name, HPI_LOG_TYPE_ECG_RECORD);
    log_wipe_folder(log_file_name);

    hpi_log_get_path(log_file_name, HPI_LOG_TYPE_BIOZ_RECORD);
    log_wipe_folder(log_file_name);

    hpi_log_get_path(log_file_name, HPI_LOG_TYPE_PPG_WRIST_RECORD);
    log_wipe_folder(log_file_name);

    hpi_log_get_path(log_file_name, HPI_LOG_TYPE_PPG_FINGER_RECORD);
    log_wipe_folder(log_file_name);
}