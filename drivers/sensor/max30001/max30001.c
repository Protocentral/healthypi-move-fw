// ProtoCentral Electronics (info@protocentral.com)
// SPDX-License-Identifier: Apache-2.0

#define DT_DRV_COMPAT maxim_max30001

#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>

#include "max30001.h"

LOG_MODULE_REGISTER(SENSOR_MAX30001, CONFIG_MAX30001_LOG_LEVEL);

#if DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 0
#warning "MAX30001 driver enabled without any devices"
#endif

int _max30001RegWrite(const struct device *dev, uint8_t reg, uint32_t val)
{
    const struct max30001_config *config = dev->config;
    uint8_t cmd[] = {((reg << 1) | WREG), (uint8_t)(val >> 16), (uint8_t)(val >> 8), (uint8_t)val};

    const struct spi_buf tx_buf = {.buf = cmd, .len = sizeof(cmd)};
    const struct spi_buf_set tx = {.buffers = &tx_buf, .count = 1};
    int ret;

    ret = spi_write_dt(&config->spi, &tx);
    if (ret)
    {
        LOG_DBG("spi_write FAIL %d\n", ret);
        return ret;
    }
    return 0;
}

static int _max30001_read_chip_id(const struct device *dev, uint8_t *buf)
{
    const struct max30001_config *config = dev->config;
    uint8_t spiTxCommand = ((INFO << 1) | RREG);

    const struct spi_buf tx_buf = {.buf = &spiTxCommand, .len = 1};
    const struct spi_buf_set tx = {.buffers = &tx_buf, .count = 1};
    struct spi_buf rx_buf[2] = {{.buf = NULL, .len = 1}, {.buf = buf, .len = 3}}; // 24 bit register + 1 dummy byte
    const struct spi_buf_set rx = {.buffers = rx_buf, .count = 2};

    spi_transceive_dt(&config->spi, &tx, &rx); // regRxBuffer 0 contains NULL (for sent command), so read from 1 onwards

    LOG_DBG("Chip ID: %x %x %x", (uint8_t)buf[0], (uint8_t)buf[1], (uint8_t)buf[2]);

    return 0;
}

uint32_t max30001_read_status(const struct device *dev)
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

uint32_t max30001_read_reg(const struct device *dev, uint8_t reg)
{
    const struct max30001_config *config = dev->config;
    uint8_t spiTxCommand = ((reg << 1) | RREG);

    uint8_t buf[3];

    const struct spi_buf tx_buf[1] = {{.buf = &spiTxCommand, .len = 1}};
    const struct spi_buf_set tx = {.buffers = tx_buf, .count = 1};
    struct spi_buf rx_buf[2] = {{.buf = NULL, .len = 1}, {.buf = buf, .len = 3}}; // 24 bit register + 1 dummy byte
    const struct spi_buf_set rx = {.buffers = rx_buf, .count = 2};

    spi_transceive_dt(&config->spi, &tx, &rx); // regRxBuffer 0 contains NULL (for sent command), so read from 1 onwards

    return (uint32_t)(buf[0] << 16) | (buf[1] << 8) | buf[2];
}

static void _max30001SwReset(const struct device *dev)
{
    _max30001RegWrite(dev, SW_RST, 0x000000);
    k_sleep(K_MSEC(100));
}

void max30001_synch(const struct device *dev)
{
    _max30001RegWrite(dev, SYNCH, 0x000000);
}

void max30001_fifo_reset(const struct device *dev)
{
    _max30001RegWrite(dev, FIFO_RST, 0x000000);
}

