/*
 * (c) 2024-2025 Protocentral Electronics
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * MAXM86146 Integrated Optical Biosensing Module Driver
 * Based on MAX32664 Sensor Hub protocol
 */
#ifndef MAXM86146_H
#define MAXM86146_H

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/byteorder.h>

#ifdef CONFIG_SENSOR_ASYNC_API
#include <zephyr/rtio/rtio.h>
#endif

#define MAXM86146_I2C_ADDRESS 0x55

#define MAXM86146_HUB_STAT_DRDY_MASK 0x08
#define MAXM86146_HUB_STAT_SCD_MASK 0x80

#define MAXM86146_DEFAULT_CMD_DELAY 10

/*
 * Firmware version prefix for device identification.
 * The hub_ver[1] byte indicates the device type:
 * - 32 = MAX32664C (with external MAX86141 AFE)
 * - 33 = MAXM86146 (integrated optical module)
 * Example: version 33.13.31 = MAXM86146 with firmware 13.31
 */
#define MAXM86146_FW_VERSION_PREFIX 33

/* AFE IDs - MAXM86146 may report same as MAX86141 or different */
#define MAXM86146_INTEGRATED_AFE_ID 0x24
#define MAXM86146_AFE_ID_ALT 0x25

/* MAXM86146 may or may not have accelerometer depending on variant */
#define MAXM86146_ACC_ID 0x1B

/* Motion detection parameters - MAXM86146 specific
 * WUFC: Wake-up filter coefficient (time in 40ms units)
 * ATH: Acceleration threshold (in 15.6mg units)
 */
#define MAXM86146_MOTION_WUFC 0x14  /* 0.8 seconds */
#define MAXM86146_MOTION_ATH 0x20   /* ~500mg threshold */

/*
 * ============================================================================
 * MAXM86146-SPECIFIC LED AND OPTICAL CONFIGURATION
 * The integrated module has different optical characteristics than MAX32664C+MAX86141
 * These values are optimized for the integrated photodiodes and LEDs
 * ============================================================================
 */

/* LED current settings for MAXM86146 integrated LEDs (register 0x23-0x25)
 * Range: 0x00-0xFF, current = value * 0.12mA (for green) or 0.2mA (for red/IR)
 * MAXM86146 integrated LEDs have different efficiency than external MAX86141
 */
#define MAXM86146_LED1_CURRENT_DEFAULT  0x50  /* Green LED: ~9.6mA - lower for integrated */
#define MAXM86146_LED2_CURRENT_DEFAULT  0x50  /* Red LED: ~16mA */
#define MAXM86146_LED3_CURRENT_DEFAULT  0x80  /* IR LED: ~25.6mA */

/* LED current for SCD (Skin Contact Detection) mode - lower power */
#define MAXM86146_LED_SCD_CURRENT       0x02  /* Minimal current for SCD */

/* Sample rate configuration (register 0x12)
 * MAXM86146 supports: 25Hz (0x00), 50Hz (0x18), 100Hz (0x19), 200Hz (0x1A)
 */
#define MAXM86146_SAMPLE_RATE_25HZ      0x00
#define MAXM86146_SAMPLE_RATE_50HZ      0x18
#define MAXM86146_SAMPLE_RATE_100HZ     0x19
#define MAXM86146_SAMPLE_RATE_200HZ     0x1A
#define MAXM86146_SAMPLE_RATE_DEFAULT   MAXM86146_SAMPLE_RATE_100HZ

/* Pulse width / ADC resolution configuration
 * MAXM86146 pulse width affects SNR and power consumption
 */
#define MAXM86146_PULSE_WIDTH_DEFAULT   0x03  /* 411us pulse width */

/* SPO2 calibration coefficients optimized for MAXM86146 integrated optics
 * These may differ from MAX32664C due to different optical path characteristics
 */
