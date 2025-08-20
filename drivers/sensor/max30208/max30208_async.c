/*
 * MAX30208 async sensor_read support (one-shot)
 * Protocentral Electronics
 * SPDX-License-Identifier: Apache-2.0
 */

/* Only build when sensor subsystem is enabled */
#ifdef CONFIG_SENSOR

#include "max30208.h"
#include <zephyr/rtio/work.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/drivers/sensor_clock.h>


LOG_MODULE_REGISTER(MAX30208_ASYNC, CONFIG_SENSOR_LOG_LEVEL);

static void max30208_complete_result(struct rtio *ctx, const struct rtio_sqe *sqe, void *arg)
{
    struct rtio_iodev_sqe *iodev_sqe = (struct rtio_iodev_sqe *)sqe->userdata;
    struct rtio_cqe *cqe;
    int err = 0;

    do {
        cqe = rtio_cqe_consume(ctx);
        if (cqe != NULL) {
            LOG_DBG("MAX30208_ASYNC: CQE result=%d userdata=%p", cqe->result, cqe->userdata);
            err = cqe->result;

            /* If a buffer was provided back via userdata, log the raw bytes for diagnosis */
            if (cqe->userdata != NULL) {
                uint8_t *buf = (uint8_t *)cqe->userdata;
                /* Attempt to print the first 8 bytes (header + sample) if present */
                LOG_DBG("MAX30208_ASYNC: CQE userdata bytes: %02x %02x %02x %02x %02x %02x %02x %02x",
                        buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
            }

            rtio_cqe_release(ctx, cqe);
        }
    } while (cqe != NULL);

    if (err) {
        rtio_iodev_sqe_err(iodev_sqe, err);
    } else {
        rtio_iodev_sqe_ok(iodev_sqe, 0);
    }

    LOG_DBG("MAX30208 one-shot fetch completed");
}

static void max30208_submit_one_shot(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe)
{
    uint8_t *buf;
    uint32_t buf_len;
    uint32_t min_buf_len = sizeof(struct max30208_encoded_data);
    int rc;

    rc = rtio_sqe_rx_buf(iodev_sqe, min_buf_len, min_buf_len, &buf, &buf_len);
    if (rc != 0) {
        LOG_ERR("Failed to get a read buffer of size %u bytes", min_buf_len);
        rtio_iodev_sqe_err(iodev_sqe, rc);
        return;
    }

    struct max30208_encoded_data *edata = (struct max30208_encoded_data *)buf;

    uint64_t cycles;
    rc = sensor_clock_get_cycles(&cycles);
    if (rc != 0) {
        LOG_ERR("Failed to get sensor clock cycles");
        rtio_iodev_sqe_err(iodev_sqe, rc);
        return;
    }

    edata->header.timestamp = sensor_clock_cycles_to_ns(cycles);

    /* Simpler: perform the blocking spot measurement sequence here (uses same
     * implementation as the sync path), then write the signed int16 raw value
     * into the RTIO-provided buffer in big-endian order. This avoids building
     * a chained RTIO I2C transaction and ensures the correct conversion
     * sequence per the device requirements.
     */
    int temp_raw = max30208_get_temp(dev, 500);
    if (temp_raw < 0) {
        LOG_ERR("MAX30208_ASYNC: blocking get_temp failed: %d", temp_raw);
        rtio_iodev_sqe_err(iodev_sqe, temp_raw);
        return;
    }

    uint16_t u = (uint16_t)temp_raw;
    edata->raw_be[0] = (uint8_t)((u >> 8) & 0xFF);
    edata->raw_be[1] = (uint8_t)(u & 0xFF);

    /* Report success and the number of bytes written (encoded payload size) */
    rtio_iodev_sqe_ok(iodev_sqe, sizeof(struct max30208_encoded_data));
}

void max30208_submit(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe)
{
    const struct sensor_read_config *cfg = iodev_sqe->sqe.iodev->data;

    ARG_UNUSED(cfg);

    /* Currently only support one-shot reads */
    max30208_submit_one_shot(dev, iodev_sqe);
}

#endif /* CONFIG_SENSOR */
