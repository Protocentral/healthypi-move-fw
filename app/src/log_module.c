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

struct hpi_log_header_t log_get_file_header(char* file_id)
{
    LOG_DBG("Getting header for file %s\n", file_id);

    LOG_DBG("Header size: %d\n", HPI_LOG_HEADER_SIZE);

    struct hpi_log_header_t m_header;

    //char m_file_name[30];
    //snprintf(m_file_name, sizeof(m_file_name), "/lfs/log/%u", file_id);

    struct fs_file_t m_file;
    fs_file_t_init(&m_file);

    int rc = 0;
    rc = fs_open(&m_file, file_id, FS_O_READ);

    if (rc != 0)
    {
        printk("Error opening file %d\n", rc);
        // return;
    }

    rc = fs_read(&m_file, (struct hpi_log_header_t *) &m_header, HPI_LOG_HEADER_SIZE);
    if (rc < 0)
    {
        printk("Error reading file %d\n", rc);
        // return;
    }

    // m_header = *((struct tes_session_log_header_t *)m_header_buffer);

    rc = fs_close(&m_file);
    if (rc != 0)
    {
        printk("Error closing file %d\n", rc);
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

    uint16_t log_count = 0;
    uint16_t buf_log_index = 0;

    fs_dir_t_init(&dirp);

    char m_path[40] = "/lfs/log";

    hpi_log_get_path(m_path, m_log_type);

    /* Verify fs_opendir() */
    res = fs_opendir(&dirp, m_path);
    if (res)
    {
        LOG_ERR("Error opening dir %s [%d]\n", m_path, res);
        return res;
    }

    LOG_PRINTK("\nGet Index CMD %s ...\n", m_path);
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

            /*printk("Total log count: %d\n", log_count);
            if (log_count > 0)
            {
                //cmdif_send_ble_data_idx(buf_log, buf_log_index);
                // cmdif_send_ble_command(WISER_CMD_SESS_LOG_GET_INDEX,buf_log,buf_log_index);
            }*/

            break;
        }

        if (entry.type != FS_DIR_ENTRY_DIR)
        {   
            char file_name[70] = "";
            strcpy(file_name, m_path);

            strcat(file_name,entry.name);

            //char* file_name = entry.name;
            //uint16_t session_id = atoi(entry.name);

            struct hpi_log_header_t m_header = log_get_file_header(file_name);
            
            //struct hpi_log_header_t m_header;

            //m_header.log_file_length = entry.size;
            

            LOG_DBG("Log File Start: %ld | Size: %d", m_header.start_time, m_header.log_file_length);

            //memcpy(&buf_log, &m_header, sizeof(struct hpi_log_trend_header_t));
            // buf_log_index += sizeof(struct tes_session_log_header_t);

            cmdif_send_ble_data_idx(&m_header, HPI_LOG_HEADER_SIZE);

            // log_count++;

            // LOG_PRINTK("%d,%d\n", session_id, session_size);
            /*LOG_PRINTK("Session ID: %d\n", session_id);

            k_sleep(K_MSEC(100));
            for(int i=0;i<sizeof(struct tes_session_log_header_t);i++)
            {
                LOG_PRINTK("%02X", buf_log[i]);
                k_sleep(K_MSEC(10));
            }
            LOG_PRINTK("\n");

            LOG_PRINTK("TM Size: %d %d\n", sizeof(struct tm), sizeof(int));
            */
        }

        //LOG_PRINTK("Log Buff Size: %d\n", buf_log_index);
    }

    // cmdif_send_ble_command(WISER_CMD_SESS_LOG_GET_INDEX, buf_log, buf_log_index);
    // cmdif_send_ble_data_idx(buf_log, buf_log_index);

    /* Verify fs_closedir() */
    fs_closedir(&dirp);

    return res;
}

void log_get(uint16_t file_id)
{
    printk("Get Log %u\n", file_id);
    transfer_send_file(file_id);
}

void log_delete(uint16_t file_id)
{
    char log_file_name[30];

    snprintf(log_file_name, sizeof(log_file_name), "/lfs/log/%d", file_id);

    printk("Deleting %s\n", log_file_name);
    fs_unlink(log_file_name);
}

void log_wipe_all(void)
{
    int err;
    struct fs_dir_t dir;

    char file_name[100] = "";

    fs_dir_t_init(&dir);

    err = fs_opendir(&dir, "/lfs/log");
    if (err)
    {
        printk("Unable to open (err %d)", err);
    }

    while (1)
    {
        struct fs_dirent entry;

        err = fs_readdir(&dir, &entry);
        if (err)
        {
            printk("Unable to read directory");
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
        strcpy(file_name, "/lfs/log/");
        strcat(file_name, entry.name);

        printk("Deleting %s\n", file_name);
        fs_unlink(file_name);
    }

    fs_closedir(&dir);
}