static int _max30001_read_ecg_fifo(const struct device *dev, int num_bytes)
{
    unsigned char ecg_etag;

    uint8_t buf[num_bytes + 1];

    uint32_t uecgtemp;
    int32_t secgtemp;

    int secg_counter = 0;

    const struct max30001_config *config = dev->config;
    struct max30001_data *drv_data = dev->data;

    uint8_t spiTxCommand = ((ECG_FIFO_BURST << 1) | RREG);

    const struct spi_buf tx_buf[1] = {{.buf = &spiTxCommand, .len = 1}};
    const struct spi_buf_set tx = {.buffers = tx_buf, .count = 1};

    struct spi_buf rx_buf[2] = {{.buf = NULL, .len = 1}, {.buf = &buf, .len = num_bytes}}; // 24 bit register + 1 dummy byte
    const struct spi_buf_set rx = {.buffers = rx_buf, .count = 2};

    spi_transceive_dt(&config->spi, &tx, &rx);

    // regRxBuffer 0 contains NULL (for sent command), so read from 1 onwards

    for (int i = 0; i < num_bytes; i += 3)
    {
        // Get etag
        ecg_etag = ((((unsigned char)buf[i + 2]) & 0x38) >> 3);
        // printk("E%x ", ecg_etag);

        if (ecg_etag == 0x00 || ecg_etag == 0x02) // Valid sample
        {
            // uecgtemp=(unsigned long)((unsigned long)readBuffer[i]<<16 |(unsigned long)readBuffer[i+1]<<8| (unsigned long)(readBuffer[i+2]&0xC0));
            uecgtemp = (unsigned long)(((unsigned long)buf[i] << 16 | (unsigned long)buf[i + 1] << 8) | (unsigned long)(buf[i + 2] & 0xC0));
            uecgtemp = (unsigned long)(uecgtemp << 8);

            secgtemp = (signed long)uecgtemp;
            secgtemp = (signed long)secgtemp >> 8;

            drv_data->s32ECGData[secg_counter++] = secgtemp;
            // drv_data->s32ecg_sample = secgtemp;
        }
        else if (ecg_etag == 0x07) // FIFO Overflow
        {
            LOG_DBG("EOVF ");
            max30001_fifo_reset(dev);
            max30001_synch(dev);
        }
    }

    return 0;
}

static int _max30001_read_bioz_fifo(const struct device *dev, int num_bytes)
{
    uint8_t btag;

    uint8_t buf[num_bytes + 1];

    uint32_t u_bioz_temp;
    int32_t s_bioz_temp;

    int s_counter = 0;

    const struct max30001_config *config = dev->config;
    struct max30001_data *drv_data = dev->data;

    uint8_t spiTxCommand = ((BIOZ_FIFO_BURST << 1) | RREG);

    const struct spi_buf tx_buf[1] = {{.buf = &spiTxCommand, .len = 1}};
    const struct spi_buf_set tx = {.buffers = tx_buf, .count = 1};

    struct spi_buf rx_buf[2] = {{.buf = NULL, .len = 1}, {.buf = &buf, .len = num_bytes}}; // 24 bit register + 1 dummy byte
    const struct spi_buf_set rx = {.buffers = rx_buf, .count = 2};

    spi_transceive_dt(&config->spi, &tx, &rx);

    /*
     * BioZ FIFO format (24 bits per sample):
     *   Bits 23-4: 20-bit signed ADC data
     *   Bits 3-0:  BTAG (tag bits)
     *
     * Extract 20-bit ADC and convert to conductance (µS) using datasheet formula:
     *   Z (Ω) = ADC × VREF / (2^19 × CGMAG × GAIN)
     *   Conductance (µS) = 1/Z × 10^6
     */

    for (int i = 0; i < num_bytes; i += 3)
    {
        /* Extract BTAG from bits 3-0 of third byte */
        btag = buf[i + 2] & 0x07;

        if ((btag == 0x00) || (btag == 0x02)) /* Valid sample */
        {
            /* Extract 20-bit ADC data (bits 23-4):
             * - Byte 0: bits 23-16 (MSB)
             * - Byte 1: bits 15-8
             * - Byte 2 & 0xF0: bits 7-4 (mask out BTAG in bits 3-0) */
            u_bioz_temp = (uint32_t)(((uint32_t)buf[i] << 16) |
                                      ((uint32_t)buf[i + 1] << 8) |
                                      ((uint32_t)(buf[i + 2] & 0xF0)));

            /* Shift left then right to sign-extend the 20-bit value to 32-bit */
            u_bioz_temp = u_bioz_temp << 8;
            s_bioz_temp = (int32_t)u_bioz_temp;
            s_bioz_temp = s_bioz_temp >> 12;  /* 8 + 4 = 12 to get 20-bit signed value */

            /* Convert raw ADC to conductance in µS and store as fixed-point (×100) */
            float conductance_uS = max30001_bioz_raw_to_uS(s_bioz_temp,
                                                           config->bioz_gain,
                                                           config->bioz_cgmag);
            drv_data->s32BIOZData[s_counter++] = (int32_t)(conductance_uS * 100.0f);
        }
        else if (btag == 0x06) /* FIFO empty */
        {
            return 0;
        }
        else if (btag == 0x07) /* FIFO Overflow */
        {
            max30001_fifo_reset(dev);
            max30001_synch(dev);
        }
    }
    return 0;
}

