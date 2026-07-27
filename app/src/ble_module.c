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
#include <zephyr/sys/byteorder.h>
#include <app_version.h>
#include <time.h>

#include "hpi_common_types.h"
#include "hpi_sys.h"
#include "hpi_dfu.h"
#include "ble_module.h"
#include "ui/move_ui.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
LOG_MODULE_REGISTER(ble_module, LOG_LEVEL_DBG);

/* Live link state for the Settings "Bluetooth" row (A4). Maintained from the BT
 * RX thread (connected/disconnected callbacks) and read from the LVGL thread, so
 * it is a counter and not a bt_conn pointer the reader could dereference after
 * the stack freed it. The old `current_conn` global was never assigned. */
static atomic_t ble_conn_count;

bool hpi_ble_is_connected(void)
{
	return atomic_get(&ble_conn_count) > 0;
}

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

/* Legacy framed Command Service (UUIDs 01bf…, SOF 0x0A 0xFA) and its LOG /
 * RECORDING file-pull commands are fully removed. Health history sync is the
 * HPI_HS MCUmgr group (id 0x1000) — see docs/HPI_HS_API.md. Device time uses
 * MCUmgr OS datetime. Live waveforms remain GATT notify on the services below. */

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL,
				  BT_UUID_16_ENCODE(BT_UUID_HRS_VAL),
				  BT_UUID_16_ENCODE(BT_UUID_BAS_VAL),
				  BT_UUID_16_ENCODE(BT_UUID_DIS_VAL))};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

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

BT_GATT_SERVICE_DEFINE(hpi_spo2_service,
					   BT_GATT_PRIMARY_SERVICE(HPI_SPO2_SERVICE),
					   BT_GATT_CHARACTERISTIC(HPI_SPO2_CHAR,
											  BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
											  BT_GATT_PERM_READ_ENCRYPT,
											  NULL, NULL, NULL),
					   BT_GATT_CCC(spo2_on_cccd_changed,
								   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE), );

/* B4: HTS Temperature Measurement is INDICATE per the spec (the client has to
 * acknowledge each measurement), not NOTIFY. It was declared NOTIFY here while
 * nothing sent anything at all; fixed together with the wiring below. */
