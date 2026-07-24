# SPDX-License-Identifier: Apache-2.0

add_subdirectory_ifdef(CONFIG_DISPLAY display)
add_subdirectory_ifdef(CONFIG_SENSOR sensor)
# RTC: RV-8263-C8 uses the Zephyr in-tree driver; no out-of-tree subdirectory.
add_subdirectory_ifdef(CONFIG_INPUT input)