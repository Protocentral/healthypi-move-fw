/*
 * HealthyPi Move
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/zbus/zbus.h>
#include <string.h>
#include <math.h>

#include "haptic_module.h"
#include "hpi_common_types.h"

/* HR threshold for motor trigger (BPM) */
#define HAPTIC_HR_THRESHOLD_BPM  100

/* GSR threshold for motor trigger (raw ADC units, empirically tuned) */
#define HAPTIC_GSR_THRESHOLD         2000

/* Max change between consecutive batches before treating as motion artifact */
#define HAPTIC_GSR_ROC_THRESHOLD     5000

/* EMA alpha = 5/100 = 0.05 — smooths noise, tracks slow stress responses */
#define HAPTIC_GSR_EMA_ALPHA_NUM     5
#define HAPTIC_GSR_EMA_ALPHA_DEN     100

/* Consecutive batches of smoothed signal above threshold before triggering (~2s) */
#define HAPTIC_GSR_DEBOUNCE_BATCHES  16

/* Minimum time between triggers (ms) */
#define HAPTIC_COOLDOWN_MS       30000

LOG_MODULE_REGISTER(haptic_module, LOG_LEVEL_DBG);

#define DOG_DEVICE_NAME "Dog Device Receiver"

/* Service UUID: 19B10000-E8F2-537E-4F6C-D104768A1214 */
static struct bt_uuid_128 dog_svc_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x19B10000, 0xE8F2, 0x537E, 0x4F6C, 0xD104768A1214));

/* Characteristic UUID: 19B10001-E8F2-537E-4F6C-D104768A1214 */
static struct bt_uuid_128 dog_chr_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x19B10001, 0xE8F2, 0x537E, 0x4F6C, 0xD104768A1214));

static struct bt_conn *dog_conn;
static uint16_t dog_chr_handle;
static bool chr_handle_valid;
static struct bt_gatt_discover_params discover_params;
static struct bt_gatt_write_params write_params;
static uint8_t write_buf;

static int64_t last_trigger_time;

static void start_scan(void);

static void scan_start_work_handler(struct k_work *work)
{
	LOG_ERR("haptic: starting scan");
	start_scan();
}

static K_WORK_DELAYABLE_DEFINE(scan_start_work, scan_start_work_handler);

/* ------------------------------------------------------------------ */
/* GATT discovery                                                       */
/* ------------------------------------------------------------------ */

static uint8_t discover_cb(struct bt_conn *conn,
			   const struct bt_gatt_attr *attr,
			   struct bt_gatt_discover_params *params)
{
	if (!attr) {
		LOG_INF("GATT discovery complete");
		(void)memset(params, 0, sizeof(*params));
		return BT_GATT_ITER_STOP;
	}

	if (params->type == BT_GATT_DISCOVER_PRIMARY) {
		LOG_INF("Dog service found, discovering characteristic");
		discover_params.uuid = &dog_chr_uuid.uuid;
		discover_params.start_handle = attr->handle + 1;
		discover_params.end_handle =
			((struct bt_gatt_service_val *)attr->user_data)->end_handle;
		discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;
		int err = bt_gatt_discover(conn, &discover_params);
		if (err) {
			LOG_ERR("Characteristic discovery failed: %d", err);
		}
		return BT_GATT_ITER_STOP;
	}

	if (params->type == BT_GATT_DISCOVER_CHARACTERISTIC) {
		struct bt_gatt_chrc *chrc = (struct bt_gatt_chrc *)attr->user_data;

		dog_chr_handle = chrc->value_handle;
		chr_handle_valid = true;
		LOG_INF("Dog characteristic ready, handle: 0x%04X", dog_chr_handle);
		return BT_GATT_ITER_STOP;
	}

	return BT_GATT_ITER_STOP;
}

/* ------------------------------------------------------------------ */
/* Scanning                                                            */
/* ------------------------------------------------------------------ */

struct adv_parse_data {
	bool found;
};

