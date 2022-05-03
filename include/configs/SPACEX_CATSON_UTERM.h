/*
 * Copyright (c) 2015 STMicroelectronics.
 * SPDX-License-Identifier:	GPL-2.0+
 *
 */

#ifndef __CONFIG_H
#define __CONFIG_H

#define CATSON_SYS_SDRAM_PHYS_BASE 0x80000000
#define CATSON_SYS_SDRAM_PHYS_SIZE 0x40000000
#define SX_NO_LINUX_CONSOLE 1

/* u-boot entrypoint. Must match hand-off value hard-coded in BL31. */
#define CONFIG_SYS_TEXT_BASE 0xBF000000
#define CATSON_SYS_TEXT_SIZE (CATSON_SYS_SDRAM_PHYS_BASE + \
			CATSON_SYS_SDRAM_PHYS_SIZE - CONFIG_SYS_TEXT_BASE)

#include "SPACEX_CATSON_COMMON.h"

/*
 * Do not use any memory above 256MB.
 */
#define CONFIG_SYS_BOOTMAPSZ (0x10000000)

#include "spacex_common.h"

#ifndef CONFIG_SYS_MAX_FLASH_BANKS
#define CONFIG_SYS_MAX_FLASH_BANKS 1
#endif

/*
 * Hard code the IP and MAC address for now.
 *
 * We follow the satellite convention for mac addresses (26:12 + ip address)
 * for now until we figure out how to provision real MAC addresses.
 */
#ifdef CONFIG_TARGET_SPACEX_CATSON_TRANSCEIVER
#define SX_UTERM_IPADDR  "172.26.128.65"
#define SX_UTERM_ETHADDR "26:12:ac:1a:80:41"
#else
#define SX_UTERM_IPADDR	 "172.26.128.1"
#define SX_UTERM_ETHADDR "26:12:ac:1a:80:01"
#endif


/*
 * Check for the JTAG flash script, otherwise run the A/B boot command.
 */
#define CONFIG_BOOTCOMMAND "run ${modeboot}"

/* Common Catson configuration for 128MByte SPI-NOR A/B boot. */
#include "spacex_catson_boot.h"

#undef CONFIG_EXTRA_ENV_SETTINGS
#define CONFIG_EXTRA_ENV_SETTINGS \
	SPACEX_BASE_ENV_SETTINGS \
	SPACEX_CATSON_BOOT_SETTINGS \
	"serverip=172.16.1.1\0" \
	"ipaddr=" SX_UTERM_IPADDR "\0" \
	"ethaddr=" SX_UTERM_ETHADDR "\0" \
	"proxy_timeout=30\0" \
	"serialnum=00000\0" \
	"stdout=nulldev\0" \
	"stderr=nulldev\0"

#endif /* __CONFIG_H */
