/*
 * Copyright (c) 2017, NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/i2c.h>

#define MAX30208_ADDRESS1 0x51 // Default I2C address

#define MAX30208_CHIP_ID 0x30

#define MAX30208_ALARM_HIGH_MSB 0x10
#define MAX30208_ALARM_HIGH_LSB 0x11
#define MAX30208_ALARM_LOW_MSB 0x12
#define MAX30208_ALARM_LOW_LSB 0x13
#define MAX30208_TEMP_SENSOR_SETUP 0x14

#define MAX30208_FIFO_WRITE_POINTER 0x04
#define MAX30208_FIFO_READ_POINTER 0x05
#define MAX30208_FIFO_OVERFLOW_COUNTER 0x06
#define MAX30208_FIFO_DATA_COUNTER 0x07
#define MAX30208_FIFO_DATA 0x08
#define MAX30208_FIFO_CONFIGURATION1 0x09
#define MAX30208_FIFO_CONFIGURATION2 0x0A

#define MAX30208_INTERRUPT_STATUS 0x00
#define MAX30208_INTERRUPT_ENABLE 0x01
#define MAX30208_INT_STATUS_AFULL 0x80
#define MAX30208_INT_STATUS_TEMP_LOW 0x04
#define MAX30208_INT_STATUS_TEMP_HIGH 0x02
#define MAX30208_INT_STATUS_TEMP_RDY 0x01

struct max30208_config
{
  struct i2c_dt_spec i2c;
};

struct max30208_data
{
  int32_t temp_int;
  float temperature;
};
