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
#include <zephyr/kernel.h>
#include <string.h>

#include "max32664_updater.h"

LOG_MODULE_REGISTER(max32664_updater, LOG_LEVEL_DBG);

#define MAX32664C_DEFAULT_CMD_DELAY 30

#define MAX32664C_FW_UPDATE_WRITE_SIZE 8208 // Page size 8192 + 16 bytes for CRC
#define MAX32664C_FW_UPDATE_START_ADDR 0x4C

#define MAX32664C_FW_BIN_INCLUDE 0
#define MAX32664C_WR_SIM_ONLY 0

// File paths for firmware binaries
#define MAX32664C_FW_PATH "/lfs/sys/max32664c_30_13_31.msbl"
#define MAX32664D_FW_PATH "/lfs/sys/max32664d_40_6_0.msbl"

// Small shared buffer for temporary operations
// SAFETY: This buffer is only used in single-threaded context during firmware updates
#define SHARED_BUFFER_SIZE 1026
static uint8_t shared_rw_buffer[SHARED_BUFFER_SIZE]; // Reusable buffer for I2C operations and file reading

// Track the current device type being updated
static enum max32664_updater_device_type current_update_device_type = MAX32664_UPDATER_DEV_TYPE_MAX32664C;

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

	LOG_DBG("BL Version: %x.%x.%x\n", rd_buf[1], rd_buf[2], rd_buf[3]);
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

	// Enter BL command completed
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

	// Page size read
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
	k_sleep(K_MSEC(100));
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
	
	// Seek to the correct position in the file
	int seek_ret = fs_seek(file, msbl_page_offset, FS_SEEK_SET);
	if (seek_ret < 0) {
		LOG_ERR("Failed to seek to offset %d, error: %d", msbl_page_offset, seek_ret);
		return seek_ret;
	}

	// Allocate buffer for command + full page data
	uint8_t *page_buffer = k_malloc(MAX32664C_FW_UPDATE_WRITE_SIZE + 2);
	if (!page_buffer) {
		LOG_ERR("Failed to allocate page buffer");
		return -ENOMEM;
	}

	// Set command bytes
	page_buffer[0] = 0x80;
	page_buffer[1] = 0x04;

	// Read entire page data into buffer after command bytes
	ssize_t bytes_read = fs_read(file, &page_buffer[2], MAX32664C_FW_UPDATE_WRITE_SIZE);
	if (bytes_read != MAX32664C_FW_UPDATE_WRITE_SIZE) {
		LOG_ERR("Failed to read full page data, read %d bytes (expected %d)", 
				bytes_read, MAX32664C_FW_UPDATE_WRITE_SIZE);
		k_free(page_buffer);
		return -EIO;
	}

	LOG_DBG("Read full page data (%d bytes), starting write", MAX32664C_FW_UPDATE_WRITE_SIZE);

	// Set MFIO low before starting page write sequence
	gpio_pin_set_dt(&config->mfio_gpio, 0);
	k_sleep(K_USEC(300));

	// Send command + entire page data in one transaction
	int ret = i2c_write_dt(&config->i2c, page_buffer, MAX32664C_FW_UPDATE_WRITE_SIZE + 2);
	if (ret < 0) {
		LOG_ERR("Failed to write page data, error: %d", ret);
		gpio_pin_set_dt(&config->mfio_gpio, 1);
		k_free(page_buffer);
		return ret;
	}

	LOG_DBG("Successfully sent page data (%d bytes total)", MAX32664C_FW_UPDATE_WRITE_SIZE + 2);

	// Wait for processing - page write takes significant time
	k_sleep(K_MSEC(1000));

	// Read response
	ret = i2c_read_dt(&config->i2c, rd_buf, sizeof(rd_buf));
	if (ret < 0) {
		LOG_ERR("Failed to read page write response, error: %d", ret);
		gpio_pin_set_dt(&config->mfio_gpio, 1);
		k_free(page_buffer);
		return ret;
	}

	// Set MFIO back to high
	gpio_pin_set_dt(&config->mfio_gpio, 1);
	k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));

	// Free the allocated buffer
	k_free(page_buffer);

	LOG_DBG("Write Page RSP: 0x%02x", rd_buf[0]);

	// Check for error responses
	if (rd_buf[0] != 0x00) {
		LOG_ERR("Page write failed with response: 0x%02x", rd_buf[0]);
		return -EIO;
	}

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
	
	// Progress logging with status information
	if (status == MAX32664_UPDATER_STATUS_FILE_NOT_FOUND) {
		LOG_ERR("Update failed: Firmware file not found (progress: %d%%)", progress);
	} else if (status == MAX32664_UPDATER_STATUS_FAILED) {
		LOG_ERR("Update failed: Error during update process (progress: %d%%)", progress);
	} else if (status == MAX32664_UPDATER_STATUS_SUCCESS) {
		LOG_INF("Update completed successfully (progress: %d%%)", progress);
	} else {
		LOG_DBG("Update: %d%% (status: %d)", progress, status);
	}
}

