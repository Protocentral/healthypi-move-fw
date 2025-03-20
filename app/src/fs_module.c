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

#ifdef CONFIG_MCUMGR_GRP_FS
#include <zephyr/device.h>
#endif

LOG_MODULE_REGISTER(fs_module, LOG_LEVEL_INF);

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

static int lsdir(const char *path)
{
    int res;
    struct fs_dir_t dirp;
    static struct fs_dirent entry;

    fs_dir_t_init(&dirp);

    /* Verify fs_opendir() */
    res = fs_opendir(&dirp, path);
    if (res)
    {
        LOG_ERR("Error opening dir %s [%d]\n", path, res);
        return res;
    }

    LOG_PRINTK("\nListing dir %s ...\n", path);
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

    /* Verify fs_closedir() */
    fs_closedir(&dirp);

    return res;
}

void record_wipe_all(void)
{
    int err;
    struct fs_dir_t dir;

    char file_name[100] = "";

    fs_dir_t_init(&dir);

    err = fs_opendir(&dir, "/lfs/trhr");
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

        LOG_DBG("%s%s %d\n", entry.name,
                (entry.type == FS_DIR_ENTRY_DIR) ? "/" : "", entry.size);

        // if (strstr(entry.name, "") != NULL)
        //{
        strcpy(file_name, "/lfs/trhr/");
        strcat(file_name, entry.name);

        LOG_DBG("Deleting %s\n", file_name);
        fs_unlink(file_name);
    }

    fs_closedir(&dir);
}

uint32_t transfer_get_file_length(char *m_file_name)
{
    printk("Getting file length for file %s\n", m_file_name);

    uint32_t file_len = 0;
    int rc = 0;

    struct fs_dirent dirent;
    rc = fs_stat(m_file_name, &dirent);
    printk("\n%s stat: %d\n", m_file_name, rc);
    if (rc >= 0)
    {
        printk("\nfn '%s' siz %u\n", dirent.name, dirent.size);
        file_len = dirent.size;
    }

    printk("File length: %d\n", file_len);

    return file_len;
}

void transfer_send_file(uint16_t file_id)
{
    printk("Sending file %u\n", file_id);
    uint8_t m_buffer[FILE_TRANSFER_BLE_PACKET_SIZE + 1];

    char m_file_name[30] = "/lfs/trhr/67db5a80";
    //snprintf(m_file_name, sizeof(m_file_name), "/lfs/log/%u", file_id);

    uint32_t file_len = transfer_get_file_length(m_file_name);
    uint32_t number_writes = file_len / FILE_TRANSFER_BLE_PACKET_SIZE;

    uint32_t i = 0;
    struct fs_file_t m_file;
    int rc = 0;

    if (file_len % FILE_TRANSFER_BLE_PACKET_SIZE != 0)
    {
        number_writes++; // Last write will be smaller than 64 bytes
    }

    printk("File name: %s Size:%d NW: %d \n", m_file_name, file_len, number_writes);

    fs_file_t_init(&m_file);

    rc = fs_open(&m_file, m_file_name, FS_O_READ);

    if (rc != 0)
    {
        printk("Error opening file %d\n", rc);
        return;
    }

    for (i = 0; i < number_writes; i++)
    {
        rc = fs_read(&m_file, m_buffer, FILE_TRANSFER_BLE_PACKET_SIZE);
        if (rc < 0)
        {
            printk("Error reading file %d\n", rc);
            return;
        }

        cmdif_send_ble_data(m_buffer, rc); // FILE_TRANSFER_BLE_PACKET_SIZE);
        k_sleep(K_MSEC(50));
    }

    rc = fs_close(&m_file);
    if (rc != 0)
    {
        printk("Error closing file %d\n", rc);
        return;
    }

    printk("File sent\n");
}



void hpi_init_fs_struct(void)
{
    struct fs_dir_t dir;
    int ret;

    //record_wipe_all();

    // Create FS directories

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
            mp->mnt_point,
            sbuf.f_bsize, sbuf.f_frsize,
            sbuf.f_blocks, sbuf.f_bfree);

    //record_wipe_all();

    rc = lsdir("/lfs");
    if (rc < 0)
    {
        LOG_ERR("FAIL: lsdir %s: %d\n", mp->mnt_point, rc);
    }

    rc = lsdir("/lfs/trhr");
    if (rc < 0)
    {
        LOG_ERR("FAIL: lsdir %s: %d\n", mp->mnt_point, rc);
        LOG_INF("Creating trend directory structure");
        hpi_init_fs_struct();
        lsdir("/lfs/trhr");
    }
}