BT_GATT_SERVICE_DEFINE(hpi_temp_service,
					   BT_GATT_PRIMARY_SERVICE(HPI_TEMP_SERVICE),
					   BT_GATT_CHARACTERISTIC(HPI_TEMP_CHAR,
											  BT_GATT_CHRC_READ | BT_GATT_CHRC_INDICATE,
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

	/* ECG streams at 128 sps in batches of 8, so this fires ~16x/s and is the
	 * first thing to starve when the BLE TX pool is momentarily empty (MCUmgr
	 * or a health-store sync sharing the connection interval). The return used
	 * to be discarded, which made the app's intermittent live plot invisible
	 * from the device side — count the drops so it is diagnosable. -ENOTCONN /
	 * -EINVAL just mean nobody is subscribed; those are not drops. */
	int err = bt_gatt_notify(NULL, &hpi_ecg_gsr_service.attrs[2], &out_data, len * 4);
	if (err && err != -ENOTCONN && err != -EINVAL)
	{
		static uint32_t ecg_notify_drops;
		if ((++ecg_notify_drops % 50) == 0)
		{
			LOG_WRN("ECG notify dropped %u batches (last err %d)", ecg_notify_drops, err);
		}
	}
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

void ble_hrs_notify(uint16_t hr_val)
{
	bt_hrs_notify(hr_val);
}

void ble_bas_notify(uint8_t batt_level)
{
	bt_bas_set_battery_level(batt_level);
}

/* ---------------------------------------------------------------------------
 * B1-B4 — the four standard SIG services, fed from zbus.
 *
 * HRS/BAS/PLX/HTS were declared (and HRS/BAS/DIS even advertised) since v1, but
 * nothing ever pushed a value into them: ble_hrs_notify()/ble_bas_notify() had
 * no callers and the PLX/HTS characteristics were never written. Each listener
 * below runs in the context of the thread that published the value (hw_thread,
 * data_thread, PPG SMF), so they must stay short — queue a GATT PDU and return.
 * Every one of them bails out when no central is connected, and every one of
 * them stands down during a DFU: the OTA owns the link for minutes at a time,
 * and competing ATT traffic (an HTS *indication* especially, which holds the
 * bearer until the peer confirms) delays the SMP responses the phone is waiting
 * on. Same quiesce the sensor/record paths already apply (data_module.c,
 * smf_ppg_wrist.c, smf_ecg_bioz.c).
 */
static inline bool ble_notify_allowed(void)
{
	return hpi_ble_is_connected() && !hpi_dfu_is_active();
}
ZBUS_CHAN_DECLARE(hr_chan, batt_chan, spo2_chan, temp_chan);

/* Last HR seen on hr_chan. The SpO2 channel carries no pulse rate, but the PLX
 * spot-check record has a mandatory PR field — feed it the most recent HR and
 * fall back to the IEEE-11073 NaN when there is none. */
static uint16_t s_last_hr;

/* IEEE-11073 16-bit SFLOAT: 4-bit signed exponent : 12-bit signed mantissa.
 * Both PLX fields here are whole percent / bpm, so the exponent is 0. */
#define SFLOAT_NAN 0x07FFU
static inline uint16_t sfloat_from_uint(uint16_t v)
{
	return (uint16_t)(v & 0x0FFFU);
}

/* B1 — Heart Rate Service (0x180D) */
static void ble_hr_listener(const struct zbus_channel *chan)
{
	const struct hpi_hr_t *m = zbus_chan_const_msg(chan);

	if (m->hr == 0 || !m->hr_ready_flag)
	{
		return;
	}

	s_last_hr = m->hr;

	if (ble_notify_allowed())
	{
		bt_hrs_notify(m->hr);
	}
}
ZBUS_LISTENER_DEFINE(ble_hr_lis, ble_hr_listener);

/* B2 — Battery Service (0x180F). bt_bas_set_battery_level() stores the level and
 * notifies only subscribers, so no connection guard is needed (and a value kept
 * up to date while disconnected is what the next reader should see). */
static void ble_batt_listener(const struct zbus_channel *chan)
{
	const struct hpi_batt_status_t *m = zbus_chan_const_msg(chan);
	uint8_t level = (m->batt_level > 100) ? 100 : m->batt_level;

	/* Keeping the stored level current while disconnected is fine, but during a
	 * DFU even the notify this may trigger competes with the upload. */
	if (hpi_dfu_is_active())
	{
		return;
	}

	bt_bas_set_battery_level(level);
}
ZBUS_LISTENER_DEFINE(ble_batt_lis, ble_batt_listener);

/* B3 — Pulse Oximeter Service (0x1822), Spot-check Measurement (0x2A5E).
 * spo2_chan is only published on a *completed* spot check (both the wrist and
 * finger SMFs publish once, after the confidence gate), so every publish maps
 * 1:1 to one PLX record. */
static void ble_spo2_listener(const struct zbus_channel *chan)
{
	const struct hpi_spo2_point_t *m = zbus_chan_const_msg(chan);
	uint8_t buf[12];
	uint8_t i = 0;
	bool clock_set = hpi_sys_is_time_valid();
	bool ts_present = clock_set && (m->timestamp > 0);

	if (m->spo2 == 0 || !ble_notify_allowed())
	{
		return;
	}

	/* Flags: bit0 = timestamp present, bit4 = device clock is not set. */
	buf[i++] = (ts_present ? BIT(0) : 0) | (clock_set ? 0 : BIT(4));

	sys_put_le16(sfloat_from_uint(m->spo2), &buf[i]);
	i += 2;
	sys_put_le16(s_last_hr ? sfloat_from_uint(s_last_hr) : SFLOAT_NAN, &buf[i]);
	i += 2;

	if (ts_present)
	{
		/* org.bluetooth.characteristic.date_time, in local time. */
		time_t local = (time_t)(m->timestamp + hpi_sys_get_utc_offset());
		struct tm tm_local;

		gmtime_r(&local, &tm_local);
		sys_put_le16((uint16_t)(tm_local.tm_year + 1900), &buf[i]);
		i += 2;
		buf[i++] = (uint8_t)(tm_local.tm_mon + 1);
		buf[i++] = (uint8_t)tm_local.tm_mday;
		buf[i++] = (uint8_t)tm_local.tm_hour;
		buf[i++] = (uint8_t)tm_local.tm_min;
		buf[i++] = (uint8_t)tm_local.tm_sec;
	}

	bt_gatt_notify(NULL, &hpi_spo2_service.attrs[2], buf, i);
}
ZBUS_LISTENER_DEFINE(ble_spo2_lis, ble_spo2_listener);

/* B4 — Health Thermometer Service (0x1809), Temperature Measurement (0x2A1C).
 * temp_chan publishes every 5 s while the watch is on-skin; that is far more
 * traffic than a thermometer client needs, so rate-limit to a change of
 * >=0.1 degC or one indication per 30 s. */
#define HTS_MIN_INTERVAL_MS 30000
#define HTS_MIN_DELTA_C     0.1

static struct bt_gatt_indicate_params s_temp_ind_params;
static uint8_t s_temp_ind_buf[5];
static atomic_t s_temp_ind_busy;

static void temp_indicate_destroy(struct bt_gatt_indicate_params *params)
{
	ARG_UNUSED(params);
	atomic_clear(&s_temp_ind_busy);
}

static void ble_temp_listener(const struct zbus_channel *chan)
{
	const struct hpi_temp_t *m = zbus_chan_const_msg(chan);
	static int64_t last_sent_ms;
	static double last_sent_c;
	int64_t now = k_uptime_get();
	double delta;
	int32_t mantissa;
	int err;

	if (m->temp_c <= 0.0 || !ble_notify_allowed())
	{
		return;
	}

	delta = m->temp_c - last_sent_c;
	if (delta < 0.0)
	{
		delta = -delta;
	}
	if (last_sent_ms != 0 && (now - last_sent_ms) < HTS_MIN_INTERVAL_MS &&
		delta < HTS_MIN_DELTA_C)
	{
		return;
	}

	/* One indication in flight at a time — the params and the value buffer are
	 * owned by the stack until it calls destroy. */
	if (!atomic_cas(&s_temp_ind_busy, 0, 1))
	{
		return;
	}

	/* Flags byte 0: Celsius, no timestamp, no temperature type. Value is an
	 * IEEE-11073 32-bit FLOAT — 24-bit signed mantissa (LE) + int8 exponent. */
	mantissa = (int32_t)(m->temp_c * 100.0);
	s_temp_ind_buf[0] = 0;
	s_temp_ind_buf[1] = (uint8_t)(mantissa & 0xFF);
	s_temp_ind_buf[2] = (uint8_t)((mantissa >> 8) & 0xFF);
	s_temp_ind_buf[3] = (uint8_t)((mantissa >> 16) & 0xFF);
	s_temp_ind_buf[4] = (uint8_t)((int8_t)-2);

	s_temp_ind_params.attr = &hpi_temp_service.attrs[2];
	s_temp_ind_params.func = NULL;
	s_temp_ind_params.destroy = temp_indicate_destroy;
	s_temp_ind_params.data = s_temp_ind_buf;
	s_temp_ind_params.len = sizeof(s_temp_ind_buf);

	err = bt_gatt_indicate(NULL, &s_temp_ind_params);
	if (err)
	{
		/* -ENOTCONN simply means nobody subscribed to HTS; not an error worth
		 * logging every 30 s. The stack never took ownership, so clear here. */
		atomic_clear(&s_temp_ind_busy);
		return;
	}

	last_sent_ms = now;
	last_sent_c = m->temp_c;
}
ZBUS_LISTENER_DEFINE(ble_temp_lis, ble_temp_listener);

/* A connectable advertising set stops automatically once a connection is
 * established, so it must be restarted after a disconnect. Do it from a work
 * item rather than inline in the disconnected callback (BT RX context). */
static void adv_work_handler(struct k_work *work)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad),
							  sd, ARRAY_SIZE(sd));
	if (err && err != -EALREADY)
	{
		LOG_ERR("Advertising failed to restart (err %d)\n", err);
	}
	else
	{
		LOG_INF("Advertising restarted");
	}
}

static K_WORK_DEFINE(adv_work, adv_work_handler);

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

	atomic_inc(&ble_conn_count);

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

	if (atomic_get(&ble_conn_count) > 0)
	{
		atomic_dec(&ble_conn_count);
	}

	/* App dropped off mid-OTA: fail fast so the watch leaves the update modal
	 * and returns to normal instead of hanging (a raw disconnect emits no
	 * img-mgmt DFU_STOPPED). The display's stall timeout is the backstop. */
	if (hpi_dfu_is_active()) {
		LOG_WRN("BLE disconnect during DFU - failing the update");
		hpi_dfu_set_state(HPI_DFU_FAILED);
	}

	/* Connectable advertising stopped on connect; bring it back so the
	 * device is discoverable again after the peer goes away. */
	k_work_submit(&adv_work);
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

	/* NCS 3.2: BT_LE_ADV_CONN was removed; BT_LE_ADV_CONN_FAST_2 is the identical
	 * replacement (BT_LE_ADV_OPT_CONN + FAST_INT_MIN_2/MAX_2). */
	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
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

/* ble_bpt_lis / legacy Command Service removed — health sync is HPI_HS (MCUmgr). */

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
