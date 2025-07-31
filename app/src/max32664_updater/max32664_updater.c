/*
 * HealthyPi Move
 * 
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Author: Ashwin Whitchurch, Protocentral Electronics
 * Contact: ashwin@protocentral.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h>

#include "max32664_updater.h"

LOG_MODULE_REGISTER(max32664_updater, LOG_LEVEL_DBG); // CONFIG_MAX32664_UPDATER_LOG_LEVEL);

#define MAX32664C_DEFAULT_CMD_DELAY 10

#define MAX32664C_FW_UPDATE_WRITE_SIZE 8208 // Page size 8192 + 16 bytes for CRC
#define MAX32664C_FW_UPDATE_START_ADDR 0x4C

#define MAX32664C_FW_BIN_INCLUDE 0
#define MAX32664C_WR_SIM_ONLY 0

// File paths for firmware binaries
#define MAX32664C_FW_PATH "/lfs/max32664c_msbl.bin"
#define MAX32664D_FW_PATH "/lfs/max32664d_40_6_0.bin"

// Small shared buffer for temporary operations
// SAFETY: This buffer is only used in single-threaded context during firmware updates
#define SHARED_BUFFER_SIZE 1026
static uint8_t shared_rw_buffer[SHARED_BUFFER_SIZE]; // Reusable buffer for I2C operations and file reading

static int m_read_op_mode(const struct device *dev);

struct max32664_config
{
	struct i2c_dt_spec i2c;
	struct gpio_dt_spec reset_gpio;
	struct gpio_dt_spec mfio_gpio;
};

static int m_read_bl_ver(const struct device *dev)
{
	const struct max32664_config *config = dev->config;
	uint8_t rd_buf[4] = {0x00, 0x00, 0x00, 0x00};
	uint8_t wr_buf[2] = {0x81, 0x00};

	gpio_pin_set_dt(&config->mfio_gpio, 0);
	k_sleep(K_USEC(300));
	i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
	k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));

	i2c_read_dt(&config->i2c, rd_buf, sizeof(rd_buf));
	k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));
	gpio_pin_set_dt(&config->mfio_gpio, 1);

	LOG_DBG("BL Version = %d.%d.%d", rd_buf[1], rd_buf[2], rd_buf[3]);

	return 0;
}

static int m_wr_cmd_enter_bl(const struct device *dev)
{
	const struct max32664_config *config = dev->config;
	uint8_t wr_buf[3] = {0x01, 0x00, 0x08};
	uint8_t rd_buf[1] = {0x00};

	i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
	k_sleep(K_MSEC(2));
	i2c_read_dt(&config->i2c, rd_buf, sizeof(rd_buf));

	LOG_DBG("CMD Enter BL RSP: %x", rd_buf[0]);

	return 0;
}

static int m_read_bl_page_size(const struct device *dev, uint16_t *bl_page_size)
{
	const struct max32664_config *config = dev->config;
	uint8_t rd_buf[3] = {0x00, 0x00, 0x00};
	uint8_t wr_buf[2] = {0x81, 0x01};

	k_sleep(K_USEC(300));
	i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
	k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));

	i2c_read_dt(&config->i2c, rd_buf, sizeof(rd_buf));
	k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));

	LOG_DBG("BL PS Read: %x %x", rd_buf[0], rd_buf[1]);
	*bl_page_size = (uint16_t)(rd_buf[1] << 8) | rd_buf[2];

	return 0;
}

static int m_write_set_num_pages(const struct device *dev, uint8_t num_pages)
{
	const struct max32664_config *config = dev->config;
	uint8_t wr_buf[4] = {0x80, 0x02, 0x00, 0x00};
	uint8_t rd_buf[1] = {0x00};

	wr_buf[3] = num_pages;

	i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
	k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));
	i2c_read_dt(&config->i2c, rd_buf, sizeof(rd_buf));

	LOG_DBG("Write Num Pages RSP: %x", rd_buf[0]);

	return rd_buf[0];
}

static int m_write_init_vector(const struct device *dev, uint8_t *init_vector)
{
	const struct max32664_config *config = dev->config;
	uint8_t wr_buf[13];
	uint8_t rd_buf[1] = {0x00};

	wr_buf[0] = 0x80;
	wr_buf[1] = 0x00;

	memcpy(&wr_buf[2], init_vector, 11);

	k_sleep(K_USEC(300));
	i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
	k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));

	i2c_read_dt(&config->i2c, rd_buf, sizeof(rd_buf));
	k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));

	LOG_DBG("Write Init Vec RSP: %x", rd_buf[0]);

	return rd_buf[0];
}

static int m_write_auth_vector(const struct device *dev, uint8_t *auth_vector)
{
	const struct max32664_config *config = dev->config;
	uint8_t wr_buf[18];
	uint8_t rd_buf[1] = {0x00};

	wr_buf[0] = 0x80;
	wr_buf[1] = 0x01;

	memcpy(&wr_buf[2], auth_vector, 16);

	i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
	k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));
	i2c_read_dt(&config->i2c, rd_buf, sizeof(rd_buf));

	LOG_DBG("Write Auth Vec : RSP: %x", rd_buf[0]);

	return rd_buf[0];
}

// volatile uint8_t fw_data_wr_buf[MAX32664C_FW_UPDATE_WRITE_SIZE + 2];

/*
static int m_fw_write_page_single(const struct device *dev, uint8_t *msbl_data, uint32_t msbl_page_offset)
{
	const struct max32664_config *config = dev->config;

	uint8_t rd_buf[1] = {0x00};
	uint8_t cmd_wr_buf[2] = {0x80, 0x04};

	memcpy(fw_data_wr_buf, cmd_wr_buf, 2);
	memcpy((fw_data_wr_buf + 2), &msbl_data[msbl_page_offset], (MAX32664C_FW_UPDATE_WRITE_SIZE));

	gpio_pin_set_dt(&config->mfio_gpio, 0);
	k_sleep(K_USEC(300));
	int ret = i2c_write_dt(&config->i2c, fw_data_wr_buf, sizeof(fw_data_wr_buf));
	printk("Transfer Ret: %d\n", ret);
	k_sleep(K_MSEC(700));

	i2c_read_dt(&config->i2c, rd_buf, sizeof(rd_buf));
	printk("Write Page RSP: %x\n", rd_buf[0]);

	gpio_pin_set_dt(&config->mfio_gpio, 0);

	return 0;
}
*/

