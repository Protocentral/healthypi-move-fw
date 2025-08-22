/* RTIO submit implementation moved from max32664c_async.c
 * Provides non-blocking RTIO submission and completion callback.
 */

#include <zephyr/rtio/work.h>
#include <zephyr/drivers/sensor_clock.h>
#include <zephyr/logging/log.h>

#include "max32664c_sensor.h"
#include <zephyr/drivers/sensor.h>

LOG_MODULE_REGISTER(MAX32664C_RTIO, CONFIG_MAX32664C_LOG_LEVEL);

/* Local compile-time flag to enable forwarding populated RTIO streaming
 * buffers directly into the application decoder. This is intentionally a
 * file-local preprocessor flag (not a Kconfig symbol) so builds can toggle
 * it by editing this file or setting a compiler -D. Default: enabled. */
#ifndef APP_PPG_WRIST_FORWARD
#define APP_PPG_WRIST_FORWARD 1
#endif

/* Forward declarations */
void max32664c_stream_event_handler(const struct device *dev);

/* Polling work handler for DRDY status checking */
void max32664c_poll_work_handler(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct max32664c_data *data = CONTAINER_OF(dwork, struct max32664c_data, poll_work);
    const struct device *dev = data->dev;
    static int poll_count = 0;
    ARG_UNUSED(poll_count);

    /* Only poll if we have an active streaming request */
    if (!data->streaming_sqe) {
        LOG_DBG("Poll work: no streaming SQE, stopping");
        return;
    }

    /* Read hub status to check DRDY bit */
    uint8_t hub_status = max32664c_read_hub_status(dev);
    ARG_UNUSED(hub_status);
            if (data->streaming_buf) {
                /* We reserved a header region at the start of the buffer and asked RTIO
                 * to place raw FIFO bytes starting at buffer + header_sz. Now assemble
                 * the encoded struct in-place by copying/parsing the raw FIFO bytes
                 * into the encoded arrays the consumer expects. */
                size_t header_sz = sizeof(struct max32664c_encoded_data);
                uint8_t *buf = data->streaming_buf;
                uint32_t buf_len = data->streaming_buf_len;

                /* Populate header fields first */
                if (buf_len >= sizeof(struct max32664c_encoded_data)) {
                    struct max32664c_encoded_data *edata = (struct max32664c_encoded_data *)buf;
                    edata->header.timestamp = data->timestamp;
                    edata->chip_op_mode = data->op_mode;
                    edata->num_samples = data->fifo_count;

                    /* Now parse raw FIFO bytes placed at buf + header_sz */
                    uint8_t *raw = buf + header_sz;
                    size_t raw_len = (buf_len > header_sz) ? (buf_len - header_sz) : 0;

                    /* Use same sample_len and offsets as the blocking path */
                    size_t sample_len = 62;
                    size_t expected_raw_for_fifo = (sample_len * data->fifo_count) + MAX32664C_SENSOR_DATA_OFFSET;

                    size_t copy_len = (raw_len < expected_raw_for_fifo) ? raw_len : expected_raw_for_fifo;

                    if (copy_len >= MAX32664C_SENSOR_DATA_OFFSET) {
                        uint8_t *sensor_payload = raw + MAX32664C_SENSOR_DATA_OFFSET;
                        size_t payload_len = copy_len - MAX32664C_SENSOR_DATA_OFFSET;

                        /* The blocking path packs samples in groups of sample_len bytes per sample.
                         * Iterate samples and extract green/red/ir values into edata arrays. */
                        size_t max_samples = sizeof(edata->green_samples) / sizeof(edata->green_samples[0]);
                        for (uint32_t s = 0; s < data->fifo_count && s < max_samples; s++) {
                            size_t sample_offset = s * sample_len;
                            if (sample_offset + 6 <= payload_len) {
                                /* Example layout: [g0,l0,h0][r0,l0,h0][ir0,l0,h0] or similar.
                                 * The original parser in async path expects 18 bytes per sample
                                 * arranged in 3x 3-byte values (for green/red/ir). We'll copy
                                 * using the same indices: green at offset sample_offset + 0..2,
                                 * red at +3..5, ir at +6..8, but adapt if sample_len differs.
                                 */
                                uint8_t *samp = sensor_payload + sample_offset;
                                uint32_t g = ((uint32_t)samp[0]) | (((uint32_t)samp[1]) << 8) | (((uint32_t)samp[2]) << 16);
                                uint32_t r = ((uint32_t)samp[3]) | (((uint32_t)samp[4]) << 8) | (((uint32_t)samp[5]) << 16);
                                uint32_t ir = 0;
                                if (sample_len >= 9) {
                                    ir = ((uint32_t)samp[6]) | (((uint32_t)samp[7]) << 8) | (((uint32_t)samp[8]) << 16);
                                }
                                /* Store into edata arrays */
                                edata->green_samples[s] = g;
                                edata->red_samples[s] = r;
                                edata->ir_samples[s] = ir;
                            }
                        }

                        /* If algorithm data is present after the raw samples, attempt to copy
                         * the first HR/SpO2 pair from expected position. This mirrors the
                         * blocking decoder which reads algo fields at ALGO_DATA_OFFSET. */
                        size_t algo_offset = expected_raw_for_fifo; /* algorithm data comes after samples */
                        if (payload_len >= algo_offset + 4) {
                            uint8_t *algo_src = sensor_payload + algo_offset;
                            /* HR and SpO2 are 16-bit little-endian values in the encoded struct */
                            edata->hr = (uint16_t)(algo_src[0] | (algo_src[1] << 8));
                            edata->spo2 = (uint16_t)(algo_src[2] | (algo_src[3] << 8));
                        }

                        LOG_DBG("Parsed %u samples into encoded buffer (raw_len=%zu payload_len=%zu)", data->fifo_count, raw_len, payload_len);
                    } else {
                        LOG_DBG("Not enough raw bytes to parse samples (raw_len=%zu expected=%zu)", raw_len, expected_raw_for_fifo);
                    }
                } else {
                    LOG_DBG("streaming_buf too small for encoded header: buf_len=%u header_sz=%zu", data->streaming_buf_len, header_sz);
                }
        /* Record timestamp for streaming */
        uint64_t cycles = 0;
        if (sensor_clock_get_cycles(&cycles) == 0) {
            data->timestamp = sensor_clock_cycles_to_ns(cycles);
        } else {
            data->timestamp = 0;
        }

        /* DRDY detected - debug suppressed to reduce log spam */
        /* DRDY bit is set, trigger stream event handler */
        max32664c_stream_event_handler(dev);
    } else {
        /* No data ready, reschedule next poll */
        k_work_reschedule(&data->poll_work, K_MSEC(MAX32664C_POLL_INTERVAL_MS));
    }
}

