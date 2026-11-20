/*
 * BMI323 accelerometer logger -> CSV on LittleFS.
 *
 * File:   /lfs/acc/acc_NNNN.csv   (NNNN increments across power cycles)
 * Format: t_ms,ax_mg,ay_mg,az_mg  (milli-g, t_ms since the start of the session)
 *
 * The UI only calls acc_rec_request_start()/stop() and polls the getters; all
 * sensor and filesystem work happens on the recorder thread.
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/sys/printk.h>
#include <time.h>
#include <zephyr/sys/timeutil.h>
#include "acc_recorder.h"
#include "storage.h"
#include "boot.h"
#include "activity.h"

LOG_MODULE_REGISTER(acc_rec, LOG_LEVEL_INF);

#define ACC_ODR_HZ        50
#define ACC_RANGE_G       8
#define ACC_PERIOD_MS     (1000 / ACC_ODR_HZ)

#define WBUF_SIZE         2048
#define FLUSH_AT          1024            /* bytes buffered before a write */
#define SYNC_INTERVAL_MS  5000            /* fs_sync + free-space check */
#define MIN_FREE_BYTES    (64 * 1024)     /* stop before the volume fills up */
#define MAX_READ_ERRORS   50              /* consecutive sensor errors -> abort */

#define REC_STACK_SIZE    6144
#define REC_PRIORITY      6

#define MAX_STALLS  15              /* 15 x 200 ms = 3 s without data */

static atomic_t g_state = ATOMIC_INIT(ACC_REC_IDLE);
static atomic_t g_start_req = ATOMIC_INIT(0);
static atomic_t g_stop_req = ATOMIC_INIT(0);
static volatile uint32_t g_samples;
static volatile uint32_t g_elapsed_s;
static char g_path[48];
static const char *g_err = "";

K_SEM_DEFINE(sem_rec_cmd, 0, 1);

K_MSGQ_DEFINE(acc_rec_q, sizeof(struct acc_sample), 64, 4);

static struct fs_file_t g_file;
static char wbuf[WBUF_SIZE];
static size_t wlen;


void acc_rec_request_start(void)
{
	enum acc_rec_state s = atomic_get(&g_state);

	if (s == ACC_REC_IDLE || s == ACC_REC_ERROR) {
		atomic_set(&g_start_req, 1);
		k_sem_give(&sem_rec_cmd);
	}
}

void acc_rec_request_stop(void)
{
	if (atomic_get(&g_state) == ACC_REC_RECORDING) {
		atomic_set(&g_stop_req, 1);
	}
}

enum acc_rec_state acc_rec_get_state(void) { return (enum acc_rec_state)atomic_get(&g_state); }
uint32_t acc_rec_get_samples(void) { return g_samples; }
uint32_t acc_rec_get_elapsed_s(void) { return g_elapsed_s; }
const char *acc_rec_get_error(void) { return g_err; }

const char *acc_rec_get_filename(void)
{
	const char *slash = strrchr(g_path, '/');

	return slash ? slash + 1 : g_path;
}


static int flush_buf(void)
{
	if (wlen == 0) {
		return 0;
	}
	ssize_t w = fs_write(&g_file, wbuf, wlen);

	if (w < 0) {
		return (int)w;
	}
	if ((size_t)w != wlen) {
		return -EIO;
	}
	wlen = 0;
	return 0;
}

static int buf_append(const char *s, size_t n)
{
	if (wlen + n > sizeof(wbuf)) {
		int rc = flush_buf();

		if (rc < 0) {
			return rc;
		}
	}
	memcpy(wbuf + wlen, s, n);
	wlen += n;
	return 0;
}

/* Next free acc_NNNN.csv index */
static int next_index(uint32_t *out)
{
	struct fs_dir_t dir;
	struct fs_dirent ent;
	uint32_t max = 0;

	fs_dir_t_init(&dir);
	int rc = fs_opendir(&dir, STORAGE_ACC_DIR);

	if (rc < 0) {
		return rc;
	}
	while (fs_readdir(&dir, &ent) == 0 && ent.name[0] != '\0') {
		if (ent.type == FS_DIR_ENTRY_FILE && strncmp(ent.name, "acc_", 4) == 0) {
			char *end;
			unsigned long n = strtoul(ent.name + 4, &end, 10);

			if (end != ent.name + 4 && strcmp(end, ".csv") == 0 && n > max) {
				max = (uint32_t)n;
			}
		}
	}
	fs_closedir(&dir);
	*out = max + 1;
	return 0;
}

static void fail(const char *why)
{
	LOG_ERR("Recording failed: %s", why);
	g_err = why;
	atomic_set(&g_state, ACC_REC_ERROR);
}

void acc_rec_dump_file(const char *path)
{
	struct fs_file_t f;
	char buf[128];
	ssize_t n;

	fs_file_t_init(&f);
	if (fs_open(&f, path, FS_O_READ) < 0) {
		printk("dump: cannot open %s\n", path);
		return;
	}
	printk("\n=====BEGIN %s=====\n", path);
	while ((n = fs_read(&f, buf, sizeof(buf) - 1)) > 0) {
		buf[n] = '\0';
		printk("%s", buf);
		k_msleep(10);              
	}
	printk("\n=====END=====\n");
	fs_close(&f);
}

