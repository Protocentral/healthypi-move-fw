/*
 * HealthyPi Move - Measurement Settings Storage
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Implementation of measurement storage using Zephyr's settings subsystem.
 * Uses settings_save_one() for immediate persistence and settings_load_one()
 * for loading individual values.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <string.h>

#include "hpi_measurement_settings.h"

LOG_MODULE_REGISTER(hpi_meas_settings, LOG_LEVEL_DBG);

/* Settings keys - full path including subtree */
#define KEY_HR         HPI_MEAS_SETTINGS_SUBTREE "/hr"
#define KEY_SPO2       HPI_MEAS_SETTINGS_SUBTREE "/spo2"
#define KEY_BP         HPI_MEAS_SETTINGS_SUBTREE "/bp"
#define KEY_ECG        HPI_MEAS_SETTINGS_SUBTREE "/ecg"
#define KEY_TEMP       HPI_MEAS_SETTINGS_SUBTREE "/temp"
#define KEY_STEPS      HPI_MEAS_SETTINGS_SUBTREE "/steps"
#define KEY_GSR_STRESS HPI_MEAS_SETTINGS_SUBTREE "/gsr"
#define KEY_HRV        HPI_MEAS_SETTINGS_SUBTREE "/hrv"

/* RAM cache for fast access - populated on init, updated on save */
static struct {
    struct hpi_meas_hr_t hr;
    struct hpi_meas_spo2_t spo2;
    struct hpi_meas_bp_t bp;
    struct hpi_meas_ecg_t ecg;
    struct hpi_meas_temp_t temp;
    struct hpi_meas_steps_t steps;
    struct hpi_meas_gsr_stress_t gsr_stress;
    struct hpi_meas_hrv_t hrv;
    bool initialized;
} meas_cache;

/* Mutex for thread-safe access to cache */
K_MUTEX_DEFINE(meas_cache_mutex);

/*
 * Settings handler callbacks for the "hpim" subtree.
 * These are called by the settings subsystem during settings_load().
 */

