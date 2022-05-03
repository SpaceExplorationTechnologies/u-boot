/*
 * Copyright (c) 2015 STMicroelectronics.
 * SPDX-License-Identifier: GPL-2.0+
 *
 */

#ifndef __CONFIG_H
#define __CONFIG_H

#define CATSON_SYS_SDRAM_PHYS_BASE 0x80000000
#define CATSON_SYS_SDRAM_PHYS_SIZE 0x40000000  /* All boards have >= 1GB */

/* u-boot entrypoint. Must match hand-off value hard-coded in BL31. */
#define CONFIG_SYS_TEXT_BASE 0xBF000000
#define CATSON_SYS_TEXT_SIZE (CATSON_SYS_SDRAM_PHYS_BASE + \
            CATSON_SYS_SDRAM_PHYS_SIZE - CONFIG_SYS_TEXT_BASE)

#include "SPACEX_CATSON_COMMON.h"

/*
 * Do not use any memory above 768Mbyte.
 */
#define CONFIG_SYS_BOOTMAPSZ (0x30000000)

#include "spacex_common.h"

#ifndef CONFIG_SYS_MAX_FLASH_BANKS
#define CONFIG_SYS_MAX_FLASH_BANKS 1
#endif

#define CONFIG_BOOTCOMMAND "run burn_all_${boot_mode}"

/* Common Catson configuration for 128MByte SPI-NOR A/B boot. */
#include "spacex_catson_boot.h"

#undef CONFIG_EXTRA_ENV_SETTINGS
#define CONFIG_EXTRA_ENV_SETTINGS \
    SPACEX_BASE_ENV_SETTINGS \
    SPACEX_CATSON_BOOT_SETTINGS \
    SPACEX_CATSON_NOR_BURN_SETTINGS \
    SPACEX_CATSON_EMMC_BURN_SETTINGS \
    "stdin=serial\0" \
    "stdout=serial\0" \
    "stderr=serial\0"

#endif /* __CONFIG_H */
