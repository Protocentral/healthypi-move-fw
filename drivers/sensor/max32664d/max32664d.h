/*
 * Copyright (c) 2025 Protocentral Electronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/byteorder.h>

#define MAX32664D_HUB_STAT_DRDY_MASK 0x08
#define MAX32664_DEFAULT_CMD_DELAY 10
#define CAL_VECTOR_SIZE 512 // 512 bytes of calib vector data
#define MAX32664D_MAX_CAL_INDEX 4

enum max32664_channel
{
	SENSOR_CHAN_PPG_RED = SENSOR_CHAN_PRIV_START + 4,
	SENSOR_CHAN_PPG_IR,

	SENSOR_PPG_NUM_SAMPLES,

	SENSOR_CHAN_PPG_HR,
	SENSOR_CHAN_PPG_SPO2,
	SENSOR_CHAN_PPG_BP_SYS,
	SENSOR_CHAN_PPG_BP_DIA,
	SENSOR_CHAN_PPG_BPT_STATUS,
	SENSOR_CHAN_PPG_BPT_PROGRESS,
	SENSOR_CHAN_PPG_HR_ABOVE_RESTING,
	SENSOR_CHAN_PPG_SPO2_R_VAL,
};

enum max32664_attribute
{
	MAX32664D_ATTR_OP_MODE = 0x01,
	MAX32664D_ATTR_SET_DATE_TIME = 0x02,
	MAX32664D_ATTR_START_EST = 0x05,
	MAX32664D_ATTR_STOP_EST = 0x06,
	MAX32664D_ATTR_LOAD_CALIB = 0x07,
	MAX32664D_ATTR_CAL_SET_CURR_INDEX = 0x08,
	MAX32664D_ATTR_CAL_SET_CURR_SYS = 0x09,
	MAX32664D_ATTR_CAL_SET_CURR_DIA = 0x0A,
	MAX32664D_ATTR_CAL_FETCH_VECTOR = 0x0B,

	MAX32664D_ATTR_SENSOR_ID = 0x10,
	MAX32664D_ATTR_APP_VER = 0x11,
	MAX32664D_ATTR_SENSOR_IDS = 0x12,

};

enum max32664d_mode
{
	MAX32664D_OP_MODE_RAW = 0,
	MAX32664D_OP_MODE_CAL,
	//MAX32664D_OP_MODE_BPT,
	MAX32664D_OP_MODE_BPT_EST,
	MAX32664D_OP_MODE_BPT_CAL_START,
	MAX32664D_OP_MODE_BPT_CAL_GET_VECTOR,
	MAX32664D_OP_MODE_IDLE,
};

enum max32664_reg_families_t
{
	MAX32664_CMD_FAM_HUB_STATUS = 0x00,
	MAX32664_CMD_FAM_SET_DEVICE_MODE = 0x01,
	MAX32664_CMD_FAM_READ_DEVICE_MODE = 0x02,
	MAX32664_CMD_FAM_OUTPUT_MODE = 0x10,
	MAX32664_CMD_FAM_READ_OUTPUT_MODE = 0x11, // not on the datasheet
	MAX32664_CMD_FAM_READ_DATA_OUTPUT = 0x12,
	MAX32664_CMD_FAM_READ_DATA_INPUT = 0x13,
	MAX32664_CMD_FAM_WRITE_INPUT = 0x14, // not on the datasheet
	MAX32664_CMD_FAM_WRITE_REGISTER = 0x40,
	MAX32664_CMD_FAM_READ_REGISTER = 0x41,
	MAX32664_CMD_FAM_READ_ATTRIBUTES_AFE = 0x42,
	MAX32664_CMD_FAM_DUMP_REGISTERS = 0x43,
	MAX32664_CMD_FAM_ENABLE_SENSOR = 0x44,
	MAX32664_CMD_FAM_READ_SENSOR_MODE = 0x45, // not on the datasheet
	MAX32664_CMD_FAM_CHANGE_ALGORITHM_CONFIG = 0x50,
	MAX32664_CMD_FAM_READ_ALGORITHM_CONFIG = 0x51,
	MAX32664_CMD_FAM_ENABLE_ALGORITHM = 0x52,
	MAX32664_CMD_FAM_BOOTLOADER_FLASH = 0x80,
	MAX32664_CMD_FAM_BOOTLOADER_INFO = 0x81,
	MAX32664_CMD_FAM_IDENTITY = 0xFF
};

enum max32664_reg_status_t
{
	MAX32664_STAT_SUCCESS = 0x00,
	MAX32664_STAT_ERR_UNAVAIL_CMD = 0x01,
	MAX32664_STAT_ERR_UNAVAIL_FUNC = 0x02,
	MAX32664_STAT_ERR_DATA_FORMAT = 0x03,
	MAX32664_STAT_ERR_INPUT_VALUE = 0x04,
	MAX32664_STAT_ERR_TRY_AGAIN = 0x05,
	MAX32664_STAT_ERR_BTLDR_GENERAL = 0x80,
	MAX32664_STAT_ERR_BTLDR_CHECKSUM = 0x81,
	MAX32664_STAT_ERR_BTLDR_AUTH = 0x82,
	MAX32664_STAT_ERR_BTLDR_INVALID_APP = 0x83,
	MAX32664_STAT_ERR_UNKNOWN = 0xFF
};

struct max32664d_config
{
	struct i2c_dt_spec i2c;
	struct gpio_dt_spec reset_gpio;
	struct gpio_dt_spec mfio_gpio;
};

struct max32664d_data
{
	uint8_t num_channels;
	uint8_t num_samples;

	uint32_t samples_led_ir[128];
	uint32_t samples_led_red[128];

	uint8_t op_mode;
	// uint8_t sample_len;

	uint8_t bpt_status;
	uint8_t bpt_progress;
	uint16_t hr;
	uint8_t bpt_sys;
	uint8_t bpt_dia;
	uint16_t spo2;
	uint16_t spo2_r_val;
	uint8_t hr_above_resting;

	// Chip info
	uint8_t hub_ver[4];

	uint8_t bpt_cal_vector[512];
	
	// User calibration process data
	uint8_t curr_cal_index;
	uint8_t curr_cal_sys;
	uint8_t curr_cal_dia;
};

// Async API types

struct max32664_decoder_header
{
	uint64_t timestamp;
} __attribute__((__packed__));

struct max32664d_encoded_data
{
	struct max32664_decoder_header header;
	uint8_t num_samples;

	uint32_t red_samples[32];
	uint32_t ir_samples[32];

	uint16_t hr;
	uint16_t spo2;

	// BPT Measurements
	uint8_t bpt_progress;
	uint8_t bpt_status;
	uint8_t bpt_sys;
	uint8_t bpt_dia;
};

struct max32664_enc_calib_data
{
	struct max32664_decoder_header header;

	uint8_t calib_vector[824];
};

uint8_t max32664d_read_hub_status(const struct device *dev);
int max32664d_get_fifo_count(const struct device *dev);
int _max32664_fifo_get_samples(const struct device *dev, uint8_t *buf, int len);
int max32664_get_sample_fifo(const struct device *dev);

void max32664_do_enter_bl(const struct device *dev);
int max32664d_do_enter_app(const struct device *dev);

int max32664d_load_bpt_cal_vector(const struct device *dev, uint8_t *m_bpt_cal_vector);
int max32664d_set_bpt_cal_vector(const struct device *dev, uint8_t m_bpt_cal_index, uint8_t m_bpt_cal_vector[CAL_VECTOR_SIZE]);
int max32664d_submit(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe);
int max32664_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder);
