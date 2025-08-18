/*
 * MAX32664C decoder header
 * Protocentral Electronics Pvt Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_SENSOR_MAX32664C_DECODER_H_
#define ZEPHYR_DRIVERS_SENSOR_MAX32664C_DECODER_H_

#include <stdint.h>
#include <zephyr/drivers/sensor.h>
#include "max32664c.h"

int max32664c_get_decoder(const struct device *dev,
                          const struct sensor_decoder_api **decoder);

/* Encoded data structures used by async decoder */
struct max32664c_decoder_header {
    uint64_t timestamp;
} __attribute__((__packed__));

struct max32664c_encoded_data {
    struct max32664c_decoder_header header;
    uint8_t num_samples;

    uint32_t red_samples[32];
    uint32_t ir_samples[32];

    uint16_t hr;
    uint16_t spo2;
    uint8_t spo2_conf;

    uint8_t bpt_progress;
    uint8_t bpt_status;
    uint8_t bpt_sys;
    uint8_t bpt_dia;
};

#endif /* ZEPHYR_DRIVERS_SENSOR_MAX32664C_DECODER_H_ */
