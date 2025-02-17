/*
 * Copyright (c) 2017, NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT maxim_max32664

#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/drivers/gpio.h>

// #include "maxm86146_msbl.h"
//  #include "max32664c_msbl.h"
//  #include "max32664d_msbl.h"

#include "max32664d.h"

LOG_MODULE_REGISTER(MAX32664D, CONFIG_MAX32664D_LOG_LEVEL);

#define MAX32664_FW_BIN_INCLUDE 0

// #define DEFAULT_MODE_BPT_ESTIMATION 1
#define MODE_RAW_SENSOR_DATA 2

static uint8_t buf[2048]; // 23 byte/sample * 32 samples = 736 bytes

#define CALIBVECTOR_SIZE 827 // Command 3 bytes + 824 bytes of calib vectors
#define DATE_TIME_VECTOR_SIZE 11
#define SPO2_CAL_COEFFS_SIZE 15

#define DEFAULT_SPO2_A 1.5958422
#define DEFAULT_SPO2_B -34.659664
#define DEFAULT_SPO2_C 112.68987

#define DEFAULT_DATE 0x5cc20200
#define DEFAULT_TIME 0xe07f0200

uint8_t m_date_time_vector[DATE_TIME_VECTOR_SIZE] = {0x50, 0x04, 0x04, 0x5c, 0xc2, 0x02, 0x00, 0xe0, 0x7f, 0x02, 0x00};

uint8_t m_bpt_cal_vector[CALIBVECTOR_SIZE] = {0x50, 0x04, 0x03, 0, 0, 175, 63, 3, 33, 75, 0, 0, 0, 0, 15, 198, 2, 100, 3, 32, 0, 0, 3, 207, 0, // calib vector sample
											  4, 0, 3, 175, 170, 3, 33, 134, 0, 0, 0, 0, 15, 199, 2, 100, 3, 32, 0, 0, 3,
											  207, 0, 4, 0, 3, 176, 22, 3, 33, 165, 0, 0, 0, 0, 15, 200, 2, 100, 3, 32, 0,
											  0, 3, 207, 0, 4, 0, 3, 176, 102, 3, 33, 203, 0, 0, 0, 0, 15, 201, 2, 100, 3,
											  32, 0, 0, 3, 207, 0, 4, 0, 3, 176, 178, 3, 33, 236, 0, 0, 0, 0, 15, 202, 2,
											  100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3, 176, 255, 3, 34, 16, 0, 0, 0, 0, 15,
											  203, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3, 177, 64, 3, 34, 41, 0, 0, 0, 0,
											  15, 204, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3, 177, 130, 3, 34, 76, 0, 0,
											  0, 0, 15, 205, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3, 177, 189, 3, 34, 90,
											  0, 0, 0, 0, 15, 206, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3, 177, 248, 3, 34,
											  120, 0, 0, 0, 0, 15, 207, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3, 178, 69, 3,
											  34, 137, 0, 0, 0, 0, 15, 208, 2, 100, 3, 32, 0, 0, 3, 0, 0, 175, 63, 3, 33,
											  75, 0, 0, 0, 0, 15, 198, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3, 175, 170, 3,
											  33, 134, 0, 0, 0, 0, 15, 199, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3, 176,
											  22, 3, 33, 165, 0, 0, 0, 0, 15, 200, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3,
											  176, 102, 3, 33, 203, 0, 0, 0, 0, 15, 201, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4,
											  0, 3, 176, 178, 3, 33, 236, 0, 0, 0, 0, 15, 202, 2, 100, 3, 32, 0, 0, 3, 207,
											  0, 4, 0, 3, 176, 255, 3, 34, 16, 0, 0, 0, 0, 15, 203, 2, 100, 3, 32, 0, 0, 3,
											  207, 0, 4, 0, 3, 177, 64, 3, 34, 41, 0, 0, 0, 0, 15, 204, 2, 100, 3, 32, 0, 0,
											  3, 207, 0, 4, 0, 3, 177, 130, 3, 34, 76, 0, 0, 0, 0, 15, 205, 2, 100, 3, 32,
											  0, 0, 3, 207, 0, 4, 0, 3, 177, 189, 3, 34, 90, 0, 0, 0, 0, 15, 206, 2, 100, 3,
											  32, 0, 0, 3, 207, 0, 4, 0, 3, 177, 248, 3, 34, 120, 0, 0, 0, 0, 15, 207, 2,
											  100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3, 178, 69, 3, 34, 137, 0, 0, 0, 0, 15,
											  208, 2, 100, 3, 32, 0, 0, 3, 0, 0, 175, 63, 3, 33, 75, 0, 0, 0, 0, 15, 198, 2,
											  100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3, 175, 170, 3, 33, 134, 0, 0, 0, 0, 15,
											  199, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3, 176, 22, 3, 33, 165, 0, 0, 0, 0,
											  15, 200, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3, 176, 102, 3, 33, 203, 0, 0,
											  0, 0, 15, 201, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3, 176, 178, 3, 33, 236,
											  0, 0, 0, 0, 15, 202, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3, 176, 255, 3, 34,
											  16, 0, 0, 0, 0, 15, 203, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3, 177, 64, 3,
											  34, 41, 0, 0, 0, 0, 15, 204, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3, 177,
											  130, 3, 34, 76, 0, 0, 0, 0, 15, 205, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4, 0, 3,
											  177, 189, 3, 34, 90, 0, 0, 0, 0, 15, 206, 2, 100, 3, 32, 0, 0, 3, 207, 0, 4,
											  0, 3, 177, 248, 3, 34, 120, 0, 0, 0, 0, 15, 207, 2, 100, 3, 32, 0, 0, 3, 207,
											  0, 4, 0, 3, 178, 69, 3, 34, 137, 0, 0, 0, 0, 15, 208, 2, 100, 3, 32, 0, 0, 3,
											  0, 0, 175, 63, 3, 33, 75, 0, 0, 0, 0, 15, 198, 2, 100, 3, 32, 0, 0, 3, 207, 0,
											  4, 0, 3, 175, 170, 3, 33, 134, 0, 0, 0, 0, 15, 199, 2, 100, 3, 32, 0, 0, 3,
											  207, 0, 4, 0, 3, 176, 22, 3, 33, 165, 0, 0, 0, 0, 15, 200, 2, 100, 3, 32, 0,
											  0, 3, 207, 0, 4, 0, 3, 176, 102, 3};

static int max32664_do_enter_app(const struct device *dev);

static int m_read_op_mode(const struct device *dev)
{
	// struct max32664_data *data = dev->data;
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

	LOG_DBG("Op mode %x", rd_buf[1]);

	return rd_buf[1];
}

static int m_read_mcu_id(const struct device *dev)
{
	const struct max32664_config *config = dev->config;
	uint8_t rd_buf[2] = {0x00, 0x00};
	uint8_t wr_buf[2] = {0xFF, 0x00};

	k_sleep(K_USEC(300));
	i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
	k_sleep(K_MSEC(MAX32664_DEFAULT_CMD_DELAY));

	i2c_read_dt(&config->i2c, rd_buf, sizeof(rd_buf));
	k_sleep(K_MSEC(MAX32664_DEFAULT_CMD_DELAY));

	LOG_DBG("MCU ID = %x %x\n", rd_buf[0], rd_buf[1]);

	return 0;
}

uint8_t m_read_hub_status(const struct device *dev)
{
	/*
	Table 7. Sensor Hub Status Byte
	BIT 7 6 5 4 3 2 1 0
	Field Reserved HostAccelUfInt FifoInOverInt FifoOutOvrInt DataRdyInt Err2 Err1 Err0
	*/
	// struct max32664_data *data = dev->data;
	const struct max32664_config *config = dev->config;
	uint8_t rd_buf[2] = {0x00, 0x00};
	uint8_t wr_buf[2] = {0x00, 0x00};

	gpio_pin_set_dt(&config->mfio_gpio, 0);
	k_sleep(K_USEC(300));

	i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
	i2c_read_dt(&config->i2c, rd_buf, sizeof(rd_buf));
	k_sleep(K_USEC(300));
	gpio_pin_set_dt(&config->mfio_gpio, 1);

	// printk("Stat %x | ", rd_buf[1]);

	return rd_buf[1];
}

