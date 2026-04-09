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


#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/services/hrs.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/zbus/zbus.h>

#include <zephyr/settings/settings.h>
#include <app_version.h>

#include "cmd_module.h"
#include "hpi_common_types.h"
#include "ble_module.h"
#include "ui/move_ui.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
LOG_MODULE_REGISTER(ble_module, LOG_LEVEL_DBG);

struct bt_conn *current_conn;

// BLE GATT Identifiers

#define HPI_SPO2_SERVICE BT_UUID_DECLARE_16(BT_UUID_POS_VAL)
#define HPI_SPO2_CHAR BT_UUID_DECLARE_16(BT_UUID_GATT_PLX_SCM_VAL)

#define HPI_TEMP_SERVICE BT_UUID_DECLARE_16(BT_UUID_HTS_VAL)
#define HPI_TEMP_CHAR BT_UUID_DECLARE_16(BT_UUID_TEMPERATURE_VAL)

// ECG/GSR Service 00001122-0000-1000-8000-00805f9b34fb
#define UUID_HPI_ECG_GSR_SERV BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x00001122, 0x0000, 0x1000, 0x8000, 0x00805f9b34fb))

// ECG Characteristic 00001424-0000-1000-8000-00805f9b34fb
#define UUID_HPI_ECG_CHAR BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x00001424, 0x0000, 0x1000, 0x8000, 0x00805f9b34fb))

// GSR Characteristic babe4a4c-7789-11ed-a1eb-0242ac120002
#define UUID_HPI_GSR_CHAR BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0xbabe4a4c, 0x7789, 0x11ed, 0xa1eb, 0x0242ac120002))

// PPG Service cd5c7491-4448-7db8-ae4c-d1da8cba36d0
#define UUID_HPI_PPG_SERV BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0xcd5c7491, 0x4448, 0x7db8, 0xae4c, 0xd1da8cba36d0))

// PPG Wrist Characteristic cd5c1525-4448-7db8-ae4c-d1da8cba36d0
#define UUID_HPI_PPG_WR_CHAR BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0xcd5c1525, 0x4448, 0x7db8, 0xae4c, 0xd1da8cba36d0))

// PPG Finger Characteristic cd5ca86f-4448-7db8-ae4c-d1da8cba36d0
#define UUID_HPI_PPG_FI_CHAR BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0xcd5ca86f, 0x4448, 0x7db8, 0xae4c, 0xd1da8cba36d0))

#define CMD_SERVICE_UUID 0xdc, 0xad, 0x7f, 0xc4, 0x23, 0x90, 0x4d, 0xd4, \
						 0x96, 0x8d, 0x0f, 0x97, 0x92, 0x74, 0xbf, 0x01

#define CMD_TX_CHARACTERISTIC_UUID 0xdc, 0xad, 0x7f, 0xc4, 0x23, 0x90, 0x4d, 0xd4, \
								   0x96, 0x8d, 0x0f, 0x97, 0x28, 0x15, 0xbf, 0x01

#define CMD_RX_CHARACTERISTIC_UUID 0xdc, 0xad, 0x7f, 0xc4, 0x23, 0x90, 0x4d, 0xd4, \
								   0x96, 0x8d, 0x0f, 0x97, 0x27, 0x15, 0xbf, 0x01

#define UUID_HPI_CMD_SERVICE BT_UUID_DECLARE_128(CMD_SERVICE_UUID)
#define UUID_HPI_CMD_SERVICE_CHAR_TX BT_UUID_DECLARE_128(CMD_TX_CHARACTERISTIC_UUID)
#define UUID_HPI_CMD_SERVICE_CHAR_RX BT_UUID_DECLARE_128(CMD_RX_CHARACTERISTIC_UUID)

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL,
				  BT_UUID_16_ENCODE(BT_UUID_HRS_VAL),
				  BT_UUID_16_ENCODE(BT_UUID_BAS_VAL),
				  BT_UUID_16_ENCODE(BT_UUID_DIS_VAL))};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

extern struct k_msgq q_cmd_msg;
extern struct k_sem sem_ble_thread_start;

static void spo2_on_cccd_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
}

static void temp_on_cccd_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
}

static void ppg_fi_on_cccd_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	switch (value)
	{
	case BT_GATT_CCC_NOTIFY:
		LOG_DBG("PPG Finger CCCD subscribed");
		break;
	case BT_GATT_CCC_INDICATE:
		// Start sending stuff via indications
		break;
	case 0:
		LOG_DBG("PPG Finger CCCD unsubscribed");
		break;
	default:
		LOG_DBG("Error, CCCD has been set to an invalid value");
	}
}