static void max32664c_complete_result(struct rtio *ctx,
                                      const struct rtio_sqe *sqe,
                                      void *arg)
{
    struct rtio_iodev_sqe *iodev_sqe = (struct rtio_iodev_sqe *)sqe->userdata;
    struct rtio_cqe *cqe;
    int err = 0;

    /* Consume available CQEs produced by the executor for this submission */
    do {
        cqe = rtio_cqe_consume(ctx);
        if (cqe != NULL) {
            if (cqe->result < 0) {
                err = cqe->result;
            }
            rtio_cqe_release(ctx, cqe);
        }
    } while (cqe != NULL);

    if (err) {
        rtio_iodev_sqe_err(iodev_sqe, err);
    } else {
        rtio_iodev_sqe_ok(iodev_sqe, 0);
    }

    LOG_DBG("One-shot RTIO fetch completed (err=%d)", err);
}

static void max32664c_stream_complete_cb(struct rtio *ctx,
                                         const struct rtio_sqe *sqe,
                                         void *arg)
{
    const struct device *dev = (const struct device *)arg;
    struct max32664c_data *data = dev->data;
    struct rtio_iodev_sqe *iodev_sqe = (struct rtio_iodev_sqe *)sqe->userdata;
    const struct max32664c_config *cfg = dev->config;

    LOG_DBG("Stream completion callback called: iodev_sqe=%p", (void*)iodev_sqe);

    /* Set MFIO back to high (release gate) */
    gpio_pin_set_dt(&cfg->mfio_gpio, 1);

    /* Do NOT consume CQEs here - let the application handle that!
     * The driver's job is just to submit data to the RTIO context.
     * The application's streaming thread will consume the CQEs. */
    
    LOG_DBG("Completing streaming SQE with %d FIFO samples", data->fifo_count);

    /* If we have a streaming buffer, attempt to set decoder header fields so
     * the application sees the expected encoded layout. The driver raw read
     * returns FIFO samples starting at offset MAX32664C_SENSOR_DATA_OFFSET.
     * We populate header.timestamp, chip_op_mode and num_samples here. */
    if (data->streaming_buf && data->streaming_buf_len >= sizeof(struct max32664c_encoded_data)) {
        struct max32664c_encoded_data *edata = (struct max32664c_encoded_data *)data->streaming_buf;
        /* fill timestamp */
        edata->header.timestamp = data->timestamp;
        /* set operation mode and sample count */
        edata->chip_op_mode = data->op_mode;
        edata->num_samples = data->fifo_count;
        LOG_DBG("Populated encoded header: op_mode=%d, num_samples=%d", edata->chip_op_mode, edata->num_samples);
    } else {
        LOG_DBG("No streaming buffer available to populate header (buf=%p, len=%u)", data->streaming_buf, data->streaming_buf_len);
    }

    /* If an application-level handler exists, forward the populated buffer
     * directly for immediate decoding. This lets the driver hand ownership
     * of the mempool buffer to the app decoder before the driver clears
     * its pointers (the app will call rtio_release_buffer when done).
     * The symbol is weakly linked via a header so the app can provide the
     * implementation. */
#if APP_PPG_WRIST_FORWARD
    extern void hpi_ppg_wrist_handle_stream_buffer(uint8_t *buf, uint32_t buf_len);
    if (data->streaming_buf && data->streaming_buf_len > 0) {
        hpi_ppg_wrist_handle_stream_buffer(data->streaming_buf, data->streaming_buf_len);
    }
#endif

    /* Complete this streaming SQE - the application will see it in the CQE */
    rtio_iodev_sqe_ok(iodev_sqe, data->fifo_count);

    /* Clear the streaming SQE so a new one can be set up for the next stream request */
    data->streaming_sqe = NULL;
    /* Clear the streaming buffer pointers - application owns the buffer after completion */
    data->streaming_buf = NULL;
    data->streaming_buf_len = 0;
    LOG_DBG("Cleared streaming_sqe, continuing polling");

    /* Continue polling - if the application starts streaming again, 
     * it will set a new streaming_sqe */
    k_work_reschedule(&data->poll_work, K_MSEC(MAX32664C_POLL_INTERVAL_MS));
}


