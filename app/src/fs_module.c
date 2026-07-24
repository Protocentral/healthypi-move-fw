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

    /* Iterate to the end so the return code reflects dir existence/readability;
     * the per-entry listing dump is intentionally silent (used as a boot-time
     * directory-presence check, not a console dump). */
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
    }
    fs_closedir(&dirp);

    return res;
}

void hpi_init_fs_struct(void)
{
    /* Only /lfs/sys is needed now (settings + MAX32664 MSBL firmware). The
     * health store creates its own storage when the durable log lands (H3);
     * the old trend/record/recording dirs are no longer created. */
    int ret = fs_mkdir("/lfs/sys");
    if (ret)
    {
        LOG_DBG("mkdir /lfs/sys: %d (-EEXIST is fine)", ret);
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

    /* First boot (or wiped FS): create the directory structure. */
    if (lsdir("/lfs/sys") < 0)
    {
        LOG_INF("Creating FS directory structure");
        hpi_init_fs_struct();
    }

}
