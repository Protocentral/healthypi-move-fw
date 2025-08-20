// ProtoCentral Electronics (ashwin@protocentral.com)
// SPDX-License-Identifier: Apache-2.0

#ifndef __MAX30208_H
#define __MAX30208_H

#if defined(CONFIG_SENSOR)

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/rtio/rtio.h>

#define MAX30208_CHIP_ID 0x30

#define MAX30208_REG_CHIP_ID  0xFF
#define MAX30208_REG_FIFO_DATA 0x08
#define MAX30208_REG_STATUS 0x00
#define MAX30208_REG_TEMP_SENSOR_SETUP 0x14

#define MAX30208_CONVERT_T 0x01


struct max30208_config
{
  struct i2c_dt_spec i2c;
};

struct max30208_data
{
  int32_t temp_int;
  float temperature;

  /* Optional RTIO context/iodev for async sensor_read */
  struct rtio *r;
  struct rtio_iodev *iodev;
};

/* Async encoded data structures */
struct max30208_decoder_header
{
  uint64_t timestamp;
} __attribute__((__packed__));

struct max30208_encoded_data
{
  struct max30208_decoder_header header;
  uint8_t raw_be[2]; /* raw temperature counts in big-endian (MSB, LSB) */
};

/* Async API hooks */
void max30208_submit(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe);
int max30208_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder);
/* Blocking spot measurement: returns raw signed int16 on success or negative errno */
int max30208_get_temp(const struct device *dev, int32_t timeout_ms);

#endif /* CONFIG_SENSOR */

#endif /* __MAX30208_H */
