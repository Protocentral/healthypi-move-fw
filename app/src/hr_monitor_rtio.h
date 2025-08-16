/*
 * HealthyPi Move - RTIO-based HR Monitor
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 */

#ifndef HR_MONITOR_RTIO_H
#define HR_MONITOR_RTIO_H

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* HR validation macros */
#define HR_MIN_VALID 40
#define HR_MAX_VALID 200
#define HR_CONFIDENCE_THRESHOLD 70

#define HR_IS_NORMAL_RANGE(hr) ((hr) >= HR_MIN_VALID && (hr) <= HR_MAX_VALID)
#define HR_CONFIDENCE_OK(conf) ((conf) >= HR_CONFIDENCE_THRESHOLD)

/* HR monitoring statistics */
struct hr_monitor_stats {
    uint32_t last_sample_time;     /* Timestamp of last sample */
    uint16_t current_hr;           /* Current heart rate */
    uint8_t current_confidence;    /* Current confidence level */
    bool monitoring_active;        /* Is monitoring running */
    bool streaming_active;         /* Is streaming active */
};

/**
 * @brief Initialize HR monitoring system using RTIO
 * 
 * @return 0 on success, negative error code on failure
 */
int hr_monitor_init(void);

/**
 * @brief Check if HR monitor is initialized
 * 
 * @return true if initialized, false otherwise
 */
bool hr_monitor_is_initialized(void);

/**
 * @brief Start HR monitor thread
 * 
 * @return 0 on success, negative error code on failure
 */
int hr_monitor_start_thread(void);

/**
 * @brief Start continuous HR monitoring with streaming
 * 
 * @return 0 on success, negative error code on failure
 */
int hr_monitor_start(void);

/**
 * @brief Stop HR monitoring
 * 
 * @return 0 on success, negative error code on failure
 */
int hr_monitor_stop(void);

/**
 * @brief Get latest HR data (non-blocking)
 * 
 * @param hr Pointer to store heart rate value
 * @param confidence Pointer to store confidence level
 * @return 0 on success, -ETIMEDOUT if data is stale, other negative on error
 */
int hr_monitor_get_latest(uint16_t *hr, uint8_t *confidence);

/**
 * @brief Get detailed HR data including SpO2 and SCD state
 * 
 * @param hr Pointer to store heart rate value
 * @param confidence Pointer to store confidence level
 * @param spo2 Pointer to store SpO2 value
 * @param scd_state Pointer to store skin contact detection state
 * @return 0 on success, -ETIMEDOUT if data is stale, other negative on error
 */
int hr_monitor_get_detailed(uint16_t *hr, uint8_t *confidence,
                           uint8_t *spo2, uint8_t *scd_state);

/**
 * @brief Perform on-demand HR reading (blocking)
 * 
 * @param hr Pointer to store heart rate value
 * @param confidence Pointer to store confidence level
 * @return 0 on success, negative error code on failure
 */
int hr_monitor_read_now(uint16_t *hr, uint8_t *confidence);

/**
 * @brief Wait for new HR data with timeout
 * 
 * @param timeout Maximum time to wait
 * @return 0 on success, -EAGAIN on timeout
 */
int hr_monitor_wait_for_data(k_timeout_t timeout);

/**
 * @brief Check if monitoring is active
 * 
 * @return true if monitoring is active, false otherwise
 */
bool hr_monitor_is_active(void);

/**
 * @brief Check if streaming is active
 * 
 * @return true if streaming is active, false otherwise
 */
bool hr_monitor_is_streaming(void);

/**
 * @brief Get monitoring statistics
 * 
 * @param stats Pointer to store statistics
 * @return 0 on success, negative error code on failure
 */
int hr_monitor_get_stats(struct hr_monitor_stats *stats);

#ifdef __cplusplus
}
#endif

#endif /* HR_MONITOR_RTIO_H */
