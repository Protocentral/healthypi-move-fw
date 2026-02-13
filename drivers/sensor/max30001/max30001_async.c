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

    // --- BioZ lead-off detection ---
    if ((max30001_status & BIOZ_LEAD_MASK) != 0)
    {
        data->bioz_lead_off = 1;
        *bioz_lead_off = 1;
    }
    else
    {
        data->bioz_lead_off = 0;
        *bioz_lead_off = 0;
    }

    /*while ((max30001_status & MAX30001_STATUS_MASK_EINT) != MAX30001_STATUS_MASK_EINT)
    {
        max30001_status = max30001_read_status(dev);
    }*/

    // Handle ECG FIFO (when EINT is set)
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

        // Also read BioZ FIFO when ECG is active
        if (max30001_mngr_int == 0) {
            max30001_mngr_int = max30001_read_reg(dev, MNGR_INT);
        }
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

        /*
         * BioZ FIFO format (24 bits per sample):
         *   Bits 23-4: 20-bit signed ADC data
         *   Bits 3-0:  BTAG (tag bits)
         *
         * Extract 20-bit ADC and convert to conductance (µS) using datasheet formula.
         * Store as fixed-point (×100) for integer transmission.
         */
        for (int i = 0; i < b_fifo_num_samples; i++)
        {
            uint32_t btag = buf_bioz[i * 3 + 2] & 0x07;

            if ((btag == 0x00) || (btag == 0x02)) /* Valid sample */
            {
                /* Extract 20-bit ADC data (bits 23-4) */
                uint32_t u_bioz_temp = (uint32_t)(((uint32_t)buf_bioz[i * 3] << 16) |
                                                   ((uint32_t)buf_bioz[i * 3 + 1] << 8) |
                                                   ((uint32_t)(buf_bioz[i * 3 + 2] & 0xF0)));

                /* Shift left then right to sign-extend the 20-bit value */
                u_bioz_temp = u_bioz_temp << 8;
                int32_t s_bioz_temp = (int32_t)u_bioz_temp;
                s_bioz_temp = s_bioz_temp >> 12;  /* 8 + 4 = 12 to get 20-bit signed value */

                /* Convert raw ADC to conductance in µS and store as fixed-point (×100) */
                float conductance_uS = max30001_bioz_raw_to_uS(s_bioz_temp,
                                                               config->bioz_gain,
                                                               1);
                bioz_samples[i] = (int32_t)(conductance_uS * 1000000.0f);
            }
            else if (btag == 0x06) /* FIFO empty */
            {
                break;
            }
            else if (btag == 0x07) /* FIFO Overflow */
            {
                max30001_fifo_reset(dev);
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
    
    // Handle BioZ-only FIFO (when BINT is set but EINT is not)
    else if ((max30001_status & MAX30001_STATUS_MASK_BINT) == MAX30001_STATUS_MASK_BINT)
    {
        max30001_mngr_int = max30001_read_reg(dev, MNGR_INT);
        
        // No ECG samples in this case
        *num_samples_ecg = 0;
        
        // Read BioZ FIFO
        b_fifo_num_samples = (((max30001_mngr_int & MAX30001_INT_MASK_BFIT) >> MAX30001_INT_SHIFT_BFIT) + 1);
        b_fifo_num_bytes = (b_fifo_num_samples * 3);
        *num_samples_bioz = b_fifo_num_samples;
        
        if (b_fifo_num_samples > 32) {
            b_fifo_num_samples = 32;
            *num_samples_bioz = 32;
        }

        struct spi_buf rx_bioz_buf[2] = {{.buf = NULL, .len = 1}, {.buf = &buf_bioz, .len = b_fifo_num_bytes}};
        const struct spi_buf_set rx_bioz = {.buffers = rx_bioz_buf, .count = 2};

        spi_transceive_dt(&config->spi, &tx_bioz, &rx_bioz);

        /*
         * BioZ FIFO format (24 bits per sample):
         *   Bits 23-4: 20-bit signed ADC data
         *   Bits 3-0:  BTAG (tag bits)
         *
         * Extract 20-bit ADC and convert to conductance (µS) using datasheet formula.
         * Store as fixed-point (×1000000) for integer transmission.
         */
        for (int i = 0; i < b_fifo_num_samples; i++)
        {
            uint32_t btag = buf_bioz[i * 3 + 2] & 0x07;

            LOG_DBG("BioZ sample %d: btag=0x%02X", i, btag);

            if ((btag == 0x00) || (btag == 0x02)) /* Valid sample */
            {
                /* Extract 20-bit ADC data (bits 23-4) */
                uint32_t u_bioz_temp = (uint32_t)(((uint32_t)buf_bioz[i * 3] << 16) |
                                                   ((uint32_t)buf_bioz[i * 3 + 1] << 8) |
                                                   ((uint32_t)(buf_bioz[i * 3 + 2] & 0xF0)));

                /* Shift left then right to sign-extend the 20-bit value */
                u_bioz_temp = u_bioz_temp << 8;
                int32_t s_bioz_temp = (int32_t)u_bioz_temp;
                s_bioz_temp = s_bioz_temp >> 12;  /* 8 + 4 = 12 to get 20-bit signed value */

                /* Convert raw ADC to conductance in µS and store as fixed-point (×1000000) */
                // float conductance_uS = max30001_bioz_raw_to_uS(s_bioz_temp,
                //                                                config->bioz_gain,
                //                                                config->bioz_cgmag);
                float conductance_uS = max30001_bioz_raw_to_uS(s_bioz_temp,
                                                               config->bioz_gain,
                                                            1);       //passing index 1 for the cgmag value of 110nA (low current mode) since that's the mode we're using in the driver                                      
              //  LOG_INF("BioZ sample %d: raw ADC=%d, conductance=%.4f uS", i, s_bioz_temp, (double)conductance_uS);
                bioz_samples[i] = (int32_t)(conductance_uS * 1000000.0f);
            }
            else if (btag == 0x06) /* FIFO empty */
            {
                LOG_DBG("BioZ sample %d: FIFO empty (btag=0x06)", i);
                break;
            }
            else if (btag == 0x07) /* FIFO Overflow */
            {
                LOG_WRN("BioZ FIFO overflow at sample %d", i);
                max30001_fifo_reset(dev);
                break;
            }
            else
            {
                LOG_WRN("BioZ sample %d: invalid btag=0x%02X", i, btag);
            }
        }
    }
    else
    {
        // No data available
        *num_samples_ecg = 0;
        *num_samples_bioz = 0;
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
        }
    }

    // Always output the last known good HR and RRI values (prevents displaying garbage/stale data)
    *hr = data->lastHR;
    *rri = data->lastRRI;

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
        LOG_ERR("RTIO submit failed: ret=%d", ret);
        rtio_iodev_sqe_err(iodev_sqe, ret);
        return ret;
    }

    // Check if we have any data to return
    if (m_edata->chip_op_mode == MAX30001_OP_MODE_STREAM) {
        if (m_edata->num_samples_ecg == 0 && m_edata->num_samples_bioz == 0) {
            rtio_iodev_sqe_ok(iodev_sqe, 0);  // Return 0 bytes
            return 0;
        } else {
            rtio_iodev_sqe_ok(iodev_sqe, m_min_buf_len);  // Return actual data
            return 0;
        }
    }

    rtio_iodev_sqe_ok(iodev_sqe, 0);

    return 0;
}