/*
 * MAX32664C device async decoder
 * Protocentral Electronics Pvt Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include "max32664c.h"
#include <zephyr/drivers/sensor_data_types.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MAX32664C_DECODER, CONFIG_SENSOR_LOG_LEVEL);

#define DT_DRV_COMPAT maxim_max32664c

static int max32664c_decoder_get_frame_count(const uint8_t *buffer, struct sensor_chan_spec channel,
											uint16_t *frame_count)
{
	const struct max32664c_encoded_data *edata = (const struct max32664c_encoded_data *)buffer;
	
	ARG_UNUSED(channel);

	*frame_count = edata->num_samples;
	return 0;
}

static int max32664c_decoder_get_size_info(struct sensor_chan_spec channel, size_t *base_size,
										  size_t *frame_size)
{
	switch (channel.chan_type) {
	case SENSOR_CHAN_RED:
	case SENSOR_CHAN_GREEN:
	case SENSOR_CHAN_IR:
		*base_size = sizeof(struct sensor_uint64_data);
		*frame_size = sizeof(struct sensor_uint64_sample_data);
		return 0;
		
	case SENSOR_CHAN_VOLTAGE:
	case SENSOR_CHAN_CURRENT:
		*base_size = sizeof(struct sensor_q31_data);
		*frame_size = sizeof(struct sensor_q31_sample_data);
		return 0;
		
	default:
		return -ENOTSUP;
	}
}

static int max32664c_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec channel,
								   uint32_t *fit, uint16_t max_count, void *data_out)
{
	const struct max32664c_encoded_data *edata = (const struct max32664c_encoded_data *)buffer;
	
	if (*fit != 0) {
		return 0;
	}
	
	if (max_count == 0) {
		return 0;
	}
	
	switch (channel.chan_type) {
	case SENSOR_CHAN_RED: {
		struct sensor_uint64_data *out = (struct sensor_uint64_data *)data_out;
		
		out->header.base_timestamp_ns = edata->header.timestamp;
		out->header.reading_count = MIN(edata->num_samples, max_count);
		
		for (int i = 0; i < out->header.reading_count; i++) {
			out->readings[i].timestamp_delta = 0;
			out->readings[i].value = edata->red_samples[i];
		}
		
		*fit = out->header.reading_count;
		return out->header.reading_count;
	}
	
	case SENSOR_CHAN_GREEN: {
		struct sensor_uint64_data *out = (struct sensor_uint64_data *)data_out;
		
		out->header.base_timestamp_ns = edata->header.timestamp;
		out->header.reading_count = MIN(edata->num_samples, max_count);
		
		for (int i = 0; i < out->header.reading_count; i++) {
			out->readings[i].timestamp_delta = 0;
			out->readings[i].value = edata->green_samples[i];
		}
		
		*fit = out->header.reading_count;
		return out->header.reading_count;
	}
	
	case SENSOR_CHAN_IR: {
		struct sensor_uint64_data *out = (struct sensor_uint64_data *)data_out;
		
		out->header.base_timestamp_ns = edata->header.timestamp;
		out->header.reading_count = MIN(edata->num_samples, max_count);
		
		for (int i = 0; i < out->header.reading_count; i++) {
			out->readings[i].timestamp_delta = 0;
			out->readings[i].value = edata->ir_samples[i];
		}
		
		*fit = out->header.reading_count;
		return out->header.reading_count;
	}
	
	case SENSOR_CHAN_VOLTAGE: {
		struct sensor_q31_data *out = (struct sensor_q31_data *)data_out;
		
		out->header.base_timestamp_ns = edata->header.timestamp;
		out->header.reading_count = 1;
		out->shift = 0;
		out->readings[0].timestamp_delta = 0;
		out->readings[0].value = edata->hr << 16; // Convert to q31 format
		
		*fit = 1;
		return 1;
	}
	
	case SENSOR_CHAN_CURRENT: {
		struct sensor_q31_data *out = (struct sensor_q31_data *)data_out;
		
		out->header.base_timestamp_ns = edata->header.timestamp;
		out->header.reading_count = 1;
		out->shift = 0;
		out->readings[0].timestamp_delta = 0;
		out->readings[0].value = edata->spo2 << 16; // Convert to q31 format
		
		*fit = 1;
		return 1;
	}
	
	default:
		return -ENOTSUP;
	}
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