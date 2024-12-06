# Copyright (c) 2021 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

add_subdirectory_ifdef(CONFIG_SENSOR_MAX30001 max30001)
add_subdirectory_ifdef(CONFIG_SENSOR_MAX30205 max30205)
add_subdirectory_ifdef(CONFIG_SENSOR_MAX32664 max32664)
add_subdirectory_ifdef(CONFIG_SENSOR_MAXM86146 maxm86146)
add_subdirectory_ifdef(CONFIG_SENSOR_MAX30208 max30208)
add_subdirectory_ifdef(CONFIG_SENSOR_BMI323_HPI bmi323hpi)

