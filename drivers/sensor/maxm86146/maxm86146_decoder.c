/*
 * HealthyPi Move
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 *
 * MAXM86146 Data Decoder Implementation
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(MAXM86146_DECODER, CONFIG_SENSOR_LOG_LEVEL);

#include "maxm86146.h"

static int maxm86146_decoder_get_frame_count(const uint8_t *buffer,
                                             struct sensor_chan_spec chan_spec,
                                             uint16_t *frame_count)
{
    const struct maxm86146_encoded_data *edata = (const struct maxm86146_encoded_data *)buffer;

    if (chan_spec.chan_type == SENSOR_CHAN_ALL) {
        *frame_count = edata->num_samples;
        return 0;
    }

    return -ENOTSUP;
}

static int maxm86146_decoder_get_size_info(struct sensor_chan_spec chan_spec,
                                           size_t *base_size,
                                           size_t *frame_size)
{
    if (chan_spec.chan_type == SENSOR_CHAN_ALL) {
        *base_size = sizeof(struct maxm86146_encoded_data);
        *frame_size = 0;
        return 0;
    }

    return -ENOTSUP;
}

static int maxm86146_decoder_decode(const uint8_t *buffer,
                                    struct sensor_chan_spec chan_spec,
                                    uint32_t *fit,
                                    uint16_t max_count,
                                    void *data_out)
{
    const struct maxm86146_encoded_data *edata = (const struct maxm86146_encoded_data *)buffer;

    if (chan_spec.chan_type != SENSOR_CHAN_ALL) {
        return -ENOTSUP;
    }

    if (*fit >= edata->num_samples) {
        return 0;
    }

    /* Copy the encoded data to output */
    memcpy(data_out, edata, sizeof(struct maxm86146_encoded_data));

    *fit = edata->num_samples;

    return 1;
}

static bool maxm86146_decoder_has_trigger(const uint8_t *buffer,
                                          enum sensor_trigger_type trigger)
{
    return false;
}

SENSOR_DECODER_API_DT_DEFINE() = {
    .get_frame_count = maxm86146_decoder_get_frame_count,
    .get_size_info = maxm86146_decoder_get_size_info,
    .decode = maxm86146_decoder_decode,
    .has_trigger = maxm86146_decoder_has_trigger,
};

int maxm86146_get_decoder(const struct device *dev,
                          const struct sensor_decoder_api **decoder)
{
    ARG_UNUSED(dev);
    *decoder = &SENSOR_DECODER_NAME();
    return 0;
}