void max32664c_submit(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe)
{
    struct max32664c_data *data = dev->data;
    uint32_t min_buf_len = sizeof(struct max32664c_encoded_data);
    int rc;
    uint8_t *buf;
    uint32_t buf_len;

    struct rtio *r = data->r;
    struct rtio_iodev *iodev = data->iodev;
    struct rtio_sqe *write_sqe, *read_sqe, *complete_sqe;

    /* If this request is a streaming request (sensor_read_config->is_streaming)
     * then prepare the gpio interrupt and store the streaming SQE. The actual
     * FIFO read will be performed from the MFIO event handler.
     */
    struct sensor_read_config *read_cfg = (struct sensor_read_config *)iodev_sqe->sqe.iodev->data;
    if (read_cfg && read_cfg->is_streaming) {
        /* Store streaming SQE and start polling for DRDY */
        data->streaming_sqe = iodev_sqe;
        k_work_reschedule(&data->poll_work, K_MSEC(MAX32664C_POLL_INTERVAL_MS));
        return;
    }

    /* If RTIO not available, handle fallback in other code path */
    if (!r || !iodev) {
        /* Not handling here; caller should use fallback implementation in async.c */
        rtio_iodev_sqe_err(iodev_sqe, -ENOTSUP);
        return;
    }

    /* Acquire the buffer for RTIO to write into */
    rc = rtio_sqe_rx_buf(iodev_sqe, min_buf_len, min_buf_len, &buf, &buf_len);
    if (rc != 0) {
        LOG_ERR("Failed to get a read buffer of size %u bytes", min_buf_len);
        rtio_iodev_sqe_err(iodev_sqe, rc);
        return;
    }

    /* Prepare SQEs: write command then chained read into buf, then completion callback */
    write_sqe = rtio_sqe_acquire(r);
    read_sqe = rtio_sqe_acquire(r);
    complete_sqe = rtio_sqe_acquire(r);
    if (!write_sqe || !read_sqe || !complete_sqe) {
        LOG_ERR("Failed to acquire RTIO SQEs");
        rtio_iodev_sqe_err(iodev_sqe, -ENOMEM);
        return;
    }

    uint8_t cmd_wr[2] = {0x12, 0x01}; /* FIFO/data read command */

    rtio_sqe_prep_tiny_write(write_sqe, iodev, RTIO_PRIO_HIGH, cmd_wr, sizeof(cmd_wr), NULL);
    write_sqe->flags |= RTIO_SQE_TRANSACTION;

    rtio_sqe_prep_read(read_sqe, iodev, RTIO_PRIO_HIGH, buf, buf_len, NULL);
    read_sqe->iodev_flags |= RTIO_IODEV_I2C_STOP | RTIO_IODEV_I2C_RESTART;
    read_sqe->flags |= RTIO_SQE_CHAINED;

    rtio_sqe_prep_callback_no_cqe(complete_sqe, max32664c_complete_result, (void *)dev, iodev_sqe);

    rtio_submit(r, 0);

    return;
}


