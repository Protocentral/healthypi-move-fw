#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/random/random.h>

#include "datalog_module.h"

uint8_t buf_log[1024]; // 56 bytes / session, 18 sessions / packet

extern struct fs_mount_t *lfs_storage_mnt;
extern uint16_t current_session_ecg_counter;
extern uint16_t current_session_ppg_counter;
extern uint16_t current_session_bioz_counter;
extern struct hpi_sensor_logging_data_t log_buffer[LOG_BUFFER_LENGTH];
struct hpi_log_session_header_t hpi_log_session_header;
extern bool settings_log_data_enabled;

void write_header_to_new_session()
{
    struct fs_file_t file;
    int rc;
    char session_name[200] = "/lfs/";
    char ppg_session_name[200] = "/lfs/";
    char resp_session_name[200] = "/lfs/";


    char session_id_str[20];
    sprintf(session_id_str, "%d", hpi_log_session_header.session_id);
    strcat(session_name, session_id_str);
    strcat(session_name, "_ecg.csv");

    strcat(ppg_session_name, session_id_str);
    strcat(ppg_session_name, "_ppg.csv");

    strcat(resp_session_name, session_id_str);
    strcat(resp_session_name, "_resp.csv");


    char session_record_details[200];

    sprintf(session_record_details, "Session started at: %d/%d/%d %d:%d:%d\n",
            hpi_log_session_header.session_start_time.day,
            hpi_log_session_header.session_start_time.month,
            hpi_log_session_header.session_start_time.year,
            hpi_log_session_header.session_start_time.hour,
            hpi_log_session_header.session_start_time.minute,
            hpi_log_session_header.session_start_time.second);

    char session_vital_header[100] = "ECG\n";
    char ppg_session_vital_header[100] = "PPG\n";
    char resp_session_vital_header[100] = "RESP\n";

    fs_file_t_init(&file);

    rc = fs_open(&file, session_name, FS_O_CREATE | FS_O_RDWR);
    if (rc < 0)
    {
        printk("FAIL: open %s_ecg.csv: %d", session_name, rc);
    }
    rc = fs_write(&file, session_record_details, strlen(session_record_details));
    if (rc < 0)
    {
        printk("File %s_ecg.csv Header write Fail %d\n",session_name, rc);
    }
    rc = fs_write(&file, session_vital_header, strlen(session_vital_header));
    if (rc < 0)
    {
        printk("File %s_ecg.csv vital header write Fail %d\n",session_name, rc);
    }
    rc = fs_close(&file);

    rc = fs_open(&file, ppg_session_name, FS_O_CREATE | FS_O_RDWR);
    if (rc < 0)
    {
        printk("FAIL: open %s_ppg.csv: %d", ppg_session_name, rc);
    }
    rc = fs_write(&file, session_record_details, strlen(session_record_details));
    if (rc < 0)
    {
        printk("File %s_ppg.csv Header write Fail %d\n",session_name, rc);
    }

    rc = fs_write(&file, ppg_session_vital_header, strlen(ppg_session_vital_header));
    if (rc < 0)
    {
        printk("File %s_ppg.csv vital header write Fail %d\n",session_name, rc);
    }
    rc = fs_close(&file);

    rc = fs_open(&file, resp_session_name, FS_O_CREATE | FS_O_RDWR);
    if (rc < 0)
    {
        printk("FAIL: open %s_resp.csv: %d", resp_session_name, rc);
    }
    rc = fs_write(&file, session_record_details, strlen(session_record_details));
    if (rc < 0)
    {
        printk("File %s_resp.csv Header write Fail %d\n",session_name, rc);
    }
    rc = fs_write(&file, resp_session_vital_header, strlen(resp_session_vital_header));
    if (rc < 0)
    {
        printk("File %s_resp.csv vital header write Fail %d\n",session_name, rc);
    }
    rc = fs_close(&file);

    printk("Header written to file... %d\n", hpi_log_session_header.session_id);
}

void set_current_session_id(uint8_t m_sec, uint8_t m_min, uint8_t m_hour, uint8_t m_day, uint8_t m_month, uint8_t m_year)
{
    uint8_t second, minute, hour, day, month, year;
    year = m_year;
    month = m_month;
    day = m_day;
    hour = m_hour;
    minute = m_min;
    second = m_sec;

    flush_current_session_logs();

    // update structure with new session start time
    hpi_log_session_header.session_start_time.year = year;
    hpi_log_session_header.session_start_time.month = month;
    hpi_log_session_header.session_start_time.day = day;
    hpi_log_session_header.session_start_time.hour = hour;
    hpi_log_session_header.session_start_time.minute = minute;
    hpi_log_session_header.session_start_time.second = second;

    uint8_t rand[1];
    sys_rand_get(rand, sizeof(rand));
    hpi_log_session_header.session_id = (rand[0]);
    hpi_log_session_header.session_size = 0;

    printk("Header data for session %d set\n", hpi_log_session_header.session_id);
}

