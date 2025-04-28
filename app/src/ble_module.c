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

#include <zephyr/settings/settings.h>
#include <app_version.h>
#include "cmd_module.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
LOG_MODULE_REGISTER(ble_module);

struct bt_conn *current_conn;

static uint8_t spo2_att_ble[5];
static uint8_t temp_att_ble[2];

// BLE GATT Identifiers

#define HPI_SPO2_SERVICE BT_UUID_DECLARE_16(BT_UUID_POS_VAL)
#define HPI_SPO2_CHAR BT_UUID_DECLARE_16(BT_UUID_GATT_PLX_SCM_VAL)

#define HPI_TEMP_SERVICE BT_UUID_DECLARE_16(BT_UUID_HTS_VAL)
#define HPI_TEMP_CHAR BT_UUID_DECLARE_16(BT_UUID_TEMPERATURE_VAL)

// ECG/Resp Service 00001122-0000-1000-8000-00805f9b34fb
#define UUID_HPI_ECG_RESP_SERV BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x00001122, 0x0000, 0x1000, 0x8000, 0x00805f9b34fb))

// ECG Characteristic 00001424-0000-1000-8000-00805f9b34fb
#define UUID_HPI_ECG_CHAR BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x00001424, 0x0000, 0x1000, 0x8000, 0x00805f9b34fb))

// RESP Characteristic babe4a4c-7789-11ed-a1eb-0242ac120002
#define UUID_HPI_RESP_CHAR BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0xbabe4a4c, 0x7789, 0x11ed, 0xa1eb, 0x0242ac120002))

// PPG Service cd5c7491-4448-7db8-ae4c-d1da8cba36d0
#define UUID_HPI_PPG_SERV BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0xcd5c7491, 0x4448, 0x7db8, 0xae4c, 0xd1da8cba36d0))

// PPG Characteristic cd5c1525-4448-7db8-ae4c-d1da8cba36d0
#define UUID_HPI_PPG_CHAR BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0xcd5c1525, 0x4448, 0x7db8, 0xae4c, 0xd1da8cba36d0))

// Resp rate Characteristic cd5ca86f-4448-7db8-ae4c-d1da8cba36d0
#define UUID_HPI_RESP_RATE_CHAR BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0xcd5ca86f, 0x4448, 0x7db8, 0xae4c, 0xd1da8cba36d0))

#define CMD_SERVICE_UUID 0xdc, 0xad, 0x7f, 0xc4, 0x23, 0x90, 0x4d, 0xd4, \
						 0x96, 0x8d, 0x0f, 0x97, 0x92, 0x74, 0xbf, 0x01

#define CMD_TX_CHARACTERISTIC_UUID 0xdc, 0xad, 0x7f, 0xc4, 0x23, 0x90, 0x4d, 0xd4, \
								   0x96, 0x8d, 0x0f, 0x97, 0x28, 0x15, 0xbf, 0x01

#define CMD_RX_CHARACTERISTIC_UUID 0xdc, 0xad, 0x7f, 0xc4, 0x23, 0x90, 0x4d, 0xd4, \
								   0x96, 0x8d, 0x0f, 0x97, 0x27, 0x15, 0xbf, 0x01

#define UUID_HPI_CMD_SERVICE BT_UUID_DECLARE_128(CMD_SERVICE_UUID)
#define UUID_HPI_CMD_SERVICE_CHAR_TX BT_UUID_DECLARE_128(CMD_TX_CHARACTERISTIC_UUID)
#define UUID_HPI_CMD_SERVICE_CHAR_RX BT_UUID_DECLARE_128(CMD_RX_CHARACTERISTIC_UUID)

extern struct k_msgq q_cmd_msg;

static void spo2_on_cccd_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
}

static void temp_on_cccd_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
}

static void ecg_resp_on_cccd_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	switch (value)
	{
	case BT_GATT_CCC_NOTIFY:
		LOG_DBG("ECG/RESP CCCD subscribed");
		break;

	case BT_GATT_CCC_INDICATE:
		// Start sending stuff via indications
		break;

	case 0:
		LOG_DBG("ECG/RESP CCCD unsubscribed");
		break;

	default:
		LOG_DBG("Error, CCCD has been set to an invalid value");
	}
}

