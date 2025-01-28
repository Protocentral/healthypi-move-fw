/**
 * Copyright 2024 Protocentral Electronics
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SH8601 AMOLED display driver.
 */

#define DT_DRV_COMPAT sitronix_sh8601

#include <zephyr/drivers/display.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/drivers/mipi_dbi.h>
#include <zephyr/logging/log.h>
#include "display_sh8601.h"

#include <zephyr/pm/device.h>

LOG_MODULE_REGISTER(display_sh8601, CONFIG_DISPLAY_LOG_LEVEL);

#define SH8601_PIXEL_FORMAT_RGB565 0U
#define SH8601_PIXEL_FORMAT_RGB888 1U

/*Display data struct*/
struct sh8601_data
{
	uint8_t bytes_per_pixel;
	enum display_pixel_format pixel_format;
	enum display_orientation orientation;

	bool device_in_sleep;
};

static int sh8601_set_mem_area(const struct device *dev, const uint16_t x,
							   const uint16_t y, const uint16_t w,
							   const uint16_t h);

int sh8601_transmit_cmd(const struct device *dev, uint8_t cmd, const void *tx_data,
						size_t tx_len)
{
	const struct sh8601_config *config = dev->config;

	int r;
	struct spi_buf tx_buf[3];
	struct spi_buf_set tx_bufs = {.buffers = tx_buf, .count = 2U};

	// Send Pre command
	uint8_t pre_cmd[4] = {0x02, 00};
	tx_buf[0].buf = &pre_cmd;
	tx_buf[0].len = 2U;

	uint8_t cmd_buf[2] = {cmd, 00};
	tx_buf[1].buf = &cmd_buf;
	tx_buf[1].len = 2U;

	/* send data (if any) */
	if (tx_data != NULL)
	{
		tx_buf[2].buf = (void *)tx_data;
		tx_buf[2].len = tx_len;

		tx_bufs.count = 3U;
	}

	r = spi_write_dt(&config->spi, &tx_bufs);
	if (r < 0)
	{
		return r;
	}

	return 0;
}

int sh8601_transmit_data(const struct device *dev, const void *tx_data,
						 size_t tx_len)
{
	const struct sh8601_config *config = dev->config;

	int r;
	struct spi_buf tx_buf[3];
	struct spi_buf_set tx_bufs = {.buffers = tx_buf, .count = 2U};

	//printk("Transmitting data: %d", tx_len);

	// Send Pre command
	uint8_t pre_cmd[4] = {0x02, 00};
	tx_buf[0].buf = &pre_cmd;
	tx_buf[0].len = 2U;

	// Send command
	uint8_t cmd_buf[2] = {0x2C, 00};
	tx_buf[1].buf = &cmd_buf;
	tx_buf[1].len = 2U;

	/* send data (if any) */
	if (tx_data != NULL)
	{
		tx_buf[2].buf = (void *)tx_data;
		tx_buf[2].len = tx_len;

		tx_bufs.count = 3U;
	}

	r = spi_write_dt(&config->spi, &tx_bufs);
	if (r < 0)
	{
		return r;
	}

	return 0;
}

/*
#define ROW 70
#define COL 70

uint8_t test_buf[ROW * COL * 3];

static void send_test(const struct device *dev)
{

	int r;
	uint8_t spi_data[4];

	const uint16_t x_start = 50;
	const uint16_t y_start = 250;

	const uint16_t w = x_start + ROW;
	const uint16_t h = y_start + COL;

	r= sh8601_set_mem_area(dev, x_start,y_start, w, h);

	uint32_t buf_count = 0;

	for (int i = 0; i < ROW; i++)
	{
		for (int j = 0; j < COL; j++)
		{
			test_buf[buf_count++] = 0x00;
			test_buf[buf_count++] = 0xFF;
			test_buf[buf_count++] = 0xAF;
		}
	}

	printk("Test data: %d", buf_count);

	sh8601_transmit_data(dev, test_buf, sizeof(test_buf));
}
*/

