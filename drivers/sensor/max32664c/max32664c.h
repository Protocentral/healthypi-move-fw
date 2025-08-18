/*
 * Minimal top-level header for MAX32664C that avoids pulling heavy driver headers.
 */
#ifndef _MAX32664C_H_
#define _MAX32664C_H_

/* forward declarations to avoid including <device.h> here */
struct device;

#include <stdint.h>

#define MAX32664C_I2C_ADDRESS 0x55
#define MAX32664C_DEFAULT_CMD_DELAY 10

/* Basic low-level operations that have C linkage. Sensor-specific APIs live in
 * max32664c_sensor.h and are included only by C files that need them.
 */
uint8_t max32664c_read_hub_status(const struct device *dev);
void max32664c_do_enter_bl(const struct device *dev);
int max32664c_do_enter_app(const struct device *dev);

#endif /* _MAX32664C_H_ */
