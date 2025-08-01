/*
 * HealthyPi Move
 * 
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Author: Ashwin Whitchurch, Protocentral Electronics
 * Contact: ashwin@protocentral.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


#include "hpi_user_settings_api.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(hpi_user_settings_api, LOG_LEVEL_INF);

int hpi_user_settings_init(void)
{
    return hpi_settings_persistence_init();
}

uint16_t hpi_user_settings_get_height(void)
{
    const struct hpi_user_settings *settings = hpi_settings_get_current();
    return settings ? settings->height : DEFAULT_USER_HEIGHT;
}

uint16_t hpi_user_settings_get_weight(void)
{
    const struct hpi_user_settings *settings = hpi_settings_get_current();
    return settings ? settings->weight : DEFAULT_USER_WEIGHT;
}

uint8_t hpi_user_settings_get_hand_worn(void)
{
    const struct hpi_user_settings *settings = hpi_settings_get_current();
    return settings ? settings->hand_worn : DEFAULT_HAND_WORN;
}

uint8_t hpi_user_settings_get_time_format(void)
{
    const struct hpi_user_settings *settings = hpi_settings_get_current();
    return settings ? settings->time_format : DEFAULT_TIME_FORMAT;
}

uint8_t hpi_user_settings_get_temp_unit(void)
{
    const struct hpi_user_settings *settings = hpi_settings_get_current();
    return settings ? settings->temp_unit : DEFAULT_TEMP_UNIT;
}

bool hpi_user_settings_get_auto_sleep_enabled(void)
{
    const struct hpi_user_settings *settings = hpi_settings_get_current();
    return settings ? settings->auto_sleep_enabled : DEFAULT_AUTO_SLEEP;
}

uint8_t hpi_user_settings_get_sleep_timeout(void)
{
    const struct hpi_user_settings *settings = hpi_settings_get_current();
    return settings ? settings->sleep_timeout : DEFAULT_SLEEP_TIMEOUT;
}

int hpi_user_settings_set_height(uint16_t height)
{
    if (height < 100 || height > 250) {
        return -EINVAL;
    }
    
    return hpi_settings_save_single(SETTINGS_USER_HEIGHT_KEY, &height, sizeof(height));
}

int hpi_user_settings_set_weight(uint16_t weight)
{
    if (weight < 30 || weight > 200) {
        return -EINVAL;
    }
    
    return hpi_settings_save_single(SETTINGS_USER_WEIGHT_KEY, &weight, sizeof(weight));
}

int hpi_user_settings_set_hand_worn(uint8_t hand_worn)
{
    if (hand_worn > 1) {
        return -EINVAL;
    }
    
    return hpi_settings_save_single(SETTINGS_HAND_WORN_KEY, &hand_worn, sizeof(hand_worn));
}

int hpi_user_settings_set_time_format(uint8_t time_format)
{
    if (time_format > 1) {
        return -EINVAL;
    }
    
    return hpi_settings_save_single(SETTINGS_TIME_FORMAT_KEY, &time_format, sizeof(time_format));
}

int hpi_user_settings_set_temp_unit(uint8_t temp_unit)
{
    if (temp_unit > 1) {
        return -EINVAL;
    }
    
    return hpi_settings_save_single(SETTINGS_TEMP_UNIT_KEY, &temp_unit, sizeof(temp_unit));
}

int hpi_user_settings_get_all(struct hpi_user_settings *settings)
{
    return hpi_settings_load_all(settings);
}
