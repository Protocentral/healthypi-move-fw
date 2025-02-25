# Copyright (c) 2022 Bjarki Arge Andreasen
# SPDX-License-Identifier: Apache-2.0

zephyr_syscall_header(${ZEPHYR_BASE}/include/zephyr/drivers/rtc.h)

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_RTC_RV8263 rtc_rv8263.c)
