#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/drivers/rtc.h>

#include "hw_module.h"
#include "hpi_common_types.h"

ZBUS_CHAN_DEFINE(batt_chan,                 /* Name */
                 struct hpi_batt_status_t, /* Message type */
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

ZBUS_CHAN_DEFINE(steps_chan,                 /* Name */
                 struct hpi_steps_t, /* Message type */
                 NULL,                                 /* Validator */
                 NULL,                                 /* User Data */
                 ZBUS_OBSERVERS(disp_steps_lis),
                 ZBUS_MSG_INIT(0)                      /* Initial value {0} */
);

ZBUS_CHAN_DEFINE(temp_chan,                 /* Name */
                 struct hpi_temp_t,
                 NULL,                                 /* Validator */
                 NULL,                                 /* User Data */
                 ZBUS_OBSERVERS(disp_temp_lis),
                 ZBUS_MSG_INIT(0)                      /* Initial value {0} */
);

ZBUS_CHAN_DEFINE(bpt_chan,                 /* Name */
                 struct hpi_bpt_t,
                 NULL,                                 /* Validator */
                 NULL,                                 /* User Data */
                 ZBUS_OBSERVERS(disp_bpt_lis),
                 ZBUS_MSG_INIT(0)                      /* Initial value {0} */
);

ZBUS_CHAN_DEFINE(spo2_chan,                 /* Name */
                 struct hpi_spo2_t,
                 NULL,                                 /* Validator */
                 NULL,                                 /* User Data */
                 ZBUS_OBSERVERS(disp_spo2_lis),
                 ZBUS_MSG_INIT(0)                      /* Initial value {0} */
);