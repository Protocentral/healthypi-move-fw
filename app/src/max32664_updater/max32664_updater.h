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


enum max32664_updater_device_type
{
    MAX32664_UPDATER_DEV_TYPE_MAX32664C,
    MAX32664_UPDATER_DEV_TYPE_MAX32664D,
    MAX32664_UPDATER_DEV_TYPE_MAXM86146,
};

enum max32664_updater_status
{
    MAX32664_UPDATER_STATUS_IDLE,
    MAX32664_UPDATER_STATUS_IN_PROGRESS,
    MAX32664_UPDATER_STATUS_SUCCESS,
    MAX32664_UPDATER_STATUS_FAILED,
    MAX32664_UPDATER_STATUS_FILE_NOT_FOUND,
};

void max32664_updater_start(const struct device *dev, enum max32664_updater_device_type type);
void max32664_set_progress_callback(void (*callback)(int progress, int status));
enum max32664_updater_device_type max32664_get_current_update_device_type(void);