static int max30001_enable_ecg(const struct device *dev, bool ecg_en)
{
    struct max30001_data *data = dev->data;

    if (ecg_en == true)
    {
        LOG_DBG("Enabling MAX30001 ECG...");
        data->chip_cfg.reg_cnfg_gen.bit.en_ecg = 1;
    }
    else
    {
        LOG_DBG("Disabling MAX30001 ECG...");
        data->chip_cfg.reg_cnfg_gen.bit.en_ecg = 0;
    }
    //_max30001RegWrite(dev, CNFG_EMUX, 0x0B0000); // Pins internally connection to ECG Channels
    // k_sleep(K_MSEC(100));

    return _max30001RegWrite(dev, CNFG_GEN, data->chip_cfg.reg_cnfg_gen.all);
}

static int max30001_enable_bioz(const struct device *dev, bool bioz_en)
{
    struct max30001_data *data = dev->data;

    if (bioz_en == true)
    {
        LOG_DBG("Enabling MAX30001 BioZ...");
        data->chip_cfg.reg_cnfg_gen.bit.en_bioz = 1;
        
        // Write general config with BioZ enabled
        int ret = _max30001RegWrite(dev, CNFG_GEN, data->chip_cfg.reg_cnfg_gen.all);
        if (ret != 0) {
            LOG_ERR("Failed to write CNFG_GEN for BioZ enable: %d", ret);
            return ret;
        }
        
        // Also write BioZ-specific configuration registers to ensure they're active
        _max30001RegWrite(dev, CNFG_BIOZ_LC, 0x800000); // Turn OFF low current mode
        _max30001RegWrite(dev, CNFG_BIOZ, data->chip_cfg.reg_cnfg_bioz.all);
        _max30001RegWrite(dev, CNFG_BMUX, data->chip_cfg.reg_cnfg_bmux.all);
        
        LOG_INF("BioZ configuration registers written");
        return 0;
    }
    else
    {
        LOG_DBG("Disabling MAX30001 BioZ...");
        data->chip_cfg.reg_cnfg_gen.bit.en_bioz = 0;
    }
    return _max30001RegWrite(dev, CNFG_GEN, data->chip_cfg.reg_cnfg_gen.all);
}

static void max30001_enable_rtor(const struct device *dev)
{
    _max30001RegWrite(dev, CNFG_RTOR1, 0x3fc600);
    k_sleep(K_MSEC(100));
}

static void max30001_enable_dcloff(const struct device *dev)
{
    LOG_DBG("Enabling MAX30001 DCLOFF...\n");
}

static void max30001_disable_rtor(const struct device *dev)
{
    LOG_DBG("Disabling MAX30001 RTOR...\n");
}

static void max30001_disable_dcloff(const struct device *dev)
{
    LOG_DBG("Disabling MAX30001 DCLOFF...\n");
}

static int max30001_sample_fetch(const struct device *dev,
                                 enum sensor_channel chan)
{

#ifdef CONFIG_PM_DEVICE
    enum pm_device_state state;
    (void)pm_device_state_get(dev, &state);
    /* Do not allow sample fetching from suspended state */
    if (state == PM_DEVICE_STATE_SUSPENDED)
        return -EIO;
#endif

    uint32_t max30001_status, max30001_mngr_int = 0;
    uint8_t e_fifo_num_bytes;
    uint8_t b_fifo_num_bytes;

    uint32_t max30001_rtor = 0;
    struct max30001_data *data = dev->data;

    max30001_status = max30001_read_status(dev);
    LOG_INF("MAX30001 Status: 0x%06X", max30001_status);
    
    // printk("Status: %x\n", max30001_status);

    if ((max30001_status & MAX30001_STATUS_MASK_DCLOFF) == MAX30001_STATUS_MASK_DCLOFF)
    {
        // Leads are off
        // printk("LO");
        // LOG_INF("Leads Off\n");
        data->ecg_lead_off = 1;
    }
    else
    {
        data->ecg_lead_off = 0;
    }

