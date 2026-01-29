/*
 * HealthyPi Move
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 *
 * PPG Hub Abstraction Layer
 *
 * This header provides a unified interface for wrist-based PPG sensors,
 * supporting both MAX32664C (with external MAX86141 AFE) and MAXM86146
 * (integrated optical module). Only one chip can be present at a time.
 */

#ifndef PPG_HUB_H
#define PPG_HUB_H

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

/**
 * @brief PPG Hub chip type enumeration
 */
enum ppg_hub_type {
    PPG_HUB_NONE = 0,       /**< No PPG hub detected */
    PPG_HUB_MAX32664C,      /**< MAX32664C with external MAX86141 AFE */
    PPG_HUB_MAXM86146,      /**< MAXM86146 integrated optical module */
};

/**
 * @brief PPG Hub operating modes (common to both chips)
 */
enum ppg_hub_op_mode {
    PPG_HUB_OP_MODE_CAL = 0,
    PPG_HUB_OP_MODE_IDLE,
    PPG_HUB_OP_MODE_RAW,
    PPG_HUB_OP_MODE_ALGO_AEC,
    PPG_HUB_OP_MODE_ALGO_AGC,
    PPG_HUB_OP_MODE_ALGO_EXTENDED,
    PPG_HUB_OP_MODE_SCD,
    PPG_HUB_OP_MODE_WAKE_ON_MOTION,
    PPG_HUB_OP_MODE_EXIT_WAKE_ON_MOTION,
    PPG_HUB_OP_MODE_STOP_ALGO,
};

/**
 * @brief PPG Hub algorithm modes (common to both chips)
 */
enum ppg_hub_algo_mode {
    PPG_HUB_ALGO_MODE_CONT_HR_CONT_SPO2 = 0x00,
    PPG_HUB_ALGO_MODE_CONT_HR_SHOT_SPO2 = 0x01,
    PPG_HUB_ALGO_MODE_CONT_HRM = 0x02,
    PPG_HUB_ALGO_MODE_SAMPLED_HRM = 0x03,
    PPG_HUB_ALGO_MODE_SAMPLED_HRM_SHOT_SPO2 = 0x04,
    PPG_HUB_ALGO_MODE_ACT_TRACK = 0x05,
    PPG_HUB_ALGO_MODE_SPO2_CAL = 0x06,
    PPG_HUB_ALGO_MODE_CONT_HRM_FAST_SPO2 = 0x07,
    PPG_HUB_ALGO_MODE_NONE = 0xFF,
};

/**
 * @brief SCD (Skin Contact Detection) states
 */
enum ppg_hub_scd_state {
    PPG_HUB_SCD_STATE_UNKNOWN = 0,
    PPG_HUB_SCD_STATE_OFF_SKIN = 1,
    PPG_HUB_SCD_STATE_ON_OBJECT = 2,
    PPG_HUB_SCD_STATE_ON_SKIN = 3,
};

/**
 * @brief Common sensor attribute IDs (mapped to chip-specific values)
 */
enum ppg_hub_attribute {
    PPG_HUB_ATTR_OP_MODE = 0x01,
    PPG_HUB_ATTR_DATE_TIME = 0x02,
    PPG_HUB_ATTR_BP_CAL_SYS = 0x03,
    PPG_HUB_ATTR_BP_CAL = 0x04,
    PPG_HUB_ATTR_START_EST = 0x05,
    PPG_HUB_ATTR_STOP_EST = 0x06,
    PPG_HUB_ATTR_LOAD_CALIB = 0x07,
    PPG_HUB_ATTR_ENTER_BOOTLOADER = 0x08,
    PPG_HUB_ATTR_DO_FW_UPDATE = 0x09,
    PPG_HUB_ATTR_IS_APP_PRESENT = 0x10,
    PPG_HUB_ATTR_APP_VER = 0x11,
    PPG_HUB_ATTR_SENSOR_IDS = 0x12,
};

/**
 * @brief Get the detected PPG hub type
 * @return The type of PPG hub detected during initialization
 */
enum ppg_hub_type ppg_hub_get_type(void);

/**
 * @brief Get the PPG hub device pointer
 * @return Pointer to the detected PPG hub device, or NULL if none detected
 */
const struct device *ppg_hub_get_device(void);

/**
 * @brief Check if a PPG hub is present
 * @return true if a PPG hub was detected, false otherwise
 */
bool ppg_hub_is_present(void);

/**
 * @brief Set the PPG hub operating mode
 * @param op_mode Operating mode to set
 * @param algo_mode Algorithm mode (for ALGO modes)
 * @return 0 on success, negative error code on failure
 */
int ppg_hub_set_op_mode(enum ppg_hub_op_mode op_mode, enum ppg_hub_algo_mode algo_mode);

/**
 * @brief Stop the PPG hub algorithm
 * @return 0 on success, negative error code on failure
 */
int ppg_hub_stop_algo(void);

/**
 * @brief Get the PPG hub type as a string
 * @return String representation of the PPG hub type
 */
const char *ppg_hub_get_type_string(void);

#endif /* PPG_HUB_H */