/* Streaming event handler called from MFIO gpio callback. This will be executed
 * in interrupt context via the gpio callback; it should queue RTIO work to read
 * FIFO data and complete the stored streaming iodev_sqe. The gpio interrupt is
 * disabled in the gpio callback and will be re-enabled in the completion
 * callback below.
 */
void max32664c_stream_event_handler(const struct device *dev)
{
    struct max32664c_data *data = dev->data;
    const struct max32664c_config *config = dev->config;

    struct rtio_iodev_sqe *streaming_sqe = data->streaming_sqe;
    struct rtio *r = data->r;
    struct rtio_iodev *iodev = data->iodev;

    LOG_DBG("Stream event handler called: streaming_sqe=%p, r=%p, iodev=%p", 
            (void*)streaming_sqe, (void*)r, (void*)iodev);

    if (!streaming_sqe || !r || !iodev) {
        LOG_DBG("Missing resources - streaming_sqe=%p, r=%p, iodev=%p", 
                (void*)streaming_sqe, (void*)r, (void*)iodev);
        /* Nothing to do; reschedule next poll if streaming is still active */
        if (data->streaming_sqe) {
            k_work_reschedule(&data->poll_work, K_MSEC(MAX32664C_POLL_INTERVAL_MS));
        }
        return;
    }

    /* Read FIFO count (blocking RTIO call inside helper) */
    int fifo = max32664c_get_fifo_count(dev);
    LOG_DBG("FIFO count: %d", fifo);
    if (fifo < 0) {
        LOG_ERR("Failed to get FIFO count: %d", fifo);
        k_work_reschedule(&data->poll_work, K_MSEC(MAX32664C_POLL_INTERVAL_MS));
        return;
    }

    if (fifo == 0) {
        LOG_DBG("No data in FIFO, continue polling");
        /* No data in FIFO, continue polling */
        k_work_reschedule(&data->poll_work, K_MSEC(MAX32664C_POLL_INTERVAL_MS));
        return;
    }

    /* Do not hard-clamp here; the number of samples we can read per submit
     * depends on the RTIO mempool buffer length we acquire below. We'll
     * clamp to the buffer capacity after acquiring the buffer so each
     * RTIO read pulls as many samples as possible and reduces the number
     * of concurrent mempool blocks needed to drain the FIFO. */
    data->fifo_count = fifo;
    LOG_DBG("Processing %d FIFO samples (will clamp based on buffer capacity)", fifo);

    /* For streaming, complete the current streaming SQE with data */
    /* This will generate a completion event that the application can consume */
    /* Then immediately reschedule for the next polling cycle */

    /* Acquire buffer for RTIO into the streaming SQE. Reserve space at the
     * start of the buffer for the encoded header and arrays; we'll read raw
     * FIFO bytes into the buffer after that header region and then assemble
     * the encoded_data in-place before completing the SQE. */
    uint8_t *buf;
    uint32_t buf_len;
    size_t header_sz = sizeof(struct max32664c_encoded_data);
    int rc = rtio_sqe_rx_buf(streaming_sqe, header_sz, header_sz, &buf, &buf_len);
    LOG_DBG("RTIO buffer acquisition: rc=%d, buf=%p, buf_len=%u", rc, (void*)buf, buf_len);
    if (rc != 0) {
        LOG_ERR("Failed to get rx buffer: %d", rc);
        k_work_reschedule(&data->poll_work, K_MSEC(MAX32664C_POLL_INTERVAL_MS));
        return;
    }

    /* Save buffer pointer/len in driver data so completion callback can set header */
    data->streaming_buf = buf;
    data->streaming_buf_len = buf_len;

    /* Prepare RTIO SQEs: write command then chained read into buffer, then completion cb */
    struct rtio_sqe *write_sqe = rtio_sqe_acquire(r);
    struct rtio_sqe *read_sqe = rtio_sqe_acquire(r);
    struct rtio_sqe *cb_sqe = rtio_sqe_acquire(r);

    LOG_DBG("RTIO SQE acquisition: write_sqe=%p, read_sqe=%p, cb_sqe=%p", 
            (void*)write_sqe, (void*)read_sqe, (void*)cb_sqe);

    if (!write_sqe || !read_sqe || !cb_sqe) {
        LOG_ERR("Failed to acquire RTIO SQEs");
        k_work_reschedule(&data->poll_work, K_MSEC(MAX32664C_POLL_INTERVAL_MS));
        return;
    }

    uint8_t cmd_wr[2] = {0x12, 0x01};
    size_t sample_len = 62; /* conservative default; decoder will parse based on op_mode */
    /* Determine how many samples fit into the acquired buffer and clamp fifo
     * so we read as many samples as the buffer can hold. */
    size_t max_payload_bytes = 0;
    if (buf_len > header_sz + MAX32664C_SENSOR_DATA_OFFSET) {
        max_payload_bytes = buf_len - header_sz - MAX32664C_SENSOR_DATA_OFFSET;
    } else {
        max_payload_bytes = 0;
    }
    size_t max_samples_from_buf = (sample_len > 0) ? (max_payload_bytes / sample_len) : 0;
    if ((size_t)fifo > max_samples_from_buf) {
        LOG_DBG("Clamping FIFO samples from %d to %zu based on buffer capacity", fifo, max_samples_from_buf);
        fifo = (int)max_samples_from_buf;
    }

    /* raw FIFO bytes to read (sensor payload only) */
    size_t raw_fifo_len = (sample_len * fifo) + MAX32664C_SENSOR_DATA_OFFSET;
    /* total RTIO read will be placed after the header region */
    size_t read_len = 0;
    if (header_sz + raw_fifo_len > buf_len) {
        /* clamp raw_fifo_len so header + raw fits */
        if (buf_len > header_sz) {
            raw_fifo_len = buf_len - header_sz;
        } else {
            raw_fifo_len = 0;
        }
    }
    read_len = raw_fifo_len;

    LOG_DBG("Read length: %zu (sample_len=%zu, fifo=%d, buf_len=%u)", 
            read_len, sample_len, fifo, buf_len);

    /* MFIO control - ensure proper timing for I2C transaction */
    LOG_DBG("Setting MFIO low for I2C transaction");
    gpio_pin_set_dt(&config->mfio_gpio, 0);
    /* Give the sensor time to prepare for the transaction */
    k_sleep(K_USEC(500));  /* Increased from 300us to 500us */

    /* Prepare write SQE - send read FIFO command */
    rtio_sqe_prep_tiny_write(write_sqe, iodev, RTIO_PRIO_HIGH, cmd_wr, sizeof(cmd_wr), NULL);
    write_sqe->flags |= RTIO_SQE_TRANSACTION;

    /* Prepare read SQE - read the sensor data */
    /* read into buf + header_sz so header region remains for encoded data */
    rtio_sqe_prep_read(read_sqe, iodev, RTIO_PRIO_HIGH, buf + header_sz, read_len, NULL);
    read_sqe->iodev_flags |= RTIO_IODEV_I2C_STOP | RTIO_IODEV_I2C_RESTART;
    read_sqe->flags |= RTIO_SQE_CHAINED;

    /* Completion callback: consume cqe and finish the streaming_sqe, then continue streaming */
    rtio_sqe_prep_callback(cb_sqe, max32664c_stream_complete_cb, (void *)dev, streaming_sqe);

    LOG_DBG("Submitting RTIO chain (write cmd: 0x%02x 0x%02x, read %zu bytes)", 
            cmd_wr[0], cmd_wr[1], read_len);

    /* Submit all SQEs */
    rc = rtio_submit(r, 0);
    LOG_DBG("RTIO submit result: %d", rc);
    if (rc) {
        LOG_ERR("Failed to submit streaming RTIO: %d", rc);
        gpio_pin_set_dt(&config->mfio_gpio, 1);
        k_work_reschedule(&data->poll_work, K_MSEC(MAX32664C_POLL_INTERVAL_MS));
        return;
    }

    LOG_DBG("RTIO operations submitted successfully, waiting for completion");
    /* Keep the main streaming SQE active so polling continues */
    /* The completion callback will generate individual data completion events */
}


