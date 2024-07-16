#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/dac.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <stdio.h>

#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/settings/settings.h>


#include "sampling_module.h"

LOG_MODULE_REGISTER(fs_module);

K_SEM_DEFINE(sem_fs_module, 0, 1);

const char fname_sessions[30] = "/lfs/sessions";

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

/* Default values are assigned to settings values consuments
 * All of them will be overwritten if storage contain proper key-values
 */
uint8_t angle_val;
uint64_t length_val = 100;
uint16_t length_1_val = 40;
uint32_t length_2_val = 60;
int32_t voltage_val = -3000;

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
        printk("FAIL: mount id %" PRIuPTR " at %s: %d\n",
               (uintptr_t)mp->storage_dev, mp->mnt_point, rc);
        return rc;
    }
    printk("%s mount: %d\n", mp->mnt_point, rc);


    return 0;
}

int hpi_settings_handle_set(const char *name, size_t len, settings_read_cb read_cb,
                            void *cb_arg);
int hpi_settings_handle_commit(void);
int hpi_settings(int (*cb)(const char *name,
                           const void *value, size_t val_len));

/* dynamic main tree handler */
struct settings_handler hpi_handler = {
    .name = "hpi",
    .h_get = NULL,
    .h_set = hpi_settings_handle_set,
    .h_commit = hpi_settings_handle_commit,
    .h_export = hpi_settings};

int hpi_settings_handle_set(const char *name, size_t len, settings_read_cb read_cb,
                            void *cb_arg)
{
    const char *next;
    size_t next_len;
    int rc;

    if (settings_name_steq(name, "angle/1", &next) && !next)
    {
        if (len != sizeof(angle_val))
        {
            return -EINVAL;
        }
        rc = read_cb(cb_arg, &angle_val, sizeof(angle_val));
        printk("<hpi/angle/1> = %d\n", angle_val);
        return 0;
    }

    next_len = settings_name_next(name, &next);

    if (!next)
    {
        return -ENOENT;
    }

    if (!strncmp(name, "length", next_len))
    {
        next_len = settings_name_next(name, &next);

        if (!next)
        {
            rc = read_cb(cb_arg, &length_val, sizeof(length_val));
            printk("<hpi/length> = %" PRId64 "\n", length_val);
            return 0;
        }

        if (!strncmp(next, "1", next_len))
        {
            rc = read_cb(cb_arg, &length_1_val,
                         sizeof(length_1_val));
            printk("<hpi/length/1> = %d\n", length_1_val);
            return 0;
        }

        if (!strncmp(next, "2", next_len))
        {
            rc = read_cb(cb_arg, &length_2_val,
                         sizeof(length_2_val));
            printk("<hpi/length/2> = %d\n", length_2_val);
            return 0;
        }

        return -ENOENT;
    }

    return -ENOENT;
}

int hpi_settings_handle_commit(void)
{
    printk("loading all settings is done\n");
    return 0;
}

int hpi_settings(int (*cb)(const char *name,
                           const void *value, size_t val_len))
{
    printk("export keys under <hpi> handler\n");
    (void)cb("hpi/angle/1", &angle_val, sizeof(angle_val));
    (void)cb("hpi/length", &length_val, sizeof(length_val));
    (void)cb("hpi/length/1", &length_1_val, sizeof(length_1_val));
    (void)cb("hpi/length/2", &length_2_val, sizeof(length_2_val));

    return 0;
}

void init_settings()
{
    int rc;

    rc = settings_subsys_init();
    if (rc)
    {
        printk("settings subsys initialization: fail (err %d)\n", rc);
        return;
    }

    rc = settings_register(&hpi_handler);
    if (rc)
    {
        printk("subtree <%s> handler registered: fail (err %d)\n",
               hpi_handler.name, rc);
    }

    // settings_load();

    int32_t val_s32 = 25;
    /* save certain key-value directly*/
    printk("\nsave <hpi/beta/voltage> key directly: ");
    rc = settings_save_one("hpi/beta/voltage", (const void *)&val_s32,
                           sizeof(val_s32));
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

void record_write_to_file(int current_session_log_id, int current_session_log_counter, struct hpi_ecg_bioz_sensor_data_t *current_session_log_points)
{
    struct fs_file_t file;
    struct fs_statvfs sbuf;

    fs_file_t_init(&file);

    fs_mkdir("/lfs/log");

    char fname[30] = "/lfs/log/";

    printf("Write to file... %d\n", current_session_log_id);
    char session_id_str[5];
    sprintf(session_id_str, "%d", current_session_log_id);
    strcat(fname, session_id_str);

    printf("Session Length: %d\n", current_session_log_counter);

    int rc = fs_open(&file, fname, FS_O_CREATE | FS_O_RDWR | FS_O_APPEND);
    if (rc < 0)
    {
        printk("FAIL: open %s: %d", fname, rc);
    }
    // Session log header
    // rc=fs_write(&file, &current_session_log_, sizeof(current_session_log_id));

    for (int i = 0; i < current_session_log_counter; i++)
    {
        rc = fs_write(&file, &current_session_log_points[i], sizeof(struct hpi_ecg_bioz_sensor_data_t));
    }

    rc = fs_close(&file);
    rc = fs_sync(&file);

    rc = fs_statvfs(mp->mnt_point, &sbuf);
    if (rc < 0)
    {
        printk("FAIL: statvfs: %d\n", rc);
        // goto out;
    }
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

void fs_module_init(void)
{
    int rc;
    struct fs_statvfs sbuf;

    printk("Initing FS...\n");

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

    printk("%s: bsize = %lu ; frsize = %lu ;"
           " blocks = %lu ; bfree = %lu\n",
           mp->mnt_point,
           sbuf.f_bsize, sbuf.f_frsize,
           sbuf.f_blocks, sbuf.f_bfree);
    
    rc = lsdir("/lfs/log");
    if (rc < 0)
    {
        LOG_PRINTK("FAIL: lsdir %s: %d\n", mp->mnt_point, rc);
        // goto out;
    }
}