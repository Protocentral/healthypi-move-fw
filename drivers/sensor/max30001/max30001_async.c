#include <zephyr/drivers/sensor.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MAX30001_ASYNC, CONFIG_SENSOR_LOG_LEVEL);

#include "max30001.h"

static uint32_t _max30001_async_read_status(const struct device *dev)
{
    const struct max30001_config *config = dev->config;
    uint8_t spiTxCommand = ((STATUS << 1) | RREG);

    uint8_t buf[3];

    const struct spi_buf tx_buf[1] = {{.buf = &spiTxCommand, .len = 1}};
    const struct spi_buf_set tx = {.buffers = tx_buf, .count = 1};
    struct spi_buf rx_buf[2] = {{.buf = NULL, .len = 1}, {.buf = buf, .len = 3}}; // 24 bit register + 1 dummy byte

    const struct spi_buf_set rx = {.buffers = rx_buf, .count = 2};

    spi_transceive_dt(&config->spi, &tx, &rx); // regRxBuffer 0 contains NULL (for sent command), so read from 1 onwards
    // printk("Stat: %x %x %x\n", (uint8_t)buf[0], (uint8_t)buf[1], (uint8_t)buf[2]);

    return (uint32_t)(buf[0] << 16) | (buf[1] << 8) | buf[2];
}

static uint32_t _max30001_async_read_reg(const struct device *dev, uint8_t reg)
{
    const struct max30001_config *config = dev->config;
    uint8_t spiTxCommand = ((reg << 1) | RREG);

    uint8_t buf[3];

    const struct spi_buf tx_buf[1] = {{.buf = &spiTxCommand, .len = 1}};
    const struct spi_buf_set tx = {.buffers = tx_buf, .count = 1};
    struct spi_buf rx_buf[2] = {{.buf = NULL, .len = 1}, {.buf = buf, .len = 3}}; // 24 bit register + 1 dummy byte
    const struct spi_buf_set rx = {.buffers = rx_buf, .count = 2};

    spi_transceive_dt(&config->spi, &tx, &rx); // regRxBuffer 0 contains NULL (for sent command), so read from 1 onwards
    // printk("Reg: %x %x %x\n", (uint8_t)buf[0], (uint8_t)buf[1], (uint8_t)buf[2]);

    return (uint32_t)(buf[0] << 16) | (buf[1] << 8) | buf[2];
}

static void _max30001Synch(const struct device *dev)
{
    _max30001RegWrite(dev, SYNCH, 0x000000);
}

static void _max30001FIFOReset(const struct device *dev)
{
    _max30001RegWrite(dev, FIFO_RST, 0x000000);
}

static int max30001_async_sample_fetch(const struct device *dev,
                                       uint32_t *num_samples_ecg, uint32_t *num_samples_bioz, int32_t ecg_samples[32],
                                       int32_t bioz_samples[32], uint16_t *rri, uint16_t hr,
                                       uint8_t ecg_lead_off, uint8_t bioz_lead_off)
{
    struct max30001_data *data = dev->data;
    const struct max30001_config *config = dev->config;

    uint32_t max30001_status, max30001_mngr_int = 0;
    uint32_t e_fifo_num_bytes, b_fifo_num_bytes;

    uint32_t e_fifo_num_samples = 0;
    uint32_t b_fifo_num_samples = 0;

    uint8_t buf[1024];
    int num_bytes=0;

    uint8_t spiTxCommand = ((ECG_FIFO_BURST << 1) | RREG);
    const struct spi_buf tx_buf[1] = {{.buf = &spiTxCommand, .len = 1}};
    const struct spi_buf_set tx = {.buffers = tx_buf, .count = 1};

    struct spi_buf rx_buf[2] = {{.buf = NULL, .len = 1}, {.buf = &buf, .len = num_bytes}}; // 24 bit register + 1 dummy byte
    const struct spi_buf_set rx = {.buffers = rx_buf, .count = 2};

    max30001_status = _max30001_async_read_status(dev);

    if ((max30001_status & MAX30001_STATUS_MASK_DCLOFF) == MAX30001_STATUS_MASK_DCLOFF)
    {
        // LOG_INF("Leads Off\n");
        data->ecg_lead_off = 1;
        ecg_lead_off = 1;
    }
    else
    {
        data->ecg_lead_off = 0;
        ecg_lead_off = 0;
    }

    if ((max30001_status & MAX30001_STATUS_MASK_EINT) == MAX30001_STATUS_MASK_EINT) // EINT bit is set, FIFO is full
    {
        max30001_mngr_int = _max30001_async_read_reg(dev, MNGR_INT);
        e_fifo_num_samples = ((max30001_mngr_int & MAX30001_INT_MASK_EFIT) >> MAX30001_INT_SHIFT_EFIT)+1;
        e_fifo_num_bytes = (e_fifo_num_samples * 3); // 24 bit register + 1 dummy byte
        *num_samples_ecg = e_fifo_num_samples;

        //printk("ES: %d ", e_fifo_num_samples);
        //_max30001_read_ecg_fifo(dev, e_fifo_num_bytes);
        
        rx_buf[1].buf = &buf;
        rx_buf[1].len = e_fifo_num_bytes;

        spi_transceive_dt(&config->spi, &tx, &rx);

        //Read all the samples from the FIFO
        for(int i=0;i<e_fifo_num_samples;i++)
        {
            uint32_t ecg_etag = ((((unsigned char)buf[i*3 + 2]) & 0x38) >> 3);

            //printk("E %x ", ecg_etag);

            if ((ecg_etag == 0x00) || (ecg_etag == 0x02)) // Valid sample
            {
                uint32_t uecgtemp = (uint32_t)(((uint32_t)buf[i*3] << 16 | (uint32_t)buf[i*3 + 1] << 8) | (uint32_t)(buf[i*3 + 2] & 0xC0));
                uecgtemp = (uint32_t)(uecgtemp << 8);

                int32_t secgtemp = (int32_t)uecgtemp;
                secgtemp = (int32_t)secgtemp >> 8;
                //printf("%d ", secgtemp);

                ecg_samples[i] = secgtemp;
            }
            else if (ecg_etag == 0x06)
            {
                break;
            }
            else if (ecg_etag == 0x07) // FIFO Overflow
            {
                _max30001FIFOReset(dev);
                _max30001Synch(dev);
                break;
            }
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

    /* Get the buffer for the frame, it may be allocated dynamically by the rtio context */
    ret = rtio_sqe_rx_buf(iodev_sqe, m_min_buf_len, m_min_buf_len, &buf, &buf_len);
    if (ret != 0)
    {
        LOG_ERR("Failed to get a read buffer of size %u bytes", m_min_buf_len);
        rtio_iodev_sqe_err(iodev_sqe, ret);
        return ret;
    }

    m_edata = (struct max30001_encoded_data *)buf;
    m_edata->header.timestamp = k_ticks_to_ns_floor64(k_uptime_ticks());
    ret = max30001_async_sample_fetch(dev, &m_edata->num_samples_ecg, &m_edata->num_samples_bioz,
                                      m_edata->ecg_samples, m_edata->bioz_samples, &m_edata->rri, &m_edata->hr, &m_edata->ecg_lead_off, &m_edata->bioz_lead_off);

    if (ret != 0)
    {
        rtio_iodev_sqe_err(iodev_sqe, ret);
        return ret;
    }

    rtio_iodev_sqe_ok(iodev_sqe, 0);

    return 0;
}