#define MAXM86146_SPO2_COEFF_A  1.5958422f
#define MAXM86146_SPO2_COEFF_B  -34.659664f
#define MAXM86146_SPO2_COEFF_C  112.68987f

/* Alternative wake detection mode when accelerometer is not present
 * Uses periodic SCD polling instead of motion detection
 */
#define MAXM86146_SCD_POLL_INTERVAL_S   5  /* Poll SCD every 5 seconds when off-skin */

/* FIFO and reporting configuration specific to MAXM86146 */
#define MAXM86146_INT_THRESHOLD         0x01  /* DataRdyInt threshold (as per MAXM86146EVSYS) */
#define MAXM86146_REPORT_PERIOD         0x01  /* Report every sample period (40ms) */

/*
 * ============================================================================
 * MAXM86146 LED FIRING SEQUENCE AND PD MAPPING (from MAXM86146EVSYS kit)
 * The integrated module has 6 LEDs and 2 photodiodes with specific mapping
 * ============================================================================
 */

/* LED Firing Sequence Configuration (Command: 0x50 0x07 0x19)
 * Maps time slots to LEDs:
 * - Slot 1: LED1 (Green1)
 * - Slot 2: LED3 (Green2)
 * - Slot 3: LED5 (Red)
 * - Slot 4: LED6 (IR)
 * Byte format: [slot1_led | slot2_led] [slot3_led | slot4_led] [slot5_led | slot6_led]
 */
#define MAXM86146_LED_SEQ_BYTE1         0x13  /* Slot1=LED1(0x1), Slot2=LED3(0x3) */
#define MAXM86146_LED_SEQ_BYTE2         0x56  /* Slot3=LED5(0x5), Slot4=LED6(0x6) */
#define MAXM86146_LED_SEQ_BYTE3         0x00  /* Slot5=none, Slot6=none */

/* HR Input Mapping Configuration (Command: 0x50 0x07 0x17)
 * Maps HR algorithm inputs to time slots and photodiodes:
 * - HR Input 1: Slot 1, PD1
 * - HR Input 2: Slot 2, PD2
 * Byte format: [input1_slot | input1_pd] [input2_slot | input2_pd]
 */
#define MAXM86146_HR_MAP_BYTE1          0x00  /* HR Input 1: Slot 1 (0x0), PD1 (0x0) */
#define MAXM86146_HR_MAP_BYTE2          0x11  /* HR Input 2: Slot 2 (0x1), PD2 (0x1) */

/* SpO2 Input Mapping Configuration (Command: 0x50 0x07 0x18)
 * Maps SpO2 algorithm inputs to time slots and photodiodes:
 * - SpO2 IR Input: Slot 4, PD1
 * - SpO2 Red Input: Slot 3, PD1
 * Byte format: [ir_slot | ir_pd] [red_slot | red_pd]
 */
#define MAXM86146_SPO2_MAP_BYTE1        0x30  /* SpO2 IR: Slot 4 (0x3), PD1 (0x0) */
#define MAXM86146_SPO2_MAP_BYTE2        0x20  /* SpO2 Red: Slot 3 (0x2), PD1 (0x0) */

uint8_t maxm86146_read_hub_status(const struct device *dev);
void maxm86146_do_enter_bl(const struct device *dev);
int maxm86146_do_enter_app(const struct device *dev);

/* Motion detection functions */
int maxm86146_test_motion_detection(const struct device *dev);
int maxm86146_async_sample_fetch_wake_on_motion(const struct device *dev, uint8_t *chip_op_mode);

enum maxm86146_mode
{
	MAXM86146_OP_MODE_CAL,
	MAXM86146_OP_MODE_IDLE,
	MAXM86146_OP_MODE_RAW,
	MAXM86146_OP_MODE_ALGO_AEC,
	MAXM86146_OP_MODE_ALGO_AGC,
	MAXM86146_OP_MODE_ALGO_EXTENDED,
	MAXM86146_OP_MODE_SCD,
	MAXM86146_OP_MODE_WAKE_ON_MOTION,
	MAXM86146_OP_MODE_EXIT_WAKE_ON_MOTION,
	MAXM86146_OP_MODE_STOP_ALGO,
};

