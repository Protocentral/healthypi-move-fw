#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h> 

#include "max32664_updater.h"

LOG_MODULE_REGISTER(max32664_updater, LOG_LEVEL_DBG);

uint8_t max32664_fw_init_vector[11] = {0};
uint8_t max32664_fw_auth_vector[16] = {0};

#define MAX32664C_DEFAULT_CMD_DELAY 10

#define MAX32664C_FW_UPDATE_WRITE_SIZE 8208 // Page size 8192 + 16 bytes for CRC
#define MAX32664C_FW_UPDATE_START_ADDR 0x4C

// Firmware paths in littlefs
#define MAX32664C_FW_PATH "/lfs/firmware/max32664c_30_13_31.bin"
#define MAX32664D_FW_PATH "/lfs/firmware/max32664d_40_6_0.bin"

// Fallback: Include arrays for first-time initialization if files don't exist
#ifdef CONFIG_MAX32664_UPDATER_INCLUDE_FALLBACK_ARRAYS
extern const uint8_t max32664c_msbl[238112];
extern const uint8_t max32664d_40_6_0[172448];
#endif

// Buffer for reading firmware data page by page
static uint8_t fw_page_buffer[MAX32664C_FW_UPDATE_WRITE_SIZE];
static uint8_t fw_header_buffer[0x100]; // Buffer for firmware header

static int m_read_op_mode(const struct device *dev);
static void (*progress_callback)(int progress, int status) = NULL;

struct max32664_config
{
	struct i2c_dt_spec i2c;
	struct gpio_dt_spec reset_gpio;
	struct gpio_dt_spec mfio_gpio;
};

static void update_progress(int progress, int status)
{
	if (progress_callback) {
		progress_callback(progress, status);
	}
}

void max32664_set_progress_callback(void (*callback)(int progress, int status))
{
	progress_callback = callback;
}

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

static int m_write_set_num_pages(const struct device *dev, uint8_t num_pages)
{
	const struct max32664_config *config = dev->config;
	uint8_t rd_buf[1] = {0x00};
	uint8_t wr_buf[3] = {0x80, 0x02, num_pages};

	gpio_pin_set_dt(&config->mfio_gpio, 0);
	k_sleep(K_USEC(300));
	i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
	k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));

	i2c_read_dt(&config->i2c, rd_buf, sizeof(rd_buf));
	k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));
	gpio_pin_set_dt(&config->mfio_gpio, 1);

	LOG_DBG("Write Set Num Pages: %d RSP: %x", num_pages, rd_buf[0]);

	return rd_buf[0];
}

static int m_write_init_vector(const struct device *dev, uint8_t *init_vec)
{
	const struct max32664_config *config = dev->config;
	uint8_t rd_buf[1] = {0x00};
	uint8_t wr_buf[13] = {0x80, 0x00};

	memcpy(&wr_buf[2], init_vec, 11);

	gpio_pin_set_dt(&config->mfio_gpio, 0);
	k_sleep(K_USEC(300));
	i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
	k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));

	i2c_read_dt(&config->i2c, rd_buf, sizeof(rd_buf));
	k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));
	gpio_pin_set_dt(&config->mfio_gpio, 1);

	LOG_DBG("Write Init Vec : RSP: %x", rd_buf[0]);

	return rd_buf[0];
}

static int m_write_auth_vector(const struct device *dev, uint8_t *auth_vec)
{
	const struct max32664_config *config = dev->config;
	uint8_t rd_buf[1] = {0x00};
	uint8_t wr_buf[18] = {0x80, 0x01};

	memcpy(&wr_buf[2], auth_vec, 16);

	gpio_pin_set_dt(&config->mfio_gpio, 0);
	k_sleep(K_USEC(300));
	i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
	k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));

	i2c_read_dt(&config->i2c, rd_buf, sizeof(rd_buf));
	k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));
	gpio_pin_set_dt(&config->mfio_gpio, 1);

	LOG_DBG("Write Auth Vec : RSP: %x", rd_buf[0]);

	return rd_buf[0];
}

static uint8_t tmp_wr_buf[8][1026];

