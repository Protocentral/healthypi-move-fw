/*
 * Copyright (c) 2017, NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/i2c.h>

#define MAX30208_ADDRESS1 0x51 // 8bit address converted to 7bit

// Registers
#define MAX30208_TEMPERATURE 0x00   //  get temperature ,Read only
#define MAX30208_CONFIGURATION 0x01 //
#define MAX30208_THYST 0x02         //
#define MAX30208_TOS 0x03           //

typedef enum
{                // For configuration registers
  SHUTDOWN,      // shutdwon mode to reduce power consumption <3.5uA
  COMPARATOR,    // Bit 0 = operate OS in comparator mode, 1= INTERRUPT MODE
  OS_POLARITY,   // Polarity bit ;Bit 0 = Active low output, Bit 1 = Active high
  FAULT_QUEUE_0, // Fault indication bits
  FAULT_QUEUE_1, // Fault indication bits
  DATA_FORMAT,   // Data Format
  TIME_OUT,      // Time out
  ONE_SHOT       // 1= One shot, 0 = Continuos
} configuration;

struct max30208_config
{
  struct i2c_dt_spec i2c;
};

struct max30208_data
{
  int32_t temp_int;
  float temperature;
};