enum maxm86146_algo_op_mode
{
	MAXM86146_ALGO_MODE_CONT_HR_CONT_SPO2 = 0x00,
	MAXM86146_ALGO_MODE_CONT_HR_SHOT_SPO2 = 0x01,
	MAXM86146_ALGO_MODE_CONT_HRM = 0x02,
	MAXM86146_ALGO_MODE_SAMPLED_HRM = 0x03,
	MAXM86146_ALGO_MODE_SAMPLED_HRM_SHOT_SPO2 = 0x04,
	MAXM86146_ALGO_MODE_ACT_TRACK = 0x05,
	MAXM86146_ALGO_MODE_SPO2_CAL = 0x06,
	MAXM86146_ALGO_MODE_CONT_HRM_FAST_SPO2 = 0x07,

	MAXM86146_ALGO_MODE_NONE = 0xFF,
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
	MAXM86146_ATTR_APP_VER = 0x11,
	MAXM86146_ATTR_SENSOR_IDS = 0x12,
	MAXM86146_ATTR_FW_PREFIX = 0x13,  /* Returns hub_ver[1] - device type prefix (32=MAX32664C, 33=MAXM86146) */
};

enum maxm86146_scd_states
{
	MAXM86146_SCD_STATE_UNKNOWN = 0,
	MAXM86146_SCD_STATE_OFF_SKIN = 1,
	MAXM86146_SCD_STATE_ON_OBJECT = 2,
	MAXM86146_SCD_STATE_ON_SKIN = 3,
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

    uint32_t samples_led_ir[128];
    uint32_t samples_led_red[128];

    uint8_t op_mode;

    uint16_t hr;
    uint16_t spo2;
    uint16_t spo2_r_val;
    uint8_t hr_above_resting;

    uint8_t calib_vector[824];

	/* Chip info */
	uint8_t hub_ver[4];
	uint8_t afe_id;
	uint8_t accel_id;

	/* Flag indicating if accelerometer is present */
	bool has_accel;
};

/* Async API types */

struct maxm86146_decoder_header
{
	uint64_t timestamp;
} __attribute__((__packed__));

struct maxm86146_encoded_data
{
	struct maxm86146_decoder_header header;
	uint8_t chip_op_mode;

	uint32_t num_samples;

	/*
	 * Encoded sample arrays contain LED ADC values normalized to 20-bit
	 * right-aligned integers. The sensor FIFO provides 24-bit MSB-first
	 * bytes; driver assembly packs these then right-shifts by 4 bits
	 * (i.e. assembled_24bit >> 4) so the higher layers receive canonical
	 * 20-bit values that match the datasheet ADC resolution.
	 */
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

	/* Extended algo mode only */
	uint8_t activity_class;
	uint32_t steps_run;
	uint32_t steps_walk;
};

void maxm86146_submit(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe);
int maxm86146_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder);

/* I2C wrapper helpers */
int maxm86146_i2c_write(const struct i2c_dt_spec *i2c, const void *buf, size_t len);
int maxm86146_i2c_read(const struct i2c_dt_spec *i2c, void *buf, size_t len);

#ifdef CONFIG_SENSOR_ASYNC_API
int maxm86146_i2c_rtio_write(struct rtio *r, struct rtio_iodev *iodev, const void *buf, size_t len);
int maxm86146_i2c_rtio_read(struct rtio *r, struct rtio_iodev *iodev, void *buf, size_t len);
#endif

#if defined(CONFIG_SENSOR_ASYNC_API) && defined(MAXM86146_USE_RTIO_IMPL)
void maxm86146_register_rtio_context(struct rtio *r, struct rtio_iodev *iodev);
#endif

#endif /* MAXM86146_H */
