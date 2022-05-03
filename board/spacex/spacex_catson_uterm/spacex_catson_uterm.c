/*
 * (C) Copyright 2014-2105 STMicroelectronics.
 *
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <command.h>
#include <stm/soc.h>
#include <asm/io.h>
#include <spi.h>
#include <spacex/common.h>
#include <spacex/sxid.h>
#include <fdt_support.h>
#include <linux/stringify.h>
#include <malloc.h>

/*
 * Register definitions
 */
#define BACKBONE_PIO_A_PIO2_PIN 0x08102010 /* PIO12 */
#define BACKBONE_PIO_B_PIO0_PIN 0x08110010 /* PIO20 */

/*
 * Strings used for each board revision.
 */
#define BOARD_REV_UTDEV3     "#utdev"
#define BOARD_REV_UTDEV4     "#utdev4"
#define BOARD_REV_MMUTV3     "#mmut"
#define BOARD_REV_MMUTV4     "#mmut4"
#define BOARD_REV_1_1P3      "#rev1_proto1p3"
#define BOARD_REV_1_2P1      "#rev1_proto2p1"
#define BOARD_REV_1_2P2      "#rev1_proto2p2"
#define BOARD_REV_1_3P0      "#rev1_proto3"
#define BOARD_REV_1_PRE_PROD "#rev1_pre_production"
#define BOARD_REV_1_PROD     "#rev1_production"
#define BOARD_REV_2_0P0      "#rev2_proto0"
#define BOARD_REV_2_1P0      "#rev2_proto1"
#define BOARD_REV_2_2P0      "#rev2_proto2"
#define BOARD_REV_2_3P0      "#rev2_proto3"
#define BOARD_REV_2_4P0      "#rev2_proto4"
#define BOARD_REV_3_0P0      "#rev3_proto0"
#define BOARD_REV_3_1P0      "#rev3_proto1"
#define BOARD_REV_3_2P0      "#rev3_proto2"
#define BOARD_REV_HP_1_0P0   "#hp1_proto0"

#define BOARD_REV_TRANSCEIVER_1P5 "#transceiver_rev1p5"
#define BOARD_REV_TRANSCEIVER_2P0 "#transceiver_rev2p0"
#define BOARD_REV_TRANSCEIVER_2P5 "#transceiver_rev2p5"

#define UTERM_LINUX_DEV_CONSOLES	\
	"earlycon=stasc,mmio32,0x8850000," __stringify(CONFIG_BAUDRATE) "n8"	\
	" console=" SX_LINUX_CONSOLE "," __stringify(CONFIG_BAUDRATE) " "

#define UTERM_LINUX_PROD_CONSOLE	"console=ttynull" " "

/*
 * uart pio
 * pio10[0] uart0_txd (sys_conf 0)
 * pio10[1] uart0_rxd (sys_conf 0)
 */

struct stm_uart_config stm_uart_config_dev[] = {
	{ {10, 0, 1}, {10, 1, 1} }, /* GLLCFF_ASC0 */
};

struct stm_uart_config stm_uart_config_prod[] = {
	{ {10, 0, 0}, {10, 1, 0} }, /* GLLCFF_ASC0 */
};

/* emmc pin configuration */
#define MMC_FLASH(_port, _pin, _func) \
		{ \
				.pio       = { _port, _pin, _func, }, \
				.direction = stm_pad_direction_ignored, \
				.retime    = NULL, \
		}

struct stm_mmc_pio_getcd mmc_pio_getcd[] = {
{0, 0}
};

const struct stm_pad_pin stm_mmc_pad_configs[][10] = { {
			MMC_FLASH(1, 0, 2), /* DATA[0]=port_x, pin_x, alter_x */
			MMC_FLASH(1, 1, 2), /* DATA[1]=port_x, pin_x, alter_x */
			MMC_FLASH(1, 2, 2), /* DATA[2]=port_x, pin_x, alter_x */
			MMC_FLASH(1, 3, 2), /* DATA[3]=port_x, pin_x, alter_x */
			MMC_FLASH(1, 4, 2), /* DATA[4]=port_x, pin_x, alter_x */
			MMC_FLASH(1, 5, 2), /* DATA[5]=port_x, pin_x, alter_x */
			MMC_FLASH(1, 6, 2), /* DATA[6]=port_x, pin_x, alter_x */
			MMC_FLASH(1, 7, 2), /* DATA[7]=port_x, pin_x, alter_x */
			MMC_FLASH(0, 7, 2), /* CMD=port_x, pin_x, alter_x */
			MMC_FLASH(0, 6, 2), /* Clock=port_x, pin_x, alter_x */
			}
};

