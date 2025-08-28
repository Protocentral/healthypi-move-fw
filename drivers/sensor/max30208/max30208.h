/*
 * Copyright (c) 2017, NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/devicetree.h>

#define MAX30208_CHIP_ID 0x30

#define MAX30208_REG_CHIP_ID  0xFF
#define MAX30208_REG_FIFO_DATA 0x08
#define MAX30208_REG_STATUS 0x00
#define MAX30208_REG_TEMP_SENSOR_SETUP 0x14
#define MAX30208_REG_GPIO_SETUP 0x20

#define MAX30208_CONVERT_T 0x01


struct max30208_config
{
  struct i2c_dt_spec i2c;
  struct gpio_dt_spec gpio0;
  struct gpio_dt_spec gpio1;

  /* Mode values (two-bit each) for gpio0 and gpio1 read from DT properties
   * Default is 2 which corresponds to "Input with 1MOhm pulldown"
   */
  uint8_t gpio0_mode;
  uint8_t gpio1_mode;
};

struct max30208_data
{
  int32_t temp_int;
  float temperature;
};