void get_session_header(char *session_id, struct hpi_log_session_header_t *session_header_data)
{
    char m_session_name[100] = "/lfs/";
    strcat(m_session_name,session_id);

    struct fs_file_t m_file;
    fs_file_t_init(&m_file);

    int rc = 0;
    rc = fs_open(&m_file, m_session_name, FS_O_READ);

    if (rc != 0)
    {
        printk("Error opening file %d\n", rc);
    }

    char header_data[36];

    rc = fs_read(&m_file, header_data, 37);
    if (rc < 0)
    {
        printk("Error reading file %d\n", rc);
    }

    rc = fs_close(&m_file);
    if (rc != 0)
    {
        printk("Error closing file %d\n", rc);
        // return;
    }

    char *saveptr;
    strtok_r(header_data, " ", &saveptr); // "Session"
    strtok_r(NULL, " ", &saveptr);        // "started"
    strtok_r(NULL, " ", &saveptr);        // "at:"

    session_header_data->session_start_time.day = (uint8_t)atoi(strtok_r(NULL, "/", &saveptr));
    session_header_data->session_start_time.month = (uint8_t)atoi(strtok_r(NULL, "/", &saveptr));
    session_header_data->session_start_time.year = (uint8_t)atoi(strtok_r(NULL, " ", &saveptr)); // Space after year

    session_header_data->session_start_time.hour = (uint8_t)atoi(strtok_r(NULL, ":", &saveptr));
    session_header_data->session_start_time.minute = (uint8_t)atoi(strtok_r(NULL, ":", &saveptr));
    session_header_data->session_start_time.second = (uint8_t)atoi(strtok_r(NULL, " ", &saveptr));
}

void hpi_get_session_count(void)
{
    int res;
    struct fs_dir_t dirp;
    static struct fs_dirent entry;

    uint16_t session_count = 0;

    fs_dir_t_init(&dirp);

    const char *path = "/lfs";

    /* Verify fs_opendir() */
    res = fs_opendir(&dirp, path);
    if (res)
    {
        printk("Error opening dir %s [%d]\n", path, res);
        //return res;
    }

    for (;;)
    {
        /* Verify fs_readdir() */
        res = fs_readdir(&dirp, &entry);

        /* entry.name[0] == 0 means end-of-dir */
        if (res || entry.name[0] == 0)
        {
            if (res < 0)
            {
                printk("Error reading dir [%d]\n", res);
            }
            break;
        }

        if (entry.type != FS_DIR_ENTRY_DIR)
        {
            session_count++;
        }
    }

    /* Verify fs_closedir() */
    fs_closedir(&dirp);

    printk("Total session count: %d\n", session_count);

    cmdif_send_session_count(session_count,84);

}

void hpi_get_session_index(void)
{
    int res;
    struct fs_dir_t dirp;
    static struct fs_dirent entry;

    fs_dir_t_init(&dirp);

    const char *path = "/lfs/";

    /* Verify fs_opendir() */
    res = fs_opendir(&dirp, path);
    if (res)
    {
        printk("Error opening dir %s [%d]\n", path, res);
        //return res;
    }

    for (;;)
    {
        /* Verify fs_readdir() */
        res = fs_readdir(&dirp, &entry);

        /* entry.name[0] == 0 means end-of-dir */
        if (res || entry.name[0] == 0)
        {
            if (res < 0)
            {
                printk("Error reading dir [%d]\n", res);
            }

            break;
        }

        if (entry.type != FS_DIR_ENTRY_DIR)
        {
            //char session_header[80];

            hpi_log_session_header.session_id =(uint16_t)atoi(entry.name);
            hpi_log_session_header.session_size = (uint32_t)entry.size;

            get_session_header(entry.name, &hpi_log_session_header);  

            char file_name[50];
            strcpy(file_name,entry.name);
            char *saveptr;
            strtok_r(file_name, "_", &saveptr);
            char *extension =  strtok_r(NULL, ".", &saveptr);
            if (strncmp(extension,"ECG",strlen(extension)) == 0)
                hpi_log_session_header.file_no = 1;
            else if (strncmp(extension,"PPG",strlen(extension)) == 0)
                hpi_log_session_header.file_no = 2;
            else if (strncmp(extension,"RESP",strlen(extension)) == 0)
                hpi_log_session_header.file_no = 3;
            else
                hpi_log_session_header.file_no = 1;
            
            memcpy(&buf_log, &hpi_log_session_header, 15);
            cmdif_send_ble_data_idx(buf_log, 15);
            printk("Header of session id: %d size %d sent\n", hpi_log_session_header.session_id,hpi_log_session_header.session_size);
        }
    }
    fs_closedir(&dirp);
}