static int sh8601_send_cmd(const struct device *dev, uint8_t cmd)
{
	int r = 0;

	r = sh8601_transmit_cmd(dev, cmd, NULL, 0);
	if (r < 0)
	{
		return r;
	}

	return 0;
}

/**
 * @brief To turn off the sleep mode.
 *
 * @param dev SH8601 device instance.
 * @return 0 on success, errno otherwise.
 */
static int sh8601_exit_sleep(const struct device *dev)
{
	int r;

	// r = sh8601_transmit(dev, SH8601_SLPOUT, NULL, 0);
	if (r < 0)
	{
		return r;
	}

	// k_msleep(SH8601_SLEEP_OUT_TIME);

	return 0;
}

/**
 * @brief To turn off the sleep mode.
 *
 * @param dev SH8601 device instance.
 * @return 0 when succesful, errno otherwise.
 */
static int sh8601_hw_reset(const struct device *dev)
{
	const struct sh8601_config *config = dev->config;

	if (config->reset.port == NULL)
	{
		return -ENODEV;
	}

	gpio_pin_set_dt(&config->reset, 1);
	k_msleep(100);
	gpio_pin_set_dt(&config->reset, 0);
	k_msleep(100);
	gpio_pin_set_dt(&config->reset, 1);
	k_msleep(100);
	return 0;
}

/**
 * @brief To recover from the sleep mode
 */
static int sh8601_display_blanking_off(const struct device *dev)
{
	LOG_DBG("Turning display blanking off");
	return sh8601_send_cmd(dev, SH8601_C_DISPON);
}

/**
 * @brief To enter into DISPLAY OFF mode
 *
 */
static int sh8601_display_blanking_on(const struct device *dev)
{
	LOG_DBG("Turning display blanking on");
	return sh8601_send_cmd(dev, SH8601_C_DISPOFF);
}

/**
 * @brief To set the pixel format
 *
 */
static int sh8601_set_pixel_format(const struct device *dev,
								   const enum display_pixel_format pixel_format)
{
	struct sh8601_data *data = dev->data;
	uint8_t bytes_per_pixel = 3;
	// int r;
	//  uint8_t tx_data;

	if (pixel_format == PIXEL_FORMAT_RGB_565)
	{
		bytes_per_pixel = 2U;
		// tx_data = SH8601_W_COLORSET0; // SH8601_PIXSET_MCU_16_BIT | SH8601_PIXSET_RGB_16_BIT;
	}
	else if (pixel_format == PIXEL_FORMAT_RGB_888)
	{
		bytes_per_pixel = 3U;
		// tx_data = SH8601_W_COLORSET0; // SH8601_PIXSET_MCU_18_BIT | SH8601_PIXSET_RGB_18_BIT;
	}
	else
	{
		LOG_ERR("Unsupported pixel format");
		return -ENOTSUP;
	}

	// r = sh8601_transmit_cmd(dev, SH8601_W_COLOROPTION, &tx_data, 1U);
	// if (r < 0)
	//{
	//	return r;
	//}

	data->pixel_format = pixel_format;
	data->bytes_per_pixel = bytes_per_pixel;

	return 0;
}

/**
 * @brief To set the display orientation.
 *
 * @param dev SH8601 device instance.
 * @param orientation Display orientation
 * @return 0 when succesful, errno otherwise.
 */