extern int board_early_init_f(void)
{
	/**
	 * Even though the uBoot env vars stdout and stderr are
	 * set to nulldev, uBoot still prints:
	 *
	 * U-Boot 2021.04 (Aug 11 2021 - 18:14:56 +0000)
	 *
	 *   Model: Catson
	 *   DRAM:  1004 MiB
	 *   MMC:   Fast boot:eMMC: 8xbit - div2
	 *   stm-sdhci0: 0
	 *   In:    nulldev
	 *   Out:   nulldev
	 *   Err:   nulldev
	 *
	 *   To turn this off, the Catson's UART0 register is set
	 *   below as well.
	 */
	/* setup asc 0 pio */
	if (is_prod_hw()) {
		stm_uart_init(&stm_uart_config_prod[0]);
	} else {
		stm_uart_init(&stm_uart_config_dev[0]);
	}

	return 0;
}

extern int board_init(void)
{
	return 0;
}

int checkboard(void)
{
#ifdef CONFIG_SPI_FLASH
	/*
	 * Configure for the SPI Serial Flash.
	 * Note: for CONFIG_SYS_BOOT_FROM_SPI + CONFIG_ENV_IS_IN_EEPROM, this
	 * needs to be done after env_init(), hence it is done
	 * here, and not in board_init().
	 */
	stm_configure_spi_flashSS(SPI_NOR_1);
#endif

	return 0;
}

/*
 * Late board initialization. Set unique ID based on board strapping,
 * generate MAC address, call corresponding SoC function.
 */
