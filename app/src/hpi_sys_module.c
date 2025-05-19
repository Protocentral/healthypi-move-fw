#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <stdio.h>

#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <time.h>

#include "hpi_common_types.h"
#include "hpi_sys.h"

LOG_MODULE_REGISTER(hpi_sys_module, LOG_LEVEL_DBG);

static const char hpi_sys_update_time_file[] = "/lfs/sys/hpi_sys_update_time";
// Externs
extern struct k_sem sem_hpi_sys_thread_start;

static struct hpi_last_update_time_t g_hpi_last_update = {
    .bp_last_update_ts = 0,
    .hr_last_update_ts = 0,
    .spo2_last_update_ts = 0,
    .ecg_last_update_ts = 0,

    .hr_last_value = 0,
    .spo2_last_value = 0,
    .bp_sys_last_value = 0,
    .bp_dia_last_value = 0,
};
    

K_MUTEX_DEFINE(mutex_hpi_last_update_time);

static int hpi_sys_store_update_time(void)
{
    int ret=0;

    LOG_DBG("Storing last update time");
  
    struct fs_file_t m_file;
    fs_file_t_init(&m_file);

    ret = fs_open(&m_file, hpi_sys_update_time_file, FS_O_CREATE | FS_O_WRITE);
    if (ret != 0)
    {
        LOG_ERR("Error opening file %d", ret);
        return ret;
    }

    ret = fs_write(&m_file, &g_hpi_last_update, sizeof(g_hpi_last_update));
    if (ret < 0)
    {
        LOG_ERR("Error writing file %d", ret);
        return ret;
    }

    ret = fs_close(&m_file);
    if (ret != 0)
    {
        LOG_ERR("Error closing file %d", ret);
        return ret;
    }

    return ret;
}

static int hpi_sys_load_update_time(void)
{
    int ret=0;

    struct hpi_last_update_time_t m_hpi_last_update_time = {
        .bp_last_update_ts = 0,
        .hr_last_update_ts = 0,
        .spo2_last_update_ts = 0,
        .ecg_last_update_ts = 0,

        .hr_last_value = 0,
        .spo2_last_value = 0,
        .bp_sys_last_value = 0,
        .bp_dia_last_value = 0,

    };

    struct fs_file_t m_file;
    fs_file_t_init(&m_file);

    ret = fs_open(&m_file, hpi_sys_update_time_file, FS_O_READ);
    if (ret != 0)
    {
        LOG_ERR("Error opening file %d", ret);
        return ret;
    }

    ret = fs_read(&m_file, &m_hpi_last_update_time, sizeof(m_hpi_last_update_time));
    if (ret < 0)
    {
        LOG_ERR("Error reading file %d", ret);
        return ret;
    }

    ret = fs_close(&m_file);
    if (ret != 0)
    {
        LOG_ERR("Error closing file %d", ret);
        return ret;
    }

    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    g_hpi_last_update.bp_last_update_ts = m_hpi_last_update_time.bp_last_update_ts;
    g_hpi_last_update.hr_last_update_ts = m_hpi_last_update_time.hr_last_update_ts;
    g_hpi_last_update.spo2_last_update_ts = m_hpi_last_update_time.spo2_last_update_ts;
    g_hpi_last_update.ecg_last_update_ts = m_hpi_last_update_time.ecg_last_update_ts;
    g_hpi_last_update.hr_last_value = m_hpi_last_update_time.hr_last_value;
    g_hpi_last_update.spo2_last_value = m_hpi_last_update_time.spo2_last_value;
    g_hpi_last_update.bp_sys_last_value = m_hpi_last_update_time.bp_sys_last_value;
    g_hpi_last_update.bp_dia_last_value = m_hpi_last_update_time.bp_dia_last_value;
    k_mutex_unlock(&mutex_hpi_last_update_time);

    return ret;
}

void hpi_sys_set_last_hr_update(uint16_t hr_last_value, int64_t hr_last_update_ts)
{
    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    g_hpi_last_update.hr_last_value = hr_last_value;
    g_hpi_last_update.hr_last_update_ts = hr_last_update_ts;
    k_mutex_unlock(&mutex_hpi_last_update_time);
} 