     // --- BioZ lead-off detection ---
    if ((max30001_status & BIOZ_LEAD_MASK) != 0)
    {
        data->bioz_lead_off = 1;
    }
    else
    {
        data->bioz_lead_off = 0;
    }

    if ((max30001_status & MAX30001_STATUS_MASK_EINT) == MAX30001_STATUS_MASK_EINT) // EINT bit is set, FIFO is full
    {
        max30001_mngr_int = max30001_read_reg(dev, MNGR_INT);
        e_fifo_num_bytes = ((((max30001_mngr_int & MAX30001_INT_MASK_EFIT) >> MAX30001_INT_SHIFT_EFIT) + 1) * 3);
        // printk("EFN %d ", e_fifo_num_bytes);
        _max30001_read_ecg_fifo(dev, e_fifo_num_bytes);
    }

    if ((max30001_status & MAX30001_STATUS_MASK_BINT) == MAX30001_STATUS_MASK_BINT) // BIOZ FIFO is full
    {
        LOG_INF("BINT flag set - reading BioZ FIFO");
        max30001_mngr_int = max30001_read_reg(dev, MNGR_INT);
        b_fifo_num_bytes = (((max30001_mngr_int & MAX30001_INT_MASK_BFIT) >> MAX30001_INT_SHIFT_BFIT) + 1) * 3;
        LOG_INF("BioZ FIFO bytes to read: %d", b_fifo_num_bytes);
        // printk("BFN %d ", b_fifo_num_bytes);
        _max30001_read_bioz_fifo(dev, b_fifo_num_bytes);
    }
    else
    {
        LOG_INF("BINT flag NOT set - no BioZ data available");
    }

    if ((max30001_status & MAX30001_STATUS_MASK_RRINT) == MAX30001_STATUS_MASK_RRINT)
    {
        max30001_rtor = max30001_read_reg(dev, RTOR);
        if (max30001_rtor > 0)
        {
            data->lastRRI = (uint16_t)(max30001_rtor >> 10) * 8;
            data->lastHR = (uint16_t)(60 * 1000 / data->lastRRI);
        }
    }

    if (((max30001_status & MAX30001_STATUS_MASK_BOVF) == MAX30001_STATUS_MASK_BOVF) || ((max30001_status & MAX30001_STATUS_MASK_EOVF) == MAX30001_STATUS_MASK_EOVF))
    {
        max30001_fifo_reset(dev);
    }

    return 0;
}

static int max30001_channel_get(const struct device *dev,
                                enum sensor_channel chan,
                                struct sensor_value *val)
{
    struct max30001_data *data = dev->data;

    enum max30001_channel max30001_chan = chan;

    switch (max30001_chan)
    {
    case SENSOR_CHAN_ECG_UV:
        // Val 1 is one sample //Val 2 is 2nd sample from FIFO
        val->val1 = data->s32ECGData[0] * 47.6837158203; // Output in uV * 0.04768371582 * 1000000; // ECG mV = (ADC* VREF)/(2^17*ECG_GAIN)
        val->val2 = 0;                                   // * 0.04768371582 * 1000000;
        break;
    case SENSOR_CHAN_BIOZ_UV:
        val->val1 = data->s32BIOZData[0];
        val->val2 = data->s32BIOZData[1];
        break;
    case SENSOR_CHAN_RTOR:
        val->val1 = data->lastRRI;
        break;
    case SENSOR_CHAN_HR:
        val->val1 = data->lastHR;
        break;
    case SENSOR_CHAN_LDOFF:
        val->val1 = data->ecg_lead_off;
        break;
    default:
        return -EINVAL;
    }
    return 0;
}

