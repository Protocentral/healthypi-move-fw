/*
 * HealthyPi Move - Log Module
 * 
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Author: Ashwin Whitchurch, Protocentral Electronics
 * Contact: ashwin@protocentral.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


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

// Error handling macro for file operations
#define CHECK_FS_OP(op, fname, msg) do { \
    int ret = (op); \
    if (ret < 0) { \
        LOG_ERR("FAIL: %s %s: %d", msg, fname, ret); \
        return ret; \
    } \
} while(0)

// Log path lookup table for better performance and maintainability
static const char* const log_paths[] = {
    [HPI_LOG_TYPE_TREND_HR] = "/lfs/trhr/",
    [HPI_LOG_TYPE_TREND_SPO2] = "/lfs/trspo2/",
    [HPI_LOG_TYPE_TREND_TEMP] = "/lfs/trtemp/",
    [HPI_LOG_TYPE_TREND_STEPS] = "/lfs/trsteps/",
    [HPI_LOG_TYPE_TREND_BPT] = "/lfs/trbpt/",
    [HPI_LOG_TYPE_ECG_RECORD] = "/lfs/ecg/",
    [HPI_LOG_TYPE_BIOZ_RECORD] = "/lfs/bioz/",
    [HPI_LOG_TYPE_PPG_WRIST_RECORD] = "/lfs/ppgw/",
    [HPI_LOG_TYPE_PPG_FINGER_RECORD] = "/lfs/ppgf/",
    [HPI_LOG_TYPE_HRV_RR_RECORD] = "/lfs/hrv/",
};

#define LOG_PATHS_COUNT (sizeof(log_paths) / sizeof(log_paths[0]))

// Externs
extern struct fs_mount_t *mp;
extern const char *hpi_sys_update_time_file;

static int hpi_log_get_path(char *m_path, uint8_t m_log_type)
{
    if (m_path == NULL) {
        LOG_ERR("Invalid path parameter");
        return -EINVAL;
    }

    if (m_log_type < LOG_PATHS_COUNT && log_paths[m_log_type] != NULL) {
        strcpy(m_path, log_paths[m_log_type]);
    } else {
        strcpy(m_path, "/lfs/log/");
    }

    return 0;
}

// Timestamp validation function
// Returns true if timestamp is valid (after Jan 1, 2020 and before Jan 1, 2030)
// This prevents log files from being created with invalid timestamps that could
// indicate system time is not properly synchronized or corrupted.
static bool is_timestamp_valid(int64_t timestamp)
{
    // Define reasonable timestamp bounds for the HealthyPi Move device
    // Jan 1, 2020 00:00:00 UTC = 1577836800 seconds since epoch
    const int64_t MIN_VALID_TIMESTAMP = 1577836800L;
    // Jan 1, 2030 00:00:00 UTC = 1893456000 seconds since epoch  
    const int64_t MAX_VALID_TIMESTAMP = 1893456000L;
    
    return (timestamp >= MIN_VALID_TIMESTAMP && timestamp <= MAX_VALID_TIMESTAMP);
}

// Generic file writer function to reduce code duplication
static int write_trend_to_file(uint8_t log_type, const void *data, size_t data_size, int64_t timestamp)
{
    struct fs_file_t file;
    char fname[50];  // Increased size to accommodate full path
    char base_path[20];
    
    // Validate timestamp before writing
    if (!is_timestamp_valid(timestamp)) {
        LOG_ERR("Invalid timestamp: %" PRId64 " - refusing to write log file", timestamp);
        return -EINVAL;
    }
    
    fs_file_t_init(&file);
    
    // Get base path using existing function
    if (hpi_log_get_path(base_path, log_type) != 0) {
        LOG_ERR("Failed to get path for log type %d", log_type);
        return -EINVAL;
    }
    
    LOG_DBG("Write to file... %s | Size: %zu", fname, data_size);
    
    CHECK_FS_OP(fs_open(&file, fname, FS_O_CREATE | FS_O_RDWR | FS_O_APPEND), "open", fname);
    CHECK_FS_OP(fs_write(&file, data, data_size), "write", fname);
    CHECK_FS_OP(fs_sync(&file), "sync", fname);  // Sync before close
    CHECK_FS_OP(fs_close(&file), "close", fname);
    
    return 0;
}

void hpi_write_ecg_record_file(int32_t *ecg_record_buffer, uint16_t ecg_record_length, int64_t start_ts)
{
    if (ecg_record_buffer == NULL || ecg_record_length == 0) {
        LOG_ERR("Invalid ECG record parameters");
        return;
    }
    
    // Validate timestamp before writing
    if (!is_timestamp_valid(start_ts)) {
        LOG_ERR("Invalid timestamp for ECG record: %" PRId64 " - refusing to write", start_ts);
        return;
    }
    
    // Use generic writer for ECG records
    write_trend_to_file(HPI_LOG_TYPE_ECG_RECORD, ecg_record_buffer, 
                       ecg_record_length * sizeof(int32_t), start_ts);
}
void hpi_write_hrv_rr_record_file(int32_t *rr_buffer, uint16_t rr_count, int64_t start_ts)
{
     if(rr_buffer == NULL || rr_count == 0)
     {
        LOG_ERR("Invalid HRV Record parameters");
        return;
     }

   write_trend_to_file(HPI_LOG_TYPE_HRV_RR_RECORD, rr_buffer, rr_count * sizeof(int32_t),start_ts);
}

void hpi_hr_trend_wr_point_to_file(struct hpi_hr_trend_point_t m_trend_point, int64_t day_ts)
{
    write_trend_to_file(HPI_LOG_TYPE_TREND_HR, &m_trend_point, 
                       sizeof(m_trend_point), day_ts);
}

void hpi_spo2_trend_wr_point_to_file(struct hpi_spo2_point_t m_spo2_point, int64_t day_ts)
{
    write_trend_to_file(HPI_LOG_TYPE_TREND_SPO2, &m_spo2_point, 
                       sizeof(m_spo2_point), day_ts);
}

void hpi_bpt_trend_wr_point_to_file(struct hpi_bpt_point_t m_bpt_point, int64_t day_ts)
{
    write_trend_to_file(HPI_LOG_TYPE_TREND_BPT, &m_bpt_point, 
                       sizeof(m_bpt_point), day_ts);
}

void hpi_temp_trend_wr_point_to_file(struct hpi_temp_trend_point_t m_temp_point, int64_t day_ts)
{
    write_trend_to_file(HPI_LOG_TYPE_TREND_TEMP, &m_temp_point, 
                       sizeof(m_temp_point), day_ts);
}

void hpi_steps_trend_wr_point_to_file(struct hpi_steps_t m_steps_point, int64_t day_ts)
{
    write_trend_to_file(HPI_LOG_TYPE_TREND_STEPS, &m_steps_point, 
                       sizeof(m_steps_point), day_ts);
}

// Generic directory iterator function to reduce duplication
typedef enum {
    DIR_OP_COUNT,
    DIR_OP_INDEX
} dir_operation_t;

static int iterate_directory(uint8_t log_type, dir_operation_t operation)
{
    int res;
    struct fs_dir_t dirp;
    static struct fs_dirent entry;
    uint16_t log_count = 0;
    char m_path[40] = "";

    fs_dir_t_init(&dirp);
    hpi_log_get_path(m_path, log_type);

    res = fs_opendir(&dirp, m_path);
    if (res) {
        LOG_ERR("Error opening dir %s [%d]", m_path, res);
        return res;
    }

    LOG_DBG("%s operation on %s", (operation == DIR_OP_COUNT) ? "Count" : "Index", m_path);
    
    for (;;) {
        res = fs_readdir(&dirp, &entry);

        /* entry.name[0] == 0 means end-of-dir */
        if (res || entry.name[0] == 0) {
            if (res < 0) {
                LOG_ERR("Error reading dir [%d]", res);
            }
            if (operation == DIR_OP_COUNT) {
                LOG_DBG("Total log count: %d", log_count);
            }
            break;
        }

        if (entry.type != FS_DIR_ENTRY_DIR) {
            if (operation == DIR_OP_COUNT) {
                log_count++;
            } else { // DIR_OP_INDEX
                char file_name[300];  // Generous buffer size for file paths
                int path_ret = snprintf(file_name, sizeof(file_name), "%s%s", m_path, entry.name);
                if (path_ret >= sizeof(file_name)) {
                    LOG_ERR("File path too long: %s%s", m_path, entry.name);
                    continue;
                }

                struct fs_dirent trend_file_ent;
                int ret = fs_stat(file_name, &trend_file_ent);
                if (ret < 0) {
                    LOG_ERR("FAIL: stat %s: %d", file_name, ret);
                    if (ret == -ENOENT) {
                        LOG_ERR("File not found: %s", file_name);
                    }
                    continue;
                }

                int64_t filename_int = atoi(trend_file_ent.name);
                struct hpi_log_index_t m_index = {
                    .start_time = filename_int,
                    .log_file_length = trend_file_ent.size,
                    .log_type = log_type
                };

                LOG_DBG("Log File Start: %" PRId64 " | Size: %d | Type: %d", 
                       m_index.start_time, m_index.log_file_length, m_index.log_type);
                cmdif_send_ble_data_idx((uint8_t *)&m_index, HPI_FILE_IDX_SIZE);
            }
        }
    }

    fs_closedir(&dirp);
    return (operation == DIR_OP_COUNT) ? log_count : res;
}

