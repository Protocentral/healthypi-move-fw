/*
 * MAX32664C device async decoder
 * Protocentral Electronics Pvt Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>
#include "max32664c.h"
#include "max32664c_decoder.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MAX32664C_DECODER, CONFIG_SENSOR_LOG_LEVEL);

#define DT_DRV_COMPAT maxim_max32664c

static int max32664c_decoder_get_frame_count(const uint8_t *buffer,
											struct sensor_chan_spec chan_spec,
											uint16_t *frame_count)
{
	ARG_UNUSED(buffer);
	ARG_UNUSED(chan_spec);

	/* This sensor lacks a FIFO; there will always only be one frame at a time. */
	*frame_count = 1;
	return 0;
}

static int max32664c_decoder_get_size_info(struct sensor_chan_spec chan_spec,
										  size_t *base_size,
										  size_t *frame_size)
{
	switch (chan_spec.chan_type)
	{
	case SENSOR_CHAN_MAGN_X:
	case SENSOR_CHAN_MAGN_Y:
	case SENSOR_CHAN_MAGN_Z:
	case SENSOR_CHAN_MAGN_XYZ:
		//*base_size = sizeof(struct sensor_three_axis_data);
		//*frame_size = sizeof(struct sensor_three_axis_sample_data);
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int max32664c_decoder_decode(const uint8_t *buffer,
								   struct sensor_chan_spec chan_spec,
								   uint32_t *fit,
								   uint16_t max_count, void *data_out)
{
	const struct max32664c_encoded_data *edata = (const struct max32664c_encoded_data *)buffer;
	const struct max32664c_decoder_header *header = &edata->header;

	ARG_UNUSED(chan_spec);
	ARG_UNUSED(max_count);
	ARG_UNUSED(data_out);

	if (*fit != 0) {
		return 0;
	}

	return 0;
}


SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = max32664c_decoder_get_frame_count,
	.get_size_info = max32664c_decoder_get_size_info,
	.decode = max32664c_decoder_decode,
};

int max32664c_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();

	return 0;
}