static void ecg_on_cccd_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	switch (value)
	{
	case BT_GATT_CCC_NOTIFY:
		LOG_DBG("ECG CCCD subscribed");
		break;
	case BT_GATT_CCC_INDICATE:
		// Start sending stuff via indications
		break;
	case 0:
		LOG_DBG("ECG CCCD unsubscribed");
		break;
	default:
		LOG_DBG("Error, CCCD has been set to an invalid value");
	}
}

static void gsr_on_cccd_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	switch (value)
	{
	case BT_GATT_CCC_NOTIFY:
		
		break;
	case BT_GATT_CCC_INDICATE:
		// Start sending stuff via indications
		LOG_DBG("GSR CCCD subscribed");
		break;
	case 0:
		LOG_DBG("GSR CCCD unsubscribed");
		break;
	default:
		LOG_DBG("Error, CCCD has been set to an invalid value");
	}
}

static void ppg_wr_on_cccd_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	switch (value)
	{
	case BT_GATT_CCC_NOTIFY:
		LOG_DBG("PPG Wrist CCCD subscribed");
		break;
	case BT_GATT_CCC_INDICATE:
		// Start sending stuff via indications
		break;
	case 0:
		LOG_DBG("PPG Wrist CCCD unsubscribed");
		break;
	default:
		LOG_DBG("Error, CCCD has been set to an invalid value");
	}
}

uint8_t in_data_buffer[50];

BT_GATT_SERVICE_DEFINE(hpi_spo2_service,
					   BT_GATT_PRIMARY_SERVICE(HPI_SPO2_SERVICE),
					   BT_GATT_CHARACTERISTIC(HPI_SPO2_CHAR,
											  BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
											  BT_GATT_PERM_READ_ENCRYPT,
											  NULL, NULL, NULL),
					   BT_GATT_CCC(spo2_on_cccd_changed,
								   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE), );

BT_GATT_SERVICE_DEFINE(hpi_temp_service,
					   BT_GATT_PRIMARY_SERVICE(HPI_TEMP_SERVICE),
					   BT_GATT_CHARACTERISTIC(HPI_TEMP_CHAR,
											  BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
											  BT_GATT_PERM_READ_ENCRYPT,
											  NULL, NULL, NULL),
					   BT_GATT_CCC(temp_on_cccd_changed,
								   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE), );

