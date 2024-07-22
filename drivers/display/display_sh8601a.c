/**
 * Copyright 2024 Protocentral Electronics
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SH8601 AMOLED display driver.
 */

#include "display_sh8601a.h"

#include <zephyr/drivers/display.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(display_sh8601a, CONFIG_DISPLAY_LOG_LEVEL);

#define SH8601A_PIXEL_FORMAT_RGB565 0U
#define SH8601A_PIXEL_FORMAT_RGB888 1U

/*Display data struct*/
struct sh8601a_data
{
	uint8_t bytes_per_pixel;
	enum display_pixel_format pixel_format;
	enum display_orientation orientation;
};

/**
 * @brief Initialize SH8601A registers with DT values.
 *
 * @param dev SH8601A device instance.
 * @return 0 on success, errno otherwise.
 */
int sh8601a_regs_init(const struct device *dev)
{

	const struct sh8601a_config *config = dev->config;

	return 0;
}

/**
 * @brief Transmit values to the display driver
 *
 * @param dev SH8601A device instance.
 * @param cmd command associated with the register.
 * @param tx_data Data to be sent.
 * @param tx_len Length of buffer to be sent.
 * @return 0 on success, errno otherwise.
 */
int sh8601a_transmit(const struct device *dev, uint8_t cmd, const void *tx_data,
					 size_t tx_len)
{
	const struct sh8601a_config *config = dev->config;

	int r;
	struct spi_buf tx_buf;
	struct spi_buf_set tx_bufs = {.buffers = &tx_buf, .count = 1U};

	/* send command */
	tx_buf.buf = &cmd;
	tx_buf.len = 1U;

	//gpio_pin_set_dt(&config->cmd_data, SH8601A_CMD);
	//r = spi_write_dt(&config->spi, &tx_bufs);
	if (r < 0)
	{
		return r;
	}

	/* send data (if any) */
	if (tx_data != NULL)
	{
		tx_buf.buf = (void *)tx_data;
		tx_buf.len = tx_len;

		//gpio_pin_set_dt(&config->cmd_data, SH8601A_DATA);
		//r = spi_write_dt(&config->spi, &tx_bufs);
		if (r < 0)
		{
			return r;
		}
	}

	return 0;
}

/**
 * @brief To turn off the sleep mode.
 *
 * @param dev SH8601A device instance.
 * @return 0 on success, errno otherwise.
 */
static int sh8601a_exit_sleep(const struct device *dev)
{
	int r;

	//r = sh8601a_transmit(dev, SH8601A_SLPOUT, NULL, 0);
	if (r < 0)
	{
		return r;
	}

	//k_msleep(SH8601A_SLEEP_OUT_TIME);

	return 0;
}

/**
 * @brief To turn off the sleep mode.
 *
 * @param dev SH8601A device instance.
 * @return 0 when succesful, errno otherwise.
 */
