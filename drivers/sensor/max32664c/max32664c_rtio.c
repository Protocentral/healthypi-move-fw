/* RTIO submit implementation moved from max32664c_async.c
 * Provides non-blocking RTIO submission and completion callback.
 */

#include <zephyr/rtio/work.h>
#include <zephyr/drivers/sensor_clock.h>
#include <zephyr/logging/log.h>

#include "max32664c_sensor.h"
#include <zephyr/drivers/sensor.h>

LOG_MODULE_REGISTER(MAX32664C_RTIO, CONFIG_MAX32664C_LOG_LEVEL);

/* Forward declarations */
void max32664c_stream_event_handler(const struct device *dev);

/* Polling work handler for DRDY status checking */
void max32664c_poll_work_handler(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct max32664c_data *data = CONTAINER_OF(dwork, struct max32664c_data, poll_work);
    const struct device *dev = data->dev;
    static int poll_count = 0;

    /* Only poll if we have an active streaming request */
    if (!data->streaming_sqe) {
        LOG_DBG("Poll work: no streaming SQE, stopping");
        return;
    }

    /* Read hub status to check DRDY bit */
    uint8_t hub_status = max32664c_read_hub_status(dev);
    
    /* Log every 50th poll to monitor activity */
    if (++poll_count % 50 == 0) {
        LOG_DBG("Poll #%d: hub_status=0x%02x, DRDY=%s", poll_count, hub_status, 
                (hub_status & MAX32664C_HUB_STAT_DRDY_MASK) ? "YES" : "NO");
    }
    
    if (hub_status & MAX32664C_HUB_STAT_DRDY_MASK) {
        /* Record timestamp for streaming */
        uint64_t cycles = 0;
        if (sensor_clock_get_cycles(&cycles) == 0) {
            data->timestamp = sensor_clock_cycles_to_ns(cycles);
        } else {
            data->timestamp = 0;
        }

        LOG_DBG("DRDY detected, triggering stream event handler");
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
    /* Complete this streaming SQE - the application will see it in the CQE */
    rtio_iodev_sqe_ok(iodev_sqe, data->fifo_count);

    /* Clear the streaming SQE so a new one can be set up for the next stream request */
    data->streaming_sqe = NULL;
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

    if (fifo > 16) {
        fifo = 16;
    }

    data->fifo_count = fifo;
    LOG_DBG("Processing %d FIFO samples", fifo);

    /* For streaming, complete the current streaming SQE with data */
    /* This will generate a completion event that the application can consume */
    /* Then immediately reschedule for the next polling cycle */

    /* Acquire buffer for RTIO into the streaming SQE */
    uint8_t *buf;
    uint32_t buf_len;
    int rc = rtio_sqe_rx_buf(streaming_sqe, sizeof(struct max32664c_encoded_data), (sizeof(struct max32664c_encoded_data)), &buf, &buf_len);
    LOG_DBG("RTIO buffer acquisition: rc=%d, buf=%p, buf_len=%u", rc, (void*)buf, buf_len);
    if (rc != 0) {
        LOG_ERR("Failed to get rx buffer: %d", rc);
        k_work_reschedule(&data->poll_work, K_MSEC(MAX32664C_POLL_INTERVAL_MS));
        return;
    }

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
    size_t read_len = (sample_len * fifo) + MAX32664C_SENSOR_DATA_OFFSET;
    if (read_len > buf_len) {
        read_len = buf_len;
    }

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
    rtio_sqe_prep_read(read_sqe, iodev, RTIO_PRIO_HIGH, buf, read_len, NULL);
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