static bool parse_adv(struct bt_data *data, void *user_data)
{
	struct adv_parse_data *d = user_data;

	if (data->type != BT_DATA_NAME_COMPLETE &&
	    data->type != BT_DATA_NAME_SHORTENED) {
		return true; /* continue parsing */
	}

	if (data->data_len == sizeof(DOG_DEVICE_NAME) - 1 &&
	    memcmp(data->data, DOG_DEVICE_NAME, data->data_len) == 0) {
		d->found = true;
	}

	return false; /* stop parsing */
}

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
		    struct net_buf_simple *buf)
{
	struct adv_parse_data d = { .found = false };

	if (dog_conn) {
		return; /* already connected or connecting */
	}

	bt_data_parse(buf, parse_adv, &d);
	if (!d.found) {
		return;
	}

	LOG_INF("Found Dog Device Receiver (RSSI %d), connecting...", rssi);

	int err = bt_le_scan_stop();
	if (err) {
		LOG_ERR("Scan stop failed: %d", err);
		return;
	}

	err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN,
				BT_LE_CONN_PARAM_DEFAULT, &dog_conn);
	if (err) {
		LOG_ERR("Create connection failed: %d", err);
		start_scan();
	}
}

static void start_scan(void)
{
	int err = bt_le_scan_start(BT_LE_SCAN_ACTIVE, scan_cb);

	if (err) {
		LOG_ERR("haptic: scan start failed: %d", err);
		return;
	}
	LOG_ERR("haptic: scanning for Dog Device Receiver");
}

/* ------------------------------------------------------------------ */
/* Connection callbacks — registered via BT_CONN_CB_DEFINE so they    */
/* coexist alongside ble_module.c's own BT_CONN_CB_DEFINE block.      */
/* ------------------------------------------------------------------ */

static void haptic_connected(struct bt_conn *conn, uint8_t err)
{
	if (conn != dog_conn) {
		return; /* not our outbound connection */
	}

	if (err) {
		LOG_ERR("Dog device connection failed (err %u)", err);
		bt_conn_unref(dog_conn);
		dog_conn = NULL;
		start_scan();
		return;
	}

	LOG_INF("Connected to Dog Device Receiver");
	chr_handle_valid = false;

	discover_params.uuid = &dog_svc_uuid.uuid;
	discover_params.func = discover_cb;
	discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	discover_params.end_handle   = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	discover_params.type         = BT_GATT_DISCOVER_PRIMARY;

	int ret = bt_gatt_discover(conn, &discover_params);

	if (ret) {
		LOG_ERR("Service discovery failed: %d", ret);
	}
}

static void haptic_disconnected(struct bt_conn *conn, uint8_t reason)
{
	if (conn != dog_conn) {
		return;
	}

	LOG_INF("Disconnected from Dog Device Receiver (reason %u)", reason);
	bt_conn_unref(dog_conn);
	dog_conn = NULL;
	chr_handle_valid = false;

	start_scan(); /* auto-reconnect */
}

BT_CONN_CB_DEFINE(haptic_conn_callbacks) = {
	.connected    = haptic_connected,
	.disconnected = haptic_disconnected,
};

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

static void write_cb(struct bt_conn *conn, uint8_t err,
		     struct bt_gatt_write_params *params)
{
	if (err) {
		LOG_ERR("Alert write error: %d", err);
	} else {
		LOG_INF("Alert write confirmed by dog device");
	}
}

int haptic_send_alert(uint8_t value)
{
	if (!dog_conn || !chr_handle_valid) {
		LOG_WRN("Dog device not ready, alert dropped");
		return -ENODEV;
	}

	write_buf = value;
	write_params.func   = write_cb;
	write_params.handle = dog_chr_handle;
	write_params.offset = 0;
	write_params.data   = &write_buf;
	write_params.length = sizeof(write_buf);

	int err = bt_gatt_write(dog_conn, &write_params);
	if (err) {
		LOG_ERR("Write failed: %d", err);
		return err;
	}

	LOG_INF("Alert sent: %u", value);
	return 0;
}

