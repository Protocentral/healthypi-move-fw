# SPDX-License-Identifier: Apache-2.0

zephyr_library_amend()
zephyr_library_sources_ifdef(CONFIG_GC9A01A	display_gc9a01a.c)
zephyr_library_sources_ifdef(CONFIG_SH8601A	display_sh8601a.c)	
