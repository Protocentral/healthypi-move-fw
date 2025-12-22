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


#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/drivers/rtc.h>

#include <time.h>

#include "hw_module.h"
#include "hpi_common_types.h"
#include "recording_module.h"

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

ZBUS_CHAN_DEFINE(hrv_stat_chan, /* Name */
                 struct hpi_hrv_status_t,
                 NULL, /* Validator */
                 NULL, /* User Data */
                 ZBUS_OBSERVERS(disp_hrv_stat_lis, sys_hrv_stat_lis),
                 ZBUS_MSG_INIT(0) /* Initial value {0} */
);

#if defined(CONFIG_HPI_GSR_STRESS_INDEX)
ZBUS_CHAN_DEFINE(gsr_stress_chan, /* Name */
                 struct hpi_gsr_stress_index_t,
                 NULL, /* Validator */
                 NULL, /* User Data */
                 ZBUS_OBSERVERS(disp_gsr_stress_lis),
                 ZBUS_MSG_INIT(0) /* Initial value {0} */
);
#endif

#if defined(CONFIG_HPI_GSR_SCREEN)
// Live GSR status channel (elapsed/remaining time updates)
ZBUS_CHAN_DEFINE(gsr_status_chan,
                 struct hpi_gsr_status_t,
                 NULL,
                 NULL,
                 ZBUS_OBSERVERS(disp_gsr_status_lis),
                 ZBUS_MSG_INIT(0));
#endif

// Recording status channel (for UI updates during background recording)
ZBUS_CHAN_DEFINE(recording_status_chan,
                 struct hpi_recording_status_t,
                 NULL,
                 NULL,
                 ZBUS_OBSERVERS(disp_recording_lis),
                 ZBUS_MSG_INIT(0));