static int sh8601_set_orientation(const struct device *dev,
								  const enum display_orientation orientation)
{
	struct sh8601_data *data = dev->data;

	/*int r;
	uint8_t tx_data = SH8601_MADCTL_BGR;

	if (orientation == DISPLAY_ORIENTATION_NORMAL)
	{ // works 0
		tx_data |= 0;
	}
	else if (orientation == DISPLAY_ORIENTATION_ROTATED_90)
	{ // works CW 90
		tx_data |= SH8601_MADCTL_MV | SH8601_MADCTL_MY;
	}
	else if (orientation == DISPLAY_ORIENTATION_ROTATED_180)
	{ // works CW 180
		tx_data |= SH8601_MADCTL_MY | SH8601_MADCTL_MX | SH8601_MADCTL_MH;
	}
	else if (orientation == DISPLAY_ORIENTATION_ROTATED_270)
	{ // works CW 270
		tx_data |= SH8601_MADCTL_MV | SH8601_MADCTL_MX;
	}

	r = sh8601_transmit(dev, SH8601_MADCTL, &tx_data, 1U);
	if (r < 0)
	{
		return r;
	}*/

	data->orientation = orientation;

	return 0;
}

static int sh8601_set_brightness(const struct device *dev,
								 const uint8_t brightness)
{
	uint8_t args[1] = {brightness};

	sh8601_transmit_cmd(dev, SH8601_W_WDBRIGHTNESSVALNOR, args, 1U);

	return 0;
}

static int sh8601_configure(const struct device *dev)
{
	const struct sh8601_config *config = dev->config;

	int r;
	enum display_pixel_format pixel_format;
	enum display_orientation orientation;

	/* pixel format */
	if (config->pixel_format == SH8601_PIXEL_FORMAT_RGB565)
	{
		pixel_format = PIXEL_FORMAT_RGB_565;
	}
	else
	{
		pixel_format = PIXEL_FORMAT_RGB_888;
	}

	r = sh8601_set_pixel_format(dev, pixel_format); // Set pixel format.
	if (r < 0)
	{
		return r;
	}

	/* orientation */
	if (config->rotation == 0U)
	{
		orientation = DISPLAY_ORIENTATION_NORMAL;
	}
	else if (config->rotation == 90U)
	{
		orientation = DISPLAY_ORIENTATION_ROTATED_90;
	}
	else if (config->rotation == 180U)
	{
		orientation = DISPLAY_ORIENTATION_ROTATED_180;
	}
	else
	{
		orientation = DISPLAY_ORIENTATION_ROTATED_270;
	}

	r = sh8601_set_orientation(dev, orientation); // Set display orientation.
	if (r < 0)
	{
		return r;
	}

	if (config->inversion)
	{
		// r = sh8601_transmit(dev, SH8601_INVON, NULL, 0U); // Display inversion mode.
		if (r < 0)
		{
			return r;
		}
	}

	return 0;
}