uint8_t in_data_buffer[50];

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL,
				  BT_UUID_16_ENCODE(BT_UUID_HRS_VAL),
				  // BT_UUID_16_ENCODE(BT_UUID_POS_VAL)
				  BT_UUID_16_ENCODE(BT_UUID_BAS_VAL),
				  BT_UUID_16_ENCODE(BT_UUID_DIS_VAL))};

BT_GATT_SERVICE_DEFINE(hpi_spo2_service,
					   BT_GATT_PRIMARY_SERVICE(HPI_SPO2_SERVICE),
					   BT_GATT_CHARACTERISTIC(HPI_SPO2_CHAR,
											  BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
											  BT_GATT_PERM_READ,
											  NULL, NULL, NULL),
					   BT_GATT_CCC(spo2_on_cccd_changed,
								   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE), );

BT_GATT_SERVICE_DEFINE(hpi_temp_service,
					   BT_GATT_PRIMARY_SERVICE(HPI_TEMP_SERVICE),
					   BT_GATT_CHARACTERISTIC(HPI_TEMP_CHAR,
											  BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
											  BT_GATT_PERM_READ,
											  NULL, NULL, NULL),
					   BT_GATT_CCC(temp_on_cccd_changed,
								   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE), );

BT_GATT_SERVICE_DEFINE(hpi_ecg_resp_service,
					   // BT_GATT_PRIMARY_SERVICE(&hpi_ecg_resp_serv_uuid.uuid),
					   BT_GATT_PRIMARY_SERVICE(UUID_HPI_ECG_RESP_SERV),
					   BT_GATT_CHARACTERISTIC(UUID_HPI_ECG_CHAR,
											  BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
											  BT_GATT_PERM_READ,
											  NULL, NULL, NULL),
					   BT_GATT_CCC(ecg_resp_on_cccd_changed,
								   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
					   BT_GATT_CHARACTERISTIC(UUID_HPI_RESP_CHAR,
											  BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
											  BT_GATT_PERM_READ,
											  NULL, NULL, NULL),
					   BT_GATT_CCC(ecg_resp_on_cccd_changed,
								   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE), );

BT_GATT_SERVICE_DEFINE(hpi_ppg_resp_service,
					   BT_GATT_PRIMARY_SERVICE(UUID_HPI_PPG_SERV),
					   BT_GATT_CHARACTERISTIC(UUID_HPI_PPG_CHAR,
											  BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
											  BT_GATT_PERM_READ,
											  NULL, NULL, NULL),
					   BT_GATT_CCC(spo2_on_cccd_changed,
								   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
					   BT_GATT_CHARACTERISTIC(UUID_HPI_RESP_RATE_CHAR,
											  BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
											  BT_GATT_PERM_READ,
											  NULL, NULL, NULL),
					   BT_GATT_CCC(ecg_resp_on_cccd_changed,
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

	printk("Received CMD len %d \n", len);

	for (uint8_t i = 0; i < len; i++)
	{
		in_data_buffer[i] = buffer[i];
		printk("%02X", buffer[i]);
	}
	printk("\n");

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
											  BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
											  NULL, on_receive_cmd, NULL),
					   BT_GATT_CHARACTERISTIC(UUID_HPI_CMD_SERVICE_CHAR_RX,
											  BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_WRITE_WITHOUT_RESP | BT_GATT_CHRC_READ,
											  BT_GATT_PERM_READ,
											  NULL, NULL, NULL),
					   BT_GATT_CCC(cmd_on_cccd_changed,
								   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE), );

void hpi_ble_send_data(const uint8_t *data, uint16_t len)
{

	const struct bt_gatt_attr *attr = &hpi_cmd_service.attrs[4];

	// printk("Sending data len %d \n", len);

	bt_gatt_notify(NULL, attr, data, len);
}
void ble_spo2_notify(uint16_t spo2_val)
{
	spo2_att_ble[0] = 0x00;
	spo2_att_ble[1] = (uint8_t)spo2_val;
	spo2_att_ble[2] = (uint8_t)(spo2_val >> 8);
	spo2_att_ble[3] = 0;
	spo2_att_ble[4] = 0;

	bt_gatt_notify(NULL, &hpi_spo2_service.attrs[1], &spo2_att_ble, sizeof(spo2_att_ble));
}

void ble_resp_rate_notify(uint16_t resp_rate)
{
	bt_gatt_notify(NULL, &hpi_ppg_resp_service.attrs[4], &resp_rate, sizeof(resp_rate));
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

	bt_gatt_notify(NULL, &hpi_ecg_resp_service.attrs[1], &out_data, len * 4);
}

void ble_bioz_notify(int32_t *resp_data, uint8_t len)
{
	uint8_t out_data[128];

	for (int i = 0; i < len; i++)
	{
		out_data[i * 4] = (uint8_t)resp_data[i];
		out_data[i * 4 + 1] = (uint8_t)(resp_data[i] >> 8);
		out_data[i * 4 + 2] = (uint8_t)(resp_data[i] >> 16);
		out_data[i * 4 + 3] = (uint8_t)(resp_data[i] >> 24);
	}

	bt_gatt_notify(NULL, &hpi_ecg_resp_service.attrs[4], &out_data, len * 4);
}

void ble_ppg_notify(int16_t *ppg_data, uint8_t len)
{
	uint8_t out_data[128];

	for (int i = 0; i < len; i++)
	{
		out_data[i * 2] = (uint8_t)ppg_data[i];
		out_data[i * 2 + 1] = (uint8_t)(ppg_data[i] >> 8);
	}

	bt_gatt_notify(NULL, &hpi_ppg_resp_service.attrs[1], &out_data, len * 2);
}

void ble_temp_notify(uint16_t temp_val)
{
	temp_val = temp_val / 10;
	temp_att_ble[0] = (uint8_t)temp_val;
	temp_att_ble[1] = (uint8_t)(temp_val >> 8);

	bt_gatt_notify(NULL, &hpi_temp_service.attrs[2], &temp_att_ble, sizeof(temp_att_ble));
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
	if (err)
	{
		LOG_ERR("Connection failed (err 0x%02x)", err);
	}
	else
	{
		LOG_DBG("Connected");
		// send_status_serial(BLE_STATUS_CONNECTED);
		current_conn = bt_conn_ref(conn);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_DBG("Disconnected (reason 0x%02x)", reason);
	// send_status_serial(BLE_STATUS_DISCONNECTED);
	if (current_conn)
	{
		bt_conn_unref(current_conn);
		current_conn = NULL;
	}
}

/*static void security_changed(struct bt_conn *conn, bt_security_t level,
							 enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err)
	{
		printk("Security changed: %s level %u\n", addr, level);
	}
	else
	{
		printk("Security failed: %s level %u err %d\n", addr, level,
			   err);
	}
}*/

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	//.security_changed = security_changed,
};

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	char passkey_str[7];
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	snprintk(passkey_str, ARRAY_SIZE(passkey_str), "%06u", passkey);

	LOG_DBG("Passkey for %s: %s", addr, passkey_str);
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_DBG("Pairing cancelled: %s\n", addr);
}

static struct bt_conn_auth_cb auth_cb_display = {
	.passkey_display = auth_passkey_display,
	.passkey_entry = NULL,
	.cancel = auth_cancel,
};

void remove_separators(char *str)
{
	char *pr = str, *pw = str;
	char c = ':';
	while (*pr)
	{
		*pw = *pr++;
		pw += (*pw != c);
	}
	*pw = '\0';
}

/*
/* Runtime settings override.
static int settings_runtime_load(void)
{
#if defined(CONFIG_BT_DIS_FW_REV)
	settings_runtime_set("bt/dis/fw", APP_VERSION_STRING , sizeof(APP_VERSION_STRING));
#endif
	return 0;
}*/


void ble_module_init()
{
	int err = 0;

	err = bt_enable(NULL);
	if (err)
	{
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return;
	}
	LOG_DBG("Bluetooth init !");

	if (IS_ENABLED(CONFIG_SETTINGS))
	{
		// settings_load();
	}

	//bt_conn_auth_cb_register(&auth_cb_display);

	err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err)
	{
		LOG_ERR("Advertising failed to start (err %d)\n", err);
		return;
	}
	LOG_INF("Advertising successfully started");
}