// ProtoCentral Electronics (info@protocentral.com)
// SPDX-License-Identifier: Apache-2.0

/*
 * Copyright (c) 2017, NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>

#define MAX30001_STATUS_MASK_EINT 0x800000
#define MAX30001_STATUS_MASK_EOVF 0x400000

#define MAX30001_STATUS_MASK_BINT 0x080000
#define MAX30001_STATUS_MASK_BOVF 0x40000

#define MAX30001_STATUS_MASK_DCLOFF 0x100000

#define MAX30001_STATUS_MASK_RRINT 0x000400

#define MAX30001_INT_MASK_EFIT 0xF80000
#define MAX30001_INT_MASK_BFIT 0x070000

#define MAX30001_INT_SHIFT_BFIT 16
#define MAX30001_INT_SHIFT_EFIT 19

#define WREG 0x00
#define RREG 0x01

#define STATUS 0x01
#define EN_INT 0x02
#define EN_INT2 0x03
#define MNGR_INT 0x04
#define MNGR_DYN 0x05
#define SW_RST 0x08
#define SYNCH 0x09
#define FIFO_RST 0x0A
#define INFO 0x0F
#define CNFG_GEN 0x10
#define CNFG_CAL 0x12
#define CNFG_EMUX 0x14
#define CNFG_ECG 0x15

#define CNFG_BIOZ_LC 0x1A

#define CNFG_BMUX 0x17
#define CNFG_BIOZ 0x18

#define CNFG_RTOR1 0x1D
#define CNFG_RTOR2 0x1E

#define ECG_FIFO_BURST 0x20
#define ECG_FIFO 0x21

#define BIOZ_FIFO_BURST 0x22
#define BIOZ_FIFO 0x23

#define RTOR 0x25
#define NO_OP 0x7F

#define CLK_PIN 6
#define RTOR_INTR_MASK 0x04

enum max30001_channel
{

	/** Sensor ECG channel output in microvolts */
	SENSOR_CHAN_ECG_UV = SENSOR_CHAN_PRIV_START,

	/** Sensor BioZ channel output in microvolts */
	SENSOR_CHAN_BIOZ_UV = SENSOR_CHAN_PRIV_START + 1,
	SENSOR_CHAN_RTOR = SENSOR_CHAN_PRIV_START + 2,
	SENSOR_CHAN_HR = SENSOR_CHAN_PRIV_START + 3,
	SENSOR_CHAN_LDOFF = SENSOR_CHAN_PRIV_START + 4,

};

enum max30001_attribute
{
	MAX30001_ATTR_ECG_ENABLED = 0x01,
	MAX30001_ATTR_BIOZ_ENABLED = 0x02,
	MAX30001_ATTR_RTOR_ENABLED = 0x03,
	MAX30001_ATTR_DCLOFF_ENABLED = 0x04,
};

typedef union max30001_status_reg
{
	uint32_t all;

	struct
	{
		uint32_t loff_nl : 1;
		uint32_t loff_nh : 1;
		uint32_t loff_pl : 1;
		uint32_t loff_ph : 1;

		uint32_t bcgmn : 1;
		uint32_t bcgmp : 1;
		uint32_t reserved1 : 1;
		uint32_t reserved2 : 1;

		uint32_t pllint : 1;
		uint32_t samp : 1;
		uint32_t rrint : 1;
		uint32_t lonint : 1;

		uint32_t pedge : 1;
		uint32_t povf : 1;
		uint32_t pint : 1;
		uint32_t bcgmon : 1;

		uint32_t bundr : 1;
		uint32_t bover : 1;
		uint32_t bovf : 1;
		uint32_t bint : 1;

		uint32_t dcloffint : 1;
		uint32_t fstint : 1;
		uint32_t eovf : 1;
		uint32_t eint : 1;

		uint32_t reserved : 8;

	} bit;

} max30001_status_t;

/**
 * @brief CNFG_GEN (0x10)
 */
typedef union max30001_cnfg_gen_reg
{
	uint32_t all;
	struct
	{
		uint32_t rbiasn : 1;
		uint32_t rbiasp : 1;
		uint32_t rbiasv : 2;
		uint32_t en_rbias : 2;
		uint32_t vth : 2;
		uint32_t imag : 3;
		uint32_t ipol : 1;
		uint32_t en_dcloff : 2;
		uint32_t en_bloff : 2;
		uint32_t reserved1 : 1;
		uint32_t en_pace : 1;
		uint32_t en_bioz : 1;
		uint32_t en_ecg : 1;
		uint32_t fmstr : 2;
		uint32_t en_ulp_lon : 2;
		uint32_t reserved : 8;
	} bit;

} max30001_cnfg_gen_t;

/**
 * @brief MNGR_INT (0x04)
 */
typedef union max30001_mngr_int_reg
{
	uint32_t all;

	struct
	{
		uint32_t samp_it : 2;
		uint32_t clr_samp : 1;
		uint32_t clr_pedge : 1;
		uint32_t clr_rrint : 2;
		uint32_t clr_fast : 1;
		uint32_t reserved1 : 1;
		uint32_t reserved2 : 4;
		uint32_t reserved3 : 4;

		uint32_t b_fit : 3;
		uint32_t e_fit : 5;

		uint32_t reserved : 8;

	} bit;

} max30001_mngr_int_t;

/**
 * @brief CNFG_EMUX  (0x14)
 */
