#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/drivers/rtc.h>

#include <time.h>

#include "hw_module.h"
#include "hpi_common_types.h"

ZBUS_CHAN_DEFINE(batt_chan,                     /* Name */
                 struct hpi_batt_status_t,      /* Message type */
                 NULL,                          /* Validator */
                 NULL,                          /* User Data */
                 ZBUS_OBSERVERS(disp_batt_lis), /* observers */
                 ZBUS_MSG_INIT(0)               /* Initial value {0} */
);

ZBUS_CHAN_DEFINE(sys_time_chan, /* Name */
                 struct tm,     /* Message type */
                 NULL,          /* Validator */
                 NULL,          /* User Data */
                 ZBUS_OBSERVERS(disp_sys_time_lis, trend_sys_time_lis, sys_sys_time_lis),
                 ZBUS_MSG_INIT(0) /* Initial value {0} */
);

ZBUS_CHAN_DEFINE(hr_chan,         /* Name */
                 struct hpi_hr_t, /* Message type */
                 NULL,            /* Validator */
                 NULL,            /* User Data */
                 ZBUS_OBSERVERS(disp_hr_lis, trend_hr_lis, sys_hr_lis),
                 ZBUS_MSG_INIT(0) /* Initial value {0} */
);

ZBUS_CHAN_DEFINE(steps_chan,         /* Name */
                 struct hpi_steps_t, /* Message type */
                 NULL,               /* Validator */
                 NULL,               /* User Data */
                 ZBUS_OBSERVERS(disp_steps_lis, trend_steps_lis, sys_steps_lis),
                 ZBUS_MSG_INIT(0) /* Initial value {0} */
);

ZBUS_CHAN_DEFINE(temp_chan, /* Name */
                 struct hpi_temp_t,
                 NULL, /* Validator */
                 NULL, /* User Data */
                 ZBUS_OBSERVERS(disp_temp_lis, trend_temp_lis, sys_temp_lis),
                 ZBUS_MSG_INIT(0) /* Initial value {0} */
);

ZBUS_CHAN_DEFINE(bpt_chan, /* Name */
                 struct hpi_bpt_t,
                 NULL, /* Validator */
                 NULL, /* User Data */
                 ZBUS_OBSERVERS(disp_bpt_lis, trend_bpt_lis, ble_bpt_lis, sys_bpt_lis),
                 ZBUS_MSG_INIT(0) /* Initial value {0} */
);

ZBUS_CHAN_DEFINE(spo2_chan, /* Name */
                 struct hpi_spo2_point_t,
                 NULL, /* Validator */
                 NULL, /* User Data */
                 ZBUS_OBSERVERS(disp_spo2_lis, trend_spo2_lis),
                 ZBUS_MSG_INIT(0) /* Initial value {0} */
);

ZBUS_CHAN_DEFINE(ecg_stat_chan, /* Name */
                 struct hpi_ecg_status_t,
                 NULL, /* Validator */
                 NULL, /* User Data */
                 ZBUS_OBSERVERS(disp_ecg_stat_lis, sys_ecg_stat_lis),
                 ZBUS_MSG_INIT(0) /* Initial value {0} */
);