static int max30001_mode_set_ulp_leadon(const struct device *dev)
{
    struct max30001_data *data = dev->data;

    // General Configuration
    // data->chip_cfg.reg_cnfg_gen.bit.en_ulp_lon = 0;
    // data->chip_cfg.reg_cnfg_gen.bit.fmstr = 0;

    // data->chip_cfg.reg_cnfg_gen.bit.en_bioz = 0;

    data->chip_cfg.reg_cnfg_gen.bit.en_dcloff = 0;
    data->chip_cfg.reg_cnfg_gen.bit.en_bloff = 0;
    data->chip_cfg.reg_cnfg_gen.bit.en_rbias = 0;
    data->chip_cfg.reg_cnfg_gen.bit.vth = 0;
    data->chip_cfg.reg_cnfg_gen.bit.imag = 0;
    data->chip_cfg.reg_cnfg_gen.bit.fmstr = 0;

    data->chip_cfg.reg_cnfg_gen.bit.rbiasv = 1;
    data->chip_cfg.reg_cnfg_gen.bit.rbiasp = 0;
    data->chip_cfg.reg_cnfg_gen.bit.rbiasn = 0;

    // Disable all channels
    data->chip_cfg.reg_cnfg_gen.bit.en_bioz = 0;
    data->chip_cfg.reg_cnfg_gen.bit.en_ecg = 0;
    data->chip_cfg.reg_cnfg_gen.bit.en_pace = 0;

    // Enable ULP LON
    data->chip_cfg.reg_cnfg_gen.bit.en_ulp_lon = 1;

    _max30001RegWrite(dev, CNFG_GEN, data->chip_cfg.reg_cnfg_gen.all);

    uint32_t max30001_status = max30001_read_status(dev);
    return 0;
}

static int max30001_attr_set(const struct device *dev,
                             enum sensor_channel chan,
                             enum sensor_attribute attr,
                             const struct sensor_value *val)
{
    struct max30001_data *data = dev->data;

    switch (attr)
    {
    case MAX30001_ATTR_OP_MODE:
        if (val->val1 == MAX30001_OP_MODE_LON_DETECT)
        {
            max30001_mode_set_ulp_leadon(dev);
            data->chip_op_mode = MAX30001_OP_MODE_LON_DETECT;
        }
        else if (val->val1 == MAX30001_OP_MODE_STREAM)
        {
            data->chip_op_mode = MAX30001_OP_MODE_STREAM;
            // max30001_mode_set_stream(dev);
        }
        break;
    case MAX30001_ATTR_ECG_ENABLED:
        if (val->val1 == 1)
        {
            max30001_enable_ecg(dev, true);
            max30001_synch(dev);
        }
        else if (val->val1 == 0)
        {
            max30001_enable_ecg(dev, false);
        }
        break;
    case MAX30001_ATTR_BIOZ_ENABLED:
        if (val->val1 == 1)
        {
            max30001_enable_bioz(dev, true);
        }
        else if (val->val1 == 0)
        {
            max30001_enable_bioz(dev, false);
        }
        break;
    case MAX30001_ATTR_RTOR_ENABLED:
        if (val->val1 == 1)
        {
            max30001_enable_rtor(dev);
        }
        else if (val->val1 == 0)
        {
            // Disable RTOR
            max30001_disable_rtor(dev);
        }
        break;
    case MAX30001_ATTR_LEAD_CONFIG:
        // Configure ECG lead polarity based on hand worn setting
        // val->val1: 0 = Left hand, 1 = Right hand
        data->chip_cfg.reg_cnfg_emux.bit.pol = val->val1;
        _max30001RegWrite(dev, CNFG_EMUX, data->chip_cfg.reg_cnfg_emux.all);
        LOG_INF("ECG lead configuration set for %s hand", val->val1 ? "right" : "left");
        break;
    default:
        return -ENOTSUP;
    }
    return 0;
}

