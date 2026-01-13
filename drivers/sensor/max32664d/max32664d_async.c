#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(max32664d_async, CONFIG_SENSOR_LOG_LEVEL);

#include "max32664d.h"
#include <errno.h>

/* Local I2C wrappers to centralize bus calls (keeps parity with max32664c pattern).
 * These are simple wrappers that call the DeviceTree helpers; they can be extended
 * later to use RTIO if needed. */
static int max32664d_i2c_write(const struct i2c_dt_spec *i2c, const void *buf, size_t len)
{
    return i2c_write_dt(i2c, buf, len);
}

static int max32664d_i2c_read(const struct i2c_dt_spec *i2c, void *buf, size_t len)
{
    return i2c_read_dt(i2c, buf, len);
}

#define MAX32664D_SENSOR_DATA_OFFSET 1

/* Helper to read FIFO via I2C while toggling MFIO. Returns 0 on success or negative rc */
static int max32664d_read_fifo_i2c(const struct device *dev, uint8_t *buf, int sample_len, int fifo_count)
{
    const struct max32664d_config *config = dev->config;
    uint8_t wr_buf[2] = {0x12, 0x01};

    if (fifo_count <= 0 || buf == NULL) {
        return -EINVAL;
    }

    gpio_pin_set_dt(&config->mfio_gpio, 0);
    k_sleep(K_USEC(300));

    int rc = max32664d_i2c_write(&config->i2c, wr_buf, sizeof(wr_buf));
    if (rc != 0) {
        gpio_pin_set_dt(&config->mfio_gpio, 1);
        LOG_ERR("I2C write (FIFO read cmd) failed: %d", rc);
        return rc;
    }

    rc = max32664d_i2c_read(&config->i2c, buf, ((sample_len * fifo_count) + MAX32664D_SENSOR_DATA_OFFSET));
    if (rc != 0) {
        gpio_pin_set_dt(&config->mfio_gpio, 1);
        LOG_ERR("I2C read (FIFO data) failed: %d", rc);
        return rc;
    }

    k_sleep(K_USEC(300));
    gpio_pin_set_dt(&config->mfio_gpio, 1);
    return 0;
}

static int max32664_async_sample_fetch(const struct device *dev,
                                       uint32_t ir_samples[32], uint32_t red_samples[32], uint8_t *num_samples, uint16_t *spo2, uint8_t *spo2_conf,
                                       uint16_t *hr, uint8_t *bpt_status, uint8_t *bpt_progress, uint8_t *bpt_sys, uint8_t *bpt_dia)
{
    struct max32664d_data *data = dev->data;
    const struct max32664d_config *config = dev->config;
    static uint8_t buf[2048];

    int sample_len = 29;

    /* Read hub status once and only proceed if DRDY is set. This mirrors
     * the max32664c pattern and avoids tight busy-wait loops that hammer
     * the I2C/MFIO lines. If no DRDY, return with zero samples. */
    uint8_t hub_stat = max32664d_read_hub_status(dev);
    if (!(hub_stat & MAX32664D_HUB_STAT_DRDY_MASK))
    {
        *num_samples = 0;
        return 0;
    }

    int fifo_count = max32664d_get_fifo_count(dev);
    if (fifo_count > 32)
    {
        fifo_count = 32;
    }

    *num_samples = fifo_count;

    if (fifo_count <= 0)
    {
        return 0;
    }

    if (data->op_mode == MAX32664D_OP_MODE_RAW)
    {
        sample_len = 12;
    }
    else if ((data->op_mode == MAX32664D_OP_MODE_BPT_EST) || (data->op_mode == MAX32664D_OP_MODE_BPT_CAL_START))
    {
        sample_len = 29;
    }

    int rc = max32664d_read_fifo_i2c(dev, buf, sample_len, fifo_count);
    if (rc != 0)
    {
        return rc;
    }


        /*
         * Datasheet note: the MAX32664 provides LED samples as 24-bit MSB-first
         * bytes in the FIFO. The ADC effective resolution is 20 bits; to produce
         * canonical values for consumers we assemble the 24-bit word and
         * right-align it by 4 bits (assembled_24 >> 4) so the resulting
         * integers correspond to the datasheet's 20-bit ADC range.
         */
        for (int i = 0; i < fifo_count; i++)
        {
            uint32_t led_ir = (uint32_t)buf[(sample_len * i) + MAX32664D_SENSOR_DATA_OFFSET] << 16;
            led_ir |= (uint32_t)buf[(sample_len * i) + 1 + MAX32664D_SENSOR_DATA_OFFSET] << 8;
            led_ir |= (uint32_t)buf[(sample_len * i) + 2 + MAX32664D_SENSOR_DATA_OFFSET];
            /* Normalize assembled 24-bit IR value down by 4 bits to provide canonical scale to UI */
            ir_samples[i] = (led_ir >> 4);

            uint32_t led_red = (uint32_t)buf[(sample_len * i) + 3 + MAX32664D_SENSOR_DATA_OFFSET] << 16;
            led_red |= (uint32_t)buf[(sample_len * i) + 4 + MAX32664D_SENSOR_DATA_OFFSET] << 8;
            led_red |= (uint32_t)buf[(sample_len * i) + 5 + MAX32664D_SENSOR_DATA_OFFSET];
            /* Normalize assembled 24-bit Red value down by 4 bits to provide canonical scale to UI */
            red_samples[i] = (led_red >> 4);

        /* bytes 7..12 are ignored in current layout */

        // *bpt_status = buf[(sample_len * i) + 12 + MAX32664D_SENSOR_DATA_OFFSET];
        // *bpt_progress = buf[(sample_len * i) + 13 + MAX32664D_SENSOR_DATA_OFFSET];

        // uint16_t bpt_hr = (uint16_t)buf[(sample_len * i) + 14 + MAX32664D_SENSOR_DATA_OFFSET] << 8;
        // bpt_hr |= (uint16_t)buf[(sample_len * i) + 15 + MAX32664D_SENSOR_DATA_OFFSET];

        // *hr = (bpt_hr / 10);

        // *bpt_sys = buf[(sample_len * i) + 16 + MAX32664D_SENSOR_DATA_OFFSET];
        // *bpt_dia = buf[(sample_len * i) + 17 + MAX32664D_SENSOR_DATA_OFFSET];

        // uint16_t bpt_spo2 = (uint16_t)buf[(sample_len * i) + 18 + MAX32664D_SENSOR_DATA_OFFSET] << 8;
        // bpt_spo2 |= (uint16_t)buf[(sample_len * i) + 19 + MAX32664D_SENSOR_DATA_OFFSET];

        // *spo2 = (bpt_spo2 / 10);
        // *spo2_conf = buf[(sample_len * i) + 25 + MAX32664D_SENSOR_DATA_OFFSET];
        // CORRECT: Official byte offsets (sample_len=29, offset=1)
        int base_idx = (sample_len * i) + MAX32664D_SENSOR_DATA_OFFSET;
        *bpt_status = buf[base_idx + 12];      // Byte 12: bpt_status ✓
        *bpt_progress = buf[base_idx + 13];    // Byte 13: progress ✓
        *hr = ((buf[base_idx + 14] << 8) | buf[base_idx + 15]) / 10;
        *bpt_sys = buf[base_idx + 16];         // Byte 16: systolic
        *bpt_dia = buf[base_idx + 17];         // Byte 17: diastolic
        *spo2 = ((buf[base_idx + 18] << 8) | buf[base_idx + 19]) / 10;
        *spo2_conf = buf[base_idx + 25];       // Keep existing

        uint16_t spo2_r_val = (uint16_t)buf[(sample_len * i) + 20 + MAX32664D_SENSOR_DATA_OFFSET] << 8;
        spo2_r_val |= (uint16_t)buf[(sample_len * i) + 21 + MAX32664D_SENSOR_DATA_OFFSET];
    }

    return 0;
}

