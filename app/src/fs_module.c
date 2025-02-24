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
#include "record_module.h"

LOG_MODULE_REGISTER(fs_module, LOG_LEVEL_INF);

K_SEM_DEFINE(sem_fs_module, 0, 1);
#define PARTITION_NODE DT_NODELABEL(lfs1)

#if DT_NODE_EXISTS(PARTITION_NODE)
FS_FSTAB_DECLARE_ENTRY(PARTITION_NODE);
#else  /* PARTITION_NODE */
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage);
static struct fs_mount_t lfs_storage_mnt = {
    .type = FS_LITTLEFS,
    .fs_data = &storage,
    .storage_dev = (void *)FIXED_PARTITION_ID(storage_partition),
    .mnt_point = "/lfs",
};
#endif /* PARTITION_NODE */

struct fs_mount_t *mp =
#if DT_NODE_EXISTS(PARTITION_NODE)
    &FS_FSTAB_ENTRY(PARTITION_NODE)
#else
    &lfs_storage_mnt
#endif
    ;

/*
static int littlefs_flash_erase(unsigned int id)
{
    const struct flash_area *pfa;
    int rc;

    rc = flash_area_open(id, &pfa);
    if (rc < 0)
    {
        LOG_ERR("FAIL: unable to find flash area %u: %d\n",
                id, rc);
        return rc;
    }

    printk("Area %u at 0x%x on %s for %u bytes\n",
           id, (unsigned int)pfa->fa_off, pfa->fa_dev->name,
           (unsigned int)pfa->fa_size);

    /* Optional wipe flash contents 
    if (IS_ENABLED(CONFIG_APP_WIPE_STORAGE))
    {
        rc = flash_area_erase(pfa, 0, pfa->fa_size);
        LOG_ERR("Erasing flash area ... %d", rc);
    }

    flash_area_close(pfa);
    return rc;
}*/

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

        printk("%s%s %d\n", entry.name,
        	      (entry.type == FS_DIR_ENTRY_DIR) ? "/" : "",entry.size);

        // if (strstr(entry.name, "") != NULL)
        //{
        strcpy(file_name, "/lfs/log/");
        strcat(file_name, entry.name);

        printk("Deleting %s\n", file_name);
        fs_unlink(file_name);
    }

    fs_closedir(&dir);
}

void write_test_data(void)
{
    struct hpi_hr_trend_day_t hr_data;

    for (int i = 0; i < 60; i++)
    {
        hr_data.hr_points->hr = 80;
        hr_data.hr_points->timestamp = sys_clock_cycle_get_32();
    }

    hpi_rec_write_hour(sys_clock_cycle_get_32(), hr_data);
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
    
    rc = lsdir("/lfs");
    if (rc < 0)
    {
        LOG_PRINTK("FAIL: lsdir %s: %d\n", mp->mnt_point, rc);
        // goto out;
    }

    rc = lsdir("/lfs/hr");
    if (rc < 0)
    {
        LOG_PRINTK("FAIL: lsdir %s: %d\n", mp->mnt_point, rc);
        // goto out;
    }

    //write_test_data();
}