uint16_t log_get_count(uint8_t m_log_type)
{
    return iterate_directory(m_log_type, DIR_OP_COUNT);
}

int log_get_index(uint8_t m_log_type)
{
    return iterate_directory(m_log_type, DIR_OP_INDEX);
}

void log_get(uint8_t log_type, int64_t file_id)
{
    char base_path[40];
    char file_path[60];

    LOG_DBG("Getting Log type %d, File ID %" PRId64, log_type, file_id);

    if (hpi_log_get_path(base_path, log_type) != 0) {
        LOG_ERR("Failed to get path for log type %d", log_type);
        return;
    }
    
    snprintf(file_path, sizeof(file_path), "%s%" PRId64, base_path, file_id);
    transfer_send_file(file_path);
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

// Generic log wipe function to reduce duplication
static void wipe_log_types(const uint8_t *log_types, size_t count, const char *description)
{
    char log_file_name[40];
    
    LOG_DBG("Wiping %s", description);
    
    for (size_t i = 0; i < count; i++) {
        if (hpi_log_get_path(log_file_name, log_types[i]) == 0) {
            log_wipe_folder(log_file_name);
        }
    }
}

void log_wipe_trends(void)
{
    static const uint8_t trend_types[] = {
        HPI_LOG_TYPE_TREND_HR,
        HPI_LOG_TYPE_TREND_SPO2,
        HPI_LOG_TYPE_TREND_TEMP,
        HPI_LOG_TYPE_TREND_STEPS,
        HPI_LOG_TYPE_TREND_BPT,
        HPI_LOG_TYPE_ECG_RECORD,
        HPI_LOG_TYPE_HRV_RR_RECORD
    };
    
    wipe_log_types(trend_types, sizeof(trend_types), "all trend logs");
    
    fs_unlink(hpi_sys_update_time_file);
    hpi_disp_reset_all_last_updated();
    
    LOG_DBG("All trend logs wiped");
}

void log_wipe_records(void)
{
    static const uint8_t record_types[] = {
        HPI_LOG_TYPE_ECG_RECORD,
        HPI_LOG_TYPE_BIOZ_RECORD,
        HPI_LOG_TYPE_PPG_WRIST_RECORD,
        HPI_LOG_TYPE_PPG_FINGER_RECORD
    };
    
    wipe_log_types(record_types, sizeof(record_types), "all records");
}