static void record_session(void)
{
	char line[128];
	uint32_t idx;
	int rc;
	const char *end_err = NULL;

	atomic_set(&g_state, ACC_REC_OPENING);
	atomic_set(&g_stop_req, 0);
	g_samples = 0;
	g_elapsed_s = 0;
	g_err = "";
	wlen = 0;

	if (storage_free_bytes() < MIN_FREE_BYTES) {
		fail("Storage full");
		return;
	}
	if (next_index(&idx) < 0) {
		fail("Cannot list /lfs/acc");
		return;
	}

	snprintf(g_path, sizeof(g_path), STORAGE_ACC_DIR "/acc_%04u.csv", (unsigned int)idx);
	fs_file_t_init(&g_file);
	rc = fs_open(&g_file, g_path, FS_O_CREATE | FS_O_WRITE);
	if (rc < 0) {
		LOG_ERR("fs_open(%s) = %d", g_path, rc);
		fail("Cannot create file");
		return;
	}

	/* Header: rate/units and column names */
	{
		int n = snprintf(line, sizeof(line), "# rate_hz=%d unit=m/s2\n", ACC_SAMPLE_HZ);

		buf_append(line, n);
		buf_append("timestamp,ax,ay,az,class,conf_pct,infer_ms\n",
			   sizeof("timestamp,ax,ay,az,class,conf_pct,infer_ms\n") - 1);
	}

	k_msgq_purge(&acc_rec_q);          /* drop stale samples from before Start */

	LOG_INF("Recording to %s @ %d Hz", g_path, ACC_SAMPLE_HZ);
	atomic_set(&g_state, ACC_REC_RECORDING);

	int64_t t0 = k_uptime_get();
	int64_t last_sync = t0;
	int stalls = 0;
	bool first = true;

	/* Wall-clock time at Start, as epoch seconds (0 if the RTC is invalid) */
	int64_t base_epoch = 0;
	{
		struct rtc_time rt;

		if (g_boot.rtc_ok && rtc_get_time(g_rtc_dev, &rt) == 0) {
			struct tm tmv = {
				.tm_sec = rt.tm_sec,
				.tm_min = rt.tm_min,
				.tm_hour = rt.tm_hour,
				.tm_mday = rt.tm_mday,
				.tm_mon = rt.tm_mon,
				.tm_year = rt.tm_year,
			};

			base_epoch = timeutil_timegm64(&tmv);
		}
	}

	while (!atomic_get(&g_stop_req)) {
		struct acc_sample s;

		if (k_msgq_get(&acc_rec_q, &s, K_MSEC(200)) != 0) {
			if (++stalls >= MAX_STALLS) {
				end_err = "IMU stalled";
				break;
			}
		} else {
			stalls = 0;

			uint32_t t_ms = s.t_ms - (uint32_t)t0;
			struct activity_result r;
			struct tm tmv;
			time_t ts = (time_t)(base_epoch + t_ms / 1000);
			int n;

			activity_get_result(&r);
			gmtime_r(&ts, &tmv);

			/* every row carries the latest class name, confidence and inference time */
			n = snprintf(line, sizeof(line),
            "%04d-%02d-%02d %02d:%02d:%02d.%03u,%.3f,%.3f,%.3f,%s,%u,%u\n",
            tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
            tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
            (unsigned int)(t_ms % 1000),
            (double)s.x, (double)s.y, (double)s.z,
            r.label ? r.label : "none",
            (unsigned int)r.conf_pct,
            (unsigned int)r.infer_ms);

			if (n > 0 && n < (int)sizeof(line)) {
				if (buf_append(line, n) < 0) {
					end_err = "Write error";
					break;
				}
				g_samples++;
			}
			if (first) {
				first = false;
				/* sanity check: |a| should be ~9.8 m/s2 at rest */
				LOG_INF("First sample: %.2f, %.2f, %.2f m/s2",
					(double)s.x, (double)s.y, (double)s.z);
			}
		}

		if (wlen >= FLUSH_AT && flush_buf() < 0) {
			end_err = "Write error";
			break;
		}

		int64_t now = k_uptime_get();

		g_elapsed_s = (uint32_t)((now - t0) / 1000);

		if (now - last_sync >= SYNC_INTERVAL_MS) {
			last_sync = now;
			if (flush_buf() < 0 || fs_sync(&g_file) < 0) {
				end_err = "Write error";
				break;
			}
			if (storage_free_bytes() < MIN_FREE_BYTES) {
				end_err = "Storage full - stopped";
				break;
			}
		}
	}

	atomic_set(&g_state, ACC_REC_CLOSING);

	rc = flush_buf();
	if (rc < 0 && end_err == NULL) {
		end_err = "Write error";
	}
	fs_sync(&g_file);
	fs_close(&g_file);

	LOG_INF("Recording stopped: %s, %u samples, %u s", g_path, (unsigned int)g_samples,
		(unsigned int)g_elapsed_s);
     acc_rec_dump_file(g_path);

	if (end_err) {
		fail(end_err);
	} else {
		atomic_set(&g_state, ACC_REC_IDLE);
	}
}
static void rec_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	for (;;) {
		k_sem_take(&sem_rec_cmd, K_FOREVER);
		if (atomic_cas(&g_start_req, 1, 0)) {
			record_session();
		}
	}
}

K_THREAD_DEFINE(rec_thread_id, REC_STACK_SIZE, rec_thread, NULL, NULL, NULL, REC_PRIORITY, 0, 0);