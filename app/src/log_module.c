#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/dac.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <stdio.h>

#include <zephyr/sys/reboot.h>

#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>
#include <time.h>

#include "log_module.h"
#include "cmd_module.h"
#include "fs_module.h"

LOG_MODULE_REGISTER(log_module, LOG_LEVEL_DBG);

#define MAX_SESSION_LOG_LENGTH 2048
const char fname_log_seq[30] = "/lfs/log_seq";

// Buffers to store session log files
uint8_t buf_log[1024]; // 56 bytes / session, 18 sessions / packet

// Externs
extern struct fs_mount_t *mp;

static int hpi_log_get_path(char* m_path, uint8_t m_log_type)
{
    if(m_log_type == HPI_LOG_TYPE_TREND_HR)
    {
        strcpy(m_path, "/lfs/trhr/");
    } 
    else if(m_log_type == HPI_LOG_TYPE_TREND_SPO2)
    {
        strcpy(m_path, "/lfs/trspo2/");
    }
    else if(m_log_type == HPI_LOG_TYPE_TREND_TEMP)
    {
        strcpy(m_path, "/lfs/trtemp/");
    }
    else if(m_log_type == HPI_LOG_TYPE_TREND_STEPS)
    {
        strcpy(m_path, "/lfs/trsteps/");
    }
    else if(m_log_type == HPI_LOG_TYPE_TREND_BPT)
    {
        strcpy(m_path, "/lfs/trbpt/");
    }
    else if(m_log_type == HPI_LOG_TYPE_ECG_RECORD)
    {
        strcpy(m_path, "/lfs/ecg/");
    }
    else if(m_log_type == HPI_LOG_TYPE_BIOZ_RECORD)
    {
        strcpy(m_path, "/lfs/bioz/");
    }
    else if(m_log_type == HPI_LOG_TYPE_PPG_WRIST_RECORD)
    {
        strcpy(m_path, "/lfs/ppgw/");
    }
    else if(m_log_type == HPI_LOG_TYPE_PPG_FINGER_RECORD)
    {
        strcpy(m_path, "/lfs/ppgf/");
    }
    else
    {
        strcpy(m_path, "/lfs/log/");
    } 

    return 0;
}

struct hpi_log_header_t log_get_file_header(char* file_path_name)
{
    LOG_DBG("Getting header for file %s\n", file_path_name);
    LOG_DBG("Header size: %d\n", HPI_LOG_HEADER_SIZE);

    //char m_file_name[30];
    //snprintf(m_file_name, sizeof(m_file_name), "/lfs/log/%u", file_id);

    struct fs_file_t m_file;
    struct fs_dirent trend_file_ent;
    int ret=0;

    fs_file_t_init(&m_file);

    ret = fs_stat(file_path_name, &trend_file_ent);
    if (ret < 0)
    {
        LOG_ERR("FAIL: stat %s: %d", file_path_name, ret);
        if (ret == -ENOENT)
        {
            LOG_ERR("File not found: %s", file_path_name);
        }
        //return ret;
    }

    LOG_DBG("Read from file %s | Size: %d", file_path_name, trend_file_ent.size);

    int rc = 0;
    rc = fs_open(&m_file, file_path_name, FS_O_READ);

    if (rc != 0)
    {
        LOG_ERR("Error opening file %d", rc);
        // return;
    }

    int64_t file_sessionID=0;

    rc = fs_read(&m_file, (int64_t) &file_sessionID, 8);
    if (rc < 0)
    {
        LOG_ERR("Error reading file %d", rc);
        // return;
    }

    int64_t filename_int = atoi(trend_file_ent.name);
   
    struct hpi_log_header_t m_header;
    m_header.start_time = filename_int;//file_sessionID;
    m_header.log_file_length = trend_file_ent.size;
    m_header.log_type = HPI_LOG_TYPE_TREND_HR;

    // m_header = *((struct tes_session_log_header_t *)m_header_buffer);

    rc = fs_close(&m_file);
    if (rc != 0)
    {
        LOG_ERR("Error closing file %d", rc);
        // return;
    }

    return m_header;
}

uint16_t log_get_count(uint8_t m_log_type)
{
    int res;
    struct fs_dir_t dirp;
    static struct fs_dirent entry;

    uint16_t log_count = 0;
    char m_path[40] = "/lfs/log";

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

            printk("Total log count: %d\n", log_count);
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

    LOG_PRINTK("\nGet Index CMD %s ...\n", m_path);
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

            struct hpi_log_header_t m_header = log_get_file_header(file_name);          

            LOG_DBG("Log File Start: %" PRId64 " | Size: %d", m_header.start_time, m_header.log_file_length);

            cmdif_send_ble_data_idx(&m_header, HPI_LOG_HEADER_SIZE);
        }
    }

    fs_closedir(&dirp);

    return res;
}

void log_get(uint8_t log_type, int64_t file_id)
{
    LOG_DBG("Getting Log type %d , File ID %" PRId64 , log_type, file_id);
    char m_file_name[40];
    hpi_log_get_path(m_file_name, log_type);
    char temp_file_name[60];
    sprintf(temp_file_name, "%s%" PRId64, m_file_name, file_id);
    strcpy(m_file_name, temp_file_name);
    //strcat(m_file_name, "/");

    //snprintf(m_file_name, sizeof(m_file_name), "/lfs/trhr/%" PRId64, file_id);
    transfer_send_file(m_file_name);
}

void log_delete(uint16_t file_id)
{
    char log_file_name[30];
    snprintf(log_file_name, sizeof(log_file_name), "/lfs/log/%d", file_id);
    LOG_DBG("Deleting %s", log_file_name);
    fs_unlink(log_file_name);
}

void log_wipe_folder(char* folder_path)
{
    int err=0;
    struct fs_dir_t dir;
    char file_name[100] = "";

    fs_dir_t_init(&dir);

    err = fs_opendir(&dir, folder_path);
    if (err)
    {
        LOG_ERR("Unable to open (err %d)", err);
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

        // printk("%s%s %d\n", entry.name,
        //	      (entry.type == FS_DIR_ENTRY_DIR) ? "/" : "",entry.size);

        // if (strstr(entry.name, "") != NULL)
        //{
        strcpy(file_name, folder_path);
        strcat(file_name, entry.name);

        LOG_DBG("Deleting %s", file_name);

        fs_unlink(file_name);
    }
    fs_closedir(&dir);
}

void log_wipe_all(void)
{
    char log_file_name[40]="";

    // Wipe all trend logs

    LOG_DBG("Wiping all trend logs");

    hpi_log_get_path(log_file_name, HPI_LOG_TYPE_TREND_HR);
    log_wipe_folder(log_file_name);

    hpi_log_get_path(log_file_name, HPI_LOG_TYPE_TREND_SPO2);
    log_wipe_folder(log_file_name);

    hpi_log_get_path(log_file_name, HPI_LOG_TYPE_TREND_TEMP);
    log_wipe_folder(log_file_name);

    hpi_log_get_path(log_file_name, HPI_LOG_TYPE_TREND_STEPS);
    log_wipe_folder(log_file_name);
}