uint32_t hpi_log_session_get_length(char *m_file_name)
{
    int res;
    struct fs_dir_t dirp;
    static struct fs_dirent entry;
    uint32_t session_len = 0;

    fs_dir_t_init(&dirp);

    const char *path = "/lfs/";

    res = fs_opendir(&dirp, path);
    if (res)
    {
        printk("Error opening dir %s [%d]\n", path, res);
        return res;
    }

    for (;;)
    {
        res = fs_readdir(&dirp, &entry);
        if (res || entry.name[0] == 0)
        {
            if (res < 0)
            {
                printk("Error reading dir [%d]\n", res);
            }
            break;
        }

        printk("m_file_name %s entry.name %s %d %d\n",m_file_name,entry.name,sizeof(m_file_name),sizeof(entry.name));
        if (strcmp(m_file_name, entry.name) == 0)
        {
            session_len = entry.size;
            break;
        }
    }
    fs_closedir(&dirp);
    return session_len;
}

void hpi_session_fetch(uint16_t session_id,uint8_t file_no)
{
    int8_t m_buffer[FILE_TRANSFER_BLE_PACKET_SIZE];
    //char m_session_id[20];
    char m_session_name[30];
    char m_session_path[50] = "/lfs/";
    char m_session_file_type[5];

    if (file_no == 1)
        strcpy(m_session_file_type, "ECG");
    else if (file_no == 2)
        strcpy(m_session_file_type, "PPG");
    else if (file_no == 3)
        strcpy(m_session_file_type, "RESP");
    else
        strcpy(m_session_file_type, "ECG");

    snprintf(m_session_name, sizeof(m_session_name), "%d_%s.CSV", session_id,m_session_file_type);

    uint32_t session_len = hpi_log_session_get_length(m_session_name);

    uint16_t number_writes = session_len / FILE_TRANSFER_BLE_PACKET_SIZE;

    uint32_t i = 0;
    struct fs_file_t m_file;
    int rc = 0;

    if (session_len % FILE_TRANSFER_BLE_PACKET_SIZE != 0)
    {
        number_writes++; // Last write will be smaller than 64 bytes
    }

    strcat(m_session_path,m_session_name);

    fs_file_t_init(&m_file);

    rc = fs_open(&m_file, m_session_path, FS_O_READ);

    if (rc != 0)
    {
        printk("Error opening file %d\n", rc);
        return;
    }

    for (i = 0; i < number_writes; i++)
    {
        memset(m_buffer,0,64);

        rc = fs_read(&m_file, m_buffer, FILE_TRANSFER_BLE_PACKET_SIZE);
        if (rc < 0)
        {
            printk("Error reading file %d\n", rc);
            return;
        }
        printk("Writing %d\n write",i);
        
        cmdif_send_ble_session_data(m_buffer, FILE_TRANSFER_BLE_PACKET_SIZE); // FILE_TRANSFER_BLE_PACKET_SIZE);
        k_sleep(K_MSEC(50));
    }

    rc = fs_close(&m_file);
    if (rc != 0)
    {
        printk("Error closing file %d\n", rc);
        return;
    }

    printk("sess sent\n");
}

void hpi_datalog_delete_all(void)
{
    int res;
    struct fs_dir_t dir;

    char session_name[100] = "";

    fs_dir_t_init(&dir);

    res = fs_opendir(&dir, "/lfs/");
    if (res)
    {
        printk("Unable to open (err %d)", res);
    }

    while (1)
    {
        struct fs_dirent entry;

        res = fs_readdir(&dir, &entry);
        if (res)
        {
            printk("Unable to read directory");
            break;
        }

        /* entry.name[0] == 0 means end-of-dir */
        if (res || entry.name[0] == 0)
        {
            if (res < 0)
            {
                printk("Error reading dir [%d]\n", res);
            }
            break;
        }

        if (entry.type != FS_DIR_ENTRY_DIR)
        {
            strcpy(session_name, "/lfs/");
            strcat(session_name, entry.name);
            fs_unlink(session_name);
            printk("Deleting %s\n", session_name);

        }
    }

    fs_closedir(&dir);

    printk("All sessions deleted\n");
}

void hpi_datalog_delete_session(uint16_t session_id,uint8_t file_no)
{
    struct fs_dir_t dir;
    fs_dir_t_init(&dir);

    char session_name[20];
    char m_session_file_type[5];
    printk("session_id %d file_no %d\n",session_id,file_no);

    if (file_no == 1)
        strcpy(m_session_file_type, "ECG");
    else if (file_no == 2)
        strcpy(m_session_file_type, "PPG");
    else
        strcpy(m_session_file_type, "RESP");

    snprintf(session_name, sizeof(session_name), "/lfs/%d_%s.CSV", session_id,m_session_file_type);
    fs_unlink(session_name);
        
}