static int hpim_settings_set(const char *name, size_t len,
                              settings_read_cb read_cb, void *cb_arg)
{
    const char *next;
    int rc;

    if (settings_name_steq(name, "hr", &next) && !next) {
        if (len != sizeof(meas_cache.hr)) {
            LOG_WRN("HR data size mismatch: expected %zu, got %zu",
                    sizeof(meas_cache.hr), len);
            return -EINVAL;
        }
        rc = read_cb(cb_arg, &meas_cache.hr, sizeof(meas_cache.hr));
        if (rc >= 0) {
            LOG_DBG("Loaded HR: %u @ %lld", meas_cache.hr.value,
                    meas_cache.hr.timestamp);
        }
        return 0;
    }

    if (settings_name_steq(name, "spo2", &next) && !next) {
        if (len != sizeof(meas_cache.spo2)) {
            return -EINVAL;
        }
        rc = read_cb(cb_arg, &meas_cache.spo2, sizeof(meas_cache.spo2));
        if (rc >= 0) {
            LOG_DBG("Loaded SpO2: %u @ %lld", meas_cache.spo2.value,
                    meas_cache.spo2.timestamp);
        }
        return 0;
    }

    if (settings_name_steq(name, "bp", &next) && !next) {
        if (len != sizeof(meas_cache.bp)) {
            return -EINVAL;
        }
        rc = read_cb(cb_arg, &meas_cache.bp, sizeof(meas_cache.bp));
        if (rc >= 0) {
            LOG_DBG("Loaded BP: %u/%u @ %lld", meas_cache.bp.sys,
                    meas_cache.bp.dia, meas_cache.bp.timestamp);
        }
        return 0;
    }

    if (settings_name_steq(name, "ecg", &next) && !next) {
        if (len != sizeof(meas_cache.ecg)) {
            return -EINVAL;
        }
        rc = read_cb(cb_arg, &meas_cache.ecg, sizeof(meas_cache.ecg));
        if (rc >= 0) {
            LOG_DBG("Loaded ECG HR: %u @ %lld", meas_cache.ecg.hr,
                    meas_cache.ecg.timestamp);
        }
        return 0;
    }

    if (settings_name_steq(name, "temp", &next) && !next) {
        if (len != sizeof(meas_cache.temp)) {
            return -EINVAL;
        }
        rc = read_cb(cb_arg, &meas_cache.temp, sizeof(meas_cache.temp));
        if (rc >= 0) {
            LOG_DBG("Loaded Temp: %u.%02u @ %lld",
                    meas_cache.temp.value_x100 / 100,
                    meas_cache.temp.value_x100 % 100,
                    meas_cache.temp.timestamp);
        }
        return 0;
    }

    if (settings_name_steq(name, "steps", &next) && !next) {
        if (len != sizeof(meas_cache.steps)) {
            return -EINVAL;
        }
        rc = read_cb(cb_arg, &meas_cache.steps, sizeof(meas_cache.steps));
        if (rc >= 0) {
            LOG_DBG("Loaded Steps: %u @ %lld", meas_cache.steps.value,
                    meas_cache.steps.timestamp);
        }
        return 0;
    }

    if (settings_name_steq(name, "gsr", &next) && !next) {
        if (len != sizeof(meas_cache.gsr_stress)) {
            return -EINVAL;
        }
        rc = read_cb(cb_arg, &meas_cache.gsr_stress, sizeof(meas_cache.gsr_stress));
        if (rc >= 0) {
            LOG_DBG("Loaded GSR: stress=%u, tonic=%u.%02u, SCR=%u/min @ %lld",
                    meas_cache.gsr_stress.stress_level,
                    meas_cache.gsr_stress.tonic_x100 / 100,
                    meas_cache.gsr_stress.tonic_x100 % 100,
                    meas_cache.gsr_stress.peaks_per_minute,
                    meas_cache.gsr_stress.timestamp);
        }
        return 0;
    }

    if (settings_name_steq(name, "hrv", &next) && !next) {
        if (len != sizeof(meas_cache.hrv)) {
            LOG_WRN("HRV data size mismatch: expected %zu, got %zu",
                    sizeof(meas_cache.hrv), len);
            return -EINVAL;
        }
        rc = read_cb(cb_arg, &meas_cache.hrv, sizeof(meas_cache.hrv));
        if (rc >= 0) {
            LOG_DBG("Loaded HRV: LF/HF=%u.%02u, SDNN=%u.%u ms, RMSSD=%u.%u ms @ %lld",
                    meas_cache.hrv.lf_hf_ratio_x100 / 100,
                    meas_cache.hrv.lf_hf_ratio_x100 % 100,
                    meas_cache.hrv.sdnn_x10 / 10,
                    meas_cache.hrv.sdnn_x10 % 10,
                    meas_cache.hrv.rmssd_x10 / 10,
                    meas_cache.hrv.rmssd_x10 % 10,
                    meas_cache.hrv.timestamp);
        }
        return 0;
    }

    LOG_WRN("Unknown settings key: %s", name);
    return -ENOENT;
}

static int hpim_settings_export(int (*cb)(const char *name,
                                           const void *value,
                                           size_t val_len))
{
    /* Export all cached values to storage */

    if (meas_cache.hr.timestamp > 0) {
        cb(KEY_HR, &meas_cache.hr, sizeof(meas_cache.hr));
    }

    if (meas_cache.spo2.timestamp > 0) {
        cb(KEY_SPO2, &meas_cache.spo2, sizeof(meas_cache.spo2));
    }

    if (meas_cache.bp.timestamp > 0) {
        cb(KEY_BP, &meas_cache.bp, sizeof(meas_cache.bp));
    }

    if (meas_cache.ecg.timestamp > 0) {
        cb(KEY_ECG, &meas_cache.ecg, sizeof(meas_cache.ecg));
    }

    if (meas_cache.temp.timestamp > 0) {
        cb(KEY_TEMP, &meas_cache.temp, sizeof(meas_cache.temp));
    }

    if (meas_cache.steps.timestamp > 0) {
        cb(KEY_STEPS, &meas_cache.steps, sizeof(meas_cache.steps));
    }

    if (meas_cache.gsr_stress.timestamp > 0) {
        cb(KEY_GSR_STRESS, &meas_cache.gsr_stress, sizeof(meas_cache.gsr_stress));
    }

    if (meas_cache.hrv.timestamp > 0) {
        cb(KEY_HRV, &meas_cache.hrv, sizeof(meas_cache.hrv));
    }

    return 0;
}

