/* Only build when sensor subsystem is enabled */
#ifdef CONFIG_SENSOR

/*
 * MAX30208 async decoder
 * Protocentral Electronics
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT maxim_max30208

#include "max30208.h"
#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor_clock.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(MAX30208_DECODER, CONFIG_SENSOR_LOG_LEVEL);

static int max30208_decoder_get_frame_count(const uint8_t *buffer,
                                            enum sensor_channel channel,
                                            size_t channel_idx,
                                            uint16_t *frame_count)
{
    ARG_UNUSED(buffer);
    ARG_UNUSED(channel);
    ARG_UNUSED(channel_idx);

    *frame_count = 1;
    return 0;
}

static int max30208_decoder_get_size_info(enum sensor_channel channel,
                                          size_t *base_size,
                                          size_t *frame_size)
{
    if (channel != SENSOR_CHAN_AMBIENT_TEMP) {
        return -ENOTSUP;
    }

    *base_size = sizeof(struct sensor_q31_data);
    *frame_size = sizeof(struct sensor_q31_sample_data);
    return 0;
}

static int max30208_decoder_decode(const uint8_t *buffer,
                                   enum sensor_channel channel,
                                   size_t channel_idx,
                                   uint32_t *fit,
                                   uint16_t max_count, void *data_out)
{
    const struct max30208_encoded_data *edata = (const struct max30208_encoded_data *)buffer;
    ARG_UNUSED(channel_idx);
    ARG_UNUSED(max_count);

    if (*fit != 0) {
        return 0;
    }

    if (channel != SENSOR_CHAN_AMBIENT_TEMP) {
        return -ENOTSUP;
    }

    struct sensor_q31_data *out = (struct sensor_q31_data *)data_out;

    /* Convert raw temp (signed int16) to Q31 celsius: sensor uses 0.005 degC per LSB */
    int32_t raw = (int16_t)sys_get_be16(edata->raw_be); /* raw is in counts (big-endian) */
    /* temp_c = raw * 0.005 */
    int64_t temp_q31 = (int64_t)raw * 5 * (1LL << 28) / 1000; /* approximate */

    out->header.base_timestamp_ns = edata->header.timestamp;
    out->header.reading_count = 1;
    out->readings[0].temperature = (q31_t)temp_q31;
    *fit = 1;

    return 1;
}

SENSOR_DECODER_API_DT_DEFINE() = {
    .get_frame_count = max30208_decoder_get_frame_count,
    .get_size_info = max30208_decoder_get_size_info,
    .decode = max30208_decoder_decode,
};

int max30208_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
    ARG_UNUSED(dev);
    *decoder = &SENSOR_DECODER_NAME();
    return 0;
}

#endif /* CONFIG_SENSOR */