int max32664d_submit(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe)
{
    uint32_t min_buf_len = sizeof(struct max32664d_encoded_data);
    int rc;
    uint8_t *buf;
    uint32_t buf_len;

    struct max32664d_encoded_data *edata;
    struct max32664_enc_calib_data *calib_data;
    struct max32664d_data *data = dev->data;

    /* Get the buffer for the frame, it may be allocated dynamically by the rtio context */
    rc = rtio_sqe_rx_buf(iodev_sqe, min_buf_len, min_buf_len, &buf, &buf_len);
    if (rc != 0)
    {
        LOG_ERR("Failed to get a read buffer of size %u bytes", min_buf_len);
        rtio_iodev_sqe_err(iodev_sqe, rc);
        return rc;
    }

    if ((data->op_mode == MAX32664D_OP_MODE_BPT_EST) || (data->op_mode == MAX32664D_OP_MODE_RAW) || (data->op_mode == MAX32664D_OP_MODE_BPT_CAL_START))
    {
        edata = (struct max32664d_encoded_data *)buf;
        edata->header.timestamp = k_ticks_to_ns_floor64(k_uptime_ticks());
        rc = max32664_async_sample_fetch(dev, edata->ir_samples, edata->red_samples, &edata->num_samples, &edata->spo2, &edata->spo2_conf,
                                         &edata->hr, &edata->bpt_status, &edata->bpt_progress, &edata->bpt_sys, &edata->bpt_dia);
    }
    else
    {
        LOG_ERR("Invalid operation mode\n");
        // return 4;
    }

    if (rc != 0)
    {
        // LOG_ERR("Failed: %d", rc);
        rtio_iodev_sqe_err(iodev_sqe, rc);
        return rc;
    }

    rtio_iodev_sqe_ok(iodev_sqe, 0);

    return 0;
}

/*
 * Cancel any running estimation/algorithm and power the sensor down.
 * This stops the algorithm via the existing attribute handler and then
 * places the chip into a safe powered-down state by toggling reset/mfio.
 */
int max32664d_cancel(const struct device *dev)
{
    const struct max32664d_config *config = dev->config;
    struct max32664d_data *data = dev->data;
    struct sensor_value stop_val;

    LOG_INF("max32664d_cancel: stopping algorithm and powering down sensor");

    /* Request the driver to stop estimation/algorithm */
    stop_val.val1 = MAX32664D_ATTR_STOP_EST;
    sensor_attr_set(dev, SENSOR_CHAN_ALL, MAX32664D_ATTR_STOP_EST, &stop_val);

    /* Give the chip a little time to stop */
    k_msleep(50);

    /* Drive MFIO low and reset the device to ensure AFE is disabled */
    gpio_pin_configure_dt(&config->mfio_gpio, GPIO_OUTPUT);
    gpio_pin_set_dt(&config->mfio_gpio, 0);

    gpio_pin_set_dt(&config->reset_gpio, 0);
    k_msleep(10);

    /* Leave reset asserted to keep chip inactive */
    LOG_DBG("max32664d_cancel: MFIO low and RESET asserted");

    data->op_mode = MAX32664D_OP_MODE_IDLE;

    return 0;
}