static int check_filesystem_status(void)
{
	struct fs_statvfs stats;
	int ret = fs_statvfs("/lfs", &stats);
	
	if (ret < 0) {
		LOG_ERR("FS stats failed: %d", ret);
		return ret;
	}
	
	// Minimal filesystem status logging
	LOG_DBG("LFS: %lu/%lu blocks", stats.f_bfree, stats.f_blocks);
	
	return 0;
}

static int verify_firmware_files_exist(void)
{
	struct fs_dirent entry;
	bool c_variant_found = false;
	bool d_variant_found = false;
	
	// Check for MAX32664C firmware
	if (fs_stat(MAX32664C_FW_PATH, &entry) == 0) {
		c_variant_found = true;
		LOG_DBG("MAX32664C firmware found: %zu bytes", entry.size);
	} else {
		LOG_WRN("MAX32664C firmware not found at: %s", MAX32664C_FW_PATH);
	}
	
	// Check for MAX32664D firmware
	if (fs_stat(MAX32664D_FW_PATH, &entry) == 0) {
		d_variant_found = true;
		LOG_DBG("MAX32664D firmware found: %zu bytes", entry.size);
	} else {
		LOG_WRN("MAX32664D firmware not found at: %s", MAX32664D_FW_PATH);
	}
	
	if (!c_variant_found && !d_variant_found) {
		LOG_ERR("No firmware files found in filesystem");
		return -ENOENT;
	}
	
	return (c_variant_found && d_variant_found) ? 2 : 1;
}

