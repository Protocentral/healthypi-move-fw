// ProtoCentral Electronics (ashwin@protocentral.com)
// SPDX-License-Identifier: Apache-2.0

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/i2c.h>

/* AS6221 register map */
#define AS6221_REG_TVAL   0x00 /* Temperature value (read-only), 16-bit */
#define AS6221_REG_CONFIG 0x01 /* Configuration register, 16-bit */
#define AS6221_REG_TLOW   0x02 /* Low temperature threshold, 16-bit */
#define AS6221_REG_THIGH  0x03 /* High temperature threshold, 16-bit */

/*
 * Temperature resolution: 1 LSB = 0.0078125 °C (1/128 °C).
 * Expressed as micro-degrees-C scaled by 10 to keep the math integer:
 * 0.0078125 °C = 78125 / 10 µ°C per LSB.
 */
#define AS6221_LSB_UCELSIUS_X10 78125

struct as6221_config
{
	struct i2c_dt_spec i2c;
};

struct as6221_data
{
	int32_t raw_temp; /* Raw signed 16-bit TVAL reading */
};
