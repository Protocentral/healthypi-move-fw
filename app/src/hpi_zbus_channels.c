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

ZBUS_CHAN_DEFINE(batt_chan,                     /* Name */
                 struct hpi_batt_status_t,      /* Message type */
                 NULL,                          /* Validator */
                 NULL,                          /* User Data */
                 ZBUS_OBSERVERS(disp_batt_lis, ble_batt_lis), /* observers */
                 ZBUS_MSG_INIT(0)               /* Initial value {0} */
);

ZBUS_CHAN_DEFINE(sys_time_chan, /* Name */
                 struct tm,     /* Message type */
                 NULL,          /* Validator */
                 NULL,          /* User Data */
                 ZBUS_OBSERVERS(disp_sys_time_lis, sys_sys_time_lis),
                 ZBUS_MSG_INIT(0) /* Initial value {0} */
);

ZBUS_CHAN_DEFINE(hr_chan,         /* Name */
                 struct hpi_hr_t, /* Message type */
                 NULL,            /* Validator */
                 NULL,            /* User Data */
                 ZBUS_OBSERVERS(disp_hr_lis, hs_hr_lis, ble_hr_lis),
                 ZBUS_MSG_INIT(0) /* Initial value {0} */
);

ZBUS_CHAN_DEFINE(steps_chan,         /* Name */
                 struct hpi_steps_t, /* Message type */
                 NULL,               /* Validator */
                 NULL,               /* User Data */
                 ZBUS_OBSERVERS(disp_steps_lis, hs_steps_lis),
                 ZBUS_MSG_INIT(0) /* Initial value {0} */
);

ZBUS_CHAN_DEFINE(temp_chan, /* Name */
                 struct hpi_temp_t,
                 NULL, /* Validator */
                 NULL, /* User Data */
                 ZBUS_OBSERVERS(disp_temp_lis, hs_temp_lis, ble_temp_lis),
                 ZBUS_MSG_INIT(0) /* Initial value {0} */
);

ZBUS_CHAN_DEFINE(bpt_chan, /* Name */
                 struct hpi_bpt_t,
                 NULL, /* Validator */
                 NULL, /* User Data */
                 ZBUS_OBSERVERS(disp_bpt_lis, hs_bpt_lis),
                 ZBUS_MSG_INIT(0) /* Initial value {0} */
);

ZBUS_CHAN_DEFINE(spo2_chan, /* Name */
                 struct hpi_spo2_point_t,
                 NULL, /* Validator */
                 NULL, /* User Data */
                 ZBUS_OBSERVERS(disp_spo2_lis, hs_spo2_lis, ble_spo2_lis),
                 ZBUS_MSG_INIT(0) /* Initial value {0} */
);

ZBUS_CHAN_DEFINE(ecg_stat_chan, /* Name */
                 struct hpi_ecg_status_t,
                 NULL, /* Validator */
                 NULL, /* User Data */
                 ZBUS_OBSERVERS(disp_ecg_stat_lis, hs_ecg_lis),
                 ZBUS_MSG_INIT(0) /* Initial value {0} */
);

#if defined(CONFIG_HPI_GSR_STRESS_INDEX)
ZBUS_CHAN_DEFINE(gsr_stress_chan, /* Name */
                 struct hpi_gsr_stress_index_t,
                 NULL, /* Validator */
                 NULL, /* User Data */
                 ZBUS_OBSERVERS(disp_gsr_stress_lis, hs_gsr_lis),
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

/* recording_status_chan removed — recording feature torn down (health store
 * Record tier will replace it in H-REC). */