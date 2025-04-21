/**
 * Copyright (c) 2023 Bosch Sensortec GmbH. All rights reserved.
 *
 * BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * @file       bmi3_defs.h
 * @date       2023-02-17
 * @version    v2.1.0
 *
 */

#pragma once

#include <zephyr/sys/util.h>
#include <zephyr/types.h>

/********************************************************* */
/*!                 Register Addresses                    */
/********************************************************* */

/*! To define the chip id address */
#define BMI3_REG_CHIP_ID (0x00)

/*! Reports sensor error conditions */
#define BMI3_REG_ERR_REG (0x01)

/*! Sensor status flags */
#define BMI3_REG_STATUS (0x02)

/*! ACC Data X. */
#define BMI3_REG_ACC_DATA_X (0x03)

/*! ACC Data Y. */
#define BMI3_REG_ACC_DATA_Y (0x04)

/*! ACC Data Z. */
#define BMI3_REG_ACC_DATA_Z (0x05)

/*! GYR Data X. */
#define BMI3_REG_GYR_DATA_X (0x06)

/*! GYR Data Y. */
#define BMI3_REG_GYR_DATA_Y (0x07)

/*! GYR Data Z. */
#define BMI3_REG_GYR_DATA_Z (0x08)

/*! Temperature Data.
 *  The resolution is 512 LSB/K.
 *  0x0000 -> 23 degree Celsius
 *  0x8000 -> invalid
 */
#define BMI3_REG_TEMP_DATA (0x09)

/*! Sensor time LSW (15:0). */
#define BMI3_REG_SENSOR_TIME_0 (0x0A)

/*! Sensor time MSW (31:16). */
#define BMI3_REG_SENSOR_TIME_1 (0x0B)

/*! Saturation flags for each sensor and axis. */
#define BMI3_REG_SAT_FLAGS (0x0C)

/*! INT1 Status Register.
 *  This register is clear-on-read.
 */
#define BMI3_REG_INT_STATUS_INT1 (0x0D)

/*! INT2 Status Register.
 *  This register is clear-on-read.
 */
#define BMI3_REG_INT_STATUS_INT2 (0x0E)

/*! I3C IBI Status Register.
 *  This register is clear-on-read.
 */
#define BMI3_REG_INT_STATUS_IBI (0x0F)

/*! Feature engine configuration, before setting/changing an active configuration the register must be cleared
(set to 0) */
#define BMI3_REG_FEATURE_IO0 (0x10)

/*! Feature engine I/O register 0. */
#define BMI3_REG_FEATURE_IO1 (0x11)

/*! Feature engine I/O register 1. */
#define BMI3_REG_FEATURE_IO2 (0x12)

/*! Feature engine I/O register 2. */
#define BMI3_REG_FEATURE_IO3 (0x13)

/*! Feature I/O synchronization status and trigger. */
#define BMI3_REG_FEATURE_IO_STATUS (0x14)

/*! FIFO fill state in words */
#define BMI3_REG_FIFO_FILL_LEVEL (0x15)

/*! FIFO data output register */
#define BMI3_REG_FIFO_DATA (0x16)

/*! Sets the output data rate, bandwidth, range and the mode of the accelerometer */
#define BMI3_REG_ACC_CONF (0x20)

/*! Sets the output data rate, bandwidth, range and the mode of the gyroscope in the sensor */
#define BMI3_REG_GYR_CONF (0x21)

/*! Sets the alternative output data rate, bandwidth, range and the mode of the accelerometer */
#define BMI3_REG_ALT_ACC_CONF (0x28)

/*! Sets the alternative output data rate, bandwidth, range and the mode of the gyroscope */
#define BMI3_REG_ALT_GYR_CONF (0x29)

/*! Alternate configuration control */
#define BMI3_REG_ALT_CONF (0x2A)

/*! Reports the active configuration for the accelerometer and gyroscope */
#define BMI3_REG_ALT_STATUS (0x2B)

/*! FIFO watermark level */
#define BMI3_REG_FIFO_WATERMARK (0x35)

/*! Configuration of the FIFO data buffer behaviour */
#define BMI3_REG_FIFO_CONF (0x36)

/*! Control of the FIFO data buffer */
#define BMI3_REG_FIFO_CTRL (0x37)

/*! Configures the electrical behavior of the interrupt pins */
#define BMI3_REG_IO_INT_CTRL (0x38)

/*! Interrupt Configuration Register. */
#define BMI3_REG_INT_CONF (0x39)