/* ------------------------------------------------------------------ */
/* HR-based trigger                                                    */
/* ------------------------------------------------------------------ */

static void haptic_hr_alert_work_handler(struct k_work *work)
{
	haptic_send_alert(1);
}

static K_WORK_DEFINE(haptic_hr_alert_work, haptic_hr_alert_work_handler);

static void haptic_hr_listener(const struct zbus_channel *chan)
{
	const struct hpi_hr_t *hr = zbus_chan_const_msg(chan);

	if (!hr->hr_ready_flag || hr->hr == 0) {
		return;
	}

	if (hr->hr < HAPTIC_HR_THRESHOLD_BPM) {
		return;
	}

	int64_t now = k_uptime_get();

	if ((now - last_trigger_time) < HAPTIC_COOLDOWN_MS) {
		return;
	}

	last_trigger_time = now;
	LOG_INF("HR trigger: %u BPM >= %u", hr->hr, HAPTIC_HR_THRESHOLD_BPM);
	k_work_submit(&haptic_hr_alert_work);
}

ZBUS_LISTENER_DEFINE(haptic_hr_lis, haptic_hr_listener);

/* ------------------------------------------------------------------ */
/* HRV (RMSSD) based trigger                                           */
/* ------------------------------------------------------------------ */

/* Rolling window size (number of RR intervals) */
#define HAPTIC_HRV_WINDOW            20

/* RMSSD threshold — trigger when RMSSD drops below this (ms) */
#define HAPTIC_HRV_RMSSD_THRESHOLD   70

static uint16_t hrv_rr_buf[HAPTIC_HRV_WINDOW];
static uint8_t  hrv_rr_count;
static uint8_t  hrv_rr_head;

static void haptic_hrv_alert_work_handler(struct k_work *work)
{
	haptic_send_alert(1);
}

static K_WORK_DEFINE(haptic_hrv_alert_work, haptic_hrv_alert_work_handler);

void haptic_process_rtor(uint16_t rtor_ms)
{
	/* Reject physiologically impossible intervals */
	if (rtor_ms < 300 || rtor_ms > 1500) {
		return;
	}

	/* Add to circular buffer */
	hrv_rr_buf[hrv_rr_head] = rtor_ms;
	hrv_rr_head = (hrv_rr_head + 1) % HAPTIC_HRV_WINDOW;
	if (hrv_rr_count < HAPTIC_HRV_WINDOW) {
		hrv_rr_count++;
	}

	/* Need at least 2 intervals to compute RMSSD */
	if (hrv_rr_count < 2) {
		return;
	}

	/* Compute RMSSD over the window */
	uint64_t sum_sq = 0;
	uint8_t  pairs  = 0;

	for (uint8_t i = 1; i < hrv_rr_count; i++) {
		uint8_t idx_curr = (hrv_rr_head - 1 - (hrv_rr_count - 1 - i) +
				    HAPTIC_HRV_WINDOW) % HAPTIC_HRV_WINDOW;
		uint8_t idx_prev = (idx_curr - 1 + HAPTIC_HRV_WINDOW) % HAPTIC_HRV_WINDOW;
		int32_t diff = (int32_t)hrv_rr_buf[idx_curr] - (int32_t)hrv_rr_buf[idx_prev];

		sum_sq += (uint64_t)(diff * diff);
		pairs++;
	}

	if (pairs == 0) {
		return;
	}

	uint32_t rmssd = (uint32_t)sqrt((double)(sum_sq / pairs));

	static int64_t last_hrv_log;
	int64_t now = k_uptime_get();

	if ((now - last_hrv_log) >= 5000) {
		LOG_INF("HRV: rtor=%u ms  rmssd=%u ms  n=%u", rtor_ms, rmssd, hrv_rr_count);
		last_hrv_log = now;
	}

	if (rmssd > HAPTIC_HRV_RMSSD_THRESHOLD) {
		return;
	}

	if ((now - last_trigger_time) < HAPTIC_COOLDOWN_MS) {
		return;
	}

	last_trigger_time = now;
	LOG_INF("HRV trigger: rmssd=%u ms <= %u", rmssd, HAPTIC_HRV_RMSSD_THRESHOLD);
	k_work_submit(&haptic_hrv_alert_work);
}