int max32664c_get_fifo_count(const struct device *dev)
{
    const struct max32664c_config *config = dev->config;
    struct max32664c_data *data = dev->data;
    uint8_t rd_buf[2] = {0x00, 0x00};
    uint8_t wr_buf[2] = {0x12, 0x00};
    uint8_t fifo_count = 0;

    struct rtio *ctx = data->r;
    struct rtio_iodev *iodev = data->iodev;
    struct rtio_sqe *write_sqe, *read_sqe;
    struct rtio_cqe *cqe;
    int err = 0;

    /* If RTIO is not available, fall back to blocking I2C */
    if (!ctx || !iodev) {
        gpio_pin_set_dt(&config->mfio_gpio, 0);
        k_sleep(K_USEC(300));
        max32664c_i2c_write(dev, wr_buf, sizeof(wr_buf));
        max32664c_i2c_read(dev, rd_buf, sizeof(rd_buf));
        gpio_pin_set_dt(&config->mfio_gpio, 1);
        fifo_count = rd_buf[1];
        return (int)fifo_count;
    }

    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));

    /* Submit write SQE */
    write_sqe = rtio_sqe_acquire(ctx);
    if (!write_sqe) {
        gpio_pin_set_dt(&config->mfio_gpio, 1);
        return -ENOMEM;
    }

    rtio_sqe_prep_tiny_write(write_sqe, iodev, RTIO_PRIO_HIGH, wr_buf, sizeof(wr_buf), NULL);
    write_sqe->iodev_flags |= RTIO_IODEV_I2C_STOP;

    err = rtio_submit(ctx, 0);
    if (err) {
        gpio_pin_set_dt(&config->mfio_gpio, 1);
        return err;
    }

    /* Wait for write completion */
    cqe = rtio_cqe_consume_block(ctx);
    if (cqe != NULL) {
        if (cqe->result < 0) {
            err = cqe->result;
        }
        rtio_cqe_release(ctx, cqe);
    }
    while ((cqe = rtio_cqe_consume(ctx)) != NULL) {
        if (cqe->result < 0) {
            err = cqe->result;
        }
        rtio_cqe_release(ctx, cqe);
    }

    if (err < 0) {
        gpio_pin_set_dt(&config->mfio_gpio, 1);
        return err;
    }

    /* Small device command delay before read */
    k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));

    /* Submit read SQE */
    read_sqe = rtio_sqe_acquire(ctx);
    if (!read_sqe) {
        gpio_pin_set_dt(&config->mfio_gpio, 1);
        return -ENOMEM;
    }

    rtio_sqe_prep_read(read_sqe, iodev, RTIO_PRIO_HIGH, rd_buf, sizeof(rd_buf), NULL);
    read_sqe->iodev_flags |= RTIO_IODEV_I2C_STOP;

    err = rtio_submit(ctx, 0);
    if (err) {
        gpio_pin_set_dt(&config->mfio_gpio, 1);
        return err;
    }

    /* Wait for read completion */
    cqe = rtio_cqe_consume_block(ctx);
    if (cqe != NULL) {
        if (cqe->result < 0) {
            err = cqe->result;
        }
        rtio_cqe_release(ctx, cqe);
    }
    while ((cqe = rtio_cqe_consume(ctx)) != NULL) {
        if (cqe->result < 0) {
            err = cqe->result;
        }
        rtio_cqe_release(ctx, cqe);
    }

    gpio_pin_set_dt(&config->mfio_gpio, 1);

    if (err < 0) {
        return err;
    }

    fifo_count = rd_buf[1];
    return (int)fifo_count;
}


