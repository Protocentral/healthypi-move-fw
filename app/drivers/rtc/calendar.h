/**
 * @file calendar.h
 * @author Brian Bradley (brian.bradley.p@gmail.com)
 * @brief Calendar driver api headers
 * @date 2020-10-07
 * 
 * @copyright Copyright (C) 2020 Brian Bradley
 * 
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_EXTRAS_INCLUDE_DRIVERS_CALENDAR_H_
#define ZEPHYR_EXTRAS_INCLUDE_DRIVERS_CALENDAR_H_

#include <zephyr/kernel.h>
#include <zephyr/device.h>

#include <time.h>
#include <zephyr/types.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*calendar_api_settime)(const struct device * dev, struct tm * tm);
typedef int (*calendar_api_gettime)(const struct device * dev, struct tm * tm);

__subsystem struct calendar_driver_api {
    calendar_api_settime settime;
    calendar_api_gettime gettime;
};

/**
 * @brief Function for getting the current calendar time as recorded by the
 * calendar driver
 * 
 * @param dev Pointer to the device structure for the driver instance.
 * @param tm Pointer to the time structure which will be populated with the
 * current calendar date
 * @retval 0 if success
 * @retval -errno otherwise
 */
__syscall int calendar_gettime(const struct device *dev, struct tm *tm);

static inline int z_impl_calendar_gettime(const struct device *dev, struct tm *tm)
{
	const struct calendar_driver_api *api =
				(struct calendar_driver_api *)dev->api;

	return api->gettime(dev, tm);
}

/**
 * @brief Function for setting the current calendar time to be recorded by the
 * calendar driver
 * 
 * @param dev Pointer to the device structure for the driver instance.
 * @param tm Pointer to the time structure describing the current calendar date
 * @retval 0 if success
 * @retval -errno otherwise
 */
__syscall int calendar_settime(const struct device *dev, struct tm *tm);

static inline int z_impl_calendar_settime(const struct device *dev, struct tm *tm)
{
	const struct calendar_driver_api *api =
				(struct calendar_driver_api *)dev->api;

	return api->settime(dev, tm);
}

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

//#include <syscalls/calendar.h>

#endif