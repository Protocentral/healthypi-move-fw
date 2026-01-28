#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>
#include <inttypes.h>

LOG_MODULE_REGISTER(storage_info, LOG_LEVEL_INF);

/* Get LittleFS storage usage - used, free, total in bytes, KB, and MB */
void print_littlefs_usage(void)
{
    struct fs_statvfs stat;
    
    if (fs_statvfs("/lfs", &stat) != 0) {
        LOG_ERR("Cannot get LittleFS storage info");
        return;
    }
    
    /* Calculate sizes */
    uint64_t total_bytes = (uint64_t)stat.f_blocks * stat.f_frsize;
    uint64_t free_bytes = (uint64_t)stat.f_bfree * stat.f_frsize;
    uint64_t used_bytes = total_bytes - free_bytes;
    
    /* Calculate percentage */
    uint32_t percent_used = 0;
    if (total_bytes > 0) {
        percent_used = (uint32_t)((used_bytes * 100) / total_bytes);
    }
    
    /* Print in clean format */
    LOG_INF("------ LittleFS Storage Usage ------");
    LOG_INF("Total: %" PRIu64 " bytes (%" PRIu64 " KB | %" PRIu64 " MB)", 
            total_bytes, total_bytes / 1024, total_bytes / 1048576);
    LOG_INF("Used:  %" PRIu64 " bytes (%" PRIu64 " KB | %" PRIu64 " MB)", 
            used_bytes, used_bytes / 1024, used_bytes / 1048576);
    LOG_INF("Free:  %" PRIu64 " bytes (%" PRIu64 " KB | %" PRIu64 " MB)", 
            free_bytes, free_bytes / 1024, free_bytes / 1048576);
    LOG_INF("Usage: %u%%", percent_used);
    LOG_INF("----------------------------------- ");
}