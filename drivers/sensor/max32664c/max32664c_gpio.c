/*
 * (c) 2024 Protocentral Electronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include "max32664c.h"

/**
 * @brief Configure MFIO pin mode
 *
 * Helper function to properly configure the MFIO pin for either
 * command mode (output) or interrupt mode (input).
 *
 * @param dev Device instance
 * @param command_mode True for command mode (output), false for interrupt mode (input)
 * @return 0 on success, negative errno on failure
 */
int max32664c_configure_mfio(const struct device *dev, bool command_mode)
{
    const struct max32664c_config *config = dev->config;
    int rc;

    if (command_mode) {
        /* Configure MFIO as output for command mode */
        rc = gpio_pin_configure_dt(&config->mfio_gpio, GPIO_OUTPUT);
        if (rc < 0) {
            LOG_ERR("Failed to configure MFIO as output");
            return rc;
        }
        
        /* Disable any interrupts when in command mode */
        rc = gpio_pin_interrupt_configure_dt(&config->mfio_gpio, GPIO_INT_DISABLE);
        if (rc < 0) {
            LOG_ERR("Failed to disable MFIO interrupt");
            return rc;
        }
    } else {
        /* Configure MFIO as input for interrupt mode */
        rc = gpio_pin_configure_dt(&config->mfio_gpio, GPIO_INPUT);
        if (rc < 0) {
            LOG_ERR("Failed to configure MFIO as input");
            return rc;
        }
    }

    return 0;
}