/* ------------------------------------------------------------------ */
/* GSR-based trigger                                                   */
/* ------------------------------------------------------------------ */

static void haptic_gsr_alert_work_handler(struct k_work *work)
{
	haptic_send_alert(1);
}

static K_WORK_DEFINE(haptic_gsr_alert_work, haptic_gsr_alert_work_handler);

void haptic_process_gsr(const int32_t *samples, uint8_t num_samples, uint8_t lead_off)
{
	static int32_t gsr_prev_mean = INT32_MIN;
	static int32_t gsr_ema       = INT32_MIN;
	static uint16_t gsr_above_count;

	if (num_samples == 0) {
		return;
	}

	/* Skip when electrodes are not in contact */
	if (lead_off) {
		gsr_prev_mean = INT32_MIN;
		gsr_ema       = INT32_MIN;
		gsr_above_count = 0;
		return;
	}

	/* Compute mean of this batch */
	int64_t sum = 0;

	for (uint8_t i = 0; i < num_samples; i++) {
		sum += samples[i];
	}
	int32_t mean = (int32_t)(sum / num_samples);

	/* Reject negative — capacitive/reactive artifact, not real conductance */
	if (mean <= 0) {
		gsr_prev_mean = INT32_MIN;
		gsr_ema       = INT32_MIN;
		gsr_above_count = 0;
		return;
	}

	/* Reject motion artifacts — genuine stress rises slowly, spikes are instantaneous */
	if (gsr_prev_mean != INT32_MIN) {
		int32_t roc = mean - gsr_prev_mean;

		if (roc < 0) {
			roc = -roc;
		}
		if (roc > HAPTIC_GSR_ROC_THRESHOLD) {
			LOG_DBG("GSR: motion rejected (roc=%d)", roc);
			gsr_prev_mean = mean;
			gsr_above_count = 0;
			return;
		}
	}
	gsr_prev_mean = mean;

	/* Exponential moving average — smooths residual noise */
	if (gsr_ema == INT32_MIN) {
		gsr_ema = mean;
	} else {
		gsr_ema = (gsr_ema * (HAPTIC_GSR_EMA_ALPHA_DEN - HAPTIC_GSR_EMA_ALPHA_NUM) +
			   mean * HAPTIC_GSR_EMA_ALPHA_NUM) / HAPTIC_GSR_EMA_ALPHA_DEN;
	}

	int64_t now = k_uptime_get();
	static int64_t last_log_time;

	if ((now - last_log_time) >= 5000) {
		LOG_INF("GSR: raw=%d ema=%d", mean, gsr_ema);
		last_log_time = now;
	}

	if (gsr_ema < HAPTIC_GSR_THRESHOLD) {
		gsr_above_count = 0;
		return;
	}

	gsr_above_count++;
	if (gsr_above_count < HAPTIC_GSR_DEBOUNCE_BATCHES) {
		return;
	}

	gsr_above_count = 0;

	if ((now - last_trigger_time) < HAPTIC_COOLDOWN_MS) {
		return;
	}

	last_trigger_time = now;
	LOG_INF("GSR trigger: ema=%d >= %d (sustained)", gsr_ema, HAPTIC_GSR_THRESHOLD);
	k_work_submit(&haptic_gsr_alert_work);
}

int haptic_module_init(void)
{
	dog_conn = NULL;
	chr_handle_valid = false;
	dog_chr_handle = 0;
	last_trigger_time = 0;

	LOG_ERR("haptic: module init");
	k_work_schedule(&scan_start_work, K_SECONDS(5));
	return 0;
}