/* Register static settings handler for "hpim" subtree */
SETTINGS_STATIC_HANDLER_DEFINE(hpim, HPI_MEAS_SETTINGS_SUBTREE,
                                NULL,  /* h_get not needed */
                                hpim_settings_set,
                                NULL,  /* h_commit not needed */
                                hpim_settings_export);

int hpi_measurement_settings_init(void)
{
    int rc;

    k_mutex_lock(&meas_cache_mutex, K_FOREVER);

    if (meas_cache.initialized) {
        k_mutex_unlock(&meas_cache_mutex);
        return 0;
    }

    /* Initialize cache with zeros */
    memset(&meas_cache, 0, sizeof(meas_cache));

    /* Load our subtree - this will call hpim_settings_set for each saved value */
    rc = settings_load_subtree(HPI_MEAS_SETTINGS_SUBTREE);
    if (rc != 0) {
        LOG_WRN("Failed to load measurement settings: %d (may be first boot)", rc);
        /* Not a fatal error - could be first boot with no saved data */
    }

    meas_cache.initialized = true;
    k_mutex_unlock(&meas_cache_mutex);

    LOG_INF("Measurement settings initialized");
    return 0;
}

/* Helper macro for save functions */
#define SAVE_MEASUREMENT(key, data) \
    do { \
        int rc = settings_save_one(key, data, sizeof(*data)); \
        if (rc != 0) { \
            LOG_ERR("Failed to save %s: %d", key, rc); \
            return rc; \
        } \
        return 0; \
    } while (0)

int hpi_meas_save_hr(uint16_t value, int64_t timestamp)
{
    k_mutex_lock(&meas_cache_mutex, K_FOREVER);

    meas_cache.hr.version = HPI_MEAS_SETTINGS_VERSION;
    meas_cache.hr.value = value;
    meas_cache.hr.timestamp = timestamp;

    int rc = settings_save_one(KEY_HR, &meas_cache.hr, sizeof(meas_cache.hr));
    k_mutex_unlock(&meas_cache_mutex);

    if (rc != 0) {
        LOG_ERR("Failed to save HR: %d", rc);
    }
    return rc;
}

int hpi_meas_load_hr(uint16_t *value, int64_t *timestamp)
{
    if (!value || !timestamp) {
        return -EINVAL;
    }

    k_mutex_lock(&meas_cache_mutex, K_FOREVER);
    *value = meas_cache.hr.value;
    *timestamp = meas_cache.hr.timestamp;
    k_mutex_unlock(&meas_cache_mutex);

    return 0;
}

int hpi_meas_save_spo2(uint8_t value, int64_t timestamp)
{
    k_mutex_lock(&meas_cache_mutex, K_FOREVER);

    meas_cache.spo2.version = HPI_MEAS_SETTINGS_VERSION;
    meas_cache.spo2.value = value;
    meas_cache.spo2.timestamp = timestamp;

    int rc = settings_save_one(KEY_SPO2, &meas_cache.spo2, sizeof(meas_cache.spo2));
    k_mutex_unlock(&meas_cache_mutex);

    if (rc != 0) {
        LOG_ERR("Failed to save SpO2: %d", rc);
    }
    return rc;
}

int hpi_meas_load_spo2(uint8_t *value, int64_t *timestamp)
{
    if (!value || !timestamp) {
        return -EINVAL;
    }

    k_mutex_lock(&meas_cache_mutex, K_FOREVER);
    *value = meas_cache.spo2.value;
    *timestamp = meas_cache.spo2.timestamp;
    k_mutex_unlock(&meas_cache_mutex);

    return 0;
}