static int m_fw_write_page_from_buffer(const struct device *dev, const uint8_t *page_data, uint32_t page_offset_in_buffer)
{
	const struct max32664_config *config = dev->config;

	uint8_t rd_buf[1] = {0x00};
	uint8_t cmd_wr_buf[2] = {0x80, 0x04};

	int msg_len = 1026;
	int num_msgs = ((MAX32664C_FW_UPDATE_WRITE_SIZE) / msg_len);

	struct i2c_msg max32664_i2c_msgs[9];

	LOG_DBG("Num Msgs: %d", num_msgs);

	max32664_i2c_msgs[0].buf = cmd_wr_buf;
	max32664_i2c_msgs[0].len = 2;
	max32664_i2c_msgs[0].flags = I2C_MSG_WRITE;

	for (int i = 0; i < 8; i++)
	{
		memcpy(tmp_wr_buf[i], &page_data[(i * msg_len) + page_offset_in_buffer], msg_len);

		max32664_i2c_msgs[i + 1].buf = tmp_wr_buf[i];
		max32664_i2c_msgs[i + 1].len = msg_len;
		max32664_i2c_msgs[i + 1].flags = I2C_MSG_WRITE;
	}

	max32664_i2c_msgs[8].flags = I2C_MSG_WRITE | I2C_MSG_STOP;

	int ret = i2c_transfer_dt(&config->i2c, max32664_i2c_msgs, 9);

	LOG_DBG("Transfer Ret: %d", ret);

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

	i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
	k_sleep(K_MSEC(3500));

	i2c_read_dt(&config->i2c, rd_buf, sizeof(rd_buf));
	k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));

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

	LOG_DBG("MCU ID : %x.%x", rd_buf[0], rd_buf[1]);

	return 0;
}

static int max32664_do_enter_app(const struct device *dev)
{
	const struct max32664_config *config = dev->config;
	uint8_t wr_buf[2] = {0x80, 0x08};
	uint8_t rd_buf[1] = {0x00};

	i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
	k_sleep(K_MSEC(1000));

	i2c_read_dt(&config->i2c, rd_buf, sizeof(rd_buf));
	k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));

	LOG_DBG("Enter App : RSP: %x", rd_buf[0]);

	return rd_buf[0];
}

#ifdef CONFIG_MAX32664_UPDATER_INCLUDE_FALLBACK_ARRAYS
static int create_firmware_file_if_missing(const char *fw_path, const uint8_t *fw_array, size_t fw_size)
{
	struct fs_file_t fw_file;
	int ret;

	// Check if file already exists
	fs_file_t_init(&fw_file);
	ret = fs_open(&fw_file, fw_path, FS_O_READ);
	if (ret == 0) {
		// File exists, close and return
		fs_close(&fw_file);
		LOG_DBG("Firmware file already exists: %s", fw_path);
		return 0;
	}

	LOG_INF("Creating firmware file from array: %s (%d bytes)", fw_path, fw_size);

	// Create directory if it doesn't exist
	ret = fs_mkdir("/lfs/firmware");
	if (ret != 0 && ret != -EEXIST) {
		LOG_ERR("Failed to create firmware directory: %d", ret);
		return ret;
	}

	// Create new file
	ret = fs_open(&fw_file, fw_path, FS_O_CREATE | FS_O_WRITE);
	if (ret < 0) {
		LOG_ERR("Failed to create firmware file %s: %d", fw_path, ret);
		return ret;
	}

	// Write firmware data in chunks to avoid large stack usage
	const size_t chunk_size = 4096;
	size_t bytes_written = 0;
	
	while (bytes_written < fw_size) {
		size_t write_size = MIN(chunk_size, fw_size - bytes_written);
		ret = fs_write(&fw_file, &fw_array[bytes_written], write_size);
		if (ret < 0) {
			LOG_ERR("Failed to write firmware data: %d", ret);
			fs_close(&fw_file);
			return ret;
		}
		bytes_written += write_size;
	}

	fs_close(&fw_file);
	LOG_INF("Successfully created firmware file: %s", fw_path);
	return 0;
}
#endif

