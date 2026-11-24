#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/logging/log.h>
#include "storage.h"

LOG_MODULE_REGISTER(storage, LOG_LEVEL_INF);

/* ---- Which flash partition holds the base firmware's /lfs volume? ----------
 * The first matching devicetree label below is used. If none of them is the one
 * in your overlay / pm_static.yml, define HPI_LFS_STORAGE_ID yourself, e.g. in
 * CMakeLists.txt:  zephyr_compile_definitions(HPI_LFS_STORAGE_ID=PM_<name>_ID)
 * or                zephyr_compile_definitions("HPI_LFS_STORAGE_ID=FIXED_PARTITION_ID(label)")
 * ------------------------------------------------------------------------- */
// #ifndef HPI_LFS_STORAGE_ID
// #if DT_NODE_EXISTS(DT_NODELABEL(lfs_partition))
// #define HPI_LFS_STORAGE_ID DT_FIXED_PARTITION_ID(DT_NODELABEL(lfs_partition))
// #elif DT_NODE_EXISTS(DT_NODELABEL(littlefs_partition))
// #define HPI_LFS_STORAGE_ID DT_FIXED_PARTITION_ID(DT_NODELABEL(littlefs_partition))
// #elif DT_NODE_EXISTS(DT_NODELABEL(littlefs_storage))
// #define HPI_LFS_STORAGE_ID DT_FIXED_PARTITION_ID(DT_NODELABEL(littlefs_storage))
// #elif DT_NODE_EXISTS(DT_NODELABEL(lfs1_partition))
// #define HPI_LFS_STORAGE_ID DT_FIXED_PARTITION_ID(DT_NODELABEL(lfs1_partition))
// #elif DT_NODE_EXISTS(DT_NODELABEL(storage_partition))
// #define HPI_LFS_STORAGE_ID DT_FIXED_PARTITION_ID(DT_NODELABEL(storage_partition))
// #else
// #error "LittleFS partition not found: define HPI_LFS_STORAGE_ID (see comment in storage.c)"
// #endif
// #endif
#define PARTITION_NODE DT_NODELABEL(lfs1)
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(hpi_lfs_data);

static struct fs_mount_t lfs_mnt = {
	.type = FS_LITTLEFS,
	.fs_data = &hpi_lfs_data,
	.storage_dev = (void *)FIXED_PARTITION_ID(littlefs_storage),
	.mnt_point = STORAGE_MNT,
};
int storage_init(void)
{
	int rc = fs_mount(&lfs_mnt);

	if (rc == -EBUSY) {
		rc = 0; /* already mounted */
	}
	if (rc < 0) {
		/* Deliberately NOT formatting: the volume also holds the base
		 * firmware's /lfs/sys MAX32664 images and stored health data. */
		LOG_ERR("LittleFS mount failed: %d", rc);
		return rc;
	}

	rc = fs_mkdir(STORAGE_ACC_DIR);
	if (rc < 0 && rc != -EEXIST) {
		LOG_ERR("mkdir %s failed: %d", STORAGE_ACC_DIR, rc);
		return rc;
	}

	LOG_INF("LittleFS mounted at %s", STORAGE_MNT);
	return 0;
}

int64_t storage_free_bytes(void)
{
	struct fs_statvfs sb;
	int rc = fs_statvfs(STORAGE_MNT, &sb);

	if (rc < 0) {
		return rc;
	}
	return (int64_t)sb.f_bfree * (int64_t)sb.f_frsize;
}