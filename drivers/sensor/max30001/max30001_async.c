#include <zephyr/drivers/sensor.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(MAX30001_ASYNC, CONFIG_SENSOR_LOG_LEVEL);

#include "max30001.h"

static int max30001_async_sample_fetch_lon_detect(const struct device *dev, uint8_t *lon_state)
{
    uint32_t max30001_status = 0;

    max30001_status = max30001_read_status(dev);

    // printk("Status: %x\n", max30001_status);
    if ((max30001_status & MAX30001_STATUS_MASK_LONINT) == MAX30001_STATUS_MASK_LONINT)
    {
        // printk("LONINT\n");
        *lon_state = 1;
    }
    else
    {
        *lon_state = 0;
    }

    return 0;
}

static int max30001_async_sample_fetch(const struct device *dev,
                                       uint8_t *num_samples_ecg, uint8_t *num_samples_bioz, int32_t ecg_samples[32],
                                       int32_t bioz_samples[32], uint16_t *rri, uint16_t *hr, uint8_t *rrint,
                                       uint8_t *ecg_lead_off, uint8_t *bioz_lead_off)
{
    struct max30001_data *data = dev->data;
    const struct max30001_config *config = dev->config;

    uint32_t max30001_status, max30001_mngr_int = 0;
    uint32_t e_fifo_num_bytes, b_fifo_num_bytes;

    uint32_t e_fifo_num_samples = 0;
    uint32_t b_fifo_num_samples = 0;

    uint8_t buf_ecg[512];
    uint8_t buf_bioz[512];

    uint8_t cmd_tx_ecg_fifo_burst = ((ECG_FIFO_BURST << 1) | RREG);
    const struct spi_buf tx_buf_ecg[1] = {{.buf = &cmd_tx_ecg_fifo_burst, .len = 1}};
    const struct spi_buf_set tx_ecg = {.buffers = tx_buf_ecg, .count = 1};

    uint8_t cmd_tx_bioz_fifo_burst = ((BIOZ_FIFO_BURST << 1) | RREG);
    const struct spi_buf tx_buf_bioz[1] = {{.buf = &cmd_tx_bioz_fifo_burst, .len = 1}};
    const struct spi_buf_set tx_bioz = {.buffers = tx_buf_bioz, .count = 1};

    uint32_t max30001_rtor = 0;

    max30001_status = max30001_read_status(dev);

    if ((max30001_status & MAX30001_STATUS_MASK_DCLOFF) == MAX30001_STATUS_MASK_DCLOFF)
    {
        // printk("LOff");
        data->ecg_lead_off = 1;
        *ecg_lead_off = 1;
    }
    else
    {
        data->ecg_lead_off = 0;
        *ecg_lead_off = 0;
    }

    /*while ((max30001_status & MAX30001_STATUS_MASK_EINT) != MAX30001_STATUS_MASK_EINT)
    {
        max30001_status = max30001_read_status(dev);
    }*/

    if ((max30001_status & MAX30001_STATUS_MASK_EINT) == MAX30001_STATUS_MASK_EINT) // EINT bit is set, FIFO is full
    // while ((max30001_status & MAX30001_STATUS_MASK_EINT) != MAX30001_STATUS_MASK_EINT) // EINT bit is set, FIFO is full
    {
        max30001_mngr_int = max30001_read_reg(dev, MNGR_INT);
        e_fifo_num_samples = (((max30001_mngr_int & MAX30001_INT_MASK_EFIT) >> MAX30001_INT_SHIFT_EFIT) + 1); // No of samples = EFIT + 1
        e_fifo_num_bytes = ((e_fifo_num_samples * 3));                                                        // 24 bit register + 1 dummy byte

        // printk("ES: %d ", e_fifo_num_samples);

        if (e_fifo_num_samples > 16)
        {
            e_fifo_num_samples = 16;
        }
        *num_samples_ecg = e_fifo_num_samples;

        //_max30001_read_ecg_fifo(dev, e_fifo_num_bytes);

        struct spi_buf rx_ecg_buf[2] = {{.buf = NULL, .len = 1}, {.buf = &buf_ecg, .len = (e_fifo_num_bytes)}}; // 24 bit register + 1 dummy byte
        const struct spi_buf_set rx_ecg = {.buffers = rx_ecg_buf, .count = 2};

        spi_transceive_dt(&config->spi, &tx_ecg, &rx_ecg);

        b_fifo_num_samples = (((max30001_mngr_int & MAX30001_INT_MASK_BFIT) >> MAX30001_INT_SHIFT_BFIT) + 1);
        b_fifo_num_bytes = (b_fifo_num_samples * 3);
        *num_samples_bioz = b_fifo_num_samples;

        struct spi_buf rx_bioz_buf[2] = {{.buf = NULL, .len = 1}, {.buf = &buf_bioz, .len = b_fifo_num_bytes}}; // 24 bit register
        const struct spi_buf_set rx_bioz = {.buffers = rx_bioz_buf, .count = 2};

        spi_transceive_dt(&config->spi, &tx_bioz, &rx_bioz);

        // Read all the samples from the FIFO
        for (int i = 0; i < e_fifo_num_samples; i++)
        {
            uint32_t etag = ((((uint8_t)buf_ecg[i * 3 + 2]) & 0x38) >> 3);

            // printk("E %x ", etag);

            if ((etag == 0x00) || (etag == 0x01) || (etag == 0x02)) // Valid sample
            {
                uint32_t uecgtemp = (uint32_t)(((uint32_t)buf_ecg[i * 3] << 16 | (uint32_t)buf_ecg[i * 3 + 1] << 8) | (uint32_t)(buf_ecg[i * 3 + 2] & 0xC0));
                uecgtemp = (uint32_t)(uecgtemp << 8);

                int32_t secgtemp = (int32_t)uecgtemp;
                secgtemp = (int32_t)secgtemp >> 6;

                ecg_samples[i] = (int32_t)(secgtemp); //((secgtemp*1000*1000)/2621440);   // Convert to microvolts
                // printf("%d ", ecg_samples[i]);
            }
            else if (etag == 0x06)
            {
                break;
            }
            else if (etag == 0x07) // FIFO Overflow
            {
                // printk("EOVF ");
                max30001_fifo_reset(dev);
                // max30001_synch(dev);
                break;
            }
        }

        // Read all the samples from the FIFO
        for (int i = 0; i < b_fifo_num_samples; i++)
        {
            uint32_t btag = ((((uint8_t)buf_bioz[i * 3 + 2]) & 0x07));

            // printk("B %x ", btag);

            if ((btag == 0x00) || (btag == 0x02)) // Valid sample
            {
                uint32_t u_bioz_temp = (uint32_t)(((uint32_t)buf_bioz[i * 3] << 16 | (uint32_t)buf_bioz[i * 3 + 1] << 8) | (uint32_t)(buf_bioz[i * 3 + 2] & 0xF0));
                u_bioz_temp = (uint32_t)(u_bioz_temp << 8);

                int32_t s_bioz_temp = (int32_t)u_bioz_temp;
                s_bioz_temp = (int32_t)(s_bioz_temp >> 4);
                // printf("%d ", secgtemp);

                bioz_samples[i] = s_bioz_temp;
            }
            else if (btag == 0x06)
            {
                break;
            }
            else if (btag == 0x07) // FIFO Overflow
            {
                // printk("BOVF ");
                max30001_fifo_reset(dev);
                // max30001_synch(dev);
                break;
            }
        }

        /*max30001_rtor = max30001_read_reg(dev, RTOR);
        if (max30001_rtor > 0)
        {
            data->lastRRI = (uint16_t)((double)((max30001_rtor >> 10) * 7.8125)*1000);
            data->lastHR = (uint16_t)(60 * 1000 / data->lastRRI);

            *hr = data->lastHR;
            *rri = data->lastRRI;
        }
        if ((max30001_status & MAX30001_STATUS_MASK_RRINT) == MAX30001_STATUS_MASK_RRINT)
        {
            *rrint = 1;
        }
        else
        {
            *rrint = 0;
        }*/
    }

    /*if ((max30001_status & MAX30001_STATUS_MASK_BINT) == MAX30001_STATUS_MASK_BINT)
    {
        max30001_mngr_int = max30001_read_reg(dev, MNGR_INT);


    }*/

    if ((max30001_status & MAX30001_STATUS_MASK_RRINT) == MAX30001_STATUS_MASK_RRINT)
    {
        max30001_rtor = max30001_read_reg(dev, RTOR);
        if (max30001_rtor > 0)
        {
            data->lastRRI = (uint16_t)(max30001_rtor >> 10) * 8;
            data->lastHR = (uint16_t)(60 * 1000 / data->lastRRI);

            *hr = data->lastHR;
            *rri = data->lastRRI;
        }
    }

    return 0;
}

