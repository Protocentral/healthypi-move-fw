#ifndef TOUCH_H
#define TOUCH_H

#include <zephyr/device.h>

enum hpi_touch_ctrl {
	HPI_TOUCH_NONE = 0,
	HPI_TOUCH_CHSC5816, /* display module V1 */
	HPI_TOUCH_CST816S,  /* display module V2 */
};
void hpi_display_signal_touch_wakeup(void);
enum hpi_touch_ctrl hpi_touch_detect(void);
const struct device *hpi_touch_get_dev(void);

#endif