/*! Mapping of feature engine interrupts to outputs */
#define BMI3_REG_INT_MAP1 (0x3A)

/*! Mapping of feature engine interrupts, data ready interrupts for signals and FIFO buffer interrupts to outputs */
#define BMI3_REG_INT_MAP2 (0x3B)

/*! Feature engine control register */
#define BMI3_REG_FEATURE_CTRL (0x40)

/*! Address register for feature data: configurations and extended output. */
#define BMI3_REG_FEATURE_DATA_ADDR (0x41)

#define BMI3_BASE_ADDR_STEP_CNT                      UINT8_C(0x10)

/*! I/O port for the data values of the feature engine. */
#define BMI3_REG_FEATURE_DATA_TX (0x42)

/*! Status of the data access to the feature engine. */
#define BMI3_REG_FEATURE_DATA_STATUS (0x43)

/*! Status of the feature engine. */
#define BMI3_REG_FEATURE_ENGINE_STATUS (0x45)

/*! Register of extended data on feature events. The register content is valid in combination with an active bit
in INT_STATUS_INT1/2. */
#define BMI3_REG_FEATURE_EVENT_EXT (0x47)

/*! Pull down behavior control */
#define BMI3_REG_IO_PDN_CTRL (0x4F)

/*! Configuration register for the SPI interface */
#define BMI3_REG_IO_SPI_IF (0x50)

/*! Configuration register for the electrical characteristics of the pads */
#define BMI3_REG_IO_PAD_STRENGTH (0x51)

/*! Configuration register for the I2C interface. */
#define BMI3_REG_IO_I2C_IF (0x52)

/*! ODR Deviation Trim Register (OTP backed) - User mirror register */
#define BMI3_REG_IO_ODR_DEVIATION (0x53)

/*! Data path register for the accelerometer offset of axis x */
#define BMI3_REG_ACC_DP_OFF_X (0x60)

/*! Data path register for the accelerometer re-scale of axis x */
#define BMI3_REG_ACC_DP_DGAIN_X (0x61)

/*! Data path register for the accelerometer offset of axis y */
#define BMI3_REG_ACC_DP_OFF_Y (0x62)

/*! Data path register for the accelerometer re-scale of axis y */
#define BMI3_REG_ACC_DP_DGAIN_Y (0x63)

/*! Data path register for the accelerometer offset of axis z */
#define BMI3_REG_ACC_DP_OFF_Z (0x64)

/*! Data path register for the accelerometer re-scale of axis z */
#define BMI3_REG_ACC_DP_DGAIN_Z (0x65)

/*! Data path register for the gyroscope offset of axis x */
#define BMI3_REG_GYR_DP_OFF_X (0x66)

/*! Data path register for the gyroscope re-scale of axis x */
#define BMI3_REG_GYR_DP_DGAIN_X (0x67)

/*! Data path register for the gyroscope offset of axis y */
#define BMI3_REG_GYR_DP_OFF_Y (0x68)

/*! Data path register for the gyroscope re-scale of axis y */
#define BMI3_REG_GYR_DP_DGAIN_Y (0x69)

/*! Data path register for the gyroscope offset of axis z */
#define BMI3_REG_GYR_DP_OFF_Z (0x6A)

/*! Data path register for the gyroscope re-scale of axis z */
#define BMI3_REG_GYR_DP_DGAIN_Z (0x6B)

/*! I3C Timing Control Sync TPH Register */
#define BMI3_REG_I3C_TC_SYNC_TPH (0x70)

/*! I3C Timing Control Sync TU Register */
#define BMI3_REG_I3C_TC_SYNC_TU (0x71)

/*! I3C Timing Control Sync ODR Register */
#define BMI3_REG_I3C_TC_SYNC_ODR (0x72)

/*! Command Register */
#define BMI3_REG_CMD (0x7E)

/*! Reserved configuration */
#define BMI3_REG_CFG_RES (0x7F)

/*! Macro to define start address of data in RAM patch */
#define BMI3_CONFIG_ARRAY_DATA_START_ADDR (4)

/*!
 * @brief Structure to define accelerometer configuration
 */
struct bmi3_accel_config
{
    /*! Output data rate in Hz */
    uint8_t odr;

    /*! Bandwidth parameter */
    uint8_t bwp;

    /*! Filter accel mode */
    uint8_t acc_mode;

    /*! Gravity range */
    uint8_t range;

    /*! Defines the number of samples to be averaged */
    uint8_t avg_num;
};