static int m_get_ver(const struct device *dev, uint8_t *ver_buf)
{
	const struct max32664_config *config = dev->config;

	uint8_t wr_buf[2] = {0xFF, 0x03};

	k_sleep(K_USEC(300));
	i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
	k_sleep(K_MSEC(MAX32664_DEFAULT_CMD_DELAY));

	i2c_read_dt(&config->i2c, ver_buf, 4);
	k_sleep(K_MSEC(MAX32664_DEFAULT_CMD_DELAY));

	// LOG_INF("Version (decimal) = %d.%d.%d\n", ver_buf[1], ver_buf[2], ver_buf[3]);

	if (ver_buf[1] == 0x00 && ver_buf[2] == 0x00 && ver_buf[3] == 0x00)
	{
		return -ENODEV;
	}

	return 0;
}

/*static int max32664_load_fw(const struct device *dev, uint8_t *fw_bin_array)
{
	uint8_t msbl_num_pages = 0;
	uint16_t msbl_write_pos = 0;

#if (MAX32664_FW_BIN_INCLUDE == 1)
	printk("---\nLoading MSBL\n");
	printk("MSBL Array Size: %d\n", sizeof(maxm86146_msbl));

	msbl_num_pages = fw_bin_array[0x44];
	printk("MSBL Load: Pages: %d (%x)\n", msbl_num_pages, msbl_num_pages);

	m_read_mcu_id(dev);

	m_write_set_num_pages(dev, msbl_num_pages);

	memcpy(max32664_fw_init_vector, &fw_bin_array[0x28], 11);
	m_write_init_vector(dev, max32664_fw_init_vector);
	printk("MSBL Init Vector: %x %x %x %x %x %x %x %x %x %x %x\n", max32664_fw_init_vector[0], max32664_fw_init_vector[1], max32664_fw_init_vector[2], max32664_fw_init_vector[3], max32664_fw_init_vector[4], max32664_fw_init_vector[5], max32664_fw_init_vector[6], max32664_fw_init_vector[7], max32664_fw_init_vector[8], max32664_fw_init_vector[9], max32664_fw_init_vector[10]);

	memcpy(max32664_fw_auth_vector, &fw_bin_array[0x34], 16);
	m_write_auth_vector(dev, max32664_fw_auth_vector);

	m_erase_app(dev);
	k_sleep(K_MSEC(2000));

	// Write MSBL

	for (int i = 0; i < msbl_num_pages; i++)
	{
		printk("Writing Page: %d of %d\n", (i + 1), msbl_num_pages);

		// memcpy(max32664_fw_page_buf, &fw_bin_array[MAX32664_FW_UPDATE_START_ADDR + (i * MAX32664_FW_UPDATE_WRITE_SIZE)], MAX32664_FW_UPDATE_WRITE_SIZE);
		uint32_t msbl_page_offset = (MAX32664_FW_UPDATE_START_ADDR + (i * MAX32664_FW_UPDATE_WRITE_SIZE));
		printk("MSBL Page Offset: %d (%x)\n", msbl_page_offset, msbl_page_offset);
		m_fw_write_page(dev, maxm86146_msbl, msbl_page_offset);

		k_sleep(K_MSEC(500));
	}
#endif

	max32664_do_enter_app(dev);

	printk("End Load MSBL\n---\n");

	return 0;
}
*/

