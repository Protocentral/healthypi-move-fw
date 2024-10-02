#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/drivers/rtc.h>

#include "hw_module.h"
#include "sampling_module.h"

ZBUS_CHAN_DEFINE(batt_chan,                 /* Name */
                 struct batt_status, /* Message type */
                 NULL,                                 /* Validator */
                 NULL,                                 /* User Data */
                 ZBUS_OBSERVERS(disp_batt_lis),  //ZBUS_OBSERVERS_EMPTY, /* observers */
                 ZBUS_MSG_INIT(0)                      /* Initial value {0} */
);

ZBUS_CHAN_DEFINE(sys_time_chan,                 /* Name */
                 struct rtc_time, /* Message type */
                 NULL,                                 /* Validator */
                 NULL,                                 /* User Data */
                 ZBUS_OBSERVERS(disp_sys_time_lis),
                 ZBUS_MSG_INIT(0)                      /* Initial value {0} */
);

ZBUS_CHAN_DEFINE(hr_chan,                 /* Name */
                 struct hpi_hr_t, /* Message type */
                 NULL,                                 /* Validator */
                 NULL,                                 /* User Data */
                 ZBUS_OBSERVERS(disp_hr_lis),
                 ZBUS_MSG_INIT(0)                      /* Initial value {0} */
);