typedef union max30001_cnfg_emux_reg
{
	uint32_t all;
	struct
	{
		uint32_t reserved1 : 16;
		uint32_t caln_sel : 2;
		uint32_t calp_sel : 2;
		uint32_t openn : 1;
		uint32_t openp : 1;
		uint32_t reserved2 : 1;
		uint32_t pol : 1;
		uint32_t reserved : 8;
	} bit;

} max30001_cnfg_emux_t;

/**
 * @brief CNFG_ECG   (0x15)
 */
typedef union max30001_cnfg_ecg_reg
{
	uint32_t all;
	struct
	{
		uint32_t reserved1 : 12;
		uint32_t dlpf : 2;
		uint32_t dhpf : 1;
		uint32_t reserved2 : 1;
		uint32_t gain : 2;
		uint32_t reserved3 : 4;
		uint32_t rate : 2;

		uint32_t reserved : 8;
	} bit;

} max30001_cnfg_ecg_t;

/**
 * @brief CNFG_BMUX   (0x17)
 */
typedef union max30001_cnfg_bmux_reg
{
	uint32_t all;
	struct
	{
		uint32_t fbist : 2;
		uint32_t reserved1 : 2;
		uint32_t rmod : 3;
		uint32_t reserved2 : 1;
		uint32_t rnom : 3;
		uint32_t en_bist : 1;
		uint32_t cg_mode : 2;
		uint32_t reserved3 : 2;
		uint32_t caln_sel : 2;
		uint32_t calp_sel : 2;
		uint32_t openn : 1;
		uint32_t openp : 1;
		uint32_t reserved4 : 2;
		uint32_t reserved : 8;
	} bit;

} max30001_cnfg_bmux_t;

/**
 * @brief CNFG_BIOZ   (0x18)
 */
typedef union max30001_bioz_reg
{
	uint32_t all;
	struct
	{
		uint32_t phoff : 4;
		uint32_t cgmag : 3;
		uint32_t cgmon : 1;
		uint32_t fcgen : 4;
		uint32_t dlpf : 2;
		uint32_t dhpf : 2;
		uint32_t gain : 2;
		uint32_t ln_bioz : 1;
		uint32_t ext_rbias : 1;
		uint32_t ahpf : 3;
		uint32_t rate : 1;
		uint32_t reserved : 8;
	} bit;

} max30001_cnfg_bioz_t;

/**
 * @brief CNFG_RTOR1   (0x1D)
 */
typedef union max30001_cnfg_rtor1_reg
{
	uint32_t all;
	struct
	{
		uint32_t reserved1 : 8;
		uint32_t ptsf : 4;
		uint32_t pavg : 2;
		uint32_t reserved2 : 1;
		uint32_t en_rtor : 1;
		uint32_t gain : 4;
		uint32_t wndw : 4;
		uint32_t reserved : 8;
	} bit;

} max30001_cnfg_rtor1_t;

/**
 * @brief CNFG_RTOR2 (0x1E)
 */
typedef union max30001_cnfg_rtor2_reg
{
	uint32_t all;
	struct
	{
		uint32_t reserved1 : 8;
		uint32_t rhsf : 3;
		uint32_t reserved2 : 1;
		uint32_t ravg : 2;
		uint32_t reserved3 : 2;
		uint32_t hoff : 6;
		uint32_t reserved4 : 2;
		uint32_t reserved : 8;
	} bit;

} max30001_cnfg_rtor2_t;

struct max30001_chip_internal_config
{
	max30001_cnfg_gen_t reg_cnfg_gen;
	max30001_cnfg_ecg_t reg_cnfg_ecg;
	max30001_cnfg_bioz_t reg_cnfg_bioz;
	max30001_cnfg_bmux_t reg_cnfg_bmux;
	max30001_cnfg_emux_t reg_cnfg_emux;
};

struct max30001_config
{
	struct spi_dt_spec spi;
	struct gpio_dt_spec intb_gpio;
	struct gpio_dt_spec int2b_gpio;

	bool ecg_enabled;
	bool bioz_enabled;
	bool rtor_enabled;

	// Lead Off Detection
	bool ecg_dcloff_enabled;
	int ecg_dcloff_current;

	// ECG channel settings
	int ecg_gain;
	bool ecg_invert;

	// BioZ channel settings
	int bioz_gain;
	int bioz_cgmag;
	bool bioz_lc_hi_lo_en;
	bool bioz_ln_en;
	int bioz_dlpf;
	int bioz_dhpf;
	int bioz_ahpf;
};

struct max30001_decoder_header
{
	uint64_t timestamp;
} __attribute__((__packed__));

struct max30001_data
{
	struct max30001_chip_internal_config chip_cfg;

	int32_t s32ECGData[128];
	int32_t s32BIOZData[128];

	int32_t s32ecg_sample;
	int32_t s32bioz_sample;

	uint16_t lastRRI;
	uint16_t lastHR;

	uint8_t ecg_lead_off;
	uint8_t bioz_lead_off;
};

struct max30001_encoded_data
{
	struct max30001_decoder_header header;
	int32_t ecg_samples[32];
	int32_t bioz_samples[32];
	
	uint32_t num_samples_ecg;
	uint32_t num_samples_bioz;

	uint16_t rri;
	uint16_t hr;

	uint8_t ecg_lead_off;
	uint8_t bioz_lead_off;
};

int max30001_submit(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe);
int max30001_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder);

void max30001_synch(const struct device *dev);
void max30001_fifo_reset(const struct device *dev);
uint32_t max30001_read_reg(const struct device *dev, uint8_t reg);
uint32_t max30001_read_status(const struct device *dev);