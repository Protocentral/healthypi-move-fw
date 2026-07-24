/*
 * Copyright (c) 2017, NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/i2c.h>

#define MAX30208_CHIP_ID 0x30

#define MAX30208_REG_CHIP_ID  0xFF
#define MAX30208_REG_FIFO_DATA 0x08
#define MAX30208_REG_STATUS 0x00
#define MAX30208_REG_TEMP_SENSOR_SETUP 0x14

#define MAX30208_CONVERT_T 0x01

/* STATUS register (0x00) bit0 = TEMP_RDY: set when a one-shot conversion has
 * completed and the sample is in the FIFO. Conversion is 15 ms typ / 50 ms max
 * (MAX30208 datasheet) — poll this instead of a blind worst-case sleep. */
#define MAX30208_STATUS_TEMP_RDY 0x01


struct max30208_config
{
  struct i2c_dt_spec i2c;
};

struct max30208_data
{
  int32_t temp_int;
  float temperature;
};
