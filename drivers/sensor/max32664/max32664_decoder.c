/*
 * MAX32664 device async decoder
 * Protocentral Electronics Pvt Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "max32664.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MAX32664_DECODER, CONFIG_SENSOR_LOG_LEVEL);

#define DT_DRV_COMPAT maxim_max32664

static int max32664_decoder_get_frame_count(const uint8_t *buffer, enum sensor_channel channel,
											size_t channel_idx, uint16_t *frame_count)
{
	ARG_UNUSED(buffer);
	ARG_UNUSED(channel);
	ARG_UNUSED(channel_idx);

	/* This sensor lacks a FIFO; there will always only be one frame at a time. */
	*frame_count = 1;
	return 0;
}

static int max32664_decoder_get_size_info(enum sensor_channel channel, size_t *base_size,
										  size_t *frame_size)
{
	switch (channel)
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

static int max32664_convert_raw_to_q31(uint16_t reading, q31_t *out)
{
	/*int64_t intermediate = ((int64_t)reading 10) *
			       ((int64_t)INT32_MAX + 1) /
			       ((1 << AKM09918C_SHIFT) * INT64_C(1000000));
	*/
	int32_t intermediate = (int32_t)reading;
	*out = CLAMP(intermediate, INT32_MIN, INT32_MAX);
	return 0;
}

static int max32664_decoder_decode(const uint8_t *buffer, enum sensor_channel channel,
								   size_t channel_idx, uint32_t *fit,
								   uint16_t max_count, void *data_out)
{
	const struct max32664_encoded_data *edata = (const struct max32664_encoded_data *)buffer;
	const struct max32664_decoder_header *header = &edata->header;

	if (*fit != 0)
	{
		return 0;
	}

	printk("D ");
	//printk("Num samples: %u\n", edata->samples[0].ir_sample);

	switch (channel)
	{
	case SENSOR_CHAN_PPG_RED:

		struct sensor_ppg_data *out = data_out;
		out->header.base_timestamp_ns = header->timestamp;
		//out->readings[0].sample_ir = edata->ir_samples[0];
		//out->readings[0].sample_red = edata->red_samples[0];
		out->readings[0].timestamp_delta = 0;

		//out->header.base_timestamp_ns = edata->header.timestamp;
		//out->header.reading_count = edata->num_samples;

		

		//((struct sensor_three_axis_data *)data_out)->x = edata->samples[0];
		break;
	}
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