static int max30001_chip_init(const struct device *dev)
{
    const struct max30001_config *config = dev->config;
    struct max30001_data *data = dev->data;

    int err;

    err = spi_is_ready_dt(&config->spi);
    if (err < 0)
    {
        LOG_DBG("bus check failed: %d", err);
        return err;
    }

    _max30001SwReset(dev);
    k_sleep(K_MSEC(100));

    uint8_t chip_id[3];
    _max30001_read_chip_id(dev, chip_id);

    if ((chip_id[0] != 0x54) && chip_id[0] != 0x52)
    {
        LOG_ERR("MAX30001 not found");
        return -ENODEV;
    }

    // General Configuration
    data->chip_cfg.reg_cnfg_gen.bit.en_ulp_lon = 0;
    data->chip_cfg.reg_cnfg_gen.bit.fmstr = 0;

    // data->chip_cfg.reg_cnfg_gen.bit.en_bioz = 0;

    data->chip_cfg.reg_cnfg_gen.bit.en_dcloff = 0;
    data->chip_cfg.reg_cnfg_gen.bit.en_bloff = 0;
    data->chip_cfg.reg_cnfg_gen.bit.en_rbias = 0;
    data->chip_cfg.reg_cnfg_gen.bit.rbiasv = 1;
    data->chip_cfg.reg_cnfg_gen.bit.rbiasp = 0;
    data->chip_cfg.reg_cnfg_gen.bit.rbiasn = 0;

    // Gen Config enable/disable ECG
    if (config->ecg_enabled)
    {
        data->chip_cfg.reg_cnfg_gen.bit.en_ecg = 1;
    }
    else
    {
        data->chip_cfg.reg_cnfg_gen.bit.en_ecg = 0;
    }

    // Gen Config enable/disable BIOZ
    if (config->bioz_enabled)
    {
        data->chip_cfg.reg_cnfg_gen.bit.en_bioz = 1;
    }
    else
    {
        data->chip_cfg.reg_cnfg_gen.bit.en_bioz = 0;
    }

    // Gen Config enable/disable LOFF
    if (config->ecg_dcloff_enabled)
    {
        data->chip_cfg.reg_cnfg_gen.bit.en_dcloff = 1;
        data->chip_cfg.reg_cnfg_gen.bit.imag = config->ecg_dcloff_current;
    }
    else
    {
        data->chip_cfg.reg_cnfg_gen.bit.en_dcloff = 0;
    }

    // ECG Configuration
    data->chip_cfg.reg_cnfg_ecg.bit.rate = 0b10;             // 128 SPS
    data->chip_cfg.reg_cnfg_ecg.bit.gain = config->ecg_gain; // From DTS
    data->chip_cfg.reg_cnfg_ecg.bit.dlpf = 0b01;             // 40 Hz
    data->chip_cfg.reg_cnfg_ecg.bit.dhpf = 0b01;             // 0.5 Hz

    // ECG MUX Configuration
    data->chip_cfg.reg_cnfg_emux.bit.openp = 0;
    data->chip_cfg.reg_cnfg_emux.bit.openn = 0;
    data->chip_cfg.reg_cnfg_emux.bit.pol = config->ecg_invert; // From DTS
    data->chip_cfg.reg_cnfg_emux.bit.calp_sel = 0;
    data->chip_cfg.reg_cnfg_emux.bit.caln_sel = 0;

    // BIOZ Configuration
    // data->chip_cfg.reg_cnfg_bioz.bit.rate = 1;                 // 32 sps with FMSTR=0 (lowest rate without changing FMSTR)
    // data->chip_cfg.reg_cnfg_bioz.bit.ahpf = 0b010;             // 500 Hz
    // data->chip_cfg.reg_cnfg_bioz.bit.dlpf = 0b01;              // 40 Hz
    // data->chip_cfg.reg_cnfg_bioz.bit.dhpf = 0b010;             // 0.5 Hz
    // data->chip_cfg.reg_cnfg_bioz.bit.gain = config->bioz_gain; // FROM DTS
    // data->chip_cfg.reg_cnfg_bioz.bit.fcgen = 0b0100;
    // data->chip_cfg.reg_cnfg_bioz.bit.ext_rbias = 0;
    // data->chip_cfg.reg_cnfg_bioz.bit.cgmon = 0;
    // data->chip_cfg.reg_cnfg_bioz.bit.cgmag = config->bioz_cgmag; // FROM DTS
    // data->chip_cfg.reg_cnfg_bioz.bit.phoff = 0b0011;

    // // GSR CONFIGURATION 
    data->chip_cfg.reg_cnfg_bioz.bit.rate = 1;              // 32 sps with FMSTR=0 (lowest rate without changing FMSTR)
   // data->chip_cfg.reg_cnfg_bioz.bit.ahpf = 0b000;             // 125Hz HPF
    data->chip_cfg.reg_cnfg_bioz.bit.ahpf = 0b110;  // Bypass AHPF (11x pattern) // BYPASS analog HPF to preserve DC/low-freq GSR signal
    data->chip_cfg.reg_cnfg_bioz.bit.dhpf = 0b00;              // bypass HPF 
    data->chip_cfg.reg_cnfg_bioz.bit.dlpf = 0b01;              // 4Hz LPF 
    data->chip_cfg.reg_cnfg_bioz.bit.gain = config->bioz_gain;           // 40V/V 
    data->chip_cfg.reg_cnfg_bioz.bit.fcgen = 0b0111;          // 1000 Hz EXCITATION FREQUENCY 
    data->chip_cfg.reg_cnfg_bioz.bit.ext_rbias = 0;   
    data->chip_cfg.reg_cnfg_bioz.bit.cgmon = 0;       
    data->chip_cfg.reg_cnfg_bioz.bit.cgmag = config->bioz_cgmag;          // 8μA LOW CURRENT ✓
    data->chip_cfg.reg_cnfg_bioz.bit.phoff = 0b0000;           // 0° phase ✓

    // BIOZ MUX Configuration
    data->chip_cfg.reg_cnfg_bmux.bit.openp = 0;
    data->chip_cfg.reg_cnfg_bmux.bit.openn = 0;
    data->chip_cfg.reg_cnfg_bmux.bit.calp_sel = 0;
    data->chip_cfg.reg_cnfg_bmux.bit.caln_sel = 0;
    data->chip_cfg.reg_cnfg_bmux.bit.cg_mode = 0;
    data->chip_cfg.reg_cnfg_bmux.bit.en_bist = 0;
    data->chip_cfg.reg_cnfg_bmux.bit.rnom = 0;
   // data->chip_cfg.reg_cnfg_bmux.bit.rmod = 0b100;
    data->chip_cfg.reg_cnfg_bmux.bit.rmod = 0b000;  // GSR impedance range
    data->chip_cfg.reg_cnfg_bmux.bit.fbist = 0;

    _max30001RegWrite(dev, CNFG_GEN, data->chip_cfg.reg_cnfg_gen.all);
    k_sleep(K_MSEC(100));

    _max30001RegWrite(dev, CNFG_ECG, data->chip_cfg.reg_cnfg_ecg.all);
    k_sleep(K_MSEC(100));

    _max30001RegWrite(dev, CNFG_EMUX, data->chip_cfg.reg_cnfg_emux.all);
    k_sleep(K_MSEC(100));

    // Set MAX30001G specific BioZ LC
    _max30001RegWrite(dev, CNFG_BIOZ_LC, 0x800000); // Turn OFF low current mode
    k_sleep(K_MSEC(100));

    _max30001RegWrite(dev, CNFG_BIOZ, data->chip_cfg.reg_cnfg_bioz.all);
    k_sleep(K_MSEC(100));

    _max30001RegWrite(dev, CNFG_BMUX, data->chip_cfg.reg_cnfg_bmux.all);
    k_sleep(K_MSEC(100));

    // Enable DC Leads-Off and BioZ compliance monitor
    data->chip_cfg.reg_cnfg_gen.bit.en_bloff = 1;
    _max30001RegWrite(dev, CNFG_GEN, data->chip_cfg.reg_cnfg_gen.all);
    k_sleep(K_MSEC(10));

    data->chip_cfg.reg_cnfg_bioz.bit.cgmon = 1;
    data->chip_cfg.reg_cnfg_bioz.bit.cgmag = config->bioz_cgmag;
    _max30001RegWrite(dev, CNFG_BIOZ, data->chip_cfg.reg_cnfg_bioz.all);
    k_sleep(K_MSEC(10));


    max30001_enable_rtor(dev);

    _max30001RegWrite(dev, CNFG_CAL, 0x702000); // Calibration sources disabled
    k_sleep(K_MSEC(100));

    //_max30001RegWrite(dev, MNGR_INT, 0x190000); // EFIT=4, BFIT=2
     _max30001RegWrite(dev, MNGR_INT, 0x7B0000); // EFIT=16, BFIT=8
    //_max30001RegWrite(dev, MNGR_INT, 0xFF0000); // EFIT=31, BFIT=15 (MAX)
    //_max30001RegWrite(dev, MNGR_INT, 0x3B0000); // EFIT=8, BFIT=4
    //_max30001RegWrite(dev, MNGR_INT, 0x080000); // EFIT=2, BFIT=2
    //_max30001RegWrite(dev, MNGR_INT, 0x000000); // EFIT=1, BFIT=1
    k_sleep(K_MSEC(100));

    _max30001RegWrite(dev, MNGR_DYN, 0xBFFFFF); // Enable automatic fast recovery
    //_max30001RegWrite(dev, MNGR_DYN, 0x7FFFFF); //  Enable manual fast recovery
    k_sleep(K_MSEC(100));

    //_max30001RegWrite(dev, EN_INT, 0x800003); // Disable all interrupts

    if (config->rtor_enabled)
    {
        max30001_enable_rtor(dev);
    }
    else
    {
        max30001_disable_rtor(dev);
    }

    max30001_synch(dev);
    k_sleep(K_MSEC(100));

    // For POWER DEBUG ONLY
    //_max30001RegWrite(dev, CNFG_GEN, 0x400000);

    LOG_DBG("\"%s\" OK", dev->name);
    return 0;
}