/*!
 * @brief Structure to define gyroscope configuration
 */
struct bmi3_gyro_config
{
    /*! Output data rate in Hz */
    uint8_t odr;

    /*! Bandwidth parameter */
    uint8_t bwp;

    /*! Filter gyro mode */
    uint8_t gyr_mode;

    /*! Gyroscope Range */
    uint8_t range;

    /*! Defines the number of samples to be averaged */
    uint8_t avg_num;
};

/*!
 * @brief Structure to define any-motion configuration
 */
struct bmi3_any_motion_config
{
    /*! Duration in 50Hz samples(20msec) */
    uint16_t duration;

    /*! Acceleration slope threshold */
    uint16_t slope_thres;

    /*! Mode of accel reference update */
    uint8_t acc_ref_up;

    /*! Hysteresis for the slope of the acceleration signal */
    uint16_t hysteresis;

    /*! Wait time for clearing the event after slope is below threshold */
    uint16_t wait_time;
};

/*!
 * @brief Structure to define no-motion configuration
 */
struct bmi3_no_motion_config
{
    /*! Duration in 50Hz samples(20msec) */
    uint16_t duration;

    /*! Acceleration slope threshold */
    uint16_t slope_thres;

    /*! Mode of accel reference update */
    uint8_t acc_ref_up;

    /*! Hysteresis for the slope of the acceleration signal */
    uint16_t hysteresis;

    /*! Wait time for clearing the event after slope is below threshold */
    uint16_t wait_time;
};

/*!
 * @brief Structure to define sig-motion configuration
 */
struct bmi3_sig_motion_config
{
    /*! Block size */
    uint16_t block_size;

    /*! Minimum value of the peak to peak acceleration magnitude */
    uint16_t peak_2_peak_min;

    /*! Minimum number of mean crossing per second in acceleration magnitude */
    uint8_t mcr_min;

    /*! Maximum value of the peak to peak acceleration magnitude */
    uint16_t peak_2_peak_max;

    /*! MAximum number of mean crossing per second in acceleration magnitude */
    uint8_t mcr_max;
};

/*!
 * @brief Structure to define step counter configuration
 */
struct bmi3_step_counter_config
{
    /*! Water-mark level */
    uint16_t watermark_level;

    /*! Reset counter */
    uint16_t reset_counter;

    /*! Step Counter param 1 */
    uint16_t env_min_dist_up;

    /*! Step Counter param 2 */
    uint16_t env_coef_up;

    /*! Step Counter param 3 */
    uint16_t env_min_dist_down;

    /*! Step Counter param 4 */
    uint16_t env_coef_down;

    /*! Step Counter param 5 */
    uint16_t mean_val_decay;

    /*! Step Counter param 6 */
    uint16_t mean_step_dur;

    /*! Step Counter param 7 */
    uint16_t step_buffer_size;

    /*! Step Counter param 8 */
    uint16_t filter_cascade_enabled;

    /*! Step Counter param 9 */
    uint16_t step_counter_increment;

    /*! Step Counter param 10 */
    uint16_t peak_duration_min_walking;

    /*! Step Counter param 11 */
    uint16_t peak_duration_min_running;

    /*! Step Counter param 12 */
    uint16_t activity_detection_factor;

    /*! Step Counter param 13 */
    uint16_t activity_detection_thres;

    /*! Step Counter param 14 */
    uint16_t step_duration_max;

    /*! Step Counter param 15 */
    uint16_t step_duration_window;

    /*! Step Counter param 16 */
    uint16_t step_duration_pp_enabled;

    /*! Step Counter param 17 */
    uint16_t step_duration_thres;

    /*! Step Counter param 18 */
    uint16_t mean_crossing_pp_enabled;

    /*! Step Counter param 19 */
    uint16_t mcr_threshold;

    /*! Step Counter param 20 */
    uint16_t sc_12_res;
};

/*!
 * @brief Structure to define gyroscope user gain configuration
 */
struct bmi3_gyro_user_gain_config
{
    /*! Gain update value for x-axis */
    uint16_t ratio_x;

    /*! Gain update value for y-axis */
    uint16_t ratio_y;

    /*! Gain update value for z-axis */
    uint16_t ratio_z;
};

/*!
 * @brief Structure to define tilt configuration
 */
struct bmi3_tilt_config
{
    /*! Duration for which the acceleration vector is averaged to be reference vector */
    uint16_t segment_size;

    /*! Minimum tilt angle */
    uint16_t min_tilt_angle;