static int m_i2c_write_cmd_4(const struct device *dev, uint8_t byte1, uint8_t byte2, uint8_t byte3, uint8_t byte4)
{
	const struct max32664_config *config = dev->config;
	uint8_t wr_buf[4];
	uint8_t rd_buf[1];

	wr_buf[0] = byte1;
	wr_buf[1] = byte2;
	wr_buf[2] = byte3;
	wr_buf[3] = byte4;

	gpio_pin_set_dt(&config->mfio_gpio, 0);
	k_sleep(K_USEC(300));

	i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
	k_sleep(K_MSEC(MAX32664_DEFAULT_CMD_DELAY));
	i2c_read_dt(&config->i2c, rd_buf, 1);
	k_sleep(K_MSEC(MAX32664_DEFAULT_CMD_DELAY));

	gpio_pin_set_dt(&config->mfio_gpio, 1);

	LOG_DBG("CMD: %x %x %x %x | RSP: %x", wr_buf[0], wr_buf[1], wr_buf[2], wr_buf[3], rd_buf[0]);

	k_sleep(K_MSEC(45));

	return 0;
}

static int m_i2c_write_cmd_5(const struct device *dev, uint8_t byte1, uint8_t byte2, uint8_t byte3, uint8_t byte4, uint8_t byte5)
{
	const struct max32664_config *config = dev->config;
	uint8_t wr_buf[5];
	uint8_t rd_buf[1];

	wr_buf[0] = byte1;
	wr_buf[1] = byte2;
	wr_buf[2] = byte3;
	wr_buf[3] = byte4;
	wr_buf[4] = byte5;

	gpio_pin_set_dt(&config->mfio_gpio, 0);
	k_sleep(K_USEC(300));

	i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
	k_sleep(K_MSEC(MAX32664_DEFAULT_CMD_DELAY));
	i2c_read_dt(&config->i2c, rd_buf, 1);
	k_sleep(K_MSEC(MAX32664_DEFAULT_CMD_DELAY));

	gpio_pin_set_dt(&config->mfio_gpio, 1);

	LOG_DBG("CMD: %x %x %x %x %x | RSP: %x", wr_buf[0], wr_buf[1], wr_buf[2], wr_buf[3], wr_buf[4], rd_buf[0]);

	k_sleep(K_MSEC(45));

	return 0;
}

