/*
 * (c) 2024 Protocentral Electronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */ 
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/rtio/rtio.h>
#include <zephyr/kernel.h>

#define MAX32664C_I2C_ADDRESS 0x55

#define MAX32664C_HUB_STAT_DRDY_MASK 0x08
#define MAX32664C_HUB_STAT_SCD_MASK 0x80

#define MAX32664C_DEFAULT_CMD_DELAY 10

#define MAX32664C_LATEST_APP_VER1 13
#define MAX32664C_LATEST_APP_VER2 31

#define MAX32664C_AFE_ID 0x25
#define MAX32664C_ACC_ID 0x1B

uint8_t max32664c_read_hub_status(const struct device *dev);
void max32664c_do_enter_bl(const struct device *dev);
//int m_read_op_mode(const struct device *dev);
int max32664c_do_enter_app(const struct device *dev);

enum max32664c_mode
{
	MAX32664C_OP_MODE_CAL,
	MAX32664C_OP_MODE_IDLE,
	MAX32664C_OP_MODE_RAW,
	MAX32664C_OP_MODE_ALGO_AEC,
	MAX32664C_OP_MODE_ALGO_AGC,
	MAX32664C_OP_MODE_ALGO_EXTENDED,
	MAX32664C_OP_MODE_SCD,
	MAX32664C_OP_MODE_WAKE_ON_MOTION,
	MAX32664C_OP_MODE_EXIT_WAKE_ON_MOTION,
	MAX32664C_OP_MODE_STOP_ALGO,
};

enum max32664c_algo_op_mode
{
	MAX32664C_ALGO_MODE_CONT_HR_CONT_SPO2 = 0x00,
	MAX32664C_ALGO_MODE_CONT_HR_SHOT_SPO2 = 0x01,
	MAX32664C_ALGO_MODE_CONT_HRM = 0x02,
	MAX32664C_ALGO_MODE_SAMPLED_HRM = 0x03,
	MAX32664C_ALGO_MODE_SAMPLED_HRM_SHOT_SPO2 = 0x04,
	MAX32664C_ALGO_MODE_ACT_TRACK = 0x05,
	MAX32664C_ALGO_MODE_SPO2_CAL = 0x06,
	MAX32664C_ALGO_MODE_CONT_HRM_FAST_SPO2 = 0x07,

	MAX32664C_ALGO_MODE_NONE = 0xFF,
};

enum max32664c_attribute
{
	MAX32664C_ATTR_OP_MODE = 0x01,
	MAX32664C_ATTR_DATE_TIME = 0x02,
	MAX32664C_ATTR_BP_CAL_SYS = 0x03,
	MAX32664C_ATTR_BP_CAL = 0x04,
	MAX32664C_ATTR_START_EST = 0x05,
	MAX32664C_ATTR_STOP_EST = 0x06,
	MAX32664C_ATTR_LOAD_CALIB = 0x07,
	MAX32664C_ATTR_ENTER_BOOTLOADER = 0x08,
	MAX32664C_ATTR_DO_FW_UPDATE = 0x09,

	MAX32664C_ATTR_IS_APP_PRESENT = 0x10,
	MAX32664C_ATTR_APP_VER=0x11,
	MAX32664C_ATTR_SENSOR_IDS=0x12,
};

enum max32664c_scd_states
{
	MAX32664C_SCD_STATE_UNKNOWN = 0,
	MAX32664C_SCD_STATE_OFF_SKIN = 1,
	MAX32664C_SCD_STATE_ON_OBJECT = 2,
	MAX32664C_SCD_STATE_ON_SKIN = 3,
};

struct max32664c_config
{
	struct i2c_dt_spec i2c;
	struct gpio_dt_spec reset_gpio;
	struct gpio_dt_spec mfio_gpio;  // Also acts as interrupt after initialization
};

struct max32664c_data
{
    uint8_t num_channels;
    uint8_t num_samples;
    uint8_t op_mode;

    // Async RTIO state
    struct {
        atomic_t in_progress;
        struct gpio_callback int_callback;
        struct k_work_delayable work;
        struct rtio_iodev_sqe *pending_sqe;
        bool interrupt_enabled;
        uint32_t last_interrupt_time;
    } async;

    // Hardware state
    uint16_t hr;
    uint16_t spo2;
    uint16_t spo2_r_val;
    uint8_t hr_above_resting;
    uint8_t scd_state;

    uint8_t calib_vector[824];

    // Buffer management
    struct {
        uint8_t fifo_count;
        uint32_t expected_samples;
        bool data_ready;
    } buffer_state;

	// Chip info	
	uint8_t hub_ver[4];
	uint8_t max86141_id;
	uint8_t accel_id;
};

// Async API types

struct max32664c_decoder_header
{
	uint64_t timestamp;
} __attribute__((__packed__));

struct max32664c_encoded_data
{
	struct max32664c_decoder_header header;
	uint8_t chip_op_mode;

	uint32_t num_samples;

	uint32_t green_samples[32];
	uint32_t red_samples[32];
	uint32_t ir_samples[32];

	uint16_t hr;
	uint8_t hr_confidence;

	uint16_t spo2;
	uint8_t spo2_confidence;
	uint8_t spo2_valid_percent_complete;
	uint8_t spo2_low_quality;
	uint8_t spo2_excessive_motion;
	uint8_t spo2_low_pi;
	uint8_t spo2_state;
	
	uint16_t rtor;
	uint8_t rtor_confidence;

	uint8_t scd_state;

	// Extended algo mode only
	uint8_t activity_class;
	uint32_t steps_run;
	uint32_t steps_walk;
};

// Function prototypes
int max32664c_submit(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe);
int max32664c_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder);

// Stream functions
int max32664c_stream_init(const struct device *dev);
int max32664c_stream_enable(const struct device *dev);
int max32664c_stream_disable(const struct device *dev);

// Trigger configuration
struct max32664c_trigger_config {
    bool data_ready_en;
    bool fifo_watermark_en;
    bool scd_change_en;
    uint8_t fifo_watermark_level;
};