    /*! Mean of acceleration vector */
    uint16_t beta_acc_mean;
};

/*!
 * @brief Structure to define orientation configuration
 */
struct bmi3_orientation_config
{
    /*! Upside/down detection */
    uint8_t ud_en;

    /*! Symmetrical, high or low Symmetrical */
    uint8_t mode;

    /*! Blocking mode */
    uint8_t blocking;

    /*! Threshold angle */
    uint8_t theta;

    /*! Hold time of device */
    uint8_t hold_time;

    /*! Acceleration hysteresis for orientation detection */
    uint8_t hysteresis;

    /*! Slope threshold */
    uint8_t slope_thres;
};

/*!
 * @brief Structure to define flat configuration
 */
struct bmi3_flat_config
{
    /*! Theta angle for flat detection */
    uint16_t theta;

    /*! Blocking mode */
    uint16_t blocking;

    /*! Hysteresis for theta flat detection */
    uint16_t hysteresis;

    /*! Holds the duration in 50Hz samples(20msec) */
    uint16_t hold_time;

    /*! Minimum slope between consecutive acceleration samples to pervent the
     * change of flat status during large movement */
    uint16_t slope_thres;
};

/*!
 * @brief Structure to define alternate accel configuration
 */
struct bmi3_alt_accel_config
{
    /*! ODR in Hz */
    uint8_t alt_acc_odr;

    /*! Filter accel mode */
    uint8_t alt_acc_mode;

    /*! Defines the number of samples to be averaged */
    uint8_t alt_acc_avg_num;
};

/*!
 * @brief Structure to define alternate gyro configuration
 */
struct bmi3_alt_gyro_config
{
    /*! ODR in Hz */
    uint8_t alt_gyro_odr;

    /*! Filter gyro mode */
    uint8_t alt_gyro_mode;

    /*! Defines the number of samples to be averaged */
    uint8_t alt_gyro_avg_num;
};

/*!
 * @brief Structure to define alternate auto configuration
 */
struct bmi3_auto_config_change
{
    /*! Mode to set features on alternate configurations */
    uint8_t alt_conf_alt_switch_src_select;

    /*! Mode to switch from alternate configurations to user configurations */
    uint8_t alt_conf_user_switch_src_select;
};

/*!
 * @brief Structure to define tap configuration
 */
struct bmi3_tap_detector_config
{
    /*! Axis selection */
    uint8_t axis_sel;

    /*! Wait time */
    uint8_t wait_for_timeout;

    /*! Maximum number of zero crossing expected around a tap */
    uint8_t max_peaks_for_tap;

    /*! Mode for detection of tap gesture */
    uint8_t mode;

    /*! Minimum threshold for peak resulting from the tap */
    uint16_t tap_peak_thres;

    /*! Maximum duration between each taps */
    uint8_t max_gest_dur;

    /*! Maximum duration between positive and negative peaks to tap */
    uint8_t max_dur_between_peaks;

    /*! Maximum duration for which tap impact is observed */
    uint8_t tap_shock_settling_dur;

    /*! Minimum duration between two tap impact */
    uint8_t min_quite_dur_between_taps;

    /*! Minimum quite time between the two gesture detection */
    uint8_t quite_time_after_gest;
};

/*!
 * @brief Union to define the sensor configurations
 */
union bmi3_sens_config_types
{
    /*! Accelerometer configuration */
    struct bmi3_accel_config acc;

    /*! Gyroscope configuration */
    struct bmi3_gyro_config gyr;

    /*! Any-motion configuration */
    struct bmi3_any_motion_config any_motion;

    /*! No-motion configuration */
    struct bmi3_no_motion_config no_motion;

    /*! Sig_motion configuration */
    struct bmi3_sig_motion_config sig_motion;

    /*! Step counter configuration */
    struct bmi3_step_counter_config step_counter;

    /*! Gyroscope user gain configuration */
    struct bmi3_gyro_user_gain_config gyro_gain_update;

    /*! Tilt configuration */
    struct bmi3_tilt_config tilt;

    /*! Orientation configuration */
    struct bmi3_orientation_config orientation;

    /*! Flat configuration */
    struct bmi3_flat_config flat;

    /*! Tap configuration */
    struct bmi3_tap_detector_config tap;

    /*! Alternate accelerometer configuration */
    struct bmi3_alt_accel_config alt_acc;

    /*! Alternate gyroscope configuration */
    struct bmi3_alt_gyro_config alt_gyr;

