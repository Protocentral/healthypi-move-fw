/*
 * Copyright (c) 2025 Protocentral Electronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT maxim_max32664

#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/drivers/gpio.h>

#include "max32664d.h"

LOG_MODULE_REGISTER(MAX32664D, CONFIG_MAX32664D_LOG_LEVEL);

// #define DEFAULT_MODE_BPT_ESTIMATION 1
#define MODE_RAW_SENSOR_DATA 2

static uint8_t buf[2048]; // 23 byte/sample * 32 samples = 736 bytes

#define DATE_TIME_VECTOR_SIZE 11
#define SPO2_CAL_COEFFS_SIZE 15

#define DEFAULT_SPO2_A 1.5958422
#define DEFAULT_SPO2_B -34.659664
#define DEFAULT_SPO2_C 112.68987

#define DEFAULT_DATE 0x5cc20200
#define DEFAULT_TIME 0xe07f0200

uint8_t m_date_time_vector[DATE_TIME_VECTOR_SIZE] = {0x50, 0x04, 0x04, 0x5c, 0xc2, 0x02, 0x00, 0xe0, 0x7f, 0x02, 0x00};

static int max32664d_write_cal_data(const struct device *dev, uint8_t *m_cal_vector);

int max32664d_set_bpt_cal_vector(const struct device *dev, uint8_t m_bpt_cal_vector[CAL_VECTOR_SIZE])
{
	const struct max32664d_config *config = dev->config;
	struct max32664d_data *data = dev->data;

	LOG_DBG("Setting BPT calibration vector");

	if (m_bpt_cal_vector == NULL)
	{
		LOG_ERR("BPT calibration vector data is NULL");
		return -EINVAL;
	}

	memcpy(data->bpt_cal_vector, m_bpt_cal_vector, CAL_VECTOR_SIZE);

	max32664d_write_cal_data(dev, data->bpt_cal_vector);
	LOG_DBG("BPT calibration data set");

	return 0;
}

int max32664d_get_bpt_cal_vector(const struct device *dev, uint8_t *m_bpt_cal_vector)
{
	struct max32664d_data *data = dev->data;

	LOG_DBG("Getting BPT calibration vector data");

	if (m_bpt_cal_vector == NULL)
	{
		LOG_ERR("BPT calibration vector data is NULL");
		return -EINVAL;
	}

	memcpy(m_bpt_cal_vector, data->bpt_cal_vector, CAL_VECTOR_SIZE);
	LOG_DBG("BPT calibration data fetched");

	return 0;
}

static int m_read_op_mode(const struct device *dev)
{
	// struct max32664d_data *data = dev->data;
	const struct max32664d_config *config = dev->config;
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

static int max32664d_write_cal_data(const struct device *dev, uint8_t *m_cal_vector)
{
	const struct max32664d_config *config = dev->config;
	uint8_t wr_buf[CAL_VECTOR_SIZE + 3];

	LOG_DBG("Writing calibration data");

	wr_buf[0] = 0x50;
	wr_buf[1] = 0x04;
	wr_buf[2] = 0x03;

	memcpy(&wr_buf[3], m_cal_vector, CAL_VECTOR_SIZE);

	i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
	k_sleep(K_MSEC(MAX32664_DEFAULT_CMD_DELAY));

	return 0;
}

static int m_read_mcu_id(const struct device *dev)
{
	const struct max32664d_config *config = dev->config;
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

uint8_t max32664d_read_hub_status(const struct device *dev)
{
	/*
	Table 7. Sensor Hub Status Byte
	BIT 7 6 5 4 3 2 1 0
	Field Reserved HostAccelUfInt FifoInOverInt FifoOutOvrInt DataRdyInt Err2 Err1 Err0
	*/
	// struct max32664_data *data = dev->data;
	const struct max32664d_config *config = dev->config;
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
	const struct max32664d_config *config = dev->config;

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