static int sh8601_init(const struct device *dev)
{
	const struct sh8601_config *config = dev->config;
	struct sh8601_data *data = dev->data;

	int r;

	if (!spi_is_ready_dt(&config->spi))
	{
		LOG_ERR("SPI device is not ready");
		// return -ENODEV;
	}

	if (config->reset.port != NULL)
	{
		if (!device_is_ready(config->reset.port))
		{
			LOG_ERR("Reset GPIO device not ready");
			// return -ENODEV;
		}

		r = gpio_pin_configure_dt(&config->reset, GPIO_OUTPUT_INACTIVE);
		if (r < 0)
		{
			LOG_ERR("Could not configure reset GPIO (%d)", r);
			// return r;
		}
	}

	sh8601_hw_reset(dev);

	k_msleep(SH8601_RST_DELAY);

	sh8601_configure(dev);

	uint8_t args[1] = {0};
	uint8_t args2[2] = {0};

	// Init sequence

	args2[0] = 0x5A;
	args2[1] = 0x5A;
	r = sh8601_transmit_cmd(dev, 0xC0, args2, 2U);
	r = sh8601_transmit_cmd(dev, 0xC1, args2, 2U);

	args[0] = 0x01;
	r = sh8601_transmit_cmd(dev, 0xE4, args, 1U);

	uint8_t args14[14] = {0x01, 0x07, 0x00, 0x64, 0x00, 0xFF, 0x03, 0x04, 0x01, 0x38, 0x1D, 0x61, 0x00, 0x7C};
	r = sh8601_transmit_cmd(dev, 0xBD, args14, 14U);

	r = sh8601_send_cmd(dev, SH8601_C_SLPOUT);
	k_msleep(SH8601_SLPOUT_DELAY);

	uint8_t args4[4] = {0x00, 0x00, 0x01, 0x85};
	r = sh8601_transmit_cmd(dev, SH8601_W_CASET, args4, 4U);
	r = sh8601_transmit_cmd(dev, SH8601_W_PASET, args4, 4U);

	args2[0] = 0x00;
	args2[1] = 0x0A;
	r = sh8601_transmit_cmd(dev, SH8601_W_SETTSL, args2, 2U);

	args[0] = 0x00;
	r = sh8601_transmit_cmd(dev, SH8601_WC_TEARON, args, 1U);

	args[0] = 0x20;
	r = sh8601_transmit_cmd(dev, SH8601_W_WCTRLD1, args, 1U);

	args[0] = 0x75;
	r = sh8601_transmit_cmd(dev, SH8601_W_PIXFMT, args, 1U);

	args2[0] = 0xFF;
	args2[1] = 0x03;
	r = sh8601_transmit_cmd(dev, SH8601_W_WDBRIGHTNESSVALNOR, args2, 2U);

	args[0] = 0x08;
	r = sh8601_transmit_cmd(dev, SH8601_W_SPIMODECTL, args, 1U);

	k_msleep(25);
	r = sh8601_send_cmd(dev, SH8601_C_DISPON);

	data->device_in_sleep = false;

	LOG_DBG("Display SH8601 init!");

	k_msleep(200);

	return 0;
}

int sh8601_reinit(const struct device *dev)
{
	sh8601_init(dev);
}

static int sh8601_set_mem_area(const struct device *dev, const uint16_t x,
							   const uint16_t y, const uint16_t w,
							   const uint16_t h)
{
	int r;
	uint8_t spi_data[4];

	spi_data[0] = x >> 8;	// sys_cpu_to_be16(x);
	spi_data[1] = x & 0xff; // sys_cpu_to_be16(x + w - 1U);
	spi_data[2] = (x + w - 1) >> 8;
	spi_data[3] = (x + w - 1) & 0xff;

	r = sh8601_transmit_cmd(dev, SH8601_W_CASET, spi_data, 4U);
	if (r < 0)
	{
		return r;
	}

	spi_data[0] = y >> 8; // sys_cpu_to_be16(y);
	spi_data[1] = y & 0xff;
	spi_data[2] = (h + y - 1) >> 8;
	spi_data[3] = (h + y - 1) & 0xff;

	r = sh8601_transmit_cmd(dev, SH8601_W_PASET, spi_data, 4U);
	if (r < 0)
	{
		return r;
	}

	return 0;
}

static int sh8601_write(const struct device *dev, const uint16_t x,
						const uint16_t y,
						const struct display_buffer_descriptor *desc,
						const void *buf)
{
	//LOG_DBG("Writing %dx%d (w,h) @ %dx%d (x,y)", desc->width, desc->height,
	//		x, y);
	int r = sh8601_set_mem_area(dev, x, y, desc->width, desc->height);
	if (r < 0)
	{
		return r;
	}
	sh8601_transmit_data(dev, buf, desc->buf_size);
	if (r < 0)
	{
		return r;
	}

	return 0;
}

static int sh8601_read(const struct device *dev, const uint16_t x,
					   const uint16_t y,
					   const struct display_buffer_descriptor *desc, void *buf)
{
	LOG_ERR("Reading not supported");
	return -ENOTSUP;
}

static void *sh8601_get_framebuffer(const struct device *dev)
{
	LOG_ERR("Direct framebuffer access not supported");
	return NULL;
}