int board_late_init(void)
{
	u32 cpu_id[3];
	int fuse_board_type;
	u32 catson_version;
	char boardserialnum[25]; /* %08x%08x%08x null-terminated */
	const char *board_rev_string = "";
	int enable_xphy = 1;
	int ret = 0;
	sxid_t sxid;
	char new_ipaddr[16];
	char new_ethaddr[18];
	int is_utdev_mmut = 0;

	/* Read the unique processor ID from fuses */
	cpu_id[0] = readl(SECURITY_BSEC_DEVICEID0);
	cpu_id[1] = readl(SECURITY_BSEC_DEVICEID1);
	cpu_id[2] = readl(SECURITY_BSEC_DEVICEID2);

	printf("CPU ID: 0x%08x 0x%08x 0x%08x\n", cpu_id[0], cpu_id[1],
		   cpu_id[2]);

	/* Set the board serial number to the CPU ID */
	if (snprintf(boardserialnum, sizeof(boardserialnum),
			 "%08x%08x%08x", cpu_id[0], cpu_id[1], cpu_id[2]) > 0)
		env_set("boardserialnum", boardserialnum);
	else
		printf("Failed to set boardserialnum\n");

	catson_version = catson_get_version();
	fuse_board_type = catson_get_board_type_fuse();
	if (fuse_board_type > 0) {
		switch (fuse_board_type) {
		case 1:
			if (catson_version >= CATSON_VERSION_CUT4) {
				board_rev_string = BOARD_REV_UTDEV4;
			} else {
				board_rev_string = BOARD_REV_UTDEV3;
			}
			is_utdev_mmut = 1;
			break;
		case 2:
			if (catson_version >= CATSON_VERSION_CUT4) {
				board_rev_string = BOARD_REV_MMUTV4;
			} else {
				board_rev_string = BOARD_REV_MMUTV3;
			}
			/* See PLAT-4996: MMUT cards are not XPHY-capable */
			enable_xphy = 0;
			is_utdev_mmut = 1;
			break;
		}
	} else {
#ifndef CONFIG_TARGET_SPACEX_CATSON_TRANSCEIVER
		/**
		 * Check board ID GPIOs to find board revision.
		 * The board IDs are mapped as follows
		 * id_b0:pio12[2]
		 * id_b1:pio12[3]
		 * id_b2:pio12[0]
		 * id_b3:pio12[1]
		 * id_b4:pio20[4]
		 */
		u32 pio12 = readl(BACKBONE_PIO_A_PIO2_PIN);
		u32 pio20 = readl(BACKBONE_PIO_B_PIO0_PIN);
		u32 board_id = (((pio12 >> 2) & 1) << 0) |
				   (((pio12 >> 3) & 1) << 1) |
				   (((pio12 >> 0) & 1) << 2) |
				   (((pio12 >> 1) & 1) << 3) |
				   (((pio20 >> 4) & 1) << 4);
		switch (board_id) {
		case 0b11111:
			board_rev_string = BOARD_REV_1_1P3;
			break;
		case 0b11100:
			board_rev_string = BOARD_REV_1_2P1;
			break;
		case 0b11000:
			board_rev_string = BOARD_REV_1_2P2;
			break;
		case 0b10100:
			board_rev_string = BOARD_REV_1_3P0;
			break;
		case 0b10000: /* rev1 pre-production */
			board_rev_string = BOARD_REV_1_PRE_PROD;
			break;
		case 0b11110: /* rev1 production */
			board_rev_string = BOARD_REV_1_PROD;
			break;
		case 0b00001:
			board_rev_string = BOARD_REV_2_0P0;
			break;
		case 0b00010:
			board_rev_string = BOARD_REV_2_1P0;
			break;
		case 0b00011:
			board_rev_string = BOARD_REV_2_2P0;
			break;
		case 0b10001:
			board_rev_string = BOARD_REV_2_3P0;
			break;
		case 0b00101:
			board_rev_string = BOARD_REV_2_4P0;
			break;
		case 0b00100:
			board_rev_string = BOARD_REV_3_0P0;
			break;
		case 0b00110:
			board_rev_string = BOARD_REV_3_1P0;
			break;
		case 0b00111:
			board_rev_string = BOARD_REV_3_2P0;
			break;
		case 0b01010:
			board_rev_string = BOARD_REV_HP_1_0P0;
			break;
		}
#else
	/**
	 * Check board ID GPIOs to find board revision.
	 * The board IDs are mapped as follows
	 * id_b0:pio12[0]
	 * id_b1:pio12[1]
	 * id_b2:pio12[2]
	 * id_b3:pio12[3]
	 */
	u32 pio12 = readl(BACKBONE_PIO_A_PIO2_PIN);
	u32 transceiver_board_id = (((pio12 >> 3) & 1) << 0) |
				(((pio12 >> 2) & 1) << 1) |
				(((pio12 >> 1) & 1) << 2) |
				(((pio12 >> 0) & 1) << 3);
	switch (transceiver_board_id) {
	case 0b0000:
		board_rev_string = BOARD_REV_TRANSCEIVER_2P5;
		break;
	case 0b1111:
		board_rev_string = BOARD_REV_TRANSCEIVER_2P0;
		break;
	case 0b1001:
	default:
		board_rev_string = BOARD_REV_TRANSCEIVER_1P5;
		break;
	}
#endif /* CONFIG_TARGET_SPACEX_CATSON_TRANSCEIVER */
	}

	if (!is_prod_hw() && (is_utdev_mmut == 0)) {
		/*
		 * Read the SXID partition to find specific IP and MAC
		 * addresses.
		 */
#ifdef CONFIG_CATSON_EMMC_BOOT
		ret = sxid_read_from_mmc(CATS_MMC_BOOT_DEV, CATS_MMC_BOOT_PART,
				CATS_SXID_OFFSET, CATS_SXID_SIZE, &sxid);
#else
		ret = sxid_read_from_flash(CATS_SXID_OFFSET, CATS_SXID_SIZE, &sxid);
#endif

		if (!ret) {
			/*
			 * Set the IP address if found.
			 */
			if  (sxid.slot_id & IPV4(255, 255, 255, 255)) {
				snprintf(new_ipaddr, sizeof(new_ipaddr),
						"%u.%u.%u.%u",
						(sxid.slot_id >> 24) & 0xFF,
						(sxid.slot_id >> 16) & 0xFF,
						(sxid.slot_id >> 8) & 0xFF,
						sxid.slot_id & 0xFF);
				printf("Pre-set IP address %s found\n",
						new_ipaddr);
				env_set("ipaddr", new_ipaddr);
			}
			u64 macaddr = ((u64)sxid.mac[0][0]) << 40 |
						((u64)sxid.mac[0][1]) << 32 |
						((u64)sxid.mac[0][2]) << 24 |
						((u64)sxid.mac[0][3]) << 16 |
						((u64)sxid.mac[0][4]) << 8 |
						((u64)sxid.mac[0][5]);
			if (macaddr) {
				snprintf(new_ethaddr, sizeof(new_ethaddr),
						"%02X:%02X:%02X:%02X:%02X:%02X",
						sxid.mac[0][0],
						sxid.mac[0][1],
						sxid.mac[0][2],
						sxid.mac[0][3],
						sxid.mac[0][4],
						sxid.mac[0][5]);
				printf("Pre-set MAC address %s found\n",
						new_ethaddr);
				env_set("ethaddr", new_ethaddr);
			}
		}
	}

	printf("Detected Board rev: %s\n", board_rev_string);
	env_set("boot_type", board_rev_string);
	/** Skip the leading hash. */
	if (board_rev_string[0] != '\0')
		env_set("board_revision", board_rev_string+1);

	/* Perform common late initialization. */
	catson_late_init();

	/* Initialize board-specific processor clocks & resets. */
	int scp_mapping = strstr(board_rev_string, "#hp")
		? scp_lite_pio32_2 : scp_lite_pio20_2;
	configure_sysconf(true, true, scp_mapping, enable_xphy, false, true);

	return 0;
}

