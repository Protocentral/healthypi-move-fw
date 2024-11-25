/*
 * (c) 2024 Protocentral Electronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/byteorder.h>

#define MAXM86146_I2C_ADDRESS 0x55
#define MAXM86146_HUB_STAT_DRDY_MASK 0x08

#define MAXM86146_DEFAULT_CMD_DELAY 10

uint8_t maxm86146_read_hub_status(const struct device *dev);
void maxm86146_do_enter_bl(const struct device *dev);
//int m_read_op_mode(const struct device *dev);
int maxm86146_do_enter_app(const struct device *dev);

enum maxm86146_mode
{
	MAXM86146_OP_MODE_CAL,
	MAXM86146_OP_MODE_BPT,
	MAXM86146_OP_MODE_BPT_CAL_START,
	MAXM86146_OP_MODE_BPT_CAL_GET_VECTOR,
	MAXM86146_OP_MODE_IDLE,
	MAXM86146_OP_MODE_RAW,
	MAXM86146_OP_MODE_ALGO_AEC,
	MAXM86146_OP_MODE_ALGO_AGC,
	MAXM86146_OP_MODE_ALGO_EXTENDED,
	
};

enum maxm86146_attribute
{
	MAXM86146_ATTR_OP_MODE = 0x01,
	MAXM86146_ATTR_DATE_TIME = 0x02,
	MAXM86146_ATTR_BP_CAL_SYS = 0x03,
	MAXM86146_ATTR_BP_CAL = 0x04,
	MAXM86146_ATTR_START_EST = 0x05,
	MAXM86146_ATTR_STOP_EST = 0x06,
	MAXM86146_ATTR_LOAD_CALIB = 0x07,
	MAXM86146_ATTR_ENTER_BOOTLOADER = 0x08,
	MAXM86146_ATTR_DO_FW_UPDATE = 0x09,

	MAXM86146_ATTR_IS_APP_PRESENT = 0x10,

};

struct maxm86146_config
{
	struct i2c_dt_spec i2c;
	struct gpio_dt_spec reset_gpio;
	struct gpio_dt_spec mfio_gpio;
};

struct maxm86146_data
{
    uint8_t num_channels;
    uint8_t num_samples;

	uint8_t hub_ver[4];

    uint32_t samples_led_ir[128];
    uint32_t samples_led_red[128];

    uint8_t op_mode;

    uint16_t hr;
    uint16_t spo2;
    uint16_t spo2_r_val;
    uint8_t hr_above_resting;

    uint8_t calib_vector[824];
};

// Async API types

struct maxm86146_decoder_header
{
	uint64_t timestamp;
} __attribute__((__packed__));

struct maxm86146_encoded_data
{
	struct maxm86146_decoder_header header;
	uint32_t num_samples;

	uint32_t green_samples[32];
	uint32_t red_samples[32];
	uint32_t ir_samples[32];

	uint16_t hr;
	uint16_t spo2;

	uint16_t rtor;

	uint8_t scd_state;

	// Extended algo mode only
	uint8_t activity_class;
	uint32_t steps_run;
	uint32_t steps_walk;
};

int maxm86146_submit(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe);
int maxm86146_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder);
