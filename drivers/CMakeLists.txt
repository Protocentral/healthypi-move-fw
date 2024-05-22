# SPDX-License-Identifier: Apache-2.0

add_subdirectory_ifdef(CONFIG_DISPLAY display)
add_subdirectory_ifdef(CONFIG_SENSOR sensor)
add_subdirectory_ifdef(CONFIG_RTC rtc)