/* Converted blocking sample-fetch moved here. This function keeps the same
 * prototype as the previous helper so callers can call it directly. It will
 * use RTIO if available or fall back to blocking I2C inside.
 */
int max32664c_async_sample_fetch(const struct device *dev, uint32_t green_samples[16], uint32_t ir_samples[16], uint32_t red_samples[16],
                                 uint32_t *num_samples, uint16_t *spo2, uint8_t *spo2_conf, uint8_t *spo2_valid_percent_complete, uint8_t *spo2_low_quality,
                                 uint8_t *spo2_excessive_motion, uint8_t *spo2_low_pi, uint8_t *spo2_state, uint16_t *hr, uint8_t *hr_conf, uint16_t *rtor,
                                 uint8_t *rtor_conf, uint8_t *scd_state, uint8_t *activity_class, uint32_t *steps_run, uint32_t *steps_walk, uint8_t *chip_op_mode)
{
    struct max32664c_data *data = dev->data;
    const struct max32664c_config *config = dev->config;

    uint8_t wr_buf[2] = {0x12, 0x01};
    static uint8_t buf[2048];

    static int sample_len = 62;

#define MAX32664C_ALGO_DATA_OFFSET 24

    uint8_t hub_stat = max32664c_read_hub_status(dev);
    if (hub_stat & MAX32664C_HUB_STAT_DRDY_MASK)
    {
        int fifo_count = max32664c_get_fifo_count(dev);

        if (fifo_count > 16)
        {
            fifo_count = 16;
        }

        *num_samples = fifo_count;

        if (fifo_count > 0)
        {
            if (data->op_mode == MAX32664C_OP_MODE_ALGO_AGC || data->op_mode == MAX32664C_OP_MODE_ALGO_AEC)
            {
                sample_len = 48; // 18 PPG + 6 accel data + 24 algo
            }
            else if (data->op_mode == MAX32664C_OP_MODE_ALGO_EXTENDED)
            {
                sample_len = 70; // 18 data + 52 algo
            }

            *chip_op_mode = data->op_mode;

            struct rtio *ctx = data->r;
            struct rtio_iodev *iodev = data->iodev;
            struct rtio_sqe *write_sqe = NULL, *read_sqe = NULL;
            struct rtio_cqe *cqe = NULL;
            int err = 0;
            size_t read_len = (sample_len * fifo_count) + MAX32664C_SENSOR_DATA_OFFSET;

            if (read_len > sizeof(buf)) {
                /* clamp to buffer size */
                read_len = sizeof(buf);
            }

            /* Use RTIO if available; otherwise fall back to blocking I2C */
            if (ctx && iodev) {
                gpio_pin_set_dt(&config->mfio_gpio, 0);
                k_sleep(K_USEC(300));

                /* write command SQE */
                write_sqe = rtio_sqe_acquire(ctx);
                if (!write_sqe) {
                    gpio_pin_set_dt(&config->mfio_gpio, 1);
                    return -ENOMEM;
                }
                rtio_sqe_prep_tiny_write(write_sqe, iodev, RTIO_PRIO_HIGH, wr_buf, sizeof(wr_buf), NULL);
                write_sqe->iodev_flags |= RTIO_IODEV_I2C_STOP;

                err = rtio_submit(ctx, 0);
                if (err) {
                    gpio_pin_set_dt(&config->mfio_gpio, 1);
                    return err;
                }

                /* wait for write completion */
                cqe = rtio_cqe_consume_block(ctx);
                if (cqe != NULL) {
                    if (cqe->result < 0) {
                        err = cqe->result;
                    }
                    rtio_cqe_release(ctx, cqe);
                }
                while ((cqe = rtio_cqe_consume(ctx)) != NULL) {
                    if (cqe->result < 0) {
                        err = cqe->result;
                    }
                    rtio_cqe_release(ctx, cqe);
                }

                if (err < 0) {
                    gpio_pin_set_dt(&config->mfio_gpio, 1);
                    return err;
                }

                k_sleep(K_MSEC(MAX32664C_DEFAULT_CMD_DELAY));

                /* read SQE into local buf */
                read_sqe = rtio_sqe_acquire(ctx);
                if (!read_sqe) {
                    gpio_pin_set_dt(&config->mfio_gpio, 1);
                    return -ENOMEM;
                }
                rtio_sqe_prep_read(read_sqe, iodev, RTIO_PRIO_HIGH, buf, read_len, NULL);
                read_sqe->iodev_flags |= RTIO_IODEV_I2C_STOP;

                err = rtio_submit(ctx, 0);
                if (err) {
                    gpio_pin_set_dt(&config->mfio_gpio, 1);
                    return err;
                }

                /* wait for read completion */
                cqe = rtio_cqe_consume_block(ctx);
                if (cqe != NULL) {
                    if (cqe->result < 0) {
                        err = cqe->result;
                    }
                    rtio_cqe_release(ctx, cqe);
                }
                while ((cqe = rtio_cqe_consume(ctx)) != NULL) {
                    if (cqe->result < 0) {
                        err = cqe->result;
                    }
                    rtio_cqe_release(ctx, cqe);
                }

                gpio_pin_set_dt(&config->mfio_gpio, 1);

                if (err < 0) {
                    return err;
                }

                /* successful RTIO read filled `buf` */
            } else {
                /* fallback blocking path */
                gpio_pin_set_dt(&config->mfio_gpio, 0);
                k_sleep(K_USEC(300));
                max32664c_i2c_write(dev, wr_buf, sizeof(wr_buf));

                max32664c_i2c_read(dev, buf, ((sample_len * fifo_count) + MAX32664C_SENSOR_DATA_OFFSET));
                k_sleep(K_USEC(300));
                gpio_pin_set_dt(&config->mfio_gpio, 1);
            }

            for (int i = 0; i < fifo_count; i++)
            {
                uint32_t led_green = (uint32_t)buf[(sample_len * i) + 0 + MAX32664C_SENSOR_DATA_OFFSET] << 16;
                led_green |= (uint32_t)buf[(sample_len * i) + 1 + MAX32664C_SENSOR_DATA_OFFSET] << 8;
                led_green |= (uint32_t)buf[(sample_len * i) + 2 + MAX32664C_SENSOR_DATA_OFFSET];

                green_samples[i] = led_green;

                uint32_t led_ir = (uint32_t)buf[(sample_len * i) + 3 + MAX32664C_SENSOR_DATA_OFFSET] << 16;
                led_ir |= (uint32_t)buf[(sample_len * i) + 4 + MAX32664C_SENSOR_DATA_OFFSET] << 8;
                led_ir |= (uint32_t)buf[(sample_len * i) + 5 + MAX32664C_SENSOR_DATA_OFFSET];

                ir_samples[i] = led_ir;

                uint32_t led_red = (uint32_t)buf[(sample_len * i) + 6 + MAX32664C_SENSOR_DATA_OFFSET] << 16;
                led_red |= (uint32_t)buf[(sample_len * i) + 7 + MAX32664C_SENSOR_DATA_OFFSET] << 8;
                led_red |= (uint32_t)buf[(sample_len * i) + 8 + MAX32664C_SENSOR_DATA_OFFSET];

                red_samples[i] = led_red;

                uint16_t hr_val = (uint16_t)buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 1 + MAX32664C_SENSOR_DATA_OFFSET] << 8;
                hr_val |= (uint16_t)buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 2 + MAX32664C_SENSOR_DATA_OFFSET];

                *hr = (hr_val / 10);

                *hr_conf = buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 3 + MAX32664C_SENSOR_DATA_OFFSET];

                uint16_t rtor_val = (uint16_t)buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 4 + MAX32664C_SENSOR_DATA_OFFSET] << 8;
                rtor_val |= (uint16_t)buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 5 + MAX32664C_SENSOR_DATA_OFFSET];

                *rtor = (rtor_val / 10);

                *rtor_conf = buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 6 + MAX32664C_SENSOR_DATA_OFFSET];

                *spo2_conf = buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 10 + MAX32664C_SENSOR_DATA_OFFSET];

                uint16_t spo2_val = (uint16_t)buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 11 + MAX32664C_SENSOR_DATA_OFFSET] << 8;
                spo2_val |= (uint16_t)buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 12 + MAX32664C_SENSOR_DATA_OFFSET];

                *spo2 = (spo2_val / 10);

                *spo2_valid_percent_complete = buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 13 + MAX32664C_SENSOR_DATA_OFFSET];
                *spo2_low_quality = buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 14 + MAX32664C_SENSOR_DATA_OFFSET];
                *spo2_excessive_motion = buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 15 + MAX32664C_SENSOR_DATA_OFFSET];
                *spo2_low_pi = buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 16 + MAX32664C_SENSOR_DATA_OFFSET];
                *spo2_state = buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 18 + MAX32664C_SENSOR_DATA_OFFSET];

                *scd_state = buf[(sample_len * i) + MAX32664C_ALGO_DATA_OFFSET + 19 + MAX32664C_SENSOR_DATA_OFFSET];
            }
        }
    }
    return 0;
}