/**
 * @brief To set contrast of the display.
 *
 * @param dev  SH8601 device instance.
 * @param contrast  Contrast value in percentage.
 * @return Not supported
 */
static int sh8601_set_contrast(const struct device *dev,
							   const uint8_t contrast)
{
	LOG_ERR("Set contrast not supported");
	return -ENOTSUP;
}

static void sh8601_get_capabilities(const struct device *dev,
									struct display_capabilities *capabilities)
{
	struct sh8601_data *data = dev->data;
	const struct sh8601_config *config = dev->config;

	memset(capabilities, 0, sizeof(struct display_capabilities));

	capabilities->supported_pixel_formats =
		PIXEL_FORMAT_RGB_565 | PIXEL_FORMAT_RGB_888;
	capabilities->current_pixel_format = data->pixel_format;

	if (data->orientation == DISPLAY_ORIENTATION_NORMAL ||
		data->orientation == DISPLAY_ORIENTATION_ROTATED_180)
	{
		capabilities->x_resolution = config->x_resolution;
		capabilities->y_resolution = config->y_resolution;
	}
	else
	{
		capabilities->x_resolution = config->y_resolution;
		capabilities->y_resolution = config->x_resolution;
	}

	capabilities->current_orientation = data->orientation;
}

/*Device driver API*/
static const struct display_driver_api sh8601_api = {
	.blanking_on = sh8601_display_blanking_on,
	.blanking_off = sh8601_display_blanking_off,
	.write = sh8601_write,
	.read = sh8601_read,
	.get_framebuffer = sh8601_get_framebuffer,
	.set_brightness = sh8601_set_brightness,
	.set_contrast = sh8601_set_contrast,
	.get_capabilities = sh8601_get_capabilities,
	.set_pixel_format = sh8601_set_pixel_format,
	.set_orientation = sh8601_set_orientation,
};

#ifdef CONFIG_PM_DEVICE

static int sh8601_pm_action(const struct device *dev,
							enum pm_device_action action)
{
	switch (action)
	{
	case PM_DEVICE_ACTION_SUSPEND:
		//printk("Suspend device");
		sh8601_send_cmd(dev, SH8601_C_SLPIN);
		k_msleep(SH8601_SLPIN_DELAY);
		break;
	case PM_DEVICE_ACTION_RESUME:
		//printk("Resume device");
		sh8601_send_cmd(dev, SH8601_C_SLPOUT);
		k_msleep(SH8601_SLPOUT_DELAY);
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

#endif /* CONFIG_PM_DEVICE */

#define SH8601_INIT(inst)                                                           \
	static const struct sh8601_config sh8601_config_##inst = {                      \
		.spi = SPI_DT_SPEC_INST_GET(inst, SPI_OP_MODE_MASTER | SPI_WORD_SET(8), 1U), \
		.reset = GPIO_DT_SPEC_INST_GET_OR(inst, reset_gpios, {0}),                  \
		.pixel_format = DT_INST_PROP(inst, pixel_format),                           \
		.rotation = DT_INST_PROP(inst, rotation),                                   \
		.x_resolution = DT_INST_PROP(inst, width),                                  \
		.y_resolution = DT_INST_PROP(inst, height),                                 \
		.inversion = DT_INST_PROP(inst, display_inversion),                         \
	};                                                                              \
	static struct sh8601_data sh8601_data_##inst;                                   \
                                                                                    \
	PM_DEVICE_DT_INST_DEFINE(inst, sh8601_pm_action);                               \
                                                                                    \
	DEVICE_DT_INST_DEFINE(inst, &sh8601_init,                                       \
						  PM_DEVICE_DT_INST_GET(inst), &sh8601_data_##inst,         \
						  &sh8601_config_##inst, POST_KERNEL,                       \
						  CONFIG_DISPLAY_INIT_PRIORITY, &sh8601_api);

DT_INST_FOREACH_STATUS_OKAY(SH8601_INIT)
