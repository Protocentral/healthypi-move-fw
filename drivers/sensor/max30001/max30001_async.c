#include <zephyr/drivers/sensor.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MAX30001_ASYNC, CONFIG_SENSOR_LOG_LEVEL);

#include "max30001.h"

static int max30001_async_sample_fetch(const struct device *dev,
                                       uint32_t ecg_samples[32], uint32_t *num_samples, uint16_t *ecg_rate)
{
    struct max30001_data *data = dev->data;
    const struct max30001_config *config = dev->config; 

    return 0;
}

int max30001_submit(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe)
{
    struct max30001_data *data = dev->data;
   

    return 0;
}