int max32664_get_fifo_count(const struct device *dev)
{
	const struct max32664_config *config = dev->config;
	uint8_t rd_buf[2] = {0x00, 0x00};
	uint8_t wr_buf[2] = {0x12, 0x00};

	uint8_t fifo_count;
	gpio_pin_set_dt(&config->mfio_gpio, 0);
	k_sleep(K_USEC(300));

	i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
	i2c_read_dt(&config->i2c, rd_buf, sizeof(rd_buf));

	gpio_pin_set_dt(&config->mfio_gpio, 1);

	fifo_count = rd_buf[1];
	return (int)fifo_count;
}

static int m_i2c_write_cmd_3(const struct device *dev, uint8_t byte1, uint8_t byte2, uint8_t byte3, uint16_t cmd_delay)
{
	const struct max32664_config *config = dev->config;
	uint8_t wr_buf[3];

	uint8_t rd_buf[1] = {0x00};

	wr_buf[0] = byte1;
	wr_buf[1] = byte2;
	wr_buf[2] = byte3;

	gpio_pin_set_dt(&config->mfio_gpio, 0);
	k_sleep(K_USEC(300));

	i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
	k_sleep(K_MSEC(cmd_delay));

	i2c_read_dt(&config->i2c, rd_buf, sizeof(rd_buf));
	k_sleep(K_USEC(300));

	gpio_pin_set_dt(&config->mfio_gpio, 1);

	LOG_DBG("CMD: %x %x %x | RSP: %x", wr_buf[0], wr_buf[1], wr_buf[2], rd_buf[0]);

	k_sleep(K_MSEC(10));

	return 0;
}

static int m_i2c_write_cmd_3_rsp_2(const struct device *dev, uint8_t byte1, uint8_t byte2, uint8_t byte3)
{
	const struct max32664_config *config = dev->config;
	uint8_t wr_buf[3];

	uint8_t rd_buf[2] = {0x00, 0x00};

	wr_buf[0] = byte1;
	wr_buf[1] = byte2;
	wr_buf[2] = byte3;

	gpio_pin_set_dt(&config->mfio_gpio, 0);
	k_sleep(K_USEC(300));
	i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));

	k_sleep(K_MSEC(MAX32664_DEFAULT_CMD_DELAY));

	// gpio_pin_set_dt(&config->mfio_gpio, 0);
	k_sleep(K_USEC(300));
	i2c_read_dt(&config->i2c, rd_buf, sizeof(rd_buf));
	k_sleep(K_MSEC(500));

	gpio_pin_set_dt(&config->mfio_gpio, 1);

	LOG_DBG("CMD: %x %x %x | RSP: %x %x", wr_buf[0], wr_buf[1], wr_buf[2], rd_buf[0], rd_buf[1]);

	k_sleep(K_MSEC(10));

	return 0;
}

static int m_i2c_write(const struct device *dev, uint8_t *wr_buf, uint32_t wr_len)
{
	const struct max32664_config *config = dev->config;

	uint8_t rd_buf[1] = {0x00};

	gpio_pin_set_dt(&config->mfio_gpio, 0);
	k_sleep(K_USEC(300));
	i2c_write_dt(&config->i2c, wr_buf, wr_len);

	k_sleep(K_MSEC(MAX32664_DEFAULT_CMD_DELAY));

	k_sleep(K_USEC(300));
	i2c_read_dt(&config->i2c, rd_buf, sizeof(rd_buf));
	k_sleep(K_MSEC(MAX32664_DEFAULT_CMD_DELAY));

	gpio_pin_set_dt(&config->mfio_gpio, 1);

	LOG_DBG("Write %d bytes | RSP: %d", wr_len, rd_buf[0]);

	k_sleep(K_MSEC(45));

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

	m_read_hub_status(dev);
	k_sleep(K_MSEC(200));
	m_read_hub_status(dev);

	return 0;
}

static void bpt_time_to_byte_le(uint32_t time, uint8_t *byte_time)
{
	
	byte_time[0] = (time & 0x000000FF);
	byte_time[1] = (time & 0x0000FF00) >> 8;
	byte_time[2] = (time & 0x00FF0000) >> 16;
	byte_time[3] = (time & 0xFF000000) >> 24;

	//printk("Hex Time: %x %x %x %x\n", byte_time[0], byte_time[1], byte_time[2], byte_time[3]);
}