static int max32664_load_fw_from_fs(const struct device *dev, const char *fw_path, bool is_sim)
{
	struct fs_file_t fw_file;
	uint8_t msbl_num_pages = 0;
	int ret;

	LOG_DBG("Loading MSBL from filesystem: %s", fw_path);

	// Initialize file structure
	fs_file_t_init(&fw_file);

	// Open firmware file
	ret = fs_open(&fw_file, fw_path, FS_O_READ);
	if (ret < 0) {
		LOG_ERR("Failed to open firmware file %s: %d", fw_path, ret);
		return ret;
	}

	// Read header to get number of pages
	ret = fs_read(&fw_file, fw_header_buffer, sizeof(fw_header_buffer));
	if (ret < 0) {
		LOG_ERR("Failed to read firmware header: %d", ret);
		fs_close(&fw_file);
		return ret;
	}

	msbl_num_pages = fw_header_buffer[0x44];
	LOG_DBG("MSBL Load: Pages: %d (%x)", msbl_num_pages, msbl_num_pages);

	m_read_mcu_id(dev);
	m_write_set_num_pages(dev, msbl_num_pages);

	// Extract vectors from header
	memcpy(max32664_fw_init_vector, &fw_header_buffer[0x28], 11);
	m_write_init_vector(dev, max32664_fw_init_vector);
	LOG_DBG("MSBL Init Vector: %x %x %x %x %x %x %x %x %x %x %x", 
		max32664_fw_init_vector[0], max32664_fw_init_vector[1], 
		max32664_fw_init_vector[2], max32664_fw_init_vector[3], 
		max32664_fw_init_vector[4], max32664_fw_init_vector[5], 
		max32664_fw_init_vector[6], max32664_fw_init_vector[7], 
		max32664_fw_init_vector[8], max32664_fw_init_vector[9], 
		max32664_fw_init_vector[10]);

	memcpy(max32664_fw_auth_vector, &fw_header_buffer[0x34], 16);
	m_write_auth_vector(dev, max32664_fw_auth_vector);

	m_erase_app(dev);

	int _progress_counter = 0;
	int _progress_step = (100/msbl_num_pages);

	update_progress(_progress_counter, MAX32664_UPDATER_STATUS_IN_PROGRESS);

	// Write MSBL page by page
	if (is_sim == false)
	{
		// Seek to start of firmware data
		ret = fs_seek(&fw_file, MAX32664C_FW_UPDATE_START_ADDR, FS_SEEK_SET);
		if (ret < 0) {
			LOG_ERR("Failed to seek to firmware start: %d", ret);
			fs_close(&fw_file);
			return ret;
		}

		for (int i = 0; i < msbl_num_pages; i++)
		{
			LOG_DBG("Writing Page: %d of %d", (i + 1), msbl_num_pages);

			// Read one page worth of data from filesystem
			ret = fs_read(&fw_file, fw_page_buffer, MAX32664C_FW_UPDATE_WRITE_SIZE);
			if (ret < MAX32664C_FW_UPDATE_WRITE_SIZE) {
				LOG_ERR("Failed to read full page %d: read %d bytes, expected %d", 
					i, ret, MAX32664C_FW_UPDATE_WRITE_SIZE);
				fs_close(&fw_file);
				return -EIO;
			}

			// Write page to device
			m_fw_write_page_from_buffer(dev, fw_page_buffer, 0);
			
			_progress_counter += _progress_step;
			update_progress(_progress_counter, MAX32664_UPDATER_STATUS_IN_PROGRESS);
		}
		
		max32664_do_enter_app(dev);
		update_progress(100, MAX32664_UPDATER_STATUS_SUCCESS);		
	}

	fs_close(&fw_file);
	LOG_DBG("End Load MSBL");
	return 0;
}

static int m_read_op_mode(const struct device *dev)
{
	const struct max32664_config *config = dev->config;
	uint8_t rd_buf[2] = {0x00, 0x00};
	uint8_t wr_buf[2] = {0x02, 0x00};

	gpio_pin_set_dt(&config->mfio_gpio, 0);
	k_sleep(K_USEC(300));
	i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
	k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));

	i2c_read_dt(&config->i2c, rd_buf, sizeof(rd_buf));
	k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));
	gpio_pin_set_dt(&config->mfio_gpio, 1);

	LOG_DBG("Op Mode = %d", rd_buf[1]);

	return rd_buf[1];
}

void max32664_updater_start(const struct device *dev, enum max32664_updater_device_type type)
{
	LOG_DBG("Max32664 MSBL Update Start...");

#ifdef CONFIG_MAX32664_UPDATER_INCLUDE_FALLBACK_ARRAYS
	// Ensure firmware files exist, create from arrays if missing
	if (type == MAX32664_UPDATER_DEV_TYPE_MAX32664C) {
		create_firmware_file_if_missing(MAX32664C_FW_PATH, max32664c_msbl, sizeof(max32664c_msbl));
	} else if (type == MAX32664_UPDATER_DEV_TYPE_MAX32664D) {
		create_firmware_file_if_missing(MAX32664D_FW_PATH, max32664d_40_6_0, sizeof(max32664d_40_6_0));
	}
#endif

	// m_read_bl_ver(dev);

	gpio_pin_configure_dt(&((struct max32664_config *)dev->config)->reset_gpio, GPIO_OUTPUT_ACTIVE);
	gpio_pin_configure_dt(&((struct max32664_config *)dev->config)->mfio_gpio, GPIO_OUTPUT_ACTIVE);

	gpio_pin_set_dt(&((struct max32664_config *)dev->config)->reset_gpio, 0);
	k_sleep(K_MSEC(10));
	gpio_pin_set_dt(&((struct max32664_config *)dev->config)->reset_gpio, 1);
	k_sleep(K_MSEC(50));

	gpio_pin_set_dt(&((struct max32664_config *)dev->config)->mfio_gpio, 0);
	k_sleep(K_MSEC(10));
	gpio_pin_set_dt(&((struct max32664_config *)dev->config)->reset_gpio, 0);
	k_sleep(K_MSEC(10));
	gpio_pin_set_dt(&((struct max32664_config *)dev->config)->reset_gpio, 1);
	k_sleep(K_MSEC(1000));

	if (type == MAX32664_UPDATER_DEV_TYPE_MAX32664C)
	{
		max32664_load_fw_from_fs(dev, MAX32664C_FW_PATH, false);
	}
	else if (type == MAX32664_UPDATER_DEV_TYPE_MAX32664D)
	{
		max32664_load_fw_from_fs(dev, MAX32664D_FW_PATH, false);
	}

	LOG_DBG("Max32664 MSBL Update End.");
}