static int max32664_load_fw(const struct device *dev, const char *fw_file_path, bool is_sim)
{
	uint8_t msbl_num_pages = 0;
	struct fs_file_t file;
	struct fs_dirent entry;

	LOG_INF("Loading MSBL: %s", fw_file_path);

	// Check if file exists first
	int stat_ret = fs_stat(fw_file_path, &entry);
	if (stat_ret < 0) {
		LOG_ERR("Firmware file does not exist: %s, error: %d", fw_file_path, stat_ret);
		update_progress(5, MAX32664_UPDATER_STATUS_FILE_NOT_FOUND);
		return -ENOENT;
	}
	
	LOG_DBG("File found: %zu bytes", entry.size);

	// Progress: File validation complete
	update_progress(15, MAX32664_UPDATER_STATUS_IN_PROGRESS);

	// Initialize file structure
	fs_file_t_init(&file);

	// Open the firmware file
	int ret = fs_open(&file, fw_file_path, FS_O_READ);
	if (ret < 0)
	{
		LOG_ERR("Failed to open firmware file: %s, error: %d", fw_file_path, ret);
		update_progress(10, MAX32664_UPDATER_STATUS_FILE_NOT_FOUND);
		return ret;
	}

	LOG_DBG("File opened");

	// Clear shared buffer before use
	memset(shared_rw_buffer, 0, sizeof(shared_rw_buffer));

	// Read header to get firmware information - reuse shared buffer for header
	// Only need first ~80 bytes for header data
	ret = fs_read(&file, shared_rw_buffer, 128);
	if (ret < 0)
	{
		LOG_ERR("Failed to read firmware file header, error: %d", ret);
		fs_close(&file);
		return ret;
	}
	
	if (ret < 128) {
		LOG_ERR("Insufficient header data read: %d bytes", ret);
		fs_close(&file);
		return -EIO;
	}

	LOG_DBG("Header read: %d bytes", ret);

	msbl_num_pages = shared_rw_buffer[0x44];
	LOG_INF("Pages: %d", msbl_num_pages);
	
	// Validate page count
	if (msbl_num_pages == 0 || msbl_num_pages > 100) {
		LOG_ERR("Invalid page count: %d", msbl_num_pages);
		fs_close(&file);
		return -EINVAL;
	}

	m_read_mcu_id(dev);
	k_sleep(K_MSEC(100));

	int set_pages_ret = m_write_set_num_pages(dev, msbl_num_pages);
	if (set_pages_ret != 0x00) {
		LOG_ERR("Set number of pages failed with response: 0x%02x", set_pages_ret);
		fs_close(&file);
		return -EIO;
	}
	LOG_INF("Pages set: %d", msbl_num_pages);

	// Progress: Bootloader setup complete
	update_progress(25, MAX32664_UPDATER_STATUS_IN_PROGRESS);

	// Use local variables for vectors to save RAM
	uint8_t init_vector[11];
	uint8_t auth_vector[16];

	memcpy(init_vector, &shared_rw_buffer[0x28], 11);
	int init_vec_ret = m_write_init_vector(dev, init_vector);
	if (init_vec_ret != 0x00) {
		LOG_ERR("Write init vector failed with response: 0x%02x", init_vec_ret);
		fs_close(&file);
		return -EIO;
	}
	memcpy(auth_vector, &shared_rw_buffer[0x34], 16);
	int auth_vec_ret = m_write_auth_vector(dev, auth_vector);
	if (auth_vec_ret != 0x00) {
		LOG_ERR("Write auth vector failed with response: 0x%02x", auth_vec_ret);
		fs_close(&file);
		return -EIO;
	}

	int erase_ret = m_erase_app(dev);
	if (erase_ret != 0x00) {
		LOG_ERR("Erase app failed with response: 0x%02x", erase_ret);
		fs_close(&file);
		return -EIO;
	}
	LOG_INF("App erased");

	// Progress: Setup complete, starting page writes
	update_progress(35, MAX32664_UPDATER_STATUS_IN_PROGRESS);

	int _progress_counter = 35;  // Start from setup completion
	int _progress_step = (60 / msbl_num_pages);  // Use 60% for page writes (35% to 95%)

	update_progress(_progress_counter, MAX32664_UPDATER_STATUS_IN_PROGRESS);

	// Write MSBL
	if (is_sim == false)
	{
		for (int i = 0; i < msbl_num_pages; i++)
		{
			LOG_DBG("Page %d/%d", (i + 1), msbl_num_pages);

			uint32_t msbl_page_offset = (MAX32664C_FW_UPDATE_START_ADDR + (i * MAX32664C_FW_UPDATE_WRITE_SIZE));

			ret = m_fw_write_page(dev, &file, msbl_page_offset);
			if (ret < 0)
			{
				LOG_ERR("Failed to write firmware page %d, error: %d", i, ret);
				fs_close(&file);
				update_progress(_progress_counter, MAX32664_UPDATER_STATUS_FAILED);
				return ret;
			}

			// Page written successfully - minimal logging
			_progress_counter += _progress_step;
			update_progress(_progress_counter, MAX32664_UPDATER_STATUS_IN_PROGRESS);
		}

		fs_close(&file);
		
		// Progress: Page writes complete, entering app mode
		update_progress(95, MAX32664_UPDATER_STATUS_IN_PROGRESS);
		
		int app_ret = max32664_do_enter_app(dev);
		if (app_ret < 0) {
			LOG_ERR("Failed to enter application mode, error: %d", app_ret);
			update_progress(95, MAX32664_UPDATER_STATUS_FAILED);
			return app_ret;
		}
		
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

	LOG_DBG("Op mode = %x\n", rd_buf[1]);

	return rd_buf[1];
}

void max32664_updater_start(const struct device *dev, enum max32664_updater_device_type type)
{
	const struct max32664_config *config = dev->config;

	// Store the current device type for display purposes
	current_update_device_type = type;

	LOG_INF("MAX32664 updater start: type %d", type);
	
	// Initial progress update
	update_progress(0, MAX32664_UPDATER_STATUS_IN_PROGRESS);
	
	// Check filesystem status first
	int fs_ret = check_filesystem_status();
	if (fs_ret < 0) {
		LOG_ERR("Filesystem check failed, aborting update");
		update_progress(0, MAX32664_UPDATER_STATUS_FAILED);
		return;
	}

	// Verify firmware files are present before starting
	int fw_check = verify_firmware_files_exist();
	if (fw_check < 0) {
		LOG_ERR("Required firmware files not found in filesystem");
		update_progress(2, MAX32664_UPDATER_STATUS_FILE_NOT_FOUND);
		return;
	}
	
	LOG_INF("FW files verified (%d found)", fw_check);
	update_progress(5, MAX32664_UPDATER_STATUS_IN_PROGRESS);

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

	update_progress(10, MAX32664_UPDATER_STATUS_IN_PROGRESS);

	uint16_t bl_page_size;
	m_read_bl_page_size(dev, &bl_page_size);

	int load_ret = -EINVAL;
	const char *device_name = "Unknown";
	
	if (type == MAX32664_UPDATER_DEV_TYPE_MAX32664C)
	{
		device_name = "MAX32664C";
		load_ret = max32664_load_fw(dev, MAX32664C_FW_PATH, false);
	}
	else if (type == MAX32664_UPDATER_DEV_TYPE_MAX32664D)
	{
		device_name = "MAX32664D";
		load_ret = max32664_load_fw(dev, MAX32664D_FW_PATH, false);
	}
	else
	{
		LOG_ERR("Unknown device type: %d", type);
		update_progress(0, MAX32664_UPDATER_STATUS_FAILED);
		return;
	}
	
	if (load_ret < 0) {
		if (load_ret == -ENOENT) {
			LOG_ERR("%s firmware file not found - update cannot proceed", device_name);
			// Ensure the UI shows the correct error state with file-specific message
			update_progress(5, MAX32664_UPDATER_STATUS_FILE_NOT_FOUND);
		} else {
			LOG_ERR("%s firmware loading failed with error: %d", device_name, load_ret);
			update_progress(0, MAX32664_UPDATER_STATUS_FAILED);
		}
		return;
	}
	
	LOG_INF("%s firmware update completed successfully", device_name);
}

enum max32664_updater_device_type max32664_get_current_update_device_type(void)
{
	return current_update_device_type;
}
