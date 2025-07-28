#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "max32664_updater.h"

extern const uint8_t max32664c_msbl[238112];
extern const uint8_t max32664d_40_6_0[172448];

LOG_MODULE_REGISTER(max32664_updater, LOG_LEVEL_DBG); // CONFIG_MAX32664_UPDATER_LOG_LEVEL);

uint8_t max32664_fw_init_vector[11] = {0};
uint8_t max32664_fw_auth_vector[16] = {0};

#define MAX32664C_DEFAULT_CMD_DELAY 10

#define MAX32664C_FW_UPDATE_WRITE_SIZE 8208 // Page size 8192 + 16 bytes for CRC
#define MAX32664C_FW_UPDATE_START_ADDR 0x4C

#define MAX32664C_FW_BIN_INCLUDE 0
#define MAX32664C_WR_SIM_ONLY 0

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
static uint8_t tmp_wr_buf[8][1026];

static int m_fw_write_page(const struct device *dev, const uint8_t *msbl_data, uint32_t msbl_page_offset)
{
	const struct max32664_config *config = dev->config;

	uint8_t rd_buf[1] = {0x00};
	uint8_t cmd_wr_buf[2] = {0x80, 0x04};

	// memcpy(&fw_data_wr_buf[2], &max32664d_msbl[msbl_page_offset], (MAX32664C_FW_UPDATE_WRITE_SIZE+2));
	// memcpy(fw_data_wr_buf, &max32664_msbl[msbl_page_offset], MAX32664C_FW_UPDATE_WRITE_SIZE);

	int msg_len = 1026;
	int num_msgs = ((MAX32664C_FW_UPDATE_WRITE_SIZE) / msg_len);

	struct i2c_msg max32664_i2c_msgs[9];

	LOG_DBG("Num Msgs: %d", num_msgs);

	max32664_i2c_msgs[0].buf = cmd_wr_buf; // fw_data_wr_buf[0];
	max32664_i2c_msgs[0].len = 2;
	max32664_i2c_msgs[0].flags = I2C_MSG_WRITE;

	// #if (MAX32664C_FW_BIN_INCLUDE == 1)
	for (int i = 0; i < 8; i++)
	{
		memcpy(tmp_wr_buf[i], &max32664d_40_6_0[(i * msg_len) + msbl_page_offset], msg_len);

		max32664_i2c_msgs[i + 1].buf = tmp_wr_buf[i]; // fw_data_wr_buf[(i * msg_len)];
		max32664_i2c_msgs[i + 1].len = msg_len;
		max32664_i2c_msgs[i + 1].flags = I2C_MSG_WRITE;
		//LOG_DBG("Msg %d: L %d msg_len: %d", (i + 1), max32664_i2c_msgs[i + 1].len, msg_len);
	}
	// #endif

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

static int max32664_load_fw(const struct device *dev, const int8_t *fw_bin_array, bool is_sim)
{
	uint8_t msbl_num_pages = 0;

	LOG_DBG("Loading MSBL...");

	LOG_DBG("MSBL Array Size: %d", sizeof(fw_bin_array));

	msbl_num_pages = fw_bin_array[0x44];
	LOG_DBG("MSBL Load: Pages: %d (%x)", msbl_num_pages, msbl_num_pages);

	m_read_mcu_id(dev);

	m_write_set_num_pages(dev, msbl_num_pages);

	memcpy(max32664_fw_init_vector, &fw_bin_array[0x28], 11);
	m_write_init_vector(dev, max32664_fw_init_vector);
	LOG_DBG("MSBL Init Vector: %x %x %x %x %x %x %x %x %x %x %x", max32664_fw_init_vector[0], max32664_fw_init_vector[1], max32664_fw_init_vector[2], max32664_fw_init_vector[3], max32664_fw_init_vector[4], max32664_fw_init_vector[5], max32664_fw_init_vector[6], max32664_fw_init_vector[7], max32664_fw_init_vector[8], max32664_fw_init_vector[9], max32664_fw_init_vector[10]);

	memcpy(max32664_fw_auth_vector, &fw_bin_array[0x34], 16);
	m_write_auth_vector(dev, max32664_fw_auth_vector);

	m_erase_app(dev);

	int _progress_counter = 0;
	int _progress_step = (100/msbl_num_pages);

	update_progress(_progress_counter, MAX32664_UPDATER_STATUS_IN_PROGRESS);

	// Write MSBL
	if (is_sim == false)
	{
		for (int i = 0; i < msbl_num_pages; i++)
		{
			LOG_DBG("Writing Page: %d of %d", (i + 1), msbl_num_pages);

			// memcpy(max32664_fw_page_buf, &fw_bin_array[MAX32664C_FW_UPDATE_START_ADDR + (i * MAX32664C_FW_UPDATE_WRITE_SIZE)], MAX32664C_FW_UPDATE_WRITE_SIZE);
			uint32_t msbl_page_offset = (MAX32664C_FW_UPDATE_START_ADDR + (i * MAX32664C_FW_UPDATE_WRITE_SIZE));
			LOG_DBG("MSBL Page Offset: %d (%x)\n", msbl_page_offset, msbl_page_offset);
			m_fw_write_page(dev, fw_bin_array, msbl_page_offset);
			// m_fw_write_page_single(dev, max32664_msbl, msbl_page_offset);
			_progress_counter += _progress_step;
			update_progress(_progress_counter, MAX32664_UPDATER_STATUS_IN_PROGRESS);

			// k_sleep(K_MSEC(500));
		}
		max32664_do_enter_app(dev);

		update_progress(100, MAX32664_UPDATER_STATUS_SUCCESS);		
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
		max32664_load_fw(dev, max32664c_msbl, false);
	}
	else if (type == MAX32664_UPDATER_DEV_TYPE_MAX32664D)
	{
		max32664_load_fw(dev, max32664d_40_6_0, false);
	}
}
