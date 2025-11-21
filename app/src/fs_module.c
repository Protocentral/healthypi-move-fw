/*
 * HealthyPi Move
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
#include <zephyr/storage/flash_map.h>
#include <zephyr/settings/settings.h>

#include "hpi_common_types.h"
#include "fs_module.h"
#include "ui/move_ui.h"
#include "trends.h"
#include "cmd_module.h"

#ifdef CONFIG_MCUMGR_GRP_FS
#include <zephyr/device.h>
#endif

LOG_MODULE_REGISTER(fs_module, LOG_LEVEL_DBG);

K_SEM_DEFINE(sem_fs_module, 0, 1);
#define PARTITION_NODE DT_NODELABEL(lfs1)

FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage);
static struct fs_mount_t lfs_storage_mnt = {
    .type = FS_LITTLEFS,
    .fs_data = &storage,
    .storage_dev = (void *)FIXED_PARTITION_ID(littlefs_storage),
    .mnt_point = "/lfs",
};

struct fs_mount_t *mp = &lfs_storage_mnt;

#define FILE_TRANSFER_BLE_PACKET_SIZE 64 // (16*7)

static int littlefs_mount(struct fs_mount_t *mp)
{
    int rc;

    /*rc = littlefs_flash_erase((uintptr_t)mp->storage_dev);
    if (rc < 0)
    {
        return rc;
    }*/

    rc = fs_mount(mp);
    if (rc < 0)
    {
        LOG_DBG("FAIL: mount id %" PRIuPTR " at %s: %d\n",
                (uintptr_t)mp->storage_dev, mp->mnt_point, rc);
        return rc;
    }
    LOG_DBG("%s mount: %d\n", mp->mnt_point, rc);

    return 0;
}

/**
 * @brief Check if a file exists
 * @param file_path Path to the file to check
 * @return 0 if file exists, negative error code if file doesn't exist or on error
 */
int fs_check_file_exists(const char *file_path)
{
    struct fs_file_t file;
    int ret;

    if (file_path == NULL)
    {
        LOG_ERR("Invalid file path");
        return -EINVAL;
    }

    fs_file_t_init(&file);

    ret = fs_open(&file, file_path, FS_O_READ);
    if (ret < 0)
    {
        LOG_DBG("File %s does not exist or cannot be opened: %d", file_path, ret);
        return ret;
    }

    fs_close(&file);
    LOG_DBG("File %s exists", file_path);
    return 0;
}

static int lsdir(const char *path)
{
    int res;
    struct fs_dir_t dirp;
    static struct fs_dirent entry;

    fs_dir_t_init(&dirp);
    res = fs_opendir(&dirp, path);
    if (res)
    {
        LOG_ERR("Error opening dir %s [%d]\n", path, res);
        return res;
    }

    LOG_PRINTK("\nListing dir %s ...\n", path);
    for (;;)
    {

        res = fs_readdir(&dirp, &entry);
        if (res || entry.name[0] == 0)
        {
            if (res < 0)
            {
                LOG_ERR("Error reading dir [%d]\n", res);
            }
            break;
        }

        if (entry.type == FS_DIR_ENTRY_DIR)
        {
            LOG_PRINTK("[DIR ] %s\n", entry.name);
        }
        else
        {
            LOG_PRINTK("[FILE] %s (size = %zu)\n",
                       entry.name, entry.size);
        }
    }
    fs_closedir(&dirp);

    return res;
}

uint32_t transfer_get_file_length(char *m_file_name)
{
    LOG_DBG("Getting file length for file %s", m_file_name);

    uint32_t file_len = 0;
    int rc = 0;

    struct fs_dirent dirent;
    rc = fs_stat(m_file_name, &dirent);
    LOG_DBG("%s Stat: %d", m_file_name, rc);
    if (rc >= 0)
    {
        // printk("\nfn '%s' siz %u\n", dirent.name, dirent.size);
        file_len = dirent.size;
    }
    else
    {
        LOG_ERR("Error getting file length %d", rc);
        return 0;
    }

    LOG_DBG("File length: %d", file_len);

    return file_len;
}

void transfer_send_file(char *in_file_name)
{
    LOG_DBG("Start file transfer %s", in_file_name);
    uint8_t m_buffer[FILE_TRANSFER_BLE_PACKET_SIZE + 1];

    uint32_t file_len = transfer_get_file_length(in_file_name);
    uint32_t number_writes = file_len / FILE_TRANSFER_BLE_PACKET_SIZE;

    uint32_t i = 0;
    struct fs_file_t m_file;
    int rc = 0;

    if (file_len % FILE_TRANSFER_BLE_PACKET_SIZE != 0)
    {
        number_writes++; // Last write will be smaller than 64 bytes
    }

    LOG_DBG("Send file: %s Size:%d No Writes: %d", in_file_name, file_len, number_writes);

    fs_file_t_init(&m_file);

    rc = fs_open(&m_file, in_file_name, FS_O_READ);

    if (rc != 0)
    {
        LOG_ERR("Error opening file %d", rc);
        return;
    }

    for (i = 0; i < number_writes; i++)
    {
        rc = fs_read(&m_file, m_buffer, FILE_TRANSFER_BLE_PACKET_SIZE);
        if (rc < 0)
        {
            LOG_ERR("Error reading file %d", rc);
            return;
        }

        cmdif_send_ble_data(m_buffer, rc); // FILE_TRANSFER_BLE_PACKET_SIZE);
        k_sleep(K_MSEC(50));
    }

    rc = fs_close(&m_file);
    if (rc != 0)
    {
        LOG_ERR("Error closing file %d", rc);
        return;
    }

    LOG_INF("File sent!!");
}

