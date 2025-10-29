/*
 * Copyright (c) 2022 Carlo Caione <ccaione@baylibre.com>
 * Copyright# Copyright (c) 2024 Nordic Semiconductor
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file
 * @brief External QSPI flash Linker command/script file
 *
 * Linker script for moving desired .text and .data to the external
 * memory space.
 */

#include <zephyr/linker/sections.h>
#include <zephyr/devicetree.h>

#include <zephyr/linker/linker-defs.h>
#include <zephyr/linker/linker-tool.h>


/* Let SystemInit() be called in place of z_arm_platform_init() by default. */
PROVIDE(z_arm_platform_init = SystemInit);

MEMORY
{
     EXTFLASH (wx) : ORIGIN = 0x10120200, LENGTH = 0xFFE00
}

#include <zephyr/arch/arm/cortex_m/scripts/linker.ld>