BT_GATT_SERVICE_DEFINE(hpi_ppg_service,
					   BT_GATT_PRIMARY_SERVICE(UUID_HPI_PPG_SERV),
					   BT_GATT_CHARACTERISTIC(UUID_HPI_PPG_WR_CHAR,
											  BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
											  BT_GATT_PERM_READ,
											  NULL, NULL, NULL),
					   BT_GATT_CCC(ppg_wr_on_cccd_changed,
								   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
					   BT_GATT_CHARACTERISTIC(UUID_HPI_PPG_FI_CHAR,
											  BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
											  BT_GATT_PERM_READ,
											  NULL, NULL, NULL),
					   BT_GATT_CCC(ppg_fi_on_cccd_changed,
								   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE), );

BT_GATT_SERVICE_DEFINE(hpi_ecg_gsr_service,
					   BT_GATT_PRIMARY_SERVICE(UUID_HPI_ECG_GSR_SERV),
					   BT_GATT_CHARACTERISTIC(UUID_HPI_ECG_CHAR,
											  BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
											  BT_GATT_PERM_READ,
											  NULL, NULL, NULL),
					   BT_GATT_CCC(ecg_on_cccd_changed,
								   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
					   BT_GATT_CHARACTERISTIC(UUID_HPI_GSR_CHAR,
											  BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
											  BT_GATT_PERM_READ,
											  NULL, NULL, NULL),
					   BT_GATT_CCC(gsr_on_cccd_changed,
								   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE), );

/* This function is called whenever the RX Characteristic has been written to by a Client */
static ssize_t on_receive_cmd(struct bt_conn *conn,
							  const struct bt_gatt_attr *attr,
							  const void *buf,
							  uint16_t len,
							  uint16_t offset,
							  uint8_t flags)
{
	const uint8_t *buffer = buf;

	// LOG_DBG("Received CMD len %d \n", len);

	/*for (uint8_t i = 0; i < len; i++)
	{
		in_data_buffer[i] = buffer[i];
		printk("%02X", buffer[i]);
	}
	printk("\n");
	*/

	struct hpi_cmd_data_obj_t cmd_data_obj;
	cmd_data_obj.pkt_type = 0x00;
	cmd_data_obj.data_len = len;
	memcpy(cmd_data_obj.data, buffer, len);

	k_msgq_put(&q_cmd_msg, &cmd_data_obj, K_MSEC(100));

	return len;
}

static void cmd_on_cccd_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	switch (value)
	{
	case BT_GATT_CCC_NOTIFY:
		LOG_DBG("CMD RX/TX CCCD subscribed");
		break;

	case BT_GATT_CCC_INDICATE:
		// Start sending stuff via indications
		break;

	case 0:
		LOG_DBG("CMD RX/TX CCCD unsubscribed");
		break;

	default:
		LOG_DBG("Error, CCCD has been set to an invalid value");
	}
}

BT_GATT_SERVICE_DEFINE(hpi_cmd_service,
					   BT_GATT_PRIMARY_SERVICE(UUID_HPI_CMD_SERVICE),
					   BT_GATT_CHARACTERISTIC(UUID_HPI_CMD_SERVICE_CHAR_TX,
											  BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP | BT_GATT_CHRC_READ,
											  BT_GATT_PERM_READ_AUTHEN | BT_GATT_PERM_WRITE_AUTHEN,
											  NULL, on_receive_cmd, NULL),
					   BT_GATT_CHARACTERISTIC(UUID_HPI_CMD_SERVICE_CHAR_RX,
											  BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_WRITE_WITHOUT_RESP | BT_GATT_CHRC_READ,
											  BT_GATT_PERM_READ_AUTHEN | BT_GATT_PERM_WRITE_AUTHEN,
											  NULL, NULL, NULL),
					   BT_GATT_CCC(cmd_on_cccd_changed,
								   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE), );

void hpi_ble_send_data(const uint8_t *data, uint16_t len)
{

	const struct bt_gatt_attr *attr = &hpi_cmd_service.attrs[4];

	// printk("Sending data len %d \n", len);

	bt_gatt_notify(NULL, attr, data, len);
}

void ble_ppg_notify_wr(uint32_t *ppg_data, uint8_t len)
{
	uint8_t out_data[128];

	for (int i = 0; i < len; i++)
	{
		out_data[i * 4] = (uint8_t)ppg_data[i];
		out_data[i * 4 + 1] = (uint8_t)(ppg_data[i] >> 8);
		out_data[i * 4 + 2] = (uint8_t)(ppg_data[i] >> 16);
		out_data[i * 4 + 3] = (uint8_t)(ppg_data[i] >> 24);
	}

	// LOG_DBG("PPG Not len %d", len);

	bt_gatt_notify(NULL, &hpi_ppg_service.attrs[2], &out_data, len * 4);
}

void ble_ppg_notify_fi(uint32_t *ppg_data, uint8_t len)
{
	uint8_t out_data[128];

	for (int i = 0; i < len; i++)
	{
		out_data[i * 4] = (uint8_t)ppg_data[i];
		out_data[i * 4 + 1] = (uint8_t)(ppg_data[i] >> 8);
		out_data[i * 4 + 2] = (uint8_t)(ppg_data[i] >> 16);
		out_data[i * 4 + 3] = (uint8_t)(ppg_data[i] >> 24);
	}

	// LOG_DBG("PPG Not len %d", len);

	bt_gatt_notify(NULL, &hpi_ppg_service.attrs[4], &out_data, len * 4);
}

void ble_ecg_notify(int32_t *ecg_data, uint8_t len)
{ 
	uint8_t out_data[128];
	
	for (int i = 0; i < len; i++)
	{
		out_data[i * 4] = (uint8_t)ecg_data[i];
		out_data[i * 4 + 1] = (uint8_t)(ecg_data[i] >> 8);
		out_data[i * 4 + 2] = (uint8_t)(ecg_data[i] >> 16);
		out_data[i * 4 + 3] = (uint8_t)(ecg_data[i] >> 24);
	}

	// LOG_DBG("ECG Not len %d", len);

	bt_gatt_notify(NULL, &hpi_ecg_gsr_service.attrs[2], &out_data, len * 4);
}

void ble_gsr_notify(int32_t *gsr_data, uint8_t len)
{
	uint8_t out_data[128];
	
	for (int i = 0; i < len; i++)
    {
        out_data[i * 4]     = (uint8_t)gsr_data[i];
        out_data[i * 4 + 1] = (uint8_t)(gsr_data[i] >> 8);
        out_data[i * 4 + 2] = (uint8_t)(gsr_data[i] >> 16);
        out_data[i * 4 + 3] = (uint8_t)(gsr_data[i] >> 24);
    }

	//LOG_DBG("GSR Not len %d", len);

	bt_gatt_notify(NULL, &hpi_ecg_gsr_service.attrs[4], &out_data, len * 4);
	
}

void ble_bpt_cal_progress_notify(uint8_t bpt_status, uint8_t bpt_progress)
{
	uint8_t out_data[3];

	out_data[0] = bpt_status;
	out_data[1] = bpt_progress;
	out_data[2] = 0x00;

	bt_gatt_notify(NULL, &hpi_cmd_service.attrs[4], &out_data, sizeof(out_data));
}

void ble_hrs_notify(uint16_t hr_val)
{
	bt_hrs_notify(hr_val);
}

void ble_bas_notify(uint8_t batt_level)
{
	bt_bas_set_battery_level(batt_level);
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err)
	{
		LOG_ERR("Failed to connect to %s, err 0x%02x %s\n", addr,
				err, bt_hci_err_to_str(err));
		return;
	}

	LOG_INF("Connected to %s\n", addr);

	if (bt_conn_set_security(conn, BT_SECURITY_L2))
	{
		LOG_ERR("Failed to set security\n");
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Disconnected from %s, reason 0x%02x %s\n", addr,
			reason, bt_hci_err_to_str(reason));
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
							 enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err)
	{
		LOG_INF("Security changed: %s level %u\n", addr, level);
	}
	else
	{
		LOG_ERR("Security failed: %s level %u err %s(%d)\n", addr, level,
			   bt_security_err_to_str(err), err);
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
};

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Passkey for %s: %06u\n", addr, passkey);
	hpi_load_scr_spl(SCR_SPL_BLE, SCROLL_NONE, HPI_BLE_EVENT_PAIR_REQUEST, passkey, 0, 0);
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Pairing cancelled: %s\n", addr);
	hpi_load_scr_spl(SCR_SPL_BLE, SCROLL_NONE, HPI_BLE_EVENT_PAIR_CANCELLED, 0, 0, 0);
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Pairing completed: %s, bonded: %d\n", addr, bonded);
	hpi_load_scr_spl(SCR_SPL_BLE, SCROLL_NONE, HPI_BLE_EVENT_PAIR_SUCCESS, bonded ? 1 : 0, 0, 0);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_ERR("Pairing failed conn: %s, reason %d %s\n", addr, reason,
			bt_security_err_to_str(reason));
	hpi_load_scr_spl(SCR_SPL_BLE, SCROLL_NONE, HPI_BLE_EVENT_PAIR_FAILED, reason, 0, 0);
}