static int m_fw_write_page(const struct device *dev, struct fs_file_t *file, uint32_t msbl_page_offset)
{
	const struct max32664_config *config = dev->config;

	uint8_t rd_buf[1] = {0x00};
	uint8_t cmd_wr_buf[2] = {0x80, 0x04};

	int msg_len = 1026;

	LOG_DBG("Num Msgs: %d", ((MAX32664C_FW_UPDATE_WRITE_SIZE) / msg_len));

	// Seek to the correct position in the file
	fs_seek(file, msbl_page_offset, FS_SEEK_SET);

	// Send the command first
	int ret = i2c_write_dt(&config->i2c, cmd_wr_buf, sizeof(cmd_wr_buf));
	if (ret < 0)
	{
		LOG_ERR("Failed to send write page command, error: %d", ret);
		return ret;
	}

	// Stream the firmware data directly from file to I2C in chunks
	for (int i = 0; i < 8; i++)
	{
		// Read chunk directly into shared buffer
		ssize_t bytes_read = fs_read(file, shared_rw_buffer, msg_len);
		if (bytes_read != msg_len)
		{
			LOG_ERR("Failed to read firmware chunk %d, read %d bytes", i, bytes_read);
			return -EIO;
		}

		// Send chunk directly via I2C
		ret = i2c_write_dt(&config->i2c, shared_rw_buffer, msg_len);
		if (ret < 0)
		{
			LOG_ERR("Failed to send firmware chunk %d, error: %d", i, ret);
			return ret;
		}
	}

	k_sleep(K_MSEC(800));

	i2c_read_dt(&config->i2c, rd_buf, sizeof(rd_buf));
	k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));

	LOG_DBG("Write Page RSP: %x", rd_buf[0]);

	return 0;
}