    /*! Alternate auto configuration */
    struct bmi3_auto_config_change alt_auto_cfg;
};

/*!
 * @brief Structure to define the type of the sensor and its configurations
 */
struct bmi3_sens_config
{
    /*! Defines the type of sensor */
    uint8_t type;

    /*! Defines various sensor configurations */
    union bmi3_sens_config_types cfg;
};

#define BMI3_OK INT8_C(0)

/***************************************************************************** */
/*!         Sensor Macro Definitions                 */
/***************************************************************************** */
/*! Macros to define BMI3 sensor/feature types */
#define BMI3_ACCEL UINT8_C(0)
#define BMI3_GYRO UINT8_C(1)
#define BMI3_SIG_MOTION UINT8_C(2)
#define BMI3_ANY_MOTION UINT8_C(3)
#define BMI3_NO_MOTION UINT8_C(4)
#define BMI3_STEP_COUNTER UINT8_C(5)
#define BMI3_TILT UINT8_C(6)
#define BMI3_ORIENTATION UINT8_C(7)
#define BMI3_FLAT UINT8_C(8)
#define BMI3_TAP UINT8_C(9)
#define BMI3_ALT_ACCEL UINT8_C(10)
#define BMI3_ALT_GYRO UINT8_C(11)
#define BMI3_ALT_AUTO_CONFIG UINT8_C(12)

/******************************************************************************/
/*!        Accelerometer Macro Definitions               */
/******************************************************************************/
/*!  Accelerometer Bandwidth parameters */
#define BMI3_ACC_AVG1 UINT8_C(0x00)
#define BMI3_ACC_AVG2 UINT8_C(0x01)
#define BMI3_ACC_AVG4 UINT8_C(0x02)
#define BMI3_ACC_AVG8 UINT8_C(0x03)
#define BMI3_ACC_AVG16 UINT8_C(0x04)
#define BMI3_ACC_AVG32 UINT8_C(0x05)
#define BMI3_ACC_AVG64 UINT8_C(0x06)

/*! Accelerometer Output Data Rate */
#define BMI3_ACC_ODR_0_78HZ UINT8_C(0x01)
#define BMI3_ACC_ODR_1_56HZ UINT8_C(0x02)
#define BMI3_ACC_ODR_3_125HZ UINT8_C(0x03)
#define BMI3_ACC_ODR_6_25HZ UINT8_C(0x04)
#define BMI3_ACC_ODR_12_5HZ UINT8_C(0x05)
#define BMI3_ACC_ODR_25HZ UINT8_C(0x06)
#define BMI3_ACC_ODR_50HZ UINT8_C(0x07)
#define BMI3_ACC_ODR_100HZ UINT8_C(0x08)
#define BMI3_ACC_ODR_200HZ UINT8_C(0x09)
#define BMI3_ACC_ODR_400HZ UINT8_C(0x0A)
#define BMI3_ACC_ODR_800HZ UINT8_C(0x0B)
#define BMI3_ACC_ODR_1600HZ UINT8_C(0x0C)
#define BMI3_ACC_ODR_3200HZ UINT8_C(0x0D)
#define BMI3_ACC_ODR_6400HZ UINT8_C(0x0E)

/*! Accelerometer G Range */
#define BMI3_ACC_RANGE_2G UINT8_C(0x00)
#define BMI3_ACC_RANGE_4G UINT8_C(0x01)
#define BMI3_ACC_RANGE_8G UINT8_C(0x02)
#define BMI3_ACC_RANGE_16G UINT8_C(0x03)

/*! Accelerometer mode */
#define BMI3_ACC_MODE_DISABLE UINT8_C(0x00)
#define BMI3_ACC_MODE_LOW_PWR UINT8_C(0x03)
#define BMI3_ACC_MODE_NORMAL UINT8_C(0X04)
#define BMI3_ACC_MODE_HIGH_PERF UINT8_C(0x07)

/*! Accelerometer bandwidth */
#define BMI3_ACC_BW_ODR_HALF UINT8_C(0)
#define BMI3_ACC_BW_ODR_QUARTER UINT8_C(1)

/*! No-motion detection output */
#define BMI3_NO_MOTION_X_EN_MASK UINT16_C(0x0001)

/*! No-motion detection output */
#define BMI3_NO_MOTION_Y_EN_MASK UINT16_C(0x0002)
#define BMI3_NO_MOTION_Y_EN_POS UINT8_C(1)

