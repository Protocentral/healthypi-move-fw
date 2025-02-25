# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(.)
zephyr_library()
zephyr_library_sources(max30001.c)

zephyr_library_sources_ifdef(CONFIG_SENSOR_ASYNC_API max30001_async.c max30001_decoder.c)