static int m_erase_app(const struct device *dev)
{
	const struct max32664_config *config = dev->config;
	uint8_t wr_buf[2] = {0x80, 0x03};
	uint8_t rd_buf[1] = {0x00};

	// gpio_pin_set_dt(&config->mfio_gpio, 0);
	// k_sleep(K_USEC(300));
	i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
	k_sleep(K_MSEC(3500));

	i2c_read_dt(&config->i2c, rd_buf, sizeof(rd_buf));
	k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));
	//	gpio_pin_set_dt(&config->mfio_gpio, 1);

	LOG_DBG("Erase App : RSP: %x", rd_buf[0]);

	return rd_buf[0];
}

static int m_read_mcu_id(const struct device *dev)
{
	const struct max32664_config *config = dev->config;
	uint8_t rd_buf[2] = {0x00, 0x00};
	uint8_t wr_buf[2] = {0xFF, 0x00};

	k_sleep(K_USEC(300));
	i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
	k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));

	i2c_read_dt(&config->i2c, rd_buf, sizeof(rd_buf));
	k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));

	LOG_DBG("MCU ID: %x %x", rd_buf[0], rd_buf[1]);

	return 0;
}

static int m_get_ver(const struct device *dev, uint8_t *ver_buf)
{
	const struct max32664_config *config = dev->config;

	uint8_t wr_buf[2] = {0xFF, 0x03};

	k_sleep(K_USEC(300));
	i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
	k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));

	i2c_read_dt(&config->i2c, ver_buf, 4);
	k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));

	// LOG_INF("Version (decimal) = %d.%d.%d\n", ver_buf[1], ver_buf[2], ver_buf[3]);

	if (ver_buf[1] == 0x00 && ver_buf[2] == 0x00 && ver_buf[3] == 0x00)
	{
		return -ENODEV;
	}

	return 0;
}

static int max32664_do_enter_app(const struct device *dev)
{
	const struct max32664_config *config = dev->config;

	LOG_DBG("Entering app mode");

	gpio_pin_configure_dt(&config->mfio_gpio, GPIO_OUTPUT);

	// Enter APPLICATION mode
	gpio_pin_set_dt(&config->mfio_gpio, 1);
	k_sleep(K_MSEC(10));

	gpio_pin_set_dt(&config->reset_gpio, 0);
	k_sleep(K_MSEC(10));

	gpio_pin_set_dt(&config->reset_gpio, 1);
	k_sleep(K_MSEC(1000));
	// End of APPLICATION mode

	gpio_pin_configure_dt(&config->mfio_gpio, GPIO_INPUT);

	m_read_op_mode(dev);

	uint8_t ver_buf[4] = {0};
	if (m_get_ver(dev, ver_buf) == 0)
	{
		LOG_INF("Hub version: %d.%d.%d", ver_buf[1], ver_buf[2], ver_buf[3]);
	}
	else
	{
		// LOG_INF("MAX32664D not Found");
		return -ENODEV;
	}

	// max32664_read_hub_status(dev);
	// k_sleep(K_MSEC(200));
	// max32664_read_hub_status(dev);

	return 0;
}

static void (*progress_callback)(int progress, int status) = NULL;

void max32664_set_progress_callback(void (*callback)(int progress, int status))
{
	progress_callback = callback;
}

static void update_progress(int progress, int status)
{
	if (progress_callback)
	{
		progress_callback(progress, status);
	}
}