/*! No-motion detection output */
#define BMI3_NO_MOTION_Z_EN_MASK UINT16_C(0x0004)
#define BMI3_NO_MOTION_Z_EN_POS UINT8_C(2)

/*! Any-motion detection output */
#define BMI3_ANY_MOTION_X_EN_MASK UINT16_C(0x0008)
#define BMI3_ANY_MOTION_X_EN_POS UINT8_C(3)

/*! Any-motion detection output */
#define BMI3_ANY_MOTION_Y_EN_MASK UINT16_C(0x0010)
#define BMI3_ANY_MOTION_Y_EN_POS UINT8_C(4)

/*! Any-motion detection output */
#define BMI3_ANY_MOTION_Z_EN_MASK UINT16_C(0x0020)
#define BMI3_ANY_MOTION_Z_EN_POS UINT8_C(5)

/*! Flat detection output */
#define BMI3_FLAT_EN_MASK UINT16_C(0x0040)
#define BMI3_FLAT_EN_POS UINT8_C(6)

/*! Orientation detection output */
#define BMI3_ORIENTATION_EN_MASK UINT16_C(0x0080)
#define BMI3_ORIENTATION_EN_POS UINT8_C(7)

/*! Step detector output */
#define BMI3_STEP_DETECTOR_EN_MASK UINT16_C(0x0100)
#define BMI3_STEP_DETECTOR_EN_POS UINT8_C(8)

/*! Step counter watermark output */
#define BMI3_STEP_COUNTER_EN_MASK UINT16_C(0x0200)
#define BMI3_STEP_COUNTER_EN_POS UINT8_C(9)

/*! Sigmotion detection output */
#define BMI3_SIG_MOTION_EN_MASK UINT16_C(0x0400)
#define BMI3_SIG_MOTION_EN_POS UINT8_C(10)

/*! Tilt detection output */
#define BMI3_TILT_EN_MASK UINT16_C(0x0800)
#define BMI3_TILT_EN_POS UINT8_C(11)

/*! Tap detection output */
#define BMI3_TAP_DETECTOR_S_TAP_EN_MASK UINT16_C(0x1000)
#define BMI3_TAP_DETECTOR_S_TAP_EN_POS UINT8_C(12)

#define BMI3_TAP_DETECTOR_D_TAP_EN_MASK UINT16_C(0x2000)
#define BMI3_TAP_DETECTOR_D_TAP_EN_POS UINT8_C(13)

#define BMI3_TAP_DETECTOR_T_TAP_EN_MASK UINT16_C(0x4000)
#define BMI3_TAP_DETECTOR_T_TAP_EN_POS UINT8_C(14)

typedef union bmi323_acc_conf_reg
{
    uint16_t all;
    struct
    {
        uint16_t acc_odr : 4;
        uint16_t acc_range : 3;
        uint16_t acc_bw : 1;
        uint16_t acc_avg_num : 3;
        uint16_t reserved1 : 1;
        uint16_t acc_mode : 3;
        uint16_t reserved2 : 1;
    } bit;

} bmi323_acc_conf_t;

typedef union bmi323_feature_cfg
{
    uint16_t all;
    struct
    {
        uint16_t no_motion_x_en : 1;
        uint16_t no_motion_y_en : 1;
        uint16_t no_motion_z_en : 1;
        uint16_t any_motion_x_en : 1;
        uint16_t any_motion_y_en : 1;
        uint16_t any_motion_z_en : 1;
        uint16_t flat_en : 1;
        uint16_t orientation_en : 1;
        uint16_t step_detector_en : 1;
        uint16_t step_counter_en : 1;
        uint16_t sig_motion_en : 1;
        uint16_t tilt_en : 1;
        uint16_t s_tap_en : 1;
        uint16_t d_tap_en : 1;
        uint16_t t_tap_en : 1;
        uint16_t i3c_sync_en : 1;
    } bit;
} bmi323_feature_conf_t;

struct bmi323_chip_internal_cfg
{
    bmi323_acc_conf_t reg_acc_conf;
    bmi323_feature_conf_t reg_feature_conf;
};

enum bmi323_hpi_attribute
{
    BMI323_HPI_ATTR_EN_FEATURE_ENGINE = 0x01,
    BMI323_HPI_ATTR_EN_STEP_COUNTER = 0x02,
    BMI323_HPI_ATTR_EN_ANY_MOTION = 0x03,
    BMI323_HPI_ATTR_EN_NO_MOTION = 0x04,
    BMI323_HPI_ATTR_RESET_STEP_COUNTER = 0x05,
};