int hpi_meas_save_bp(uint8_t sys, uint8_t dia, int64_t timestamp)
{
    k_mutex_lock(&meas_cache_mutex, K_FOREVER);

    meas_cache.bp.version = HPI_MEAS_SETTINGS_VERSION;
    meas_cache.bp.sys = sys;
    meas_cache.bp.dia = dia;
    meas_cache.bp.timestamp = timestamp;

    int rc = settings_save_one(KEY_BP, &meas_cache.bp, sizeof(meas_cache.bp));
    k_mutex_unlock(&meas_cache_mutex);

    if (rc != 0) {
        LOG_ERR("Failed to save BP: %d", rc);
    }
    return rc;
}

int hpi_meas_load_bp(uint8_t *sys, uint8_t *dia, int64_t *timestamp)
{
    if (!sys || !dia || !timestamp) {
        return -EINVAL;
    }

    k_mutex_lock(&meas_cache_mutex, K_FOREVER);
    *sys = meas_cache.bp.sys;
    *dia = meas_cache.bp.dia;
    *timestamp = meas_cache.bp.timestamp;
    k_mutex_unlock(&meas_cache_mutex);

    return 0;
}

int hpi_meas_save_ecg(uint8_t hr, int64_t timestamp)
{
    k_mutex_lock(&meas_cache_mutex, K_FOREVER);

    meas_cache.ecg.version = HPI_MEAS_SETTINGS_VERSION;
    meas_cache.ecg.hr = hr;
    meas_cache.ecg.timestamp = timestamp;

    int rc = settings_save_one(KEY_ECG, &meas_cache.ecg, sizeof(meas_cache.ecg));
    k_mutex_unlock(&meas_cache_mutex);

    if (rc != 0) {
        LOG_ERR("Failed to save ECG: %d", rc);
    }
    return rc;
}

int hpi_meas_load_ecg(uint8_t *hr, int64_t *timestamp)
{
    if (!hr || !timestamp) {
        return -EINVAL;
    }

    k_mutex_lock(&meas_cache_mutex, K_FOREVER);
    *hr = meas_cache.ecg.hr;
    *timestamp = meas_cache.ecg.timestamp;
    k_mutex_unlock(&meas_cache_mutex);

    return 0;
}

int hpi_meas_save_temp(uint16_t value_x100, int64_t timestamp)
{
    k_mutex_lock(&meas_cache_mutex, K_FOREVER);

    meas_cache.temp.version = HPI_MEAS_SETTINGS_VERSION;
    meas_cache.temp.value_x100 = value_x100;
    meas_cache.temp.timestamp = timestamp;

    int rc = settings_save_one(KEY_TEMP, &meas_cache.temp, sizeof(meas_cache.temp));
    k_mutex_unlock(&meas_cache_mutex);

    if (rc != 0) {
        LOG_ERR("Failed to save Temp: %d", rc);
    }
    return rc;
}

int hpi_meas_load_temp(uint16_t *value_x100, int64_t *timestamp)
{
    if (!value_x100 || !timestamp) {
        return -EINVAL;
    }

    k_mutex_lock(&meas_cache_mutex, K_FOREVER);
    *value_x100 = meas_cache.temp.value_x100;
    *timestamp = meas_cache.temp.timestamp;
    k_mutex_unlock(&meas_cache_mutex);

    return 0;
}

int hpi_meas_save_steps(uint16_t value, int64_t timestamp)
{
    k_mutex_lock(&meas_cache_mutex, K_FOREVER);

    meas_cache.steps.version = HPI_MEAS_SETTINGS_VERSION;
    meas_cache.steps.value = value;
    meas_cache.steps.timestamp = timestamp;

    int rc = settings_save_one(KEY_STEPS, &meas_cache.steps, sizeof(meas_cache.steps));
    k_mutex_unlock(&meas_cache_mutex);

    if (rc != 0) {
        LOG_ERR("Failed to save Steps: %d", rc);
    }
    return rc;
}

int hpi_meas_load_steps(uint16_t *value, int64_t *timestamp)
{
    if (!value || !timestamp) {
        return -EINVAL;
    }

    k_mutex_lock(&meas_cache_mutex, K_FOREVER);
    *value = meas_cache.steps.value;
    *timestamp = meas_cache.steps.timestamp;
    k_mutex_unlock(&meas_cache_mutex);

    return 0;
}