/**
 * Set board-specific env vars.
 */
int spacex_initr_env(void)
{
	const char *addl_args = is_prod_hw() ?
		UTERM_LINUX_PROD_CONSOLE : UTERM_LINUX_DEV_CONSOLES;
	size_t addl_args_len = strlen(addl_args);
	char *current_bootargs = env_get("bootargs");
	size_t current_bootargs_len = strlen(current_bootargs);

	size_t new_bootargs_len = current_bootargs_len + addl_args_len + 1;
	char *new_bootargs = malloc(new_bootargs_len);
	if (new_bootargs) {
		snprintf(new_bootargs, new_bootargs_len, "%s %s",
				current_bootargs, addl_args);
		/**
		 * Cannot set these env vars anywhere but after initr_env.
		 */
		env_set("bootargs", new_bootargs);
		free(new_bootargs);
	}

	if (!is_prod_hw()) {
		env_set("stdin", "serial");
		env_set("stdout", "serial");
		env_set("stderr", "serial");
	}
	return 0;
}

/**
 * Perform board-specific initialization (relying on the device tree
 * to be loaded).
 *
 * @fdt:	A pointer to the Flattened Device Tree.
 *
 * Return: a descriptor to common board operations that are needed by
 * SpaceX code.
 */
const struct board_ops *spacex_board_init(void *fdt)
{
	return NULL;
}

/**
 * Pass board specific information to Linux via the /chosen node of the device
 * tree.
 *
 * @fdt:	A pointer to the Flattened Device Tree.
 */
void spacex_board_populate_fdt_chosen(void *fdt)
{
	catson_populate_fdt_chosen(fdt);

	/*
	 * Skip proxy check for User Terminals.
	 */
	env_set("skip-proxy", "1");
	fdt_add_env_to_chosen(fdt, "skip-proxy-check", "skip-proxy");

	/*
	 * Disable serial0
	 */
	if (is_prod_hw()) {
		fdt_set_status_by_alias(fdt, SX_LINUX_CONSOLE, FDT_STATUS_DISABLED, 0);
	}
}
