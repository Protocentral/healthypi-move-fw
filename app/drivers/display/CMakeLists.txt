# SPDX-License-Identifier: Apache-2.0

zephyr_library_amend()
zephyr_include_directories(.)
zephyr_library_sources_ifdef(CONFIG_SH8601	display_sh8601.c)