static int max32664_load_fw(const struct device *dev, const char *fw_file_path, bool is_sim)
{
	uint8_t msbl_num_pages = 0;
	struct fs_file_t file;

	LOG_DBG("Loading MSBL from file: %s", fw_file_path);

	// Open the firmware file
	int ret = fs_open(&file, fw_file_path, FS_O_READ);
	if (ret < 0)
	{
		LOG_ERR("Failed to open firmware file: %s, error: %d", fw_file_path, ret);
		return ret;
	}

	// Read header to get firmware information - reuse shared buffer for header
	// Only need first ~80 bytes for header data
	ret = fs_read(&file, shared_rw_buffer, 128);
	if (ret < 0)
	{
		LOG_ERR("Failed to read firmware file header, error: %d", ret);
		fs_close(&file);
		return ret;
	}

	msbl_num_pages = shared_rw_buffer[0x44];
	LOG_DBG("MSBL Load: Pages: %d (%x)", msbl_num_pages, msbl_num_pages);

	m_read_mcu_id(dev);

	m_write_set_num_pages(dev, msbl_num_pages);

	// Use local variables for vectors to save RAM
	uint8_t init_vector[11];
	uint8_t auth_vector[16];

	memcpy(init_vector, &shared_rw_buffer[0x28], 11);
	m_write_init_vector(dev, init_vector);
	LOG_DBG("MSBL Init Vector: %x %x %x %x %x %x %x %x %x %x %x",
			init_vector[0], init_vector[1], init_vector[2],
			init_vector[3], init_vector[4], init_vector[5],
			init_vector[6], init_vector[7], init_vector[8],
			init_vector[9], init_vector[10]);

	memcpy(auth_vector, &shared_rw_buffer[0x34], 16);
	m_write_auth_vector(dev, auth_vector);

	m_erase_app(dev);

	int _progress_counter = 0;
	int _progress_step = (100 / msbl_num_pages);

	update_progress(_progress_counter, MAX32664_UPDATER_STATUS_IN_PROGRESS);

	// Write MSBL
	if (is_sim == false)
	{
		for (int i = 0; i < msbl_num_pages; i++)
		{
			LOG_DBG("Writing Page: %d of %d", (i + 1), msbl_num_pages);

			uint32_t msbl_page_offset = (MAX32664C_FW_UPDATE_START_ADDR + (i * MAX32664C_FW_UPDATE_WRITE_SIZE));
			LOG_DBG("MSBL Page Offset: %d (%x)", msbl_page_offset, msbl_page_offset);

			ret = m_fw_write_page(dev, &file, msbl_page_offset);
			if (ret < 0)
			{
				LOG_ERR("Failed to write firmware page %d, error: %d", i, ret);
				fs_close(&file);
				update_progress(_progress_counter, MAX32664_UPDATER_STATUS_FAILED);
				return ret;
			}

			_progress_counter += _progress_step;
			update_progress(_progress_counter, MAX32664_UPDATER_STATUS_IN_PROGRESS);
		}

		fs_close(&file);
		max32664_do_enter_app(dev);
		update_progress(100, MAX32664_UPDATER_STATUS_SUCCESS);
	}
	else
	{
		fs_close(&file);
	}

	LOG_DBG("End Load MSBL");
	return 0;
}

static int m_read_op_mode(const struct device *dev)
{
	const struct max32664_config *config = dev->config;
	uint8_t rd_buf[2] = {0x00, 0x00};
	uint8_t wr_buf[2] = {0x02, 0x00};

	k_sleep(K_USEC(300));
	i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
	k_sleep(K_MSEC(45));
	gpio_pin_set_dt(&config->mfio_gpio, 0);
	k_sleep(K_USEC(300));
	i2c_read_dt(&config->i2c, rd_buf, sizeof(rd_buf));
	k_sleep(K_MSEC(45));
	gpio_pin_set_dt(&config->mfio_gpio, 1);

	// LOG_INF("Op mode = %x\n", rd_buf[1]);

	return rd_buf[1];
}

void max32664_updater_start(const struct device *dev, enum max32664_updater_device_type type)
{
	const struct max32664_config *config = dev->config;

	LOG_DBG("Entering Bootloader mode");

	gpio_pin_configure_dt(&config->mfio_gpio, GPIO_OUTPUT);

	gpio_pin_set_dt(&config->mfio_gpio, 0);
	k_sleep(K_MSEC(10));

	gpio_pin_set_dt(&config->reset_gpio, 0);
	k_sleep(K_MSEC(10));

	gpio_pin_set_dt(&config->reset_gpio, 1);
	k_sleep(K_MSEC(1000));

	m_wr_cmd_enter_bl(dev);
	m_read_op_mode(dev);
	m_read_bl_ver(dev);

	uint16_t bl_page_size;
	m_read_bl_page_size(dev, &bl_page_size);
	LOG_DBG("BL Page Size: %d", bl_page_size);

	if (type == MAX32664_UPDATER_DEV_TYPE_MAX32664C)
	{
		max32664_load_fw(dev, MAX32664C_FW_PATH, false);
	}
	else if (type == MAX32664_UPDATER_DEV_TYPE_MAX32664D)
	{
		max32664_load_fw(dev, MAX32664D_FW_PATH, false);
	}
}