static int m_i2c_write_cmd_4(const struct device *dev, uint8_t byte1, uint8_t byte2, uint8_t byte3, uint8_t byte4)
{
	const struct max32664d_config *config = dev->config;
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

int max32664d_get_fifo_count(const struct device *dev)
{
	const struct max32664d_config *config = dev->config;
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

static int max32664_fetch_cal_vector(const struct device *dev)
{
    const struct max32664d_config *config = dev->config;
    struct max32664d_data *data = dev->data;

    static uint8_t rd_buf[1024];
    uint8_t wr_buf[3] = {0x51, 0x04, 0x03};

    i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
    k_sleep(K_USEC(300));
    i2c_read_dt(&config->i2c, rd_buf, 826);

    for (int i = 0; i < 512; i++)
    {
        data->bpt_cal_vector[i] = rd_buf[i + 2];
    }
    LOG_DBG("Calibration vector fetched\n");
	LOG_HEXDUMP_INF(data->bpt_cal_vector, 512, "BPT_CAL_VECTOR");

    data->op_mode = MAX32664D_OP_MODE_IDLE;

    return 0;
}

static int m_i2c_write_cmd_3(const struct device *dev, uint8_t byte1, uint8_t byte2, uint8_t byte3, uint16_t cmd_delay)
{
	const struct max32664d_config *config = dev->config;
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

static int m_i2c_write_cmd_6(const struct device *dev, uint8_t byte1, uint8_t byte2, uint8_t byte3, uint8_t byte4, uint8_t byte5, uint8_t byte6, uint16_t cmd_delay)
{
	const struct max32664d_config *config = dev->config;
	uint8_t wr_buf[6];

	uint8_t rd_buf[1] = {0x00};

	wr_buf[0] = byte1;
	wr_buf[1] = byte2;
	wr_buf[2] = byte3;
	wr_buf[3] = byte4;
	wr_buf[4] = byte5;
	wr_buf[5] = byte6;

	gpio_pin_set_dt(&config->mfio_gpio, 0);
	k_sleep(K_USEC(300));

	i2c_write_dt(&config->i2c, wr_buf, sizeof(wr_buf));
	k_sleep(K_MSEC(cmd_delay));

	i2c_read_dt(&config->i2c, rd_buf, sizeof(rd_buf));
	k_sleep(K_USEC(300));

	gpio_pin_set_dt(&config->mfio_gpio, 1);

	LOG_DBG("CMD: %x %x %x %x %x %x | RSP: %x", wr_buf[0], wr_buf[1], wr_buf[2], wr_buf[3], wr_buf[4], wr_buf[5], rd_buf[0]);

	k_sleep(K_MSEC(10));

	return 0;
}

static int m_i2c_write_cmd_3_rsp_3(const struct device *dev, uint8_t byte1, uint8_t byte2, uint8_t byte3, uint8_t *rsp)
{
	const struct max32664d_config *config = dev->config;
	uint8_t wr_buf[3];

	uint8_t rd_buf[3] = {0x00, 0x00, 0x00};

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

	LOG_DBG("CMD: %x %x %x | RSP: %x %x %x ", wr_buf[0], wr_buf[1], wr_buf[2], rd_buf[0], rd_buf[1], rd_buf[2]);

	memcpy(rsp, rd_buf, 3);

	k_sleep(K_MSEC(10));

	return 0;
}

static int m_i2c_write(const struct device *dev, uint8_t *wr_buf, uint32_t wr_len)
{
	const struct max32664d_config *config = dev->config;

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

int max32664d_do_enter_app(const struct device *dev)
{
	const struct max32664d_config *config = dev->config;
	struct max32664d_data *data = dev->data;

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

	if (m_get_ver(dev, data->hub_ver) == 0)
	{
		LOG_INF("Hub version: %d.%d.%d", data->hub_ver[1], data->hub_ver[2], data->hub_ver[3]);
	}
	else
	{
		// LOG_INF("MAX32664D not Found");
		return -ENODEV;
	}

	max32664d_read_hub_status(dev);
	k_sleep(K_MSEC(200));
	max32664d_read_hub_status(dev);

	return 0;
}

static void bpt_time_to_byte_le(uint32_t time, uint8_t *byte_time)
{

	byte_time[0] = (time & 0x000000FF);
	byte_time[1] = (time & 0x0000FF00) >> 8;
	byte_time[2] = (time & 0x00FF0000) >> 16;
	byte_time[3] = (time & 0xFF000000) >> 24;

	// printk("Hex Time: %x %x %x %x\n", byte_time[0], byte_time[1], byte_time[2], byte_time[3]);
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

static int max32664_set_mode_bpt_est(const struct device *dev)
{
	LOG_DBG("MAX32664 Entering BPT estimation mode...");

	// Set date and time
	//m_set_date_time(dev, DEFAULT_DATE, DEFAULT_TIME);

	// Set SpO2 calibration coeffs (A, B, C)
	m_set_spo2_coeffs(dev, DEFAULT_SPO2_A, DEFAULT_SPO2_B, DEFAULT_SPO2_C);

	// Set output mode to algo + sensor data
	m_i2c_write_cmd_3(dev, 0x10, 0x00, 0x03, MAX32664_DEFAULT_CMD_DELAY);

	// Set interrupt threshold
	m_i2c_write_cmd_3(dev, 0x10, 0x01, 0x0F, MAX32664_DEFAULT_CMD_DELAY);

	// Enable AGC
	m_i2c_write_cmd_3(dev, 0x52, 0x00, 0x01, 25);
	
	// Enable AFE
	m_i2c_write_cmd_3(dev, 0x44, 0x03, 0x01, 50);
	k_sleep(K_MSEC(200));

	// Enable BPT estimation mode
	m_i2c_write_cmd_3(dev, 0x52, 0x04, 0x02, 600);
	k_sleep(K_MSEC(125));

	return 0;
}

static int max32664d_get_afe_sensor_id(const struct device *dev)
{
	uint8_t rsp[3] = {0x00, 0x00, 0x00};
	m_i2c_write_cmd_3_rsp_3(dev, 0x41, 0x03, 0xFF, rsp);

	LOG_DBG("AFE Sensor ID: %x %x %x", rsp[0], rsp[1], rsp[2]);

	return rsp[1];
}

static int max32664_bpt_cal_start(const struct device *dev)
{
	LOG_DBG("MAX32664 Starting BPT calibration...");

	struct max32664d_data *data = dev->data;

	// Output mode sensor + algo data
	m_i2c_write_cmd_3(dev, 0x10, 0x00, 0x03, MAX32664_DEFAULT_CMD_DELAY);

	// Set interrupt threshold
	m_i2c_write_cmd_3(dev, 0x10, 0x01, 0x01, MAX32664_DEFAULT_CMD_DELAY);
	k_sleep(K_MSEC(400));

	// Enable AGC
	m_i2c_write_cmd_3(dev, 0x52, 0x00, 0x01, 25);

	// Enable AFE
	m_i2c_write_cmd_3(dev, 0x44, 0x03, 0x01, 45);
	k_sleep(K_MSEC(100));

	// Set Cal Index and Sys/Dia values
	m_i2c_write_cmd_6(dev, 0x50, 0x04, 0x07, data->curr_cal_index, data->curr_cal_sys, data->curr_cal_dia, MAX32664_DEFAULT_CMD_DELAY);

	// Enable BPT algorithm in calibration mode
	m_i2c_write_cmd_3(dev, 0x52, 0x04, 0x01, 550);

	k_msleep(120);

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
	struct max32664d_data *data = dev->data;
	const struct max32664d_config *config = dev->config;

	uint8_t wr_buf[2] = {0x12, 0x01};

	uint8_t hub_stat = max32664d_read_hub_status(dev);
	if (hub_stat & MAX32664D_HUB_STAT_DRDY_MASK)
	{
		int fifo_count = max32664d_get_fifo_count(dev);

		if (fifo_count > 0)
		{
			int sample_len = 12;
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
	struct max32664d_data *data = dev->data;
	data->num_samples = 0;

	// Sensor Get/Fetch is not implemented

	return max32664_get_sample_fifo(dev);
}

static int max32664_channel_get(const struct device *dev,
								enum sensor_channel chan,
								struct sensor_value *val)
{
	struct max32664d_data *data = dev->data;
	int fifo_chan;

	// Sensor Get/Fetch is not implemented
	switch (chan)
	{
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
	struct max32664d_data *data = dev->data;
	switch (attr)
	{

		break;
	case MAX32664D_ATTR_OP_MODE:
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
			max32664_bpt_cal_start(dev);
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
	case MAX32664D_ATTR_CAL_SET_CURR_INDEX:
		if (val->val1 < MAX32664D_MAX_CAL_INDEX)
		{
			data->curr_cal_index = val->val1;
		}
		else
		{
			LOG_ERR("Invalid calibration index");
			return -EINVAL;
		}
		break;
	case MAX32664D_ATTR_CAL_FETCH_VECTOR:
		max32664_fetch_cal_vector(dev);
		break;
	case MAX32664D_ATTR_CAL_SET_CURR_SYS:
		data->curr_cal_sys = val->val1;
		break;
	case MAX32664D_ATTR_CAL_SET_CURR_DIA:
		data->curr_cal_dia = val->val1;
		break;
	case MAX32664D_ATTR_SET_DATE_TIME:
		uint32_t m_date = (uint32_t)val->val1;
		uint32_t m_time = (uint32_t)val->val2;
		m_set_date_time(dev, m_date, m_time); // val1 = date, val2 = time
		break;
	case MAX32664D_ATTR_LOAD_CALIB:
		// max32664_load_calib(dev);
		break;
	case MAX32664D_ATTR_STOP_EST:
		max32664_stop_estimation(dev);
		break;
	default:
		LOG_ERR("Unsupported sensor attribute");
		return -ENOTSUP;
	}

	return 0;
}

static int max32664d_attr_get(const struct device *dev,
							  enum sensor_channel chan,
							  enum sensor_attribute attr,
							  struct sensor_value *val)
{
	struct max32664d_data *data = dev->data;

	switch (attr)
	{
	case MAX32664D_ATTR_SENSOR_ID:
		val->val1 = max32664d_get_afe_sensor_id(dev);
		break;
	case MAX32664D_ATTR_APP_VER:
		val->val1 = data->hub_ver[2];
		val->val2 = data->hub_ver[3];
	}
}

static const struct sensor_driver_api max32664_driver_api = {
	.attr_set = max32664_attr_set,
	.attr_get = max32664d_attr_get,

	.sample_fetch = max32664_sample_fetch,
	.channel_get = max32664_channel_get,

#ifdef CONFIG_SENSOR_ASYNC_API
	.submit = max32664d_submit,
	.get_decoder = max32664_get_decoder,
#endif
};

static int max32664_chip_init(const struct device *dev)
{
	const struct max32664d_config *config = dev->config;
	// struct max32664d_data *data = dev->data;

	if (!device_is_ready(config->i2c.bus))
	{
		LOG_ERR("Bus device is not ready");
		return -ENODEV;
	}

	gpio_pin_configure_dt(&config->reset_gpio, GPIO_OUTPUT);
	gpio_pin_configure_dt(&config->mfio_gpio, GPIO_OUTPUT);

	return max32664d_do_enter_app(dev);
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


#define MAX32664_DEFINE(inst)                                       \
	static struct max32664d_data max32664_data_##inst;              \
	static const struct max32664d_config max32664d_config_##inst =  \
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
								 &max32664d_config_##inst,          \
								 POST_KERNEL,                       \
								 CONFIG_SENSOR_INIT_PRIORITY,       \
								 &max32664_driver_api)

DT_INST_FOREACH_STATUS_OKAY(MAX32664_DEFINE)
