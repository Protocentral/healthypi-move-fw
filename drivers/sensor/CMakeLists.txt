# Copyright (c) 2025 Protocentral Electronics
# SPDX-License-Identifier: Apache-2.0

add_subdirectory_ifdef(CONFIG_SENSOR_MAX30001 max30001)
add_subdirectory_ifdef(CONFIG_SENSOR_MAX32664C max32664c)
add_subdirectory_ifdef(CONFIG_SENSOR_MAX32664D max32664d)
add_subdirectory_ifdef(CONFIG_SENSOR_MAXM86146 maxm86146)
add_subdirectory_ifdef(CONFIG_SENSOR_MAX30208 max30208)
add_subdirectory_ifdef(CONFIG_SENSOR_BMI323_HPI bmi323hpi)