static int sh8601a_hw_reset(const struct device *dev)
{
	const struct sh8601a_config *config = dev->config;

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
static int sh8601a_display_blanking_off(const struct device *dev)
{
	LOG_DBG("Turning display blanking off");
	//return sh8601a_transmit(dev, SH8601A_DISPON, NULL, 0);
	return 0;
}

/**
 * @brief To enter into DISPLAY OFF mode
 *
 */
static int sh8601a_display_blanking_on(const struct device *dev)
{
	LOG_DBG("Turning display blanking on");
	//return sh8601a_transmit(dev, SH8601A_DISPOFF, NULL, 0);
	return 0;
}

/**
 * @brief To set the pixel format
 *
 */
static int sh8601a_set_pixel_format(const struct device *dev,
									const enum display_pixel_format pixel_format)
{
	struct sh8601a_data *data = dev->data;
	uint8_t bytes_per_pixel;
	/*int r;
	uint8_t tx_data;
	

	if (pixel_format == PIXEL_FORMAT_RGB_565)
	{
		bytes_per_pixel = 2U;
		tx_data = SH8601A_PIXSET_MCU_16_BIT | SH8601A_PIXSET_RGB_16_BIT;
	}
	else if (pixel_format == PIXEL_FORMAT_RGB_888)
	{
		bytes_per_pixel = 3U;
		tx_data = SH8601A_PIXSET_MCU_18_BIT | SH8601A_PIXSET_RGB_18_BIT;
	}
	else
	{
		LOG_ERR("Unsupported pixel format");
		return -ENOTSUP;
	}

	r = sh8601a_transmit(dev, SH8601A_PIXFMT, &tx_data, 1U);
	if (r < 0)
	{
		return r;
	}
	*/

	data->pixel_format = pixel_format;
	data->bytes_per_pixel = bytes_per_pixel;

	return 0;
}

/**
 * @brief To set the display orientation.
 *
 * @param dev SH8601A device instance.
 * @param orientation Display orientation
 * @return 0 when succesful, errno otherwise.
 */
static int sh8601a_set_orientation(const struct device *dev,
								   const enum display_orientation orientation)
{
	struct sh8601a_data *data = dev->data;

	/*int r;
	uint8_t tx_data = SH8601A_MADCTL_BGR;

	if (orientation == DISPLAY_ORIENTATION_NORMAL)
	{ // works 0
		tx_data |= 0;
	}
	else if (orientation == DISPLAY_ORIENTATION_ROTATED_90)
	{ // works CW 90
		tx_data |= SH8601A_MADCTL_MV | SH8601A_MADCTL_MY;
	}
	else if (orientation == DISPLAY_ORIENTATION_ROTATED_180)
	{ // works CW 180
		tx_data |= SH8601A_MADCTL_MY | SH8601A_MADCTL_MX | SH8601A_MADCTL_MH;
	}
	else if (orientation == DISPLAY_ORIENTATION_ROTATED_270)
	{ // works CW 270
		tx_data |= SH8601A_MADCTL_MV | SH8601A_MADCTL_MX;
	}

	r = sh8601a_transmit(dev, SH8601A_MADCTL, &tx_data, 1U);
	if (r < 0)
	{
		return r;
	}*/

	data->orientation = orientation;

	return 0;
}

/**
 * @brief To do overall display device configuration.
 *
 * @param dev SH8601A device instance.
 * @return 0 when succesful, errno otherwise.
 */
static int sh8601a_configure(const struct device *dev)
{
	const struct sh8601a_config *config = dev->config;

	int r;
	enum display_pixel_format pixel_format;
	enum display_orientation orientation;

	/* pixel format */
	if (config->pixel_format == SH8601A_PIXEL_FORMAT_RGB565)
	{
		pixel_format = PIXEL_FORMAT_RGB_565;
	}
	else
	{
		pixel_format = PIXEL_FORMAT_RGB_888;
	}

	r = sh8601a_set_pixel_format(dev, pixel_format); // Set pixel format.
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

	r = sh8601a_set_orientation(dev, orientation); // Set display orientation.
	if (r < 0)
	{
		return r;
	}

	if (config->inversion)
	{
		//r = sh8601a_transmit(dev, SH8601A_INVON, NULL, 0U); // Display inversion mode.
		if (r < 0)
		{
			return r;
		}
	}

	//r = config->regs_init_fn(dev); // Set all the required registers.
	if (r < 0)
	{
		return r;
	}

	return 0;
}

/**
 * @brief To set the backlight brightness of the display.
 *
 * @param dev SH8601A device instance.
 * @param brightness percentage of brightness of the backlight.
 * @return 0 when succesful, errno otherwise.
 */
static int sh8601a_set_brightness(const struct device *dev,
								  const uint8_t brightness)
{

	const struct sh8601a_config *config = dev->config;
	uint32_t step = config->backlight.period / 100;

	return 0;
}

/**
 * @brief To initialize the peripherals associated with the display.
 *
 * @param dev SH8601A device instance.
 * @return 0 when succesful, errno otherwise.
 */
static int sh8601a_init(const struct device *dev)
{
	const struct sh8601a_config *config = dev->config;

	int r;

	if (!spi_is_ready_dt(&config->spi))
	{
		LOG_ERR("SPI device is not ready");
		return -ENODEV;
	}

	if (config->reset.port != NULL)
	{
		if (!device_is_ready(config->reset.port))
		{
			LOG_ERR("Reset GPIO device not ready");
			return -ENODEV;
		}

		r = gpio_pin_configure_dt(&config->reset, GPIO_OUTPUT_INACTIVE);
		if (r < 0)
		{
			LOG_ERR("Could not configure reset GPIO (%d)", r);
			return r;
		}
	}

	sh8601a_hw_reset(dev);

	k_msleep(SH8601_RST_DELAY);

	//TODO: Add Init sequence

	return 0;
}

/**
 * @brief To set the memory area to transmit on the display
 *
 * @param dev SH8601A device instance.
 * @param x	start point of the window.
 * @param y	end point of the window.
 * @param w	width point of the window.
 * @param h	height point of the window.
 * @return 0 when succesful, errno otherwise.
 */
static int sh8601a_set_mem_area(const struct device *dev, const uint16_t x,
								const uint16_t y, const uint16_t w,
								const uint16_t h)
{
	int r;
	uint16_t spi_data[2];

	spi_data[0] = sys_cpu_to_be16(x);
	spi_data[1] = sys_cpu_to_be16(x + w - 1U);
	//r = sh8601a_transmit(dev, SH8601A_CASET, &spi_data[0], 4U);
	if (r < 0)
	{
		return r;
	}

	spi_data[0] = sys_cpu_to_be16(y);
	spi_data[1] = sys_cpu_to_be16(y + h - 1U);
	//r = sh8601a_transmit(dev, SH8601A_PASET, &spi_data[0], 4U);
	if (r < 0)
	{
		return r;
	}

	return 0;
}

/**
 * @brief To handle writing to the display(setting memory area and transmit).
 *
 * @param dev  SH8601A device instance.
 * @param x	start point of the window.
 * @param y	end point of the window.
 * @param desc pointer to the buffer descriptor.
 * @param buf pointer to the buffer to be sent
 * @return 0 when succesful, errno otherwise.
 */
static int sh8601a_write(const struct device *dev, const uint16_t x,
						 const uint16_t y,
						 const struct display_buffer_descriptor *desc,
						 const void *buf)
{
	const struct sh8601a_config *config = dev->config;
	struct sh8601a_data *data = dev->data;

	int r;
	const uint8_t *write_data_start = (const uint8_t *)buf;
	struct spi_buf tx_buf;
	struct spi_buf_set tx_bufs;
	uint16_t write_cnt;
	uint16_t nbr_of_writes;
	uint16_t write_h;

	__ASSERT(desc->width <= desc->pitch, "Pitch is smaller than width");
	__ASSERT((desc->pitch * data->bytes_per_pixel * desc->height) <=
				 desc->buf_size,
			 "Input buffer to small");

	LOG_DBG("Writing %dx%d (w,h) @ %dx%d (x,y)", desc->width, desc->height,
			x, y);
	r = sh8601a_set_mem_area(dev, x, y, desc->width, desc->height);
	if (r < 0)
	{
		return r;
	}

	if (desc->pitch > desc->width)
	{
		write_h = 1U;
		nbr_of_writes = desc->height;
	}
	else
	{
		write_h = desc->height;
		nbr_of_writes = 1U;
	}

	//r = sh8601a_transmit(dev, SH8601A_RAMWR, write_data_start,
	//					 desc->width * data->bytes_per_pixel * write_h);
	if (r < 0)
	{
		return r;
	}

	tx_bufs.buffers = &tx_buf;
	tx_bufs.count = 1;

	write_data_start += desc->pitch * data->bytes_per_pixel;
	for (write_cnt = 1U; write_cnt < nbr_of_writes; ++write_cnt)
	{
		tx_buf.buf = (void *)write_data_start;
		tx_buf.len = desc->width * data->bytes_per_pixel * write_h;

		r = spi_write_dt(&config->spi, &tx_bufs);
		if (r < 0)
		{
			return r;
		}

		write_data_start += desc->pitch * data->bytes_per_pixel;
	}

	return 0;
}

/**
 * @brief To handle reading from the display.
 *
 * @param dev  SH8601A device instance.
 * @param x	start point of the window.
 * @param y	end point of the window.
 * @param desc pointer to the buffer descriptor.
 * @param buf pointer to the buffer to be read
 * @return Not supported
 */
static int sh8601a_read(const struct device *dev, const uint16_t x,
						const uint16_t y,
						const struct display_buffer_descriptor *desc, void *buf)
{
	LOG_ERR("Reading not supported");
	return -ENOTSUP;
}

/**
 * @brief To get framebuffer from the display.
 *
 * @param dev  SH8601A device instance.
 * @return Not supported
 */
static void *sh8601a_get_framebuffer(const struct device *dev)
{
	LOG_ERR("Direct framebuffer access not supported");
	return NULL;
}

/**
 * @brief To set contrast of the display.
 *
 * @param dev  SH8601A device instance.
 * @param contrast  Contrast value in percentage.
 * @return Not supported
 */
static int sh8601a_set_contrast(const struct device *dev,
								const uint8_t contrast)
{
	LOG_ERR("Set contrast not supported");
	return -ENOTSUP;
}

/**
 * @brief To set contrast of the display.
 *
 * @param dev  SH8601A device instance.
 * @param capabilities  pointer to the capabalities struct.
 * @return None.
 */
static void sh8601a_get_capabilities(const struct device *dev,
									 struct display_capabilities *capabilities)
{
	struct sh8601a_data *data = dev->data;
	const struct sh8601a_config *config = dev->config;

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
static const struct display_driver_api sh8601a_api = {
	.blanking_on = sh8601a_display_blanking_on,
	.blanking_off = sh8601a_display_blanking_off,
	.write = sh8601a_write,
	.read = sh8601a_read,
	.get_framebuffer = sh8601a_get_framebuffer,
	.set_brightness = sh8601a_set_brightness,
	.set_contrast = sh8601a_set_contrast,
	.get_capabilities = sh8601a_get_capabilities,
	.set_pixel_format = sh8601a_set_pixel_format,
	.set_orientation = sh8601a_set_orientation,
};

#define INST_DT_SH8601A(n) DT_INST(n, shengetech_sh8601a)

#define SH8601A_INIT(n, t)                                           \
	  static const struct sh8601a_config sh8601a_config_##n = {      \
		.spi = SPI_DT_SPEC_GET(INST_DT_SH8601A(n),                   \
							   SPI_OP_MODE_MASTER | SPI_WORD_SET(8), \
							   0),                                   \
		.cmd_data = GPIO_DT_SPEC_GET(INST_DT_SH8601A(n),             \
									 cmd_data_gpios),                \
		.reset = GPIO_DT_SPEC_GET_OR(INST_DT_SH8601A(n),             \
									 reset_gpios, {0}),              \
		.pixel_format = DT_PROP(INST_DT_SH8601A(n), pixel_format),   \
		.rotation = DT_PROP(INST_DT_SH8601A(n), rotation),           \
		.x_resolution = DT_PROP(INST_DT_SH8601A(n), width),          \
		.y_resolution = DT_PROP(INST_DT_SH8601A(n), height),         \
		.inversion = DT_PROP(INST_DT_SH8601A(n), display_inversion), \
	};                                                               \
	static struct sh8601a_data sh8601a_data_##n;                     \
	DEVICE_DT_DEFINE(INST_DT_SH8601A(n), sh8601a_init,               \
					 NULL, &sh8601a_data_##n,                        \
					 &sh8601a_config_##n, POST_KERNEL,               \
					 CONFIG_DISPLAY_INIT_PRIORITY, &sh8601a_api)

#define DT_INST_FOREACH_SH8601A_STATUS_OKAY \
	LISTIFY(DT_NUM_INST_STATUS_OKAY(shengetech_sh8601a), SH8601A_INIT, (;))

#ifdef CONFIG_SH8601A
DT_INST_FOREACH_SH8601A_STATUS_OKAY;
#endif
