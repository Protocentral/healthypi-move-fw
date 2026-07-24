/*
 * MAX32664 device async decoder
 * Protocentral Electronics Pvt Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "max32664d.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MAX32664_DECODER, CONFIG_SENSOR_LOG_LEVEL);

#define DT_DRV_COMPAT maxim_max32664

/*
 * NCS 3.2: the sensor decoder API takes `struct sensor_chan_spec` instead of
 * `enum sensor_channel` + a separate index argument. These decoders are stubs;
 * the application reads this sensor via the driver's own RTIO processing path,
 * not the Zephyr sensor decoder API.
 */

static int max32664_decoder_get_frame_count(const uint8_t *buffer,
					    struct sensor_chan_spec channel,
					    uint16_t *frame_count)
{
	ARG_UNUSED(buffer);
	ARG_UNUSED(channel);

	/* This sensor lacks a FIFO; there will always only be one frame at a time. */
	*frame_count = 1;
	return 0;
}

static int max32664_decoder_get_size_info(struct sensor_chan_spec channel, size_t *base_size,
					  size_t *frame_size)
{
	ARG_UNUSED(base_size);
	ARG_UNUSED(frame_size);

	switch (channel.chan_type)
	{
	case SENSOR_CHAN_MAGN_X:
	case SENSOR_CHAN_MAGN_Y:
	case SENSOR_CHAN_MAGN_Z:
	case SENSOR_CHAN_MAGN_XYZ:
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int max32664_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec channel,
				   uint32_t *fit, uint16_t max_count, void *data_out)
{
	ARG_UNUSED(buffer);
	ARG_UNUSED(channel);
	ARG_UNUSED(max_count);
	ARG_UNUSED(data_out);

	if (*fit != 0)
	{
		return 0;
	}

	return 0;
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = max32664_decoder_get_frame_count,
	.get_size_info = max32664_decoder_get_size_info,
	.decode = max32664_decoder_decode,
};

int max32664_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();

	return 0;
}