void hpi_datalog_start_session(uint8_t *in_pkt_buf)
{
    set_current_session_id(in_pkt_buf[1], in_pkt_buf[2], in_pkt_buf[3], in_pkt_buf[4], in_pkt_buf[5], in_pkt_buf[6]);

    struct fs_statvfs sbuf;
    int rc = fs_statvfs(lfs_storage_mnt->mnt_point, &sbuf);

    if (rc < 0)
    {
        printk("FAILED to return stats");
    }

    printk("free: %lu, available : %f\n", sbuf.f_bfree, (0.25 * sbuf.f_blocks));

    if (sbuf.f_bfree >= (0.25 * sbuf.f_blocks))
    {
        settings_log_data_enabled = true;
        cmdif_send_memory_status(CMD_LOGGING_MEMORY_FREE);
        write_header_to_new_session();
    }
    else
    {
        // if memory available is less than 25%
        cmdif_send_memory_status(CMD_LOGGING_MEMORY_NOT_AVAILABLE);
    }
}

void hpi_log_session_write_file(int parameter_to_be_written)
{
    switch (parameter_to_be_written)
    {
        case ECG_DATA:
            struct fs_file_t ecg_file;
            fs_file_t_init(&ecg_file);

            char ecg_session_name[100] = "/lfs/";
            char ecg_session_id_str[100];
            char ecg_sensor_data[32];

            
            sprintf(ecg_session_id_str, "%d", hpi_log_session_header.session_id);
            strcat(ecg_session_name, ecg_session_id_str);
            strcat(ecg_session_name, "_ecg.csv");

            //printk("session_name %s\n",session_name);

            int ecg_rc = fs_open(&ecg_file, ecg_session_name, FS_O_CREATE | FS_O_RDWR | FS_O_APPEND);
            if (ecg_rc < 0)
            {
                printk("FAIL: open %s: %d", ecg_session_name, ecg_rc);
            }

            for (int i = 0; i < current_session_ecg_counter; i++)
            {
                snprintf(ecg_sensor_data, sizeof(ecg_sensor_data), "%d\n", log_buffer[i].log_ecg_sample);
                ecg_rc = fs_write(&ecg_file, ecg_sensor_data, strlen(ecg_sensor_data));
                //printk("ecg_rc %d\n",ecg_rc);
            }


            ecg_rc = fs_close(&ecg_file);

            struct fs_file_t resp_file;
            fs_file_t_init(&resp_file);

            char resp_session_name[100] = "/lfs/";
            char resp_session_id_str[100];
            char resp_sensor_data[32];

            
            sprintf(resp_session_id_str, "%d", hpi_log_session_header.session_id);
            strcat(resp_session_name, resp_session_id_str);
            strcat(resp_session_name, "_resp.csv");

            //printk("session_name %s\n",session_name);

            int resp_rc = fs_open(&resp_file, resp_session_name, FS_O_CREATE | FS_O_RDWR | FS_O_APPEND);
            if (resp_rc < 0)
            {
                printk("FAIL: open %s: %d", resp_session_name, resp_rc);
            }

            for (int i = 0; i < current_session_bioz_counter; i++)
            {
                snprintf(resp_sensor_data, sizeof(resp_sensor_data), "%d\n", log_buffer[i].log_bioz_sample);
                resp_rc = fs_write(&resp_file, resp_sensor_data, strlen(resp_sensor_data));
            }


            resp_rc = fs_close(&resp_file);

            break;

        case PPG_DATA:
            struct fs_file_t ppg_file;
            fs_file_t_init(&ppg_file);

            char ppg_session_name[100] = "/lfs/";
            char ppg_session_id_str[100];
            char ppg_sensor_data[32];

            
            sprintf(ppg_session_id_str, "%d", hpi_log_session_header.session_id);
            strcat(ppg_session_name, ppg_session_id_str);
            strcat(ppg_session_name, "_ppg.csv");

            //printk("session_name %s\n",session_name);

            int ppg_rc = fs_open(&ppg_file, ppg_session_name, FS_O_CREATE | FS_O_RDWR | FS_O_APPEND);
            if (ppg_rc < 0)
            {
                printk("FAIL: open %s: %d", ppg_session_name, ppg_rc);
            }

            for (int i = 0; i < current_session_ppg_counter; i++)
            {
                snprintf(ppg_sensor_data, sizeof(ppg_sensor_data), "%d\n", log_buffer[i].log_ppg_sample);
                ppg_rc = fs_write(&ppg_file, ppg_sensor_data, strlen(ppg_sensor_data));
            }


            ppg_rc = fs_close(&ppg_file);
            break;

        default:
            break;

    }
}