int max30001_submit(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe)
{
    struct max30001_data *data = dev->data;

    uint32_t m_min_buf_len = sizeof(struct max30001_encoded_data);

    uint8_t *buf;
    uint32_t buf_len;

    struct max30001_encoded_data *m_edata;

    int ret = 0;

    ret = rtio_sqe_rx_buf(iodev_sqe, m_min_buf_len, m_min_buf_len, &buf, &buf_len);
    if (ret != 0)
    {
        LOG_ERR("Failed to get a read buffer of size %u bytes", m_min_buf_len);
        rtio_iodev_sqe_err(iodev_sqe, ret);
        return ret;
    }

    if (data->chip_op_mode == MAX30001_OP_MODE_LON_DETECT)
    {
        m_edata = (struct max30001_encoded_data *)buf;
        m_edata->header.timestamp = k_ticks_to_ns_floor64(k_uptime_ticks());

        m_edata->chip_op_mode = MAX30001_OP_MODE_LON_DETECT;

        ret = max30001_async_sample_fetch_lon_detect(dev, &m_edata->lon_state);
    }
    else
    {
        m_edata = (struct max30001_encoded_data *)buf;
        m_edata->header.timestamp = k_ticks_to_ns_floor64(k_uptime_ticks());

        m_edata->chip_op_mode = MAX30001_OP_MODE_STREAM;
        ret = max30001_async_sample_fetch(dev, &m_edata->num_samples_ecg, &m_edata->num_samples_bioz,
                                          m_edata->ecg_samples, m_edata->bioz_samples, &m_edata->rri, &m_edata->hr, &m_edata->rrint,
                                          &m_edata->ecg_lead_off, &m_edata->bioz_lead_off);
    }
    if (ret != 0)
    {
        rtio_iodev_sqe_err(iodev_sqe, ret);
        return ret;
    }

    rtio_iodev_sqe_ok(iodev_sqe, 0);

    return 0;
}