int hpi_meas_save_gsr_stress(uint8_t stress_level, uint16_t tonic_x100,
                              uint8_t peaks_per_minute, int64_t timestamp)
{
    k_mutex_lock(&meas_cache_mutex, K_FOREVER);

    meas_cache.gsr_stress.version = HPI_MEAS_SETTINGS_VERSION;
    meas_cache.gsr_stress.stress_level = stress_level;
    meas_cache.gsr_stress.tonic_x100 = tonic_x100;
    meas_cache.gsr_stress.peaks_per_minute = peaks_per_minute;
    meas_cache.gsr_stress.timestamp = timestamp;

    int rc = settings_save_one(KEY_GSR_STRESS, &meas_cache.gsr_stress,
                                sizeof(meas_cache.gsr_stress));
    k_mutex_unlock(&meas_cache_mutex);

    if (rc != 0) {
        LOG_ERR("Failed to save GSR stress: %d", rc);
    } else {
        LOG_DBG("Saved GSR: stress=%u, tonic=%u.%02u, SCR=%u/min",
                stress_level, tonic_x100 / 100, tonic_x100 % 100, peaks_per_minute);
    }
    return rc;
}

int hpi_meas_load_gsr_stress(uint8_t *stress_level, uint16_t *tonic_x100,
                              uint8_t *peaks_per_minute, int64_t *timestamp)
{
    if (!stress_level || !tonic_x100 || !peaks_per_minute || !timestamp) {
        return -EINVAL;
    }

    k_mutex_lock(&meas_cache_mutex, K_FOREVER);
    *stress_level = meas_cache.gsr_stress.stress_level;
    *tonic_x100 = meas_cache.gsr_stress.tonic_x100;
    *peaks_per_minute = meas_cache.gsr_stress.peaks_per_minute;
    *timestamp = meas_cache.gsr_stress.timestamp;
    k_mutex_unlock(&meas_cache_mutex);

    return 0;
}

int hpi_meas_save_hrv(uint16_t lf_hf_ratio_x100, uint16_t sdnn_x10,
                      uint16_t rmssd_x10, int64_t timestamp)
{
    k_mutex_lock(&meas_cache_mutex, K_FOREVER);

    meas_cache.hrv.version = HPI_MEAS_SETTINGS_VERSION;
    meas_cache.hrv.lf_hf_ratio_x100 = lf_hf_ratio_x100;
    meas_cache.hrv.sdnn_x10 = sdnn_x10;
    meas_cache.hrv.rmssd_x10 = rmssd_x10;
    meas_cache.hrv.timestamp = timestamp;

    int rc = settings_save_one(KEY_HRV, &meas_cache.hrv, sizeof(meas_cache.hrv));
    k_mutex_unlock(&meas_cache_mutex);

    if (rc != 0) {
        LOG_ERR("Failed to save HRV: %d", rc);
    } else {
        LOG_DBG("Saved HRV: LF/HF=%u.%02u, SDNN=%u.%u ms, RMSSD=%u.%u ms",
                lf_hf_ratio_x100 / 100, lf_hf_ratio_x100 % 100,
                sdnn_x10 / 10, sdnn_x10 % 10,
                rmssd_x10 / 10, rmssd_x10 % 10);
    }
    return rc;
}

int hpi_meas_load_hrv(uint16_t *lf_hf_ratio_x100, uint16_t *sdnn_x10,
                      uint16_t *rmssd_x10, int64_t *timestamp)
{
    if (!lf_hf_ratio_x100 || !sdnn_x10 || !rmssd_x10 || !timestamp) {
        return -EINVAL;
    }

    k_mutex_lock(&meas_cache_mutex, K_FOREVER);
    *lf_hf_ratio_x100 = meas_cache.hrv.lf_hf_ratio_x100;
    *sdnn_x10 = meas_cache.hrv.sdnn_x10;
    *rmssd_x10 = meas_cache.hrv.rmssd_x10;
    *timestamp = meas_cache.hrv.timestamp;
    k_mutex_unlock(&meas_cache_mutex);

    return 0;
}