static const struct sensor_driver_api max30001_api_funcs = {
    .attr_set = max30001_attr_set,
    .sample_fetch = max30001_sample_fetch,
    .channel_get = max30001_channel_get,

#ifdef CONFIG_SENSOR_ASYNC_API
    .submit = max30001_submit,
    .get_decoder = max30001_get_decoder,
#endif
};

#define MAX30001_SPI_OPERATION (SPI_WORD_SET(8) | SPI_TRANSFER_MSB)

#ifdef CONFIG_PM_DEVICE

static int max30001_pm_action(const struct device *dev, enum pm_device_action action)
{
    switch (action)
    {
    case PM_DEVICE_ACTION_RESUME:
        /* Enable sensor */
        break;

    case PM_DEVICE_ACTION_SUSPEND:
        /* Disable sensor */
        break;

    default:
        return -ENOTSUP;
    }

    return 0;
}
#endif /* CONFIG_PM_DEVICE */
/*
 * Main instantiation macro, which selects the correct bus-specific
 * instantiation macros for the instance.
 */
#define MAX30001_DEFINE(inst)                                             \
    static struct max30001_data max30001_data_##inst;                     \
    static const struct max30001_config max30001_config_##inst =          \
        {                                                                 \
            .spi = SPI_DT_SPEC_INST_GET(                                  \
                inst, MAX30001_SPI_OPERATION, 0),                         \
            .ecg_gain = DT_INST_PROP(inst, ecg_gain),                     \
            .bioz_gain = DT_INST_PROP(inst, bioz_gain),                   \
            .bioz_cgmag = DT_INST_PROP(inst, bioz_cgmag),                 \
            .bioz_lc_hi_lo_en = DT_INST_PROP(inst, bioz_lc_hi_lo_en),     \
            .bioz_ln_en = DT_INST_PROP(inst, bioz_ln_en),                 \
            .bioz_dlpf = DT_INST_PROP(inst, bioz_dlpf),                   \
            .bioz_dhpf = DT_INST_PROP(inst, bioz_dhpf),                   \
            .bioz_ahpf = DT_INST_PROP(inst, bioz_ahpf),                   \
            .ecg_enabled = DT_INST_PROP(inst, ecg_enabled),               \
            .bioz_enabled = DT_INST_PROP(inst, bioz_enabled),             \
            .rtor_enabled = DT_INST_PROP(inst, rtor_enabled),             \
            .ecg_dcloff_enabled = DT_INST_PROP(inst, ecg_dcloff_enable),  \
            .ecg_dcloff_current = DT_INST_PROP(inst, ecg_dcloff_current), \
            .ecg_invert = DT_INST_PROP(inst, ecg_invert),                 \
                                                                          \
    };                                                                    \
    PM_DEVICE_DT_INST_DEFINE(inst, max30001_pm_action);                   \
    SENSOR_DEVICE_DT_INST_DEFINE(inst,                                    \
                                 max30001_chip_init,                      \
                                 PM_DEVICE_DT_INST_GET(inst),             \
                                 &max30001_data_##inst,                   \
                                 &max30001_config_##inst,                 \
                                 POST_KERNEL,                             \
                                 CONFIG_SENSOR_INIT_PRIORITY,             \
                                 &max30001_api_funcs);

DT_INST_FOREACH_STATUS_OKAY(MAX30001_DEFINE)