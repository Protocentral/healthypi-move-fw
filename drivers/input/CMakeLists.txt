# SPDX-License-Identifier: Apache-2.0

zephyr_library_amend()
zephyr_library_property(ALLOW_EMPTY TRUE)

# zephyr-keep-sorted-start
zephyr_library_sources_ifdef(CONFIG_INPUT_CHSC6X input_chsc6x.c)
