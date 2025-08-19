# ProtoCentral Electronics (info@protocentral.com)
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_include_directories(.)
zephyr_library_sources(max30208.c)
zephyr_library_sources_ifdef(CONFIG_SENSOR_ASYNC_API max30208_async.c max30208_decoder.c)
