/*
 * HealthyPi Move - Measurement Settings Storage
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Stores last measurement values and timestamps using Zephyr's settings subsystem.
 * Uses a separate "hpim" subtree to avoid conflicts with BLE bonds ("bt" subtree).
 */

#ifndef HPI_MEASUREMENT_SETTINGS_H
#define HPI_MEASUREMENT_SETTINGS_H

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>

/* Settings subtree name - "hpim" for HealthyPi Measurements
 * This is separate from "bt" (Bluetooth) to ensure no conflicts with BLE bonds
 */
#define HPI_MEAS_SETTINGS_SUBTREE "hpim"

/* Version for future format migrations */
#define HPI_MEAS_SETTINGS_VERSION 1

/*
 * Packed structures for efficient storage.
 * Each measurement type has its own structure with value(s) + timestamp.
 */

struct hpi_meas_hr_t {
    uint8_t version;
    uint16_t value;
    int64_t timestamp;
} __packed;

struct hpi_meas_spo2_t {
    uint8_t version;
    uint8_t value;
    int64_t timestamp;
} __packed;

struct hpi_meas_bp_t {
    uint8_t version;
    uint8_t sys;
    uint8_t dia;
    int64_t timestamp;
} __packed;

struct hpi_meas_ecg_t {
    uint8_t version;
    uint8_t hr;
    int64_t timestamp;
} __packed;

struct hpi_meas_temp_t {
    uint8_t version;
    uint16_t value_x100;  /* Temperature * 100 */
    int64_t timestamp;
} __packed;

struct hpi_meas_steps_t {
    uint8_t version;
    uint16_t value;
    int64_t timestamp;
} __packed;

struct hpi_meas_gsr_stress_t {
    uint8_t version;
    uint8_t stress_level;       /* 0-100 stress score */
    uint16_t tonic_x100;        /* SCL in μS * 100 */
    uint8_t peaks_per_minute;   /* SCR rate */
    int64_t timestamp;
} __packed;

struct hpi_meas_hrv_t {
    uint8_t version;
    uint16_t lf_hf_ratio_x100;  /* LF/HF ratio * 100 (e.g., 150 = 1.50) */
    uint16_t sdnn_x10;          /* SDNN in ms * 10 (e.g., 505 = 50.5 ms) */
    uint16_t rmssd_x10;         /* RMSSD in ms * 10 (e.g., 423 = 42.3 ms) */
    int64_t timestamp;
} __packed;

/**
 * @brief Initialize the measurement settings subsystem
 *
 * Must be called after settings_subsys_init() and filesystem mount.
 * Loads all saved measurement values into RAM cache.
 *
 * @return 0 on success, negative error code on failure
 */
int hpi_measurement_settings_init(void);

/**
 * @brief Save HR measurement
 * @param value HR in BPM
 * @param timestamp Unix timestamp of measurement
 * @return 0 on success, negative error code on failure
 */
int hpi_meas_save_hr(uint16_t value, int64_t timestamp);

/**
 * @brief Load HR measurement
 * @param value Output: HR in BPM
 * @param timestamp Output: Unix timestamp of measurement
 * @return 0 on success, negative error code on failure
 */
int hpi_meas_load_hr(uint16_t *value, int64_t *timestamp);

/**
 * @brief Save SpO2 measurement
 * @param value SpO2 percentage (0-100)
 * @param timestamp Unix timestamp of measurement
 * @return 0 on success, negative error code on failure
 */
int hpi_meas_save_spo2(uint8_t value, int64_t timestamp);

/**
 * @brief Load SpO2 measurement
 * @param value Output: SpO2 percentage
 * @param timestamp Output: Unix timestamp of measurement
 * @return 0 on success, negative error code on failure
 */
int hpi_meas_load_spo2(uint8_t *value, int64_t *timestamp);

/**
 * @brief Save blood pressure measurement
 * @param sys Systolic pressure
 * @param dia Diastolic pressure
 * @param timestamp Unix timestamp of measurement
 * @return 0 on success, negative error code on failure
 */
int hpi_meas_save_bp(uint8_t sys, uint8_t dia, int64_t timestamp);