void hpi_sys_set_last_spo2_update(uint8_t spo2_last_value, int64_t spo2_last_update_ts)
{
    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    g_hpi_last_update.spo2_last_value = spo2_last_value;
    g_hpi_last_update.spo2_last_update_ts = spo2_last_update_ts;
    k_mutex_unlock(&mutex_hpi_last_update_time);
}

void hpi_sys_set_last_bp_update(uint16_t bp_sys_last_value, uint16_t bp_dia_last_value, int64_t bp_last_update_ts)
{
    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    g_hpi_last_update.bp_sys_last_value = bp_sys_last_value;
    g_hpi_last_update.bp_dia_last_value = bp_dia_last_value;
    g_hpi_last_update.bp_last_update_ts = bp_last_update_ts;
    k_mutex_unlock(&mutex_hpi_last_update_time);
}

void hpi_sys_set_last_ecg_update(int64_t ecg_last_update_ts)
{
    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    g_hpi_last_update.ecg_last_update_ts = ecg_last_update_ts;
    k_mutex_unlock(&mutex_hpi_last_update_time);
}

int hpi_sys_get_last_hr_update(uint16_t *hr_last_value, int64_t *hr_last_update_ts)
{
    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    *hr_last_value = g_hpi_last_update.hr_last_value;
    *hr_last_update_ts = g_hpi_last_update.hr_last_update_ts;
    k_mutex_unlock(&mutex_hpi_last_update_time);

    return 0;
}

int hpi_sys_get_last_spo2_update(uint8_t *spo2_last_value, int64_t *spo2_last_update_ts)
{
    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    *spo2_last_value = g_hpi_last_update.spo2_last_value;
    *spo2_last_update_ts = g_hpi_last_update.spo2_last_update_ts;
    k_mutex_unlock(&mutex_hpi_last_update_time);

    return 0;
}
int hpi_sys_get_last_bp_update(uint16_t *bp_sys_last_value, uint16_t *bp_dia_last_value, int64_t *bp_last_update_ts)
{
    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    *bp_sys_last_value = g_hpi_last_update.bp_sys_last_value;
    *bp_dia_last_value = g_hpi_last_update.bp_dia_last_value;
    *bp_last_update_ts = g_hpi_last_update.bp_last_update_ts;
    k_mutex_unlock(&mutex_hpi_last_update_time);

    return 0;
}
int hpi_sys_get_last_ecg_update(int64_t *ecg_last_update_ts)
{
    k_mutex_lock(&mutex_hpi_last_update_time, K_FOREVER);
    *ecg_last_update_ts = g_hpi_last_update.ecg_last_update_ts;
    k_mutex_unlock(&mutex_hpi_last_update_time);

    return 0;
}

void hpi_sys_thread(void)
{
    int ret=0;

    k_sem_take(&sem_hpi_sys_thread_start, K_FOREVER);
    LOG_INF("HPI Sys Thread starting");

    // Load last update time
    ret = hpi_sys_load_update_time();
    if (ret != 0)
    {
        LOG_ERR("Error loading last update time %d", ret);
    }
    else
    {
        LOG_DBG("Last update time loaded");
        LOG_DBG("BP last update time: %lld", g_hpi_last_update.bp_last_update_ts);
        LOG_DBG("HR last update time: %lld", g_hpi_last_update.hr_last_update_ts);
        LOG_DBG("SPO2 last update time: %lld", g_hpi_last_update.spo2_last_update_ts);
        LOG_DBG("ECG last update time: %lld", g_hpi_last_update.ecg_last_update_ts);
    }

    while (1)
    {
        k_sleep(K_SECONDS(60));
        hpi_sys_store_update_time();
    }
}

#define HPI_SYS_THREAD_STACKSIZE 2048
#define HPI_SYS_THREAD_PRIORITY 5

K_THREAD_DEFINE(hpi_sys_thread_id, HPI_SYS_THREAD_STACKSIZE, hpi_sys_thread, NULL, NULL, NULL, HPI_SYS_THREAD_PRIORITY, 0, 0);