static struct bt_conn_auth_cb conn_auth_callbacks = {
	.passkey_display = auth_passkey_display,
	.cancel = auth_cancel,
};

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

void ble_module_init()
{
	int err = 0;

	err = bt_enable(NULL);
	if (err)
	{
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return;
	}

	settings_load();

	err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err)
	{
		LOG_ERR("Advertising failed to start (err %d)\n", err);
		return;
	}
	else
	{
		LOG_INF("Advertising successfully started");
	}

	bt_conn_auth_cb_register(&conn_auth_callbacks);
	bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);

	LOG_DBG("Bluetooth init !");
}

static uint8_t m_ble_prev_progress = 0;
static uint8_t m_ble_prev_status = 0;

static void ble_bpt_listener(const struct zbus_channel *chan)
{
	const struct hpi_bpt_t *hpi_bpt = zbus_chan_const_msg(chan);
	if (hpi_bpt->progress == m_ble_prev_progress &&
		hpi_bpt->status == m_ble_prev_status)
	{
		return;
	}
	m_ble_prev_progress = hpi_bpt->progress;
	m_ble_prev_status = hpi_bpt->status;

	ble_bpt_cal_progress_notify(hpi_bpt->status, hpi_bpt->progress);
	LOG_DBG("ZB BPT Status: %d Progress: %d\n", hpi_bpt->status, hpi_bpt->progress);
}
ZBUS_LISTENER_DEFINE(ble_bpt_lis, ble_bpt_listener);

void ble_thread(void)
{
	k_sem_take(&sem_ble_thread_start, K_FOREVER);

	LOG_INF("BLE Thread started");

	for (;;)
	{
		k_sleep(K_MSEC(1000));
	}
}

#define BLE_THREAD_STACKSIZE 1024
#define BLE_THREAD_PRIORITY 7

K_THREAD_DEFINE(ble_thread_id, BLE_THREAD_STACKSIZE, ble_thread, NULL, NULL, NULL, BLE_THREAD_PRIORITY, 0, 1000);