/**
 * @brief Load blood pressure measurement
 * @param sys Output: Systolic pressure
 * @param dia Output: Diastolic pressure
 * @param timestamp Output: Unix timestamp of measurement
 * @return 0 on success, negative error code on failure
 */
int hpi_meas_load_bp(uint8_t *sys, uint8_t *dia, int64_t *timestamp);

/**
 * @brief Save ECG measurement
 * @param hr Heart rate from ECG
 * @param timestamp Unix timestamp of measurement
 * @return 0 on success, negative error code on failure
 */
int hpi_meas_save_ecg(uint8_t hr, int64_t timestamp);

/**
 * @brief Load ECG measurement
 * @param hr Output: Heart rate from ECG
 * @param timestamp Output: Unix timestamp of measurement
 * @return 0 on success, negative error code on failure
 */
int hpi_meas_load_ecg(uint8_t *hr, int64_t *timestamp);

/**
 * @brief Save temperature measurement
 * @param value_x100 Temperature * 100 (e.g., 9850 = 98.50°F)
 * @param timestamp Unix timestamp of measurement
 * @return 0 on success, negative error code on failure
 */
int hpi_meas_save_temp(uint16_t value_x100, int64_t timestamp);

/**
 * @brief Load temperature measurement
 * @param value_x100 Output: Temperature * 100
 * @param timestamp Output: Unix timestamp of measurement
 * @return 0 on success, negative error code on failure
 */
int hpi_meas_load_temp(uint16_t *value_x100, int64_t *timestamp);

/**
 * @brief Save steps count
 * @param value Steps count
 * @param timestamp Unix timestamp of measurement
 * @return 0 on success, negative error code on failure
 */
int hpi_meas_save_steps(uint16_t value, int64_t timestamp);

/**
 * @brief Load steps count
 * @param value Output: Steps count
 * @param timestamp Output: Unix timestamp of measurement
 * @return 0 on success, negative error code on failure
 */
int hpi_meas_load_steps(uint16_t *value, int64_t *timestamp);

/**
 * @brief Save GSR stress measurement
 * @param stress_level Stress level 0-100
 * @param tonic_x100 Tonic level (SCL) in μS * 100
 * @param peaks_per_minute SCR rate (peaks per minute)
 * @param timestamp Unix timestamp of measurement
 * @return 0 on success, negative error code on failure
 */
int hpi_meas_save_gsr_stress(uint8_t stress_level, uint16_t tonic_x100,
                              uint8_t peaks_per_minute, int64_t timestamp);

/**
 * @brief Load GSR stress measurement
 * @param stress_level Output: Stress level 0-100
 * @param tonic_x100 Output: Tonic level (SCL) in μS * 100
 * @param peaks_per_minute Output: SCR rate (peaks per minute)
 * @param timestamp Output: Unix timestamp of measurement
 * @return 0 on success, negative error code on failure
 */
int hpi_meas_load_gsr_stress(uint8_t *stress_level, uint16_t *tonic_x100,
                              uint8_t *peaks_per_minute, int64_t *timestamp);

/**
 * @brief Save HRV measurement
 * @param lf_hf_ratio_x100 LF/HF ratio * 100 (e.g., 150 = 1.50)
 * @param sdnn_x10 SDNN in ms * 10 (e.g., 505 = 50.5 ms)
 * @param rmssd_x10 RMSSD in ms * 10 (e.g., 423 = 42.3 ms)
 * @param timestamp Unix timestamp of measurement
 * @return 0 on success, negative error code on failure
 */
int hpi_meas_save_hrv(uint16_t lf_hf_ratio_x100, uint16_t sdnn_x10,
                      uint16_t rmssd_x10, int64_t timestamp);

/**
 * @brief Load HRV measurement
 * @param lf_hf_ratio_x100 Output: LF/HF ratio * 100
 * @param sdnn_x10 Output: SDNN in ms * 10
 * @param rmssd_x10 Output: RMSSD in ms * 10
 * @param timestamp Output: Unix timestamp of measurement
 * @return 0 on success, negative error code on failure
 */
int hpi_meas_load_hrv(uint16_t *lf_hf_ratio_x100, uint16_t *sdnn_x10,
                      uint16_t *rmssd_x10, int64_t *timestamp);

#endif /* HPI_MEASUREMENT_SETTINGS_H */