void hpi_init_fs_struct(void)
{
    int ret;

    // record_wipe_all();
    //  Create FS directories
    ret = fs_mkdir("/lfs/trhr");
    if (ret)
    {
        LOG_ERR("Unable to create dir (err %d)", ret);
    }
    else
    {
        LOG_DBG("Created dir");
    }

    ret = fs_mkdir("/lfs/trspo2");
    if (ret)
    {
        LOG_ERR("Unable to create dir (err %d)", ret);
    }
    else
    {
        LOG_DBG("Created dir");
    }

    ret = fs_mkdir("/lfs/trtemp");
    if (ret)
    {
        LOG_ERR("Unable to create dir (err %d)", ret);
    }
    else
    {
        LOG_DBG("Created dir");
    }

    ret = fs_mkdir("/lfs/trsteps");
    if (ret)
    {
        LOG_ERR("Unable to create dir (err %d)", ret);
    }
    else
    {
        LOG_DBG("Created dir");
    }

    ret = fs_mkdir("/lfs/trbpt");
    if (ret)
    {
        LOG_ERR("Unable to create dir (err %d)", ret);
    }
    else
    {
        LOG_DBG("Created dir");
    }

    ret = fs_mkdir("/lfs/ecg");
    if (ret)
    {
        LOG_ERR("Unable to create dir (err %d)", ret);
    }
    else
    {
        LOG_DBG("Created dir");
    }

    ret = fs_mkdir("/lfs/log");
    if (ret)
    {
        LOG_ERR("Unable to create dir (err %d)", ret);
    }
    else
    {
        LOG_DBG("Created dir");
    }

    ret = fs_mkdir("/lfs/sys");
    if (ret)
    {
        LOG_ERR("Unable to create dir (err %d)", ret);
    }
    else
    {
        LOG_DBG("Created dir");
    }
}

int fs_load_file_to_buffer(char *m_file_name, uint8_t *buffer, uint32_t buffer_len)
{
    LOG_DBG("Loading file %s to buffer", m_file_name);

    struct fs_file_t m_file;
    int rc = 0;

    fs_file_t_init(&m_file);

    rc = fs_open(&m_file, m_file_name, FS_O_READ);
    if (rc != 0)
    {
        LOG_ERR("Error opening file %d", rc);
        return rc;
    }

    rc = fs_read(&m_file, buffer, buffer_len);
    if (rc < 0)
    {
        LOG_ERR("Error reading file %d", rc);
        return rc;
    }

    rc = fs_close(&m_file);
    if (rc != 0)
    {
        LOG_ERR("Error closing file %d", rc);
        return rc;
    }

    return 0;
}

void fs_write_buffer_to_file(char *m_file_name, uint8_t *buffer, uint32_t buffer_len)
{
    LOG_DBG("Writing buffer to file %s", m_file_name);

    struct fs_file_t m_file;
    int ret = 0;

    ret = fs_unlink(m_file_name);
    if (ret != 0)
    {
        LOG_ERR("Error unlinking file %d", ret);
    }

    fs_file_t_init(&m_file);

    ret = fs_open(&m_file, m_file_name, FS_O_CREATE | FS_O_WRITE);
    if (ret != 0)
    {
        LOG_ERR("Error opening file %d", ret);
        return;
    }

    ret = fs_write(&m_file, buffer, buffer_len);
    if (ret < 0)
    {
        LOG_ERR("Error writing file %d", ret);
        return;
    }

    ret = fs_close(&m_file);
    if (ret != 0)
    {
        LOG_ERR("Error closing file %d", ret);
        return;
    }
}

void fs_module_init(void)
{
    int rc;
    struct fs_statvfs sbuf;

    LOG_DBG("Initing FS...");

    rc = littlefs_mount(mp);
    if (rc < 0)
    {
        return;
    }

    rc = fs_statvfs(mp->mnt_point, &sbuf);
    if (rc < 0)
    {
        // printk("FAIL: statvfs: %d\n", rc);
        // goto out;
    }

    LOG_DBG("%s: bsize = %lu ; frsize = %lu ;"
            " blocks = %lu ; bfree = %lu\n",
            mp->mnt_point, sbuf.f_bsize, sbuf.f_frsize,
            sbuf.f_blocks, sbuf.f_bfree);

    // record_wipe_all();

    rc = lsdir("/lfs");
    if (rc < 0)
    {
        LOG_ERR("FAIL: lsdir %s: %d\n", mp->mnt_point, rc);
    }

    rc = lsdir("/lfs/trtemp");
    if (rc < 0)
    {
        LOG_ERR("FAIL: lsdir %s: %d\n", mp->mnt_point, rc);
    }

    rc = lsdir("/lfs/sys");
    if (rc < 0)
    {
        LOG_ERR("FAIL: lsdir %s: %d\n", mp->mnt_point, rc);
        LOG_INF("Creating FS directory structure");
        hpi_init_fs_struct();
        lsdir("/lfs/trhr");
    }
}