static int m_set_date_time(const struct device *dev, uint32_t date, uint32_t time)
{
	uint8_t wr_buf[DATE_TIME_VECTOR_SIZE] = {0x50, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	LOG_DBG("MAX32664 Set date: %d", date);

	uint8_t byte_date[4];
	bpt_time_to_byte_le(date, byte_date);

	wr_buf[3] = byte_date[0];
	wr_buf[4] = byte_date[1];
	wr_buf[5] = byte_date[2];
	wr_buf[6] = byte_date[3];

	LOG_DBG("MAX32664 Set time: %d", time);

	uint8_t byte_time[4];
	bpt_time_to_byte_le(time, byte_time);

	wr_buf[7] = byte_time[0];
	wr_buf[8] = byte_time[1];
	wr_buf[9] = byte_time[2];
	wr_buf[10] = byte_time[3];

	m_i2c_write(dev, wr_buf, sizeof(wr_buf));

	return 0;
}

static int m_set_bp_cal_values(const struct device *dev, uint32_t sys, uint32_t dia)
{
	uint8_t wr_buf[6] = {0x50, 0x04, 0x01, 0x00, 0x00, 0x00};

	wr_buf[3] = sys >> 16;
	wr_buf[4] = sys >> 8;
	wr_buf[5] = sys;

	m_i2c_write(dev, wr_buf, sizeof(wr_buf));

	// Set diastolic BP
	wr_buf[2] = 0x02;

	wr_buf[3] = dia >> 16;
	wr_buf[4] = dia >> 8;
	wr_buf[5] = dia;

	m_i2c_write(dev, wr_buf, sizeof(wr_buf));

	return 0;
}

static int m_set_spo2_coeffs(const struct device *dev, float a, float b, float c)
{
	uint8_t wr_buf[15] = {0x50, 0x04, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	int32_t a_int = (int32_t)(a * 100000);

	wr_buf[3] = (a_int & 0xff000000) >> 24;
	wr_buf[4] = (a_int & 0x00ff0000) >> 16;
	wr_buf[5] = (a_int & 0x0000ff00) >> 8;
	wr_buf[6] = (a_int & 0x000000ff);

	int32_t b_int = (int32_t)(b * 100000);

	wr_buf[7] = (b_int & 0xff000000) >> 24;
	wr_buf[8] = (b_int & 0x00ff0000) >> 16;
	wr_buf[9] = (b_int & 0x0000ff00) >> 8;
	wr_buf[10] = (b_int & 0x000000ff);

	int32_t c_int = (int32_t)(c * 100000);

	wr_buf[11] = (c_int & 0xff000000) >> 24;
	wr_buf[12] = (c_int & 0x00ff0000) >> 16;
	wr_buf[13] = (c_int & 0x0000ff00) >> 8;
	wr_buf[14] = (c_int & 0x000000ff);

	m_i2c_write(dev, wr_buf, sizeof(wr_buf));

	return 0;
}

static int max32664_set_mode_raw(const struct device *dev)
{
	LOG_INF("MAX32664 Entering RAW mode...");
	// Enter appl mode
	m_i2c_write_cmd_3(dev, 0x01, 0x00, 0x00, MAX32664_DEFAULT_CMD_DELAY);

	// Read raw sensor data
	m_i2c_write_cmd_3(dev, 0x10, 0x00, 0x01, MAX32664_DEFAULT_CMD_DELAY);

	// Set interrupt threshold
	m_i2c_write_cmd_3(dev, 0x10, 0x01, 0x02, MAX32664_DEFAULT_CMD_DELAY);

	// Enable AFE
	m_i2c_write_cmd_3(dev, 0x44, 0x03, 0x01, MAX32664_DEFAULT_CMD_DELAY);
	k_sleep(K_MSEC(600));

	// Enable BPT estimation mode
	m_i2c_write_cmd_3(dev, 0x52, 0x04, 0x02, MAX32664_DEFAULT_CMD_DELAY);
	k_sleep(K_MSEC(600));

	// Disable AGC
	m_i2c_write_cmd_3(dev, 0x52, 0x01, 0x00, MAX32664_DEFAULT_CMD_DELAY);

	// Set MAX30101 LED1 current
	m_i2c_write_cmd_4(dev, 0x40, 0x03, 0x0C, 0x1F);
	k_sleep(K_MSEC(200));

	// Set MAX30101 LED2 current
	m_i2c_write_cmd_4(dev, 0x40, 0x03, 0x0D, 0x1F);
	k_sleep(K_MSEC(200));

	return 0;
}

static int max32664_load_calib(const struct device *dev)
{
	LOG_DBG("Loading calibration vector...\n");

	struct max32664_data *data = dev->data;

	// Load calib vector
	m_i2c_write(dev, data->calib_vector, sizeof(data->calib_vector));
	k_sleep(K_MSEC(100));

	return 0;
}

static int max32664_set_mode_bpt_est(const struct device *dev)
{
	LOG_DBG("MAX32664 Entering BPT estimation mode...");

	// Enter appl mode
	// m_i2c_write_cmd_3(dev, 0x01, 0x00, 0x00);

	// Load calib vector
	// m_i2c_write(dev, data->calib_vector, sizeof(data->calib_vector));
	//m_i2c_write(dev, m_bpt_cal_vector, sizeof(m_bpt_cal_vector));

	// Set date and time
	//m_set_date_time(dev, DEFAULT_DATE, DEFAULT_TIME);

	// Set SpO2 calibration coeffs (A, B, C)
	m_set_spo2_coeffs(dev, DEFAULT_SPO2_A, DEFAULT_SPO2_B, DEFAULT_SPO2_C);

	// Set output mode to algo + sensor data
	m_i2c_write_cmd_3(dev, 0x10, 0x00, 0x03, MAX32664_DEFAULT_CMD_DELAY);

	// Set interrupt threshold
	m_i2c_write_cmd_3(dev, 0x10, 0x01, 0x04, MAX32664_DEFAULT_CMD_DELAY);

	// Enable AGC
	m_i2c_write_cmd_3(dev, 0x52, 0x00, 0x01, MAX32664_DEFAULT_CMD_DELAY);
	k_sleep(K_MSEC(200));

	// Enable AFE
	m_i2c_write_cmd_3(dev, 0x44, 0x03, 0x01, MAX32664_DEFAULT_CMD_DELAY);
	k_sleep(K_MSEC(200));

	// Enable BPT estimation mode
	m_i2c_write_cmd_3(dev, 0x52, 0x04, 0x02, MAX32664_DEFAULT_CMD_DELAY);
	k_sleep(K_MSEC(100));

	return 0;
}

static int max32664_set_mode_bpt_cal(const struct device *dev)
{
	LOG_DBG("MAX32664 Starting BPT calibration...");

	// Output mode sensor + algo data
	m_i2c_write_cmd_3(dev, 0x10, 0x00, 0x03, MAX32664_DEFAULT_CMD_DELAY);

	// Set interrupt threshold
	m_i2c_write_cmd_3(dev, 0x10, 0x01, 0x01, MAX32664_DEFAULT_CMD_DELAY);
	k_sleep(K_MSEC(400));

	// Enable AFE
	m_i2c_write_cmd_3(dev, 0x44, 0x03, 0x01, MAX32664_DEFAULT_CMD_DELAY);
	k_sleep(K_MSEC(100));

	// Enable BPT algorithm in calibration mode
	m_i2c_write_cmd_3(dev, 0x52, 0x04, 0x01, MAX32664_DEFAULT_CMD_DELAY);

	return 0;
}

static int max32664_stop_estimation(const struct device *dev)
{
	LOG_DBG("MAX32664 Stopping BPT estimation...");

	// Disable AFE
	m_i2c_write_cmd_3(dev, 0x44, 0x03, 0x00, MAX32664_DEFAULT_CMD_DELAY);
	k_sleep(K_MSEC(40));

	// Disable BPT estimation mode
	m_i2c_write_cmd_3(dev, 0x52, 0x04, 0x00, MAX32664_DEFAULT_CMD_DELAY);
	k_sleep(K_MSEC(20));

	// Disable AGC
	m_i2c_write_cmd_3(dev, 0x52, 0x00, 0x00, MAX32664_DEFAULT_CMD_DELAY);
	k_sleep(K_MSEC(20));

	return 0;
}

int max32664_get_sample_fifo(const struct device *dev)
{
	struct max32664_data *data = dev->data;
	const struct max32664_config *config = dev->config;

	uint8_t wr_buf[2] = {0x12, 0x01};

	uint8_t hub_stat = m_read_hub_status(dev);
	if (hub_stat & MAX32664_HUB_STAT_DRDY_MASK)
	{
		int fifo_count = max32664_get_fifo_count(dev);

		if (fifo_count > 0)
		{
			int sample_len=12;
			// printk("F: %d | ", fifo_count);
			data->num_samples = fifo_count;

			if (data->op_mode == MAX32664D_OP_MODE_RAW)
			{
				sample_len = 12;
			}
			else if (data->op_mode == MAX32664D_OP_MODE_BPT)
			{
				sample_len = 23;
			}

			i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
			k_sleep(K_USEC(300));
			i2c_read_dt(&config->i2c, buf, ((sample_len * fifo_count) + 1));

			for (int i = 0; i < fifo_count; i++)
			{
				uint32_t led_ir = (uint32_t)buf[(sample_len * i) + 1] << 16;
				led_ir |= (uint32_t)buf[(sample_len * i) + 2] << 8;
				led_ir |= (uint32_t)buf[(sample_len * i) + 3];

				data->samples_led_ir[i] = led_ir;

				uint32_t led_red = (uint32_t)buf[(sample_len * i) + 4] << 16;
				led_red |= (uint32_t)buf[(sample_len * i) + 5] << 8;
				led_red |= (uint32_t)buf[(sample_len * i) + 6];

				data->samples_led_red[i] = led_red;

				// bytes 7,8,9, 10,11,12 are ignored

				if (data->op_mode == MAX32664D_OP_MODE_BPT)
				{
					data->bpt_status = buf[(sample_len * i) + 13];
					data->bpt_progress = buf[(sample_len * i) + 14];

					uint16_t bpt_hr = (uint16_t)buf[(sample_len * i) + 15] << 8;
					bpt_hr |= (uint16_t)buf[(sample_len * i) + 16];

					data->hr = (bpt_hr / 10);

					data->bpt_sys = buf[(sample_len * i) + 17];
					data->bpt_dia = buf[(sample_len * i) + 18];

					uint16_t bpt_spo2 = (uint16_t)buf[(sample_len * i) + 19] << 8;
					bpt_spo2 |= (uint16_t)buf[(sample_len * i) + 20];

					data->spo2 = (bpt_spo2 / 10);

					uint16_t spo2_r_val = (uint16_t)buf[(sample_len * i) + 21] << 8;
					spo2_r_val |= (uint16_t)buf[(sample_len * i) + 22];

					data->spo2_r_val = (spo2_r_val / 1000);
					data->hr_above_resting = buf[(sample_len * i) + 23];
				}
			}
		}
	}
	else
	{
		// printk("FIFO empty\n");
	}

	return 0;
}

static int max32664_sample_fetch(const struct device *dev,
								 enum sensor_channel chan)
{
	struct max32664_data *data = dev->data;
	data->num_samples = 0;

	return max32664_get_sample_fifo(dev);
}

static int max32664_channel_get(const struct device *dev,
								enum sensor_channel chan,
								struct sensor_value *val)
{
	struct max32664_data *data = dev->data;

	int fifo_chan;

	switch (chan)
	{
	/*case SENSOR_CHAN_PPG_RED:
		val->val1 = data->samples_led_red[0];
		break;
	case SENSOR_CHAN_PPG_IR:
		val->val1 = data->samples_led_ir[0];
		break;
	case SENSOR_CHAN_PPG_RED_2:
		val->val1 = data->samples_led_red[1];
		break;
	case SENSOR_CHAN_PPG_IR_2:
		val->val1 = data->samples_led_ir[1];
		break;
	case SENSOR_CHAN_PPG_RED_3:
		val->val1 = data->samples_led_red[2];
		break;
	case SENSOR_CHAN_PPG_IR_3:
		val->val1 = data->samples_led_ir[2];
		break;
	case SENSOR_CHAN_PPG_RED_4:
		val->val1 = data->samples_led_red[3];
		break;
	case SENSOR_CHAN_PPG_IR_4:
		val->val1 = data->samples_led_ir[3];
		break;
	case SENSOR_CHAN_PPG_RED_5:
		val->val1 = data->samples_led_red[4];
		break;
	case SENSOR_CHAN_PPG_IR_5:
		val->val1 = data->samples_led_ir[4];
		break;
	case SENSOR_CHAN_PPG_RED_6:
		val->val1 = data->samples_led_red[5];
		break;
	case SENSOR_CHAN_PPG_IR_6:
		val->val1 = data->samples_led_ir[5];
		break;
	case SENSOR_PPG_NUM_SAMPLES:
		val->val1 = data->num_samples;
		break;
	case SENSOR_CHAN_PPG_HR:
		val->val1 = data->hr;
		break;
	case SENSOR_CHAN_PPG_SPO2:
		val->val1 = data->spo2;
		break;
	case SENSOR_CHAN_PPG_SPO2_R_VAL:
		val->val1 = data->spo2_r_val;
		break;
	case SENSOR_CHAN_PPG_BPT_STATUS:
		val->val1 = data->bpt_status;
		break;
	case SENSOR_CHAN_PPG_BPT_PROGRESS:
		val->val1 = data->bpt_progress;
		break;
	case SENSOR_CHAN_PPG_BP_SYS:
		val->val1 = data->bpt_sys;
		break;
	case SENSOR_CHAN_PPG_BP_DIA:
		val->val1 = data->bpt_dia;
		break;
	case SENSOR_CHAN_PPG_HR_ABOVE_RESTING:
		val->v*/
	default:
		LOG_ERR("Unsupported sensor channel");
		return -ENOTSUP;
	}

	return 0;
}

static int max32664_attr_set(const struct device *dev,
							 enum sensor_channel chan,
							 enum sensor_attribute attr,
							 const struct sensor_value *val)
{
	struct max32664_data *data = dev->data;
	switch (attr)
	{
	case MAX32664_ATTR_OP_MODE:
		if (val->val1 == MAX32664D_OP_MODE_RAW)
		{
			max32664_set_mode_raw(dev);
			data->op_mode = MAX32664D_OP_MODE_RAW;
		}
		else if (val->val1 == MAX32664D_OP_MODE_BPT)
		{
			max32664_set_mode_bpt_est(dev);
			data->op_mode = MAX32664D_OP_MODE_BPT;
		}
		else if (val->val1 == MAX32664D_OP_MODE_BPT_CAL_START) 
		{
			max32664_set_mode_bpt_cal(dev);
			data->op_mode = MAX32664D_OP_MODE_BPT_CAL_START;
		}
		else if (val->val1 == MAX32664D_OP_MODE_BPT_CAL_GET_VECTOR)
		{
			data->op_mode = MAX32664D_OP_MODE_BPT_CAL_GET_VECTOR;
		}
		else
		{
			LOG_ERR("Unsupported sensor operation mode");
			return -ENOTSUP;
		}
		break;
	case MAX32664_ATTR_DATE_TIME:
		uint32_t m_date = (uint32_t)val->val1;
		uint32_t m_time = (uint32_t)val->val2;
		m_set_date_time(dev, m_date, m_time); // val1 = date, val2 = time
		break;
	case MAX32664_ATTR_BP_CAL:
		uint32_t m_sys = (uint32_t)val->val1;
		uint32_t m_dia = (uint32_t)val->val2;
		m_set_bp_cal_values(dev, m_sys, m_dia); // va1 = sys, val2 = dia
		break;
	case MAX32664_ATTR_LOAD_CALIB:
		max32664_load_calib(dev);
		break;
	case MAX32664_ATTR_STOP_EST:
		max32664_stop_estimation(dev);
		break;
	case MAX32664_ATTR_ENTER_BOOTLOADER:
		max32664_do_enter_bl(dev);
		break;
	default:
		LOG_ERR("Unsupported sensor attribute");
		return -ENOTSUP;
	}

	return 0;
}

static const struct sensor_driver_api max32664_driver_api = {
	.attr_set = max32664_attr_set,

	.sample_fetch = max32664_sample_fetch,
	.channel_get = max32664_channel_get,

#ifdef CONFIG_SENSOR_ASYNC_API
	.submit = max32664_submit,
	.get_decoder = max32664_get_decoder,
#endif
};

static int max32664_chip_init(const struct device *dev)
{
	const struct max32664_config *config = dev->config;
	// struct max32664_data *data = dev->data;

	if (!device_is_ready(config->i2c.bus))
	{
		LOG_ERR("Bus device is not ready");
		return -ENODEV;
	}

	gpio_pin_configure_dt(&config->reset_gpio, GPIO_OUTPUT);
	gpio_pin_configure_dt(&config->mfio_gpio, GPIO_OUTPUT);

	return max32664_do_enter_app(dev);
}

#ifdef CONFIG_PM_DEVICE
static int max32664_pm_action(const struct device *dev,
							  enum pm_device_action action)
{
	int ret = 0;

	switch (action)
	{
	case PM_DEVICE_ACTION_RESUME:
		/* Re-initialize the chip */

		break;
	case PM_DEVICE_ACTION_SUSPEND:
		/* Put the chip into sleep mode */

		break;
	default:
		return -ENOTSUP;
	}

	return ret;
}
#endif /* CONFIG_PM_DEVICE */

/*
 * Main instantiation macro, which selects the correct bus-specific
 * instantiation macros for the instance.
 */
#define MAX32664_DEFINE(inst)                                       \
	static struct max32664_data max32664_data_##inst;               \
	static const struct max32664_config max32664_config_##inst =    \
		{                                                           \
			.i2c = I2C_DT_SPEC_INST_GET(inst),                      \
			.reset_gpio = GPIO_DT_SPEC_INST_GET(inst, reset_gpios), \
			.mfio_gpio = GPIO_DT_SPEC_INST_GET(inst, mfio_gpios),   \
	};                                                              \
	PM_DEVICE_DT_INST_DEFINE(inst, max32664_pm_action);             \
	SENSOR_DEVICE_DT_INST_DEFINE(inst,                              \
								 max32664_chip_init,                \
								 PM_DEVICE_DT_INST_GET(inst),       \
								 &max32664_data_##inst,             \
								 &max32664_config_##inst,           \
								 POST_KERNEL,                       \
								 CONFIG_SENSOR_INIT_PRIORITY,       \
								 &max32664_driver_api)

DT_INST_FOREACH_STATUS_OKAY(MAX32664_DEFINE)
