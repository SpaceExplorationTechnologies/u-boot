/*
 * Copyright (c) 2015 STMicroelectronics.
 * SPDX-License-Identifier: GPL-2.0+
 *
 */

#ifndef __SPACEX_CATSON_COMMON_H
#define __SPACEX_CATSON_COMMON_H

#define CONFIG_ARMV8_SWITCH_TO_EL1

#define CONFIG_FIT_VERBOSE 1
#define CONFIG_TIMESTAMP

#define CPU_RELEASE_ADDR (CONFIG_SYS_SDRAM_BASE + 0x7fff0)
#define COUNTER_FREQUENCY (0x3938700) /*  60 MHz */

/* Defer environment load so it can be loaded from flash. */
#define CONFIG_DELAY_ENVIRONMENT

#define CONFIG_STM_ASC_SERIAL /* use a ST ASC UART */
#define SYS_STM_ASC_BASE GLLCFF_SBC_ASC0_BASE
#define SYS_STM_ASC_CLK (200ul * 1000000ul) /* 200Mhz */

/* Address of the initial Stack Pointer (SP) */
#define CONFIG_SYS_INIT_SP_ADDR \
    (CONFIG_SYS_TEXT_BASE - CONFIG_SYS_MALLOC_LEN \
     - CONFIG_SYS_GBL_DATA_SIZE)

#define CONFIG_SYS_CPU_CLK COUNTER_FREQUENCY
#define CONFIG_MACH_TYPE 9999

/* DMA Coherent memory.  Maintains own allocation scheme. */
#define CATSON_DMA_COHERENT_SIZE (4 * 1024 * 1024)
#define CATSON_DMA_COHERENT_BASE (CONFIG_SYS_TEXT_BASE - CATSON_DMA_COHERENT_SIZE)

/* Start of LMI RAM (identity mapped) */
#define CONFIG_SYS_SDRAM_BASE (CATSON_SYS_SDRAM_PHYS_BASE)
#define CONFIG_SYS_SDRAM_SIZE (CATSON_DMA_COHERENT_BASE - CONFIG_SYS_SDRAM_BASE)

#ifdef CONFIG_CATSON_EMMC_ENABLED

#define CATS_MMC_BOOT_DEV   0
#define CATS_MMC_BOOT_PART  0

#define CONFIG_STM_SDHCI 1
#define CONFIG_STM_SDHCI_0 1
#define CONFIG_CMD_MMC
#define CONFIG_SUPPORT_EMMC_BOOT
#define HAVE_BLOCK_DEVICE

/** mmc0 pinout **/
#define SYSCONF_MMC0_PIO_CMD_PIN   13
#define SYSCONF_MMC0_PIO_D0_PIN    14
#define SYSCONF_MMC0_PIO_D1_PIN    15
#define SYSCONF_MMC0_PIO_D2_PIN    16
#define SYSCONF_MMC0_PIO_D3_PIN    17
#define SYSCONF_MMC0_PIO_D4_PIN    18
#define SYSCONF_MMC0_PIO_D5_PIN    19
#define SYSCONF_MMC0_PIO_D6_PIN    20
#define SYSCONF_MMC0_PIO_D7_PIN    21

#endif /* CONFIG_CATSON_EMMC_ENABLED */

#ifdef CONFIG_CATSON_NOR_ENABLED
/*
 * Use the H/W FSM for SPI, to use bit bang mode define CONFIG_SOFT_SPI here.
 * SPI flash controller with SSC is not being provided yet.
 */
#define CONFIG_STM_FSM_SPI_FLASH
#define CONFIG_CMD_SF 1
#define CONFIG_SPI_FLASH_STMICRO
#define CONFIG_SPI_FLASH_MACRONIX
#define CONFIG_SPI_FLASH_SPANSION

/* flashPHY of flashSS for SPI1-NOR pinout */
#define SPI1_nCS    0, 0, 1 /* SPI Chip-Select */
#define SPI1_CLK    0, 1, 1 /* SPI Clock */
#define SPI1_MOSI   0, 2, 1 /* D0 / Master Out, Slave In */
#define SPI1_MISO   0, 3, 1 /* D1 / Master In, Slave Out */
#define SPI1_nWP    0, 4, 1  /* D2 / SPI Write Protect */
#define SPI1_HOLD   0, 5, 1 /* D3 / SPI Hold */

/* flashPHY of flashSS for SPI2-NOR pinout */
#define SPI2_nCS    0, 6, 1 /* SPI Chip-Select */
#define SPI2_CLK    0, 7, 1 /* SPI Clock */
#define SPI2_MOSI   1, 0, 1 /* D0 / Master Out, Slave In */
#define SPI2_MISO   1, 1, 1 /* D1 / Master In, Slave Out */
#define SPI2_nWP    1, 2, 1 /* D2 / SPI Write Protect */
#define SPI2_HOLD   1, 3, 1 /* D3 / SPI Hold */

/* needed for code compatibility (SPI1 is default one) */
#define SPI_nCS SPI1_nCS
#define SPI_CLK SPI1_CLK
#define SPI_MOSI SPI1_MOSI
#define SPI_MISO SPI1_MISO
#define SPI_nWP SPI1_nWP
#define SPI_HOLD SPI1_HOLD

/* Board-specific flash paramter. Input clock frequency must match targetpack. */
#define SFC_IP_FREQ 133000000
#endif /* CONFIG_CATSON_NOR_ENABLED */

#define PHYS_DDR_1 CONFIG_SYS_SDRAM_BASE
#define PHYS_DDR_1_SIZE CONFIG_SYS_SDRAM_SIZE

#define CONFIG_SYS_MONITOR_LEN 0x100000     /* Reserve 1 MiB for Monitor */
#define CONFIG_SYS_MALLOC_LEN 0x400000      /* Reserve 4 MiB for malloc */
#define CONFIG_SYS_GBL_DATA_SIZE (1024 - 8) /* Global data structures */

#define SX_LINUX_CONSOLE "ttyAS0"

/*
 * Use an alternate default IP for recovery.
 */
#define CONFIG_IPADDR 172.26.20.1

/*
 * The default SX load address is at an address that doesnt match memory.
 */
#define CONFIG_SYS_LOAD_ADDR 0x80000000

#define PIOALT(port, pin, alt, dir) \
    do { \
        stm_pioalt_select((port), (pin), (alt)); \
        stm_pioalt_pad((port), (pin), (dir)); \
    } while (0)

#define CONFIG_BOARD_EARLY_INIT_F
#define CONFIG_BOARD_LATE_INIT

#define CATSON_NO_CONSOLE_BOOTARGS \
    SPACEX_BOOTARGS \
    "uio_pdrv_genirq.of_id=generic-uio " \
    "audit=1 " \
    "SXRUNTIME_EXPECT_SUCCESS=true "

#if !defined(SX_NO_LINUX_CONSOLE)
#define CATSON_BOOTARGS					\
	CATSON_NO_CONSOLE_BOOTARGS			\
	"earlycon=stasc,mmio32,0x8850000," __stringify(CONFIG_BAUDRATE) "n8 "
#else
#define CATSON_BOOTARGS				\
	CATSON_NO_CONSOLE_BOOTARGS
#endif /* SX_NO_LINUX_CONSOLE */

#include "SPACEX_STARLINK_COMMON.h"

#endif /* __SPACEX_CATSON_COMMON_H */
