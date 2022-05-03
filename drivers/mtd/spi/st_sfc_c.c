/*
 * ST Serial Flash Controller Rev C
 *
 * Author: Christophe Kerello <christophe.kerello@st.com>
 *
 * Copyright (C) 2010-2015 STMicroelectronics Limited
 *
 * JEDEC probe based on drivers/mtd/devices/m25p80.c
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <common.h>
#include <malloc.h>
#include <stm/soc.h>
#include <stm/socregs.h>
#include <asm/io.h>
#include <linux/bug.h>
#include <linux/mtd/mtd.h>
#include <spi.h>
#include <spi_flash.h>
#include <flash.h>
#include <linux/sizes.h>
#include <errno.h>
#include <div64.h>

#include "sf_internal.h"
#include <linux/compat.h>
#include <asm/dma-mapping.h>

/* Erase commands */
#define CMD_ERASE_4K			0x20
#define CMD_ERASE_32K			0x52
#define CMD_ERASE_CHIP			0xc7
#define CMD_ERASE_64K			0xd8

/* Write commands */
#define CMD_WRITE_STATUS		0x01
#define CMD_PAGE_PROGRAM		0x02
#define CMD_WRITE_DISABLE		0x04
#define CMD_WRITE_ENABLE		0x06
#define CMD_QUAD_PAGE_PROGRAM_EXT	0x12
#define CMD_QUAD_PAGE_PROGRAM		0x32
#define CMD_WRITE_EVCR			0x61
#define CMD_DUAL_PAGE_PROGRAM		0xa2
#define CMD_DUAL_PAGE_PROGRAM_EXT	0xd2

/* Read commands */
#define CMD_READ_ARRAY_SLOW		0x03
#define CMD_READ_ARRAY_FAST		0x0b
#define CMD_READ_DUAL_OUTPUT_FAST	0x3b
#define CMD_READ_DUAL_IO_FAST		0xbb
#define CMD_READ_QUAD_OUTPUT_FAST	0x6b
#define CMD_READ_QUAD_IO_FAST		0xeb
#define CMD_READ_ID			0x9f
#define CMD_READ_STATUS			0x05
#define CMD_READ_STATUS1		0x35
#define CMD_READ_CONFIG			0x35
#define CMD_FLAG_STATUS			0x70
#define CMD_READ_EVCR			0x65
#define CMD_READ_VOLATR			0x85

/* READ commands with 32-bit addressing */
#define CMD_READ4			0x13	/* Read data bytes (slow) */
#define CMD_READ4_FAST			0x0c	/* Read data bytes (fast) */
#define CMD_READ4_DUAL_IO_FAST		0xbc
#define CMD_READ4_DUAL			0x3c	/* Read data bytes (Dual SPI) */
#define CMD_READ4_QUAD			0x6c	/* Read data bytes (Quad SPI) */
#define CMD_READ4_QUAD_IO_FAST		0xec

/* Used for Macronix and Winbond flashes. */
#define CMD_EN4B			0xb7	/* Enter 4-byte mode */
#define CMD_EX4B			0xe9	/* Exit 4-byte mode */

/* Common status */
#define STATUS_WIP			BIT(0)
#define STATUS_QEB_WINSPAN		BIT(1)
#define STATUS_QEB_MXIC			BIT(6)
#define STATUS_PEC			BIT(7)
#define STATUS_QEB_MICRON		BIT(7)
#define SR_BP0				BIT(2)  /* Block protect 0 */
#define SR_BP1				BIT(3)  /* Block protect 1 */
#define SR_BP2				BIT(4)  /* Block protect 2 */
#define SR_BP3				BIT(5)	/* Block protect 3 */

/*
 * PPL0 output freq is 1.2 GHz.
 * At reset, divider for SPI is equal to 12 i.e SPI-CLK equal to 100MHz
 * TPack may set this divider to 9, i.e SPI-CLK at 133MHz, which is max value
 * targeted for GLLCFF
 */
#define PLL0_SPI_DIV_OFFSET		0x17C
#define PLL0_SPI_DIVIDER_RESET_VALUE	0x4b   /* i.e 12 divider */
#define PLL0_SPI_MAX_FREQ		133000000

/* SFC Controller Registers */
#define SFC_MODE_SELECT			0x0000
#define SFC_CLOCK_CONTROL		0x0004
#define SFC_INTERRUPT_ENABLE		0x000c
#define SFC_INTERRUPT_STATUS		0x0010
#define SFC_INTERRUPT_CLEAR		0x0014
#define SFC_TX_DELAY			0x001C
#define SFC_RX_DELAY			0x0020
#define SFC_IP_SW_RESET			0x0024
#define SFC_IP_STATUS			0x0028
#define SFC_FLASH_ADDRESS_CONFIG	0x0030
#define SFC_READ_FLASH_STATUS_CONFIG	0x0034
#define SFC_CHECK_FLASH_STATUS_CONFIG	0x0038
#define SFC_FLASH_STATUS_DATA		0x003c
#define SFC_POLL_SCHEDULER_0		0x0040
#define SFC_CLOCK_DIVISION		0x0050
#define SFC_TRANSMIT_CONFIG_CDM		0x0060
#define SFC_CONFIG_CDM			0x0064
#define SFC_DATA_CDM			0x0068
#define SFC_OPCODE_0			0x0100
#define SFC_OPCODE_1			0x0104
#define SFC_OPCODE_2			0x0108
#define SFC_OPCODE_3			0x010c
#define SFC_OPCODE_4			0x0110
#define SFC_OPCODE_5			0x0114
#define SFC_FLASH_ADDRESS		0x0118
#define SFC_SEQUENCE_0			0x011c
#define SFC_SEQUENCE_1			0x0120
#define SFC_SEQUENCE_2			0x0124
#define SFC_SEQUENCE_3			0x0128
#define SFC_DATA_INST_SIZE		0x012c
#define SFC_SYSTEM_MEMORY_ADDRESS	0x0130
#define SFC_SEQUENCE_OTP_0		0x0134
#define SFC_SEQUENCE_OTP_1		0x0138
#define SFC_SEQUENCE_CONFIG		0x013c

/* Register: SFC_MODE_SELECT */
#define SFC_BOOT_ENABLE			BIT(0)
#define SFC_CPU_MODE			BIT(1)
#define SFC_DMA_MODE			BIT(2)
#define SFC_WR_HS_NOT_DS		BIT(3)
#define SFC_RD_HS_NOT_DS		BIT(4)
#define SFC_CS_SEL_1_NOT_0		BIT(5)
#define SFC_T1_ENB_R_OPC		BIT(6)
#define SFC_T3_ENB_R_OPC		BIT(7)

/* Register: SFC_CLOCK_CONTROL */
#define SFC_START_DLL			BIT(4)
#define SFC_TX_DLL_SEL			BIT(5)
#define SFC_RX_DLL_SEL			BIT(6)
#define SFC_RX_CLK_SEL			BIT(7)
#define SFC_DLL_FUNC_CLK_SEL		BIT(9)

/* Register: SFC_INTERRUPT_ENABLE */
#define SFC_INT_ENABLE			BIT(0)

/* Register: SFC_INTERRUPT_STATUS */
#define SFC_INT_PENDING			BIT(0)

/* Register: SFC_INTERRUPT_ENABLE/ SFC_INTERRUPT_STATUS/SFC_INTERRUPT_CLEAR */
#define SFC_INT_SEQUENCE_COMPLETION	BIT(2)
#define SFC_INT_CHECK_ERROR		BIT(4)
#define SFC_INT_TIMEOUT_ERROR		BIT(7)
#define SFC_INT_FIFO_UNDERFLOW_ERROR	BIT(12)
#define SFC_INT_FIFO_OVERFLOW_ERROR	BIT(13)
#define SFC_INT_BOOT_ROGUE_ACCESS_ERROR	BIT(14)

/* Register: SFC_IP_SW_RESET */
#define SFC_SW_RESET_IP			BIT(0)
#define SFC_SW_RESET_RD_PLUG		BIT(4)
#define SFC_SW_RESET_WR_PLUG		BIT(8)

/* Register: SFC_IP_STATUS */
#define SFC_BDM_NOT_IDLE		BIT(16)
#define SFC_DLL_LOCKED			BIT(19)
#define SFC_RD_PLUG_SW_RESET_END	BIT(24)
#define SFC_WR_PLUG_SW_RESET_END	BIT(25)

/* Register: SFC_FLASH_ADDRESS_CONFIG */
#define SFC_ADDR_4_BYTE_MODE		BIT(0)
#define SFC_ADDR_CS_ASSERT		BIT(23)
#define SFC_ADDR_DDR_ENABLE		BIT(24)
#define SFC_ADDR_PADS(x)		(((x) & 0x3) << 25)
#define SFC_ADDR_PADS_1			BIT(25)
#define SFC_ADDR_PADS_2			BIT(26)
#define SFC_ADDR_PADS_4			GENMASK(26, 25)
#define SFC_ADDR_NO_OF_CYC(x)		(((x) & 0x1f) << 27)

/* Register: SFC_READ_FLASH_STATUS_CONFIG */
#define SFC_RFS_INST(x)			(((x) & 0xff) << 0)
#define SFC_RFS_OP_WTR(x)		(((x) & 0x3) << 8)
#define SFC_RFS_SCHED_SEL		BIT(12)
#define SFC_RFS_CS_ASSERT		BIT(23)
#define SFC_RFS_DDR_ENABLE		BIT(24)
#define SFC_RFS_PADS(x)			(((x) & 0x3) << 25)
#define SFC_RFS_PADS_1			BIT(25)
#define SFC_RFS_PADS_2			BIT(26)
#define SFC_RFS_PADS_4			GENMASK(26, 25)
#define SFC_RFS_NO_OF_CYC(x)		(((x) & 0x1f) << 27)

/* Register: SFC_CHECK_FLASH_STATUS_CONFIG */
#define SFC_CFS_INST(x)			(((x) & 0xff) << 0)
#define SFC_CFS_OP_WTR(x)		(((x) & 0x3) << 8)
#define SFC_CFS_CS_ASSERT		BIT(23)
#define SFC_CFS_DDR_ENABLE		BIT(24)
#define SFC_CFS_PADS(x)			(((x) & 0x3) << 25)
#define SFC_CFS_PADS_1			BIT(25)
#define SFC_CFS_PADS_2			BIT(26)
#define SFC_CFS_PADS_4			GENMASK(26, 25)
#define SFC_CFS_NO_OF_CYC(x)		(((x) & 0x1f) << 27)

/* Register: SFC_POLL_SCHEDULER_0 */
#define SFC_SCHED_TIMER(x)		(((x) & 0x3fffff) << 0)
#define SFC_SCHED_COUNTER(x)		(((x) & 0x3ff) << 22)

/* Register: SFC_CLOCK_DIVISION */
#define SFC_CLK_DIVISION(x)		(((x) & 0x7) << 0)
#define SFC_CLK_DIV			GENMASK(2, 0)
#define SFC_CLK_DIV_BYPASS		BIT(3)

/* Register: SFC_TRANSMIT_CONFIG_CDM */
#define SFC_TCFG_CDM_TRANSMIT_DATA(x)	(((x) & 0xffff) << 0)
#define SFC_TCFG_CDM_CS_ASSERT		BIT(23)
#define SFC_TCFG_CDM_DDR_ENABLE		BIT(24)
#define SFC_TCFG_CDM_PADS(x)		(((x) & 0x3) << 25)
#define SFC_TCFG_CDM_PADS_1		BIT(25)
#define SFC_TCFG_CDM_PADS_2		BIT(26)
#define SFC_TCFG_CDM_PADS_4		GENMASK(26, 25)
#define SFC_TCFG_CDM_NO_OF_CYC(x)	(((x) & 0x1f) << 27)

/* Register: SFC_CONFIG_CDM */
#define SFC_CFG_CDM_CS_ASSERT		BIT(23)
#define SFC_CFG_CDM_DDR_ENABLE		BIT(24)
#define SFC_CFG_CDM_PADS(x)		(((x) & 0x3) << 25)
#define SFC_CFG_CDM_PADS_1		BIT(25)
#define SFC_CFG_CDM_PADS_2		BIT(26)
#define SFC_CFG_CDM_PADS_4		GENMASK(26, 25)
#define SFC_CFG_CDM_NO_OF_CYC(x)	(((x) & 0x1f) << 27)

/* Register: SFC_OPCODE_n */
#define SFC_OPC_TRANSMIT_DATA(x)	(((x) & 0xffff) << 0)
#define SFC_OPC_CS_ASSERT		BIT(23)
#define SFC_OPC_DDR_ENABLE		BIT(24)
#define SFC_OPC_PADS(x)			(((x) & 0x3) << 25)
#define SFC_OPC_PADS_1			BIT(25)
#define SFC_OPC_PADS_2			BIT(26)
#define SFC_OPC_PADS_4			GENMASK(26, 25)
#define SFC_OPC_NO_OF_CYC(x)		(((x) & 0x1f) << 27)

/* Register: SFC_DATA_INST_SIZE */
#define SFC_DIS_SIZE(x)			((x) & 0x1ffffff)

/* Register: SFC_SEQUENCE_CONFIG */
#define SFC_SC_REPEAT_COUNT(x)		(((x) & 0xffff) << 0)
#define SFC_SC_BOOT_OTP_ON		BIT(22)
#define SFC_SC_CS_ASSERT		BIT(23)
#define SFC_SC_DDR_ENABLE		BIT(24)
#define SFC_SC_PADS(x)			(((x) & 0x3) << 25)
#define SFC_SC_PADS_1			BIT(25)
#define SFC_SC_PADS_2			BIT(26)
#define SFC_SC_PADS_4			GENMASK(26, 25)
#define SFC_SC_WRITE_NOT_READ		BIT(27)
#define SFC_SC_DMA_ON			BIT(28)
#define SFC_SC_START_SEQ		BIT(31)

/* DMA registers */
#define SFC_REG_G3_RD			0x0808
/* Freq used */
#define SFC_FLASH_SAFE_FREQ		25000000UL

/* Command address size */
#define SFC_32BIT_ADDR			32UL
#define SFC_24BIT_ADDR			24UL

/* Clk div min and max */
#define CLK_DIV_MAX			14
#define CLK_DIV_MIN			2

/* Maximum READID length */
#define SFC_MAX_READID_LEN		6

/* Cycles number for an opcode */
#define SFC_NB_OPCODE_CYCLES		8

/* Page size */
#define SFC_FLASH_PAGESIZE		256
#define SFC_DMA_ALIGNMENT		64

/* Min data transfer size */
#define SFC_MIN_DATA_TRANSFER		4

/* Pad opc/address/data/ */
#define SFC_PADS_1			1
#define SFC_PADS_2			2
#define SFC_PADS_4			3

/* Maximum operation times (in ms) */
#define SFC_FLASH_MAX_CHIP_ERASE_MS	500000 /* Chip Erase time */
#define SFC_FLASH_MAX_SEC_ERASE_MS	30000 /* Sector Erase time */
#define SFC_FLASH_MAX_PAGE_WRITE_MS	100 /* Write Page time */
#define SFC_FLASH_MAX_STA_WRITE_MS	4000 /* Write status reg time */
#define SFC_MAX_WAIT_SEQ_MS		2000 /* Sequence execution time */

/* Invert address */
#define SFC_3_BYTES_ADDR(x)		((((x) & 0xff) << 16) | \
					((x) & 0xff00) | \
					(((x) & 0xff0000) >> 16))
#define SFC_4_BYTES_ADDR(x)		((((x) & 0xff) << 24) | \
					(((x) & 0xff00) << 8) | \
					(((x) & 0xff0000) >> 8) | \
					(((x) & 0xff000000) >> 24))

/* Flags operation of default read/write/erase/lock/unlock routines */
#define SFC_CFG_READ_TOGGLE_32BIT_ADDR	0x00000001
#define SFC_CFG_WRITE_TOGGLE_32BIT_ADDR	0x00000002
#define SFC_CFG_LOCK_TOGGLE_32BIT_ADDR	0x00000004
#define SFC_CFG_ERASE_TOGGLE_32BIT_ADDR	0x00000008
#define SFC_CFG_S25FL_CHECK_ERROR_FLAGS	0x00000010
#define SFC_CFG_N25Q_CHECK_ERROR_FLAGS	0x00000020
#define SFC_CFG_WRSR_FORCE_16BITS	0x00000040
#define SFC_CFG_RD_WR_LOCK_REG		0x00000080
#define SFC_CFG_MX25_TOGGLE_QE_BIT	0x00000100

/* Device flags */
#define SFC_FLAG_SINGLE			0x000000ff
#define SFC_FLAG_READ_WRITE		0x00000001
#define SFC_FLAG_READ_FAST		0x00000002
#define SFC_FLAG_32BIT_ADDR		0x00000004
#define SFC_FLAG_RESET			0x00000008
#define SFC_FLAG_BLK_LOCKING		0x00000010
#define SFC_FLAG_BPX_LOCKING		0x00000020

#define SFC_FLAG_DUAL			0x0000ff00
#define SFC_FLAG_READ_1_1_2		0x00000100
#define SFC_FLAG_READ_1_2_2		0x00000200
#define SFC_FLAG_READ_2_2_2		0x00000400
#define SFC_FLAG_WRITE_1_1_2		0x00001000
#define SFC_FLAG_WRITE_1_2_2		0x00002000
#define SFC_FLAG_WRITE_2_2_2		0x00004000

#define SFC_FLAG_QUAD			0x00ff0000
#define SFC_FLAG_READ_1_1_4		0x00010000
#define SFC_FLAG_READ_1_4_4		0x00020000
#define SFC_FLAG_READ_4_4_4		0x00040000
#define SFC_FLAG_WRITE_1_1_4		0x00100000
#define SFC_FLAG_WRITE_1_4_4		0x00200000
#define SFC_FLAG_WRITE_4_4_4		0x00400000

/* SFC BDM Instruction Opcodes */
#define STSFC_OPC_WRITE			0x1
#define STSFC_OPC_ADDRESS		0x2
#define STSFC_OPC_DATA			0x3
#define STSFC_OPC_RSR			0x4
#define STSFC_OPC_WTR			0x5
#define STSFC_OPC_CHECK			0x6
#define STSFC_OPC_LOOP			0x7
#define STSFC_OPC_STOP			0xF

/* SFC BDM Instructions (== opcode + operand) */
#define STSFC_INSTR(cmd, op)		(cmd | (op << 4))

#define STSFC_INST_WR_OPC_0		STSFC_INSTR(STSFC_OPC_WRITE, 0)
#define STSFC_INST_WR_OPC_1		STSFC_INSTR(STSFC_OPC_WRITE, 1)
#define STSFC_INST_WR_OPC_2		STSFC_INSTR(STSFC_OPC_WRITE, 2)
#define STSFC_INST_WR_OPC_3		STSFC_INSTR(STSFC_OPC_WRITE, 3)

#define STSFC_INST_ADDR			STSFC_INSTR(STSFC_OPC_ADDRESS, 0)

#define STSFC_INST_DATA_BDM		STSFC_INSTR(STSFC_OPC_DATA, 0)
#define STSFC_INST_DATA_BOOT		STSFC_INSTR(STSFC_OPC_DATA, 8)

#define STSFC_INST_STOP			STSFC_INSTR(STSFC_OPC_STOP, 0)

/* address to managed DMA transfer, initialized to NULL */
phys_addr_t p_addr = (phys_addr_t)NULL;

/* SFC BDM sequence node - 64 bytes aligned */
struct stsfc_seq {
	uint32_t opc[6];
	uint32_t addr;
	uint8_t  seq[16];
	uint32_t data_size;
	uint32_t dma_addr;
	uint8_t  seq_otp[8];
	uint32_t seq_cfg;
} __packed __aligned(4);

struct stsfc {
	struct spi_flash flash;
	void __iomem *base;
	uint8_t *dma_buf;
	struct stm_flash_info *info;

	uint32_t configuration;
	uint32_t max_freq;
	bool booted_from_spi;
	bool reset_signal;
	bool reset_por;
	bool cs1_used;

	struct stsfc_seq stsfc_seq_read;
	uint32_t seq_read_addr_cfg;
	struct stsfc_seq stsfc_seq_write;
	uint32_t seq_write_addr_cfg;
	struct stsfc_seq stsfc_seq_erase_sector;
	uint32_t seq_erase_addr_cfg;

	/* Write cdm opcode */
	void (*enter_32bit_addr)(struct stsfc *, bool);
};

/* Parameters to configure a READ or WRITE FSM sequence */
struct seq_rw_config {
	uint32_t flags;
	uint8_t cmd;
	bool write;
	uint8_t addr_pads;
	uint8_t data_pads;
	uint16_t mode_data;
	uint8_t mode_cycles;
	uint8_t dummy_cycles;
};

/* SPI Flash Device Table */
struct stm_flash_info {
	char *name;
	u8 readid[SFC_MAX_READID_LEN];
	int readid_len;
	unsigned sector_size;
	u16 n_sectors;
	u32 flags;
	u32 max_freq;

	int (*config)(struct stsfc *);
	int (*resume)(struct stsfc *);
};

extern uint32_t stm_dma_alloc_coherent(size_t size);

static inline struct stsfc *to_sfc(struct spi_flash *flash)
{
	return container_of(flash, struct stsfc, flash);
}

static inline struct stsfc *mtd_to_sfc(struct mtd_info *mtd)
{
	struct spi_flash *flash = container_of(mtd, struct spi_flash, mtd);

	return to_sfc(flash);
}

/* Device with standard 3-byte JEDEC ID */
#define JEDEC_INFO(_name, _jedec_id, _sector_size, _n_sectors,	\
		   _flags, _max_freq, _config, _resume)		\
	{							\
		.name = (_name),				\
		.readid[0] = ((_jedec_id) >> 16 & 0xff),	\
		.readid[1] = ((_jedec_id) >>  8 & 0xff),	\
		.readid[2] = ((_jedec_id) >>  0 & 0xff),	\
		.readid_len = 3,				\
		.sector_size = (_sector_size),			\
		.n_sectors = (_n_sectors),			\
		.flags = (_flags),				\
		.max_freq = (_max_freq),			\
		.config = (_config),				\
		.resume = (_resume)				\
	}

/* Device with arbitrary-length READID */
#define RDID(...) __VA_ARGS__  /* Dummy macro to protect array argument. */
#define RDID_INFO(_name, _readid, _readid_len, _sector_size,	\
		  _n_sectors, _flags, _max_freq, _config,	\
		  _resume)					\
	{							\
		.name = (_name),				\
		.readid = _readid,				\
		.readid_len = _readid_len,			\
		.flags = (_flags),				\
		.sector_size = (_sector_size),			\
		.n_sectors = (_n_sectors),			\
		.flags = (_flags),				\
		.max_freq = (_max_freq),			\
		.config = (_config),				\
		.resume = (_resume)				\
	}

static int stsfc_n25q_config(struct stsfc *sfc);
static int stsfc_mx25_config(struct stsfc *sfc);
static int stsfc_s25fl_config(struct stsfc *sfc);
static int stsfc_w25q_config(struct stsfc *sfc);

static int stsfc_n25q_resume(struct stsfc *sfc);
static int stsfc_mx25_resume(struct stsfc *sfc);

static struct stm_flash_info flash_types[] = {
	/* Macronix MX25xxx */
#define MX25_FLAG (SFC_FLAG_READ_WRITE | \
		   SFC_FLAG_READ_FAST | \
		   SFC_FLAG_READ_1_1_2 | \
		   SFC_FLAG_READ_1_2_2 | \
		   SFC_FLAG_READ_1_1_4)
	JEDEC_INFO("mx25l3255e",  0xc29e16, 64 * 1024, 64,
		   MX25_FLAG | SFC_FLAG_WRITE_1_4_4, 86000000,
		   stsfc_mx25_config, stsfc_mx25_resume),
	JEDEC_INFO("mx25l12835f", 0xc22018, 64 * 1024, 256,
		   (MX25_FLAG | SFC_FLAG_RESET), 104000000,
		   stsfc_mx25_config, stsfc_mx25_resume),
	JEDEC_INFO("mx25l25635f", 0xc22019, 64 * 1024, 512,
		   (MX25_FLAG | SFC_FLAG_RESET), 104000000,
		   stsfc_mx25_config, stsfc_mx25_resume),
	JEDEC_INFO("mx25l25655f", 0xc22619, 64 * 1024, 512,
		   (MX25_FLAG | SFC_FLAG_RESET), 104000000,
		   stsfc_mx25_config, stsfc_mx25_resume),

	/* Micron N25Qxxx */
#define N25Q_FLAG (SFC_FLAG_READ_WRITE | \
		   SFC_FLAG_READ_FAST | \
		   SFC_FLAG_READ_1_1_2 | \
		   SFC_FLAG_READ_1_2_2 | \
		   SFC_FLAG_READ_1_1_4 | \
		   SFC_FLAG_WRITE_1_1_2 | \
		   SFC_FLAG_WRITE_1_2_2 | \
		   SFC_FLAG_WRITE_1_1_4 | \
		   SFC_FLAG_WRITE_1_4_4 | \
		   SFC_FLAG_BLK_LOCKING)
	JEDEC_INFO("n25q128", 0x20ba18, 64 * 1024,  256,
		   N25Q_FLAG, 108000000, stsfc_n25q_config, stsfc_n25q_resume),

	/*
	 * Micron N25Q256/N25Q512/N25Q00A/MT25Q02G (32-bit ADDR devices)
	 *
	 * Versions are available with or without a dedicated RESET# pin
	 * (e.g. N25Q512A83GSF40G vs. N25Q512A13GSF40G). To complicate matters,
	 * the versions that include a RESET# pin (Feature Set = 8) require a
	 * different opcode for the FLASH_CMD_WRITE_1_4_4 command.
	 * Unfortunately it is not possible to determine easily at run-time
	 * which version is being used.  We therefore remove support for
	 * FLASH_FLAG_WRITE_1_4_4 (falling back to FLASH_FLAG_WRITE_1_1_4), and
	 * defer overall support for RESET# to the board-level platform/Device
	 * Tree property "reset-signal".
	 * Check is done on only 4 bytes as fifth byte is factory dependant
	 * unlike previous that are JEDEC or Manufacturer dependant.
	 */
#define N25Q_32BIT_ADDR_FLAG  ((N25Q_FLAG | \
				SFC_FLAG_RESET) & \
			       ~SFC_FLAG_WRITE_1_4_4)
	JEDEC_INFO("n25q256", 0x20ba19, 64 * 1024,   512,
		   N25Q_32BIT_ADDR_FLAG, 108000000,
		   stsfc_n25q_config, stsfc_n25q_resume),
	RDID_INFO("n25q512", RDID({0x20, 0xba, 0x20, 0x10}), 4,
		  64 * 1024, 1024, N25Q_32BIT_ADDR_FLAG, 108000000,
		  stsfc_n25q_config, stsfc_n25q_resume),
	RDID_INFO("n25q00a", RDID({0x20, 0xba, 0x21, 0x10}), 4,
		  64 * 1024, 2048, N25Q_32BIT_ADDR_FLAG, 108000000,
		  stsfc_n25q_config, stsfc_n25q_resume),
	/* SAT-16521: Max frequency should be runnable at 166000000 */
	RDID_INFO("mt25q02g1", RDID({0x20, 0xba, 0x22, 0x10}), 4,
		  64 * 1024, 4096, N25Q_32BIT_ADDR_FLAG, 108000000,
		  stsfc_n25q_config, stsfc_n25q_resume),
	RDID_INFO("n25q512", RDID({0x20, 0xbb, 0x20, 0x10}), 4,
		  64 * 1024, 1024, N25Q_32BIT_ADDR_FLAG, 108000000,
		  stsfc_n25q_config, stsfc_n25q_resume),
	RDID_INFO("n25q00a", RDID({0x20, 0xbb, 0x21, 0x10}), 4,
		  64 * 1024, 2048, N25Q_32BIT_ADDR_FLAG, 108000000,
		  stsfc_n25q_config, stsfc_n25q_resume),
	/* SAT-16521: Max frequency should be runnable at 166000000 */
	RDID_INFO("mt25q02g2", RDID({0x20, 0xbb, 0x22, 0x10}), 4,
		  64 * 1024, 4096, N25Q_32BIT_ADDR_FLAG, 108000000,
		  stsfc_n25q_config, stsfc_n25q_resume),

	/*
	 * Spansion S25FLxxxP
	 *     - 256KiB and 64KiB sector variants (identified by ext. JEDEC)
	 *     - S25FL128Px devices do not support DUAL or QUAD I/O
	 */
#define S25FLXXXP_FLAG (SFC_FLAG_READ_WRITE | \
			SFC_FLAG_READ_1_1_2 | \
			SFC_FLAG_READ_1_2_2 | \
			SFC_FLAG_READ_1_1_4 | \
			SFC_FLAG_READ_1_4_4 | \
			SFC_FLAG_WRITE_1_1_4 | \
			SFC_FLAG_READ_FAST | \
			SFC_FLAG_BPX_LOCKING)
	RDID_INFO("s25fl032p", RDID({0x01, 0x02, 0x15, 0x4d, 0x00}), 5,
		  64 * 1024,  64, S25FLXXXP_FLAG, 80000000,
		  stsfc_s25fl_config, NULL),
	RDID_INFO("s25fl064p", RDID({0x01, 0x02, 0x16, 0x4d, 0x00}), 5,
		  64 * 1024,  128, S25FLXXXP_FLAG, 80000000,
		  stsfc_s25fl_config, NULL),
	RDID_INFO("s25fl128p1", RDID({0x01, 0x20, 0x18, 0x03, 0x00}), 5,
		  256 * 1024, 64,
		  (SFC_FLAG_READ_WRITE | SFC_FLAG_READ_FAST), 104000000,
		  NULL, NULL),
	RDID_INFO("s25fl128p0", RDID({0x01, 0x20, 0x18, 0x03, 0x01}), 5,
		  64 * 1024, 256,
		  (SFC_FLAG_READ_WRITE | SFC_FLAG_READ_FAST), 104000000,
		  NULL, NULL),
	RDID_INFO("s25fl129p0", RDID({0x01, 0x20, 0x18, 0x4d, 0x00}), 5,
		  256 * 1024,  64, S25FLXXXP_FLAG, 80000000,
		  stsfc_s25fl_config, NULL),
	RDID_INFO("s25fl129p1", RDID({0x01, 0x20, 0x18, 0x4d, 0x01}), 5,
		  64 * 1024, 256, S25FLXXXP_FLAG, 80000000,
		  stsfc_s25fl_config, NULL),

	/*
	 * Spansion S25FLxxxS
	 *     - 256KiB and 64KiB sector variants (identified by ext. JEDEC)
	 *     - RESET# signal supported by die but not bristled out on all
	 *       package types.  The package type is a function of board design,
	 *       so this information is captured in the board's flags.
	 *     - Supports 'DYB' sector protection. Depending on variant, sectors
	 *       may default to locked state on power-on.
	 *     - S25FL127Sx handled as S25FL128Sx
	 */
#define S25FLXXXS_FLAG (S25FLXXXP_FLAG | \
			SFC_FLAG_RESET | \
			SFC_FLAG_BLK_LOCKING)
	RDID_INFO("s25fl128s0", RDID({0x01, 0x20, 0x18, 0x4d, 0x00, 0x80}), 6,
		  256 * 1024, 64, S25FLXXXS_FLAG, 80000000,
		  stsfc_s25fl_config, NULL),
	RDID_INFO("s25fl128s1", RDID({0x01, 0x20, 0x18, 0x4d, 0x01, 0x80}), 6,
		  64 * 1024, 256, S25FLXXXS_FLAG, 80000000,
		  stsfc_s25fl_config, NULL),
	RDID_INFO("s25fl256s0", RDID({0x01, 0x02, 0x19, 0x4d, 0x00, 0x80}), 6,
		  256 * 1024, 128, S25FLXXXS_FLAG, 80000000,
		  stsfc_s25fl_config, NULL),
	RDID_INFO("s25fl256s1", RDID({0x01, 0x02, 0x19, 0x4d, 0x01, 0x80}), 6,
		  64 * 1024, 512, S25FLXXXS_FLAG, 80000000,
		  stsfc_s25fl_config, NULL),
	RDID_INFO("s25fl512s0", RDID({0x01, 0x02, 0x20, 0x4d, 0x00, 0x80}), 6,
		  256 * 1024, 256, S25FLXXXS_FLAG, 80000000,
		  stsfc_s25fl_config, NULL),

#define W25X_FLAG (SFC_FLAG_READ_WRITE | \
		   SFC_FLAG_READ_FAST | \
		   SFC_FLAG_READ_1_1_2 | \
		   SFC_FLAG_WRITE_1_1_2)
	JEDEC_INFO("w25x40", 0xef3013, 64 * 1024,   8, W25X_FLAG, 75000000,
		   NULL, NULL),
	JEDEC_INFO("w25x80", 0xef3014, 64 * 1024,  16, W25X_FLAG, 75000000,
		   NULL, NULL),
	JEDEC_INFO("w25x16", 0xef3015, 64 * 1024,  32, W25X_FLAG, 75000000,
		   NULL, NULL),
	JEDEC_INFO("w25x32", 0xef3016, 64 * 1024,  64, W25X_FLAG, 75000000,
		   NULL, NULL),
	JEDEC_INFO("w25x64", 0xef3017, 64 * 1024, 128, W25X_FLAG, 75000000,
		   NULL, NULL),

	/* Winbond -- w25q "blocks" are 64K, "sectors" are 4KiB */
#define W25Q_FLAG (SFC_FLAG_READ_WRITE | \
		   SFC_FLAG_READ_FAST | \
		   SFC_FLAG_READ_1_1_2 | \
		   SFC_FLAG_READ_1_2_2 | \
		   SFC_FLAG_READ_1_1_4 | \
		   SFC_FLAG_READ_1_4_4 | \
		   SFC_FLAG_WRITE_1_1_4 | \
		   SFC_FLAG_BPX_LOCKING)
	JEDEC_INFO("w25q80", 0xef4014, 64 * 1024,  16,
		   W25Q_FLAG, 80000000, stsfc_w25q_config, NULL),
	JEDEC_INFO("w25q16", 0xef4015, 64 * 1024,  32,
		   W25Q_FLAG, 80000000, stsfc_w25q_config, NULL),
	JEDEC_INFO("w25q32", 0xef4016, 64 * 1024,  64,
		   W25Q_FLAG, 80000000, stsfc_w25q_config, NULL),
	JEDEC_INFO("w25q64", 0xef4017, 64 * 1024, 128,
		   W25Q_FLAG, 80000000, stsfc_w25q_config, NULL),

	/* Spansion S25FL1xxK - same init as Winbond W25Q family devices */
#define S25FL1XXK_FLAG (SFC_FLAG_READ_WRITE | \
			SFC_FLAG_READ_FAST | \
			SFC_FLAG_READ_1_1_2 | \
			SFC_FLAG_READ_1_2_2 | \
			SFC_FLAG_READ_1_1_4 | \
			SFC_FLAG_READ_1_4_4 | \
			SFC_FLAG_BPX_LOCKING)
	JEDEC_INFO("s25fl116k", 0x014015, 64 * 1024,  32, S25FL1XXK_FLAG,
		   108000000, stsfc_w25q_config, NULL),
	JEDEC_INFO("s25fl132k", 0x014016, 64 * 1024,  64, S25FL1XXK_FLAG,
		   108000000, stsfc_w25q_config, NULL),
	JEDEC_INFO("s25fl164k", 0x014017, 64 * 1024, 128, S25FL1XXK_FLAG,
		   108000000, stsfc_w25q_config, NULL),

	{ },
};

/* SFC boot mode registers/masks */
#define GLLCFF_SYSCON_BOOT_DEV_REG	418
#define GLLCFF_SYSCON_BOOT_DEV_SPI	0x000
#define GLLCFF_SYSCON_BOOT_DEV_MASK	0x010

#define SFC_MAX_MODE_PINS		1

struct boot_dev {
	uint32_t reg;
	uint32_t spi[SFC_MAX_MODE_PINS];
	uint32_t mask;
};

#ifdef CONFIG_STM_GLLCFF
static struct boot_dev stsfc_boot_data = {
	.reg = GLLCFF_SYSCON_BOOT_DEV_REG,
	.spi = {GLLCFF_SYSCON_BOOT_DEV_SPI},
	.mask = GLLCFF_SYSCON_BOOT_DEV_MASK,
};
#else
#error Please specify boot device determination data ?
#endif

static inline void stsfc_enable_interrupts(struct stsfc *sfc, uint32_t reg)
{
	writel(reg, sfc->base + SFC_INTERRUPT_ENABLE);
}

static inline uint32_t stsfc_read_interrupts_status(struct stsfc *sfc)
{
	return readl(sfc->base + SFC_INTERRUPT_STATUS);
}

static inline void stsfc_clear_interrupts(struct stsfc *sfc, uint32_t reg)
{
	writel(reg, sfc->base + SFC_INTERRUPT_CLEAR);
}

static inline int stsfc_is_busy(struct stsfc *sfc)
{
	return readl(sfc->base + SFC_IP_STATUS) & SFC_BDM_NOT_IDLE;
}

static inline int stsfc_load_seq(struct stsfc *sfc,
				 const struct stsfc_seq *seq)
{
	void __iomem *dst = sfc->base + SFC_OPCODE_0;
	const uint32_t *src = (const uint32_t *)seq;
	int words;

	if (stsfc_is_busy(sfc)) {
		printf("SFC sequence in progress\n");
		return -EBUSY;
	}

	for (words = sizeof(*seq) / sizeof(*src); words > 0; words--) {
		writel(*src, dst);

		src++;
		dst += sizeof(*src);
	}

	return 0;
}

static inline void stsfc_load_addr_cfg(struct stsfc *sfc,
				       uint32_t addr_cfg)
{
	writel(addr_cfg, sfc->base + SFC_FLASH_ADDRESS_CONFIG);
}

static inline int stsfc_wait_seq(struct stsfc *sfc, unsigned int max_time_ms)
{
	u64 now = get_timer(0);

	while (get_timer(now) < max_time_ms) {
		if (stsfc_read_interrupts_status(sfc) &
		    SFC_INT_SEQUENCE_COMPLETION) {
			stsfc_clear_interrupts(sfc,
					SFC_INT_SEQUENCE_COMPLETION);

			return 0;
		}
	}

	printf("SFC sequence timeout\n");
	return -ETIMEDOUT;
}

static inline void stsfc_restart_seq(struct stsfc *sfc,
				     const struct stsfc_seq *seq)
{
	writel(seq->seq_cfg, sfc->base + SFC_SEQUENCE_CONFIG);
}

static inline void stsfc_load_transmit_config_cdm(struct stsfc *sfc,
						  uint32_t reg)
{
	writel(reg, sfc->base + SFC_TRANSMIT_CONFIG_CDM);
}

static inline void stsfc_load_config_cdm(struct stsfc *sfc,
					 uint32_t reg)
{
	writel(reg, sfc->base + SFC_CONFIG_CDM);
}

static inline void stsfc_load_data_cdm(struct stsfc *sfc,
				       uint32_t reg)
{
	writel(reg, sfc->base + SFC_DATA_CDM);
}

static inline uint32_t stsfc_read_data_cdm(struct stsfc *sfc)
{
	return readl(sfc->base + SFC_DATA_CDM);
}

static void stsfc_read_status(struct stsfc *sfc, uint8_t cmd, uint8_t *data);
static void stsfc_write_status(struct stsfc *sfc, uint8_t cmd,
			       uint16_t data, uint8_t bytes, bool wait_busy);

/* Default READ configurations, in order of preference */
static struct seq_rw_config default_read_configs[] = {
	{SFC_FLAG_READ_1_4_4, CMD_READ_QUAD_IO_FAST, 0,
		SFC_PADS_4, SFC_PADS_4, 0x00, 2, 4},
	{SFC_FLAG_READ_1_1_4, CMD_READ_QUAD_OUTPUT_FAST, 0,
		SFC_PADS_1, SFC_PADS_4, 0x00, 0, 8},
	{SFC_FLAG_READ_1_2_2, CMD_READ_DUAL_IO_FAST, 0,
		SFC_PADS_2, SFC_PADS_2, 0x00, 4, 0},
	{SFC_FLAG_READ_1_1_2, CMD_READ_DUAL_OUTPUT_FAST, 0,
		SFC_PADS_1, SFC_PADS_2, 0x00, 0, 8},
	{SFC_FLAG_READ_FAST,  CMD_READ_ARRAY_FAST,  0,
		SFC_PADS_1, SFC_PADS_1, 0x00, 0, 8},
	{SFC_FLAG_READ_WRITE, CMD_READ_ARRAY_SLOW,       0,
		SFC_PADS_1, SFC_PADS_1, 0x00, 0, 0},
	{},
};

/* Default WRITE configurations */
static struct seq_rw_config default_write_configs[] = {
	{SFC_FLAG_WRITE_1_4_4, CMD_QUAD_PAGE_PROGRAM_EXT, 1,
		SFC_PADS_4, SFC_PADS_4, 0x00, 0, 0},
	{SFC_FLAG_WRITE_1_1_4, CMD_QUAD_PAGE_PROGRAM, 1,
		SFC_PADS_1, SFC_PADS_4, 0x00, 0, 0},
	{SFC_FLAG_WRITE_1_2_2, CMD_DUAL_PAGE_PROGRAM_EXT, 1,
		SFC_PADS_2, SFC_PADS_2, 0x00, 0, 0},
	{SFC_FLAG_WRITE_1_1_2, CMD_DUAL_PAGE_PROGRAM, 1,
		SFC_PADS_1, SFC_PADS_2, 0x00, 0, 0},
	{SFC_FLAG_READ_WRITE,  CMD_PAGE_PROGRAM,       1,
		SFC_PADS_1, SFC_PADS_1, 0x00, 0, 0},
	{},
};

/*
 * SoC reset on 'boot-from-spi' systems
 *
 * Certain modes of operation cause the Flash device to enter a particular state
 * for a period of time (e.g. 'Erase Sector', 'Quad Enable', and 'Enter 32-bit
 * Addr' commands).  On boot-from-spi systems, it is important to consider what
 * happens if a warm reset occurs during this period.  The SPIBoot controller
 * assumes that Flash device is in its default reset state, 24-bit address mode,
 * and ready to accept commands.  This can be achieved using some form of
 * on-board logic/controller to force a device POR in response to a SoC-level
 * reset or by making use of the device reset signal if available (limited
 * number of devices only).
 *
 * Failure to take such precautions can cause problems following a warm reset.
 * For some operations (e.g. ERASE), there is little that can be done.  For
 * other modes of operation (e.g. 32-bit addressing), options are often
 * available that can help minimise the window in which a reset could cause a
 * problem.
 *
 */
static bool stsfc_can_handle_soc_reset(struct stsfc *sfc)
{
	/* Reset signal is available on the board and supported by the device */
	if (sfc->reset_signal && (sfc->info->flags & SFC_FLAG_RESET))
		return true;

	/* Board-level logic forces a power-on-reset */
	if (sfc->reset_por)
		return true;

	/* Reset is not properly handled and may result in failure to reboot */
	return false;
}

/* Prepare a ERASE sequence */
static void stsfc_prepare_erasesec_seq(struct stsfc *sfc)
{
	struct stsfc_seq *seq = &sfc->stsfc_seq_erase_sector;
	uint32_t *addr_cfg = &sfc->seq_erase_addr_cfg;
	uint8_t cycles = sfc->info->flags & SFC_FLAG_32BIT_ADDR ?
			 SFC_32BIT_ADDR : SFC_24BIT_ADDR;
	uint8_t i = 0;

	seq->opc[0] = SFC_OPC_NO_OF_CYC(SFC_NB_OPCODE_CYCLES - 1) |
		      SFC_OPC_PADS_1 |
		      SFC_OPC_TRANSMIT_DATA(CMD_WRITE_ENABLE);
	seq->opc[1] = SFC_OPC_NO_OF_CYC(SFC_NB_OPCODE_CYCLES - 1) |
		      SFC_OPC_PADS_1 |
		      SFC_OPC_CS_ASSERT |
		      SFC_OPC_TRANSMIT_DATA(CMD_ERASE_64K);

	seq->seq[i++] = STSFC_INST_WR_OPC_0;
	seq->seq[i++] = STSFC_INST_WR_OPC_1;
	seq->seq[i++] = STSFC_INST_ADDR;
	seq->seq[i++] = STSFC_INST_STOP;

	seq->seq_cfg = SFC_SC_START_SEQ |
		       SFC_SC_WRITE_NOT_READ;

	*addr_cfg = SFC_ADDR_NO_OF_CYC(cycles - 1) |
		    SFC_ADDR_PADS_1;
	if (sfc->info->flags & SFC_FLAG_32BIT_ADDR)
		*addr_cfg |= SFC_ADDR_4_BYTE_MODE;
}

/* Search for preferred configuration based on available flags */
static struct seq_rw_config *stsfc_search_seq_rw_configs(struct stsfc *sfc,
					     struct seq_rw_config cfgs[])
{
	struct seq_rw_config *config;
	int flags = sfc->info->flags;

	for (config = cfgs; config->cmd != 0; config++) {
		if ((config->flags & flags) == config->flags) {
			return config;
		}
	}

	return NULL;
}

/* Prepare a READ/WRITE sequence according to configuration parameters */
static void stsfc_prepare_rw_seq(struct stsfc *sfc, struct seq_rw_config *cfg)
{
	struct stsfc_seq *seq;
	uint32_t *addr_cfg;
	uint8_t cycles;
	uint8_t i = 0;

	if (cfg->write) {
		seq = &sfc->stsfc_seq_write;
		addr_cfg = &sfc->seq_write_addr_cfg;
	} else {
		seq = &sfc->stsfc_seq_read;
		addr_cfg = &sfc->seq_read_addr_cfg;
	}

	/* Add READ/WRITE OPC  */
	seq->opc[i++] = SFC_OPC_NO_OF_CYC(SFC_NB_OPCODE_CYCLES - 1) |
			SFC_OPC_PADS_1 |
			SFC_OPC_CS_ASSERT |
			SFC_OPC_TRANSMIT_DATA(cfg->cmd);

	/* Add WREN OPC for a WRITE sequence */
	if (cfg->write)
		seq->opc[i] = SFC_OPC_NO_OF_CYC(SFC_NB_OPCODE_CYCLES - 1) |
			      SFC_OPC_PADS_1 |
			      SFC_OPC_TRANSMIT_DATA(CMD_WRITE_ENABLE);
	i++;

	/* Add mode data bits (no. of pads taken from addr cfg) */
	if (cfg->mode_cycles)
		seq->opc[i] = SFC_OPC_NO_OF_CYC(cfg->mode_cycles - 1) |
			      SFC_OPC_PADS(cfg->addr_pads) |
			      SFC_OPC_CS_ASSERT |
			      SFC_OPC_TRANSMIT_DATA(cfg->mode_data);
	i++;

	/* Add dummy data bits (no. of pads taken from addr cfg) */
	if (cfg->dummy_cycles)
		seq->opc[i] = SFC_OPC_NO_OF_CYC(cfg->dummy_cycles - 1) |
			      SFC_OPC_PADS(cfg->addr_pads) |
			      SFC_OPC_CS_ASSERT;
	i++;

	/* Address configuration (24 or 32-bit addresses) */
	cycles = sfc->info->flags & SFC_FLAG_32BIT_ADDR ?
				      SFC_32BIT_ADDR : SFC_24BIT_ADDR;
	switch (cfg->addr_pads) {
	case SFC_PADS_1:
		break;
	case SFC_PADS_2:
		cycles /= 2;
		break;
	case SFC_PADS_4:
		cycles /= 4;
		break;
	default:
		BUG();
		break;
	}

	*addr_cfg = SFC_ADDR_NO_OF_CYC(cycles - 1) |
		    SFC_ADDR_PADS(cfg->addr_pads) |
		    SFC_ADDR_CS_ASSERT;
	if (sfc->info->flags & SFC_FLAG_32BIT_ADDR)
		*addr_cfg |= SFC_ADDR_4_BYTE_MODE;

	/* Data/Sequence configuration */
	seq->seq_cfg = SFC_SC_START_SEQ |
		       SFC_SC_DMA_ON |
		       SFC_SC_PADS(cfg->data_pads);
	if (cfg->write)
		seq->seq_cfg |= SFC_SC_WRITE_NOT_READ;

	/* Instruction sequence */
	i = 0;
	if (cfg->write)
		seq->seq[i++] = STSFC_INST_WR_OPC_1;

	seq->seq[i++] = STSFC_INST_WR_OPC_0;

	seq->seq[i++] = STSFC_INST_ADDR;

	if (cfg->mode_cycles)
		seq->seq[i++] = STSFC_INST_WR_OPC_2;

	if (cfg->dummy_cycles)
		seq->seq[i++] = STSFC_INST_WR_OPC_3;

	seq->seq[i++] = STSFC_INST_DATA_BDM;

	seq->seq[i++] = STSFC_INST_STOP;
}

static int stsfc_search_prepare_rw_seq(struct stsfc *sfc,
				       struct seq_rw_config *cfgs)
{
	struct seq_rw_config *config;

	config = stsfc_search_seq_rw_configs(sfc, cfgs);
	if (!config) {
		printf("failed to find suitable config\n");
		return -EINVAL;
	}

	stsfc_prepare_rw_seq(sfc, config);

	return 0;
}

/* Prepare a READ/WRITE/ERASE 'default' sequences */
static int stsfc_prepare_rwe_seqs_default(struct stsfc *sfc)
{
	uint32_t flags = sfc->info->flags;
	int ret;

	/* Configure 'READ' sequence */
	ret = stsfc_search_prepare_rw_seq(sfc, default_read_configs);
	if (ret) {
		printf("failed to prep READ sequence with flags [0x%08x]\n",
		       flags);
		return ret;
	}

	/* Configure 'WRITE' sequence */
	ret = stsfc_search_prepare_rw_seq(sfc, default_write_configs);
	if (ret) {
		printf("failed to prep WRITE sequence with flags [0x%08x]\n",
		       flags);
		return ret;
	}

	/* Configure 'ERASE_SECTOR' sequence */
	stsfc_prepare_erasesec_seq(sfc);

	return 0;
}

/*
 * [N25Qxxx] Configuration
 */
#define N25Q_VCR_DUMMY_CYCLES(x)	(((x) & 0xf) << 4)
#define N25Q_VCR_XIP_DISABLED		BIT(3)
#define N25Q_VCR_WRAP_CONT		0x3

/* N25Q - READ/WRITE/CLEAR NON/VOLATILE STATUS/CONFIG Registers */
#define N25Q_CMD_RFSR			0x70
#define N25Q_CMD_CLFSR			0x50
#define N25Q_CMD_WRVCR			0x81
#define N25Q_CMD_RDLOCK			0xe8
#define N25Q_CMD_WRLOCK			0xe5

/* N25Q Flags Status Register: Error Flags */
#define N25Q_FLAGS_ERR_ERASE		BIT(5)
#define N25Q_FLAGS_ERR_PROG		BIT(4)
#define N25Q_FLAGS_ERR_VPP		BIT(3)
#define N25Q_FLAGS_ERR_PROT		BIT(1)
#define N25Q_FLAGS_ERROR		(N25Q_FLAGS_ERR_ERASE | \
					 N25Q_FLAGS_ERR_PROG | \
					 N25Q_FLAGS_ERR_VPP | \
					 N25Q_FLAGS_ERR_PROT)


#define DEFAULT_NQ25_DUMMY_CYCLE 8
#define UNRELEVANT_STORED_DUMMY_VALUE 0xF
/*
 * N25Q 3-byte Address READ configurations
 *	- 'FAST' variants configured with dummy cycles.
 *
 * Note, the number of dummy cycles used for 'FAST' READ operations is
 * configurable and would normally be tuned according to the READ command and
 * operating frequency. Refer to value stored in volatile configuration
 * register of spi-nor memory.
 */
static struct seq_rw_config n25q_read3_configs[] = {
	{SFC_FLAG_READ_1_4_4, CMD_READ_QUAD_IO_FAST, 0,
		SFC_PADS_4, SFC_PADS_4, 0x00, 0, DEFAULT_NQ25_DUMMY_CYCLE},
	{SFC_FLAG_READ_1_1_4, CMD_READ_QUAD_OUTPUT_FAST, 0,
		SFC_PADS_1, SFC_PADS_4, 0x00, 0, DEFAULT_NQ25_DUMMY_CYCLE},
	{SFC_FLAG_READ_1_2_2, CMD_READ_DUAL_IO_FAST, 0,
		SFC_PADS_2, SFC_PADS_2, 0x00, 0, DEFAULT_NQ25_DUMMY_CYCLE},
	{SFC_FLAG_READ_1_1_2, CMD_READ_DUAL_OUTPUT_FAST, 0,
		SFC_PADS_1, SFC_PADS_2, 0x00, 0, DEFAULT_NQ25_DUMMY_CYCLE},
	{SFC_FLAG_READ_FAST,  CMD_READ_ARRAY_FAST,  0,
		SFC_PADS_1, SFC_PADS_1, 0x00, 0, DEFAULT_NQ25_DUMMY_CYCLE},
	{SFC_FLAG_READ_WRITE, CMD_READ_ARRAY_SLOW,       0,
		SFC_PADS_1, SFC_PADS_1, 0x00, 0, 0},
	{},
};

/*
 * N25Q 4-byte Address READ configurations
 *	- use special 4-byte address READ commands (reduces overheads, and
 *	reduces risk of hitting watchdog reset issues).
 *	- 'FAST' variants configured with dummy cycles.
 *
 * See comment above for dummy cycle value
 *
 */
static struct seq_rw_config n25q_read4_configs[] = {
	{SFC_FLAG_READ_1_4_4, CMD_READ4_QUAD_IO_FAST, 0,
		SFC_PADS_4, SFC_PADS_4, 0x00, 0, DEFAULT_NQ25_DUMMY_CYCLE},
	{SFC_FLAG_READ_1_1_4, CMD_READ4_QUAD, 0,
		SFC_PADS_1, SFC_PADS_4, 0x00, 0, DEFAULT_NQ25_DUMMY_CYCLE},
	{SFC_FLAG_READ_1_2_2, CMD_READ4_DUAL_IO_FAST, 0,
		SFC_PADS_2, SFC_PADS_2, 0x00, 0, DEFAULT_NQ25_DUMMY_CYCLE},
	{SFC_FLAG_READ_1_1_2, CMD_READ4_DUAL, 0,
		SFC_PADS_1, SFC_PADS_2, 0x00, 0, DEFAULT_NQ25_DUMMY_CYCLE},
	{SFC_FLAG_READ_FAST,  CMD_READ4_FAST,  0,
		SFC_PADS_1, SFC_PADS_1, 0x00, 0, DEFAULT_NQ25_DUMMY_CYCLE},
	{SFC_FLAG_READ_WRITE, CMD_READ4,       0,
		SFC_PADS_1, SFC_PADS_1, 0x00, 0, 0},
	{},
};

static void stsfc_n25q_enter_32bit_addr(struct stsfc *sfc, bool enter)
{
	uint32_t cmd = enter ? CMD_EN4B : CMD_EX4B;
	uint32_t tcfg_reg;

	tcfg_reg = SFC_TCFG_CDM_NO_OF_CYC(SFC_NB_OPCODE_CYCLES - 1) |
		   SFC_TCFG_CDM_PADS_1 |
		   SFC_TCFG_CDM_TRANSMIT_DATA(CMD_WRITE_ENABLE);

	stsfc_load_transmit_config_cdm(sfc, tcfg_reg);

	tcfg_reg = SFC_TCFG_CDM_NO_OF_CYC(SFC_NB_OPCODE_CYCLES - 1) |
		   SFC_TCFG_CDM_PADS_1 |
		   SFC_TCFG_CDM_TRANSMIT_DATA(cmd);

	stsfc_load_transmit_config_cdm(sfc, tcfg_reg);
}

static void stsfc_n25q_clear_flag(struct stsfc *sfc)
{
	uint32_t tcfg_reg;

	tcfg_reg = SFC_TCFG_CDM_NO_OF_CYC(SFC_NB_OPCODE_CYCLES - 1) |
		   SFC_TCFG_CDM_PADS_1 |
		   SFC_TCFG_CDM_TRANSMIT_DATA(N25Q_CMD_CLFSR);

	stsfc_load_transmit_config_cdm(sfc, tcfg_reg);
}

static int stsfc_n25q_config(struct stsfc *sfc)
{
	struct stm_flash_info *info = sfc->info;
	struct seq_rw_config *read_cfg;
	uint32_t flags = info->flags;
	uint8_t sta, nq25_dummy_cycle;
	int ret, i;

	/*
	 * Read dummy cycle value set by TPack and update 'command sequence'
	 * accordingly. Mandatory otherwise read-fast command will fail (data
	 * issued by flash will not be synchronized with CPU read)
	 */
	stsfc_read_status(sfc, CMD_READ_VOLATR, &sta);
	nq25_dummy_cycle = sta >> 4; /* sta[7:4] = dummy cycle */

	/* In case TPack has set dummy according to SPI-NOR link frequency */
	if (nq25_dummy_cycle != UNRELEVANT_STORED_DUMMY_VALUE) {
		for (i = 0; n25q_read4_configs[i].cmd != 0; i++) {
			n25q_read4_configs[i].dummy_cycles = nq25_dummy_cycle;
		}

		for (i = 0; n25q_read3_configs[i].cmd != 0; i++) {
			n25q_read3_configs[i].dummy_cycles = nq25_dummy_cycle;
		}
	}

	/* Configure 'READ' sequence */
	if (flags & SFC_FLAG_32BIT_ADDR)
		read_cfg = n25q_read4_configs;
	else
		read_cfg = n25q_read3_configs;

	ret = stsfc_search_prepare_rw_seq(sfc, read_cfg);
	if (ret) {
		printf("failed to prepare READ sequence with flags [0x%08x]\n",
		       flags);
		return ret;
	}

	/* Configure 'WRITE' sequence (default configs) */
	ret = stsfc_search_prepare_rw_seq(sfc, default_write_configs);
	if (ret) {
		printf("preparing WRITE sequence using flags [0x%08x] failed\n",
		       flags);
		return ret;
	}

	/* Configure 'ERASE_SECTOR' sequence */
	stsfc_prepare_erasesec_seq(sfc);

	/* Configure block locking scheme */
	if (flags & SFC_FLAG_BLK_LOCKING) {
		sfc->configuration |= SFC_CFG_RD_WR_LOCK_REG;
	}

	/* Force unlock by setting status register to 0x0. */
	printf("%s: unlocking NOR with WRSR\n", __func__);
	stsfc_write_status(sfc, CMD_WRITE_STATUS, 0x0, 1, true);

	/* Check/Clear Error Flags */
	sfc->configuration |= SFC_CFG_N25Q_CHECK_ERROR_FLAGS;
	stsfc_read_status(sfc, N25Q_CMD_RFSR, &sta);
	if (sta & N25Q_FLAGS_ERROR)
		stsfc_n25q_clear_flag(sfc);

	/* Configure 32-bit address support */
	if (flags & SFC_FLAG_32BIT_ADDR) {
		sfc->enter_32bit_addr = stsfc_n25q_enter_32bit_addr;

		if (stsfc_can_handle_soc_reset(sfc) || !sfc->booted_from_spi) {
			/*
			 * If we can handle SoC resets, we enable 32-bit
			 * address mode pervasively
			 */
			sfc->enter_32bit_addr(sfc, true);
		} else {
			/*
			 * If not, enable/disable for WRITE and ERASE
			 * operations (READ uses special commands)
			 */
			sfc->configuration |= SFC_CFG_WRITE_TOGGLE_32BIT_ADDR |
					      SFC_CFG_ERASE_TOGGLE_32BIT_ADDR |
					      SFC_CFG_LOCK_TOGGLE_32BIT_ADDR;
		}
	}

	return 0;
}

static int stsfc_n25q_resume(struct stsfc *sfc)
{
	struct stm_flash_info *info = sfc->info;
	uint32_t flags = info->flags;
	uint8_t sta;

	/* Check/Clear Error Flags */
	stsfc_read_status(sfc, N25Q_CMD_RFSR, &sta);
	if (sta & N25Q_FLAGS_ERROR)
		stsfc_n25q_clear_flag(sfc);

	/* Configure 32-bit address support */
	if ((flags & SFC_FLAG_32BIT_ADDR) &&
	    ((stsfc_can_handle_soc_reset(sfc) || !sfc->booted_from_spi))) {
		/*
		 * If we can handle SoC resets, we enable 32-bit
		 * address mode pervasively
		 */
		sfc->enter_32bit_addr(sfc, true);
	}

	return 0;
}

/*
 * [MX25xxx] Configuration
 */
#define MX25_CMD_WRITE_1_4_4		0x38
#define MX25_CMD_RDCR			0x15
#define MX25_CMD_RDSCUR			0x2b
#define MX25_CMD_RDSFDP			0x5a
#define MX25_CMD_SBLK			0x36
#define MX25_CMD_SBULK			0x39
#define MX25_CMD_RDBLOCK		0x3c
#define MX25_CMD_RDDPB			0xe0
#define MX25_CMD_WRDPB			0xe1

#define MX25_SR_QE			BIT(6)
#define MX25_SCUR_WPSEL			BIT(7)
#define MX25_CR_TB			BIT(3)

#define MX25_RDSFDP_DUMMY_CYCLES	8
#define MX25_RDSFDP_DATA_CYCLES		32UL

#define MX25L32_DEVICE_ID		0x16
#define MX25L128_DEVICE_ID		0x18

#define MX25_LOCK_OPCODE_ADDR		0x68
#define MX25_LOCK_OPCODE_MASK		0x3fc


/* Mx25 WRITE configurations, in order of preference */
static struct seq_rw_config mx25_write_configs[] = {
	{SFC_FLAG_WRITE_1_4_4, MX25_CMD_WRITE_1_4_4, 1,
		SFC_PADS_4, SFC_PADS_4, 0x00, 0, 0},
	{SFC_FLAG_READ_WRITE,  CMD_PAGE_PROGRAM,      1,
		SFC_PADS_1, SFC_PADS_1, 0x00, 0, 0},
	{},
};

static void stsfc_mx25_enter_32bit_addr(struct stsfc *sfc, bool enter)
{
	uint32_t cmd = enter ? CMD_EN4B : CMD_EX4B;
	uint32_t tcfg_reg;

	tcfg_reg = SFC_TCFG_CDM_NO_OF_CYC(SFC_NB_OPCODE_CYCLES - 1) |
		   SFC_TCFG_CDM_PADS_1 |
		   SFC_TCFG_CDM_TRANSMIT_DATA(cmd);

	stsfc_load_transmit_config_cdm(sfc, tcfg_reg);
}

/*
 * Read SFDP mode at @0x68, 8 dummy cycles required
 * bits 2-9 => opcode used
 */
static int stsfc_mx25_read_lock_opcode(struct stsfc *sfc,
				       uint8_t *lock_opcode)
{
	uint32_t cfg_reg, tcfg_reg;
	uint32_t data_reg;

	/* RDSFDP command sent */
	tcfg_reg = SFC_TCFG_CDM_NO_OF_CYC(SFC_NB_OPCODE_CYCLES - 1) |
		   SFC_TCFG_CDM_PADS_1 |
		   SFC_TCFG_CDM_CS_ASSERT |
		   SFC_TCFG_CDM_TRANSMIT_DATA(MX25_CMD_RDSFDP);
	stsfc_load_transmit_config_cdm(sfc, tcfg_reg);

	/* Address sent */
	cfg_reg = SFC_CFG_CDM_NO_OF_CYC(SFC_24BIT_ADDR - 1) |
		  SFC_CFG_CDM_PADS_1 |
		  SFC_CFG_CDM_CS_ASSERT;
	stsfc_load_config_cdm(sfc, cfg_reg);

	stsfc_load_data_cdm(sfc, SFC_3_BYTES_ADDR(MX25_LOCK_OPCODE_ADDR));

	/* Dummy cycles sent */
	tcfg_reg = SFC_TCFG_CDM_NO_OF_CYC(MX25_RDSFDP_DUMMY_CYCLES - 1) |
		   SFC_TCFG_CDM_PADS_1 |
		   SFC_TCFG_CDM_CS_ASSERT;
	stsfc_load_transmit_config_cdm(sfc, tcfg_reg);

	cfg_reg = SFC_CFG_CDM_NO_OF_CYC(MX25_RDSFDP_DATA_CYCLES - 1) |
		  SFC_CFG_CDM_PADS_1;
	stsfc_load_config_cdm(sfc, cfg_reg);

	/* Data read */
	data_reg = stsfc_read_data_cdm(sfc);

	*lock_opcode = (data_reg & MX25_LOCK_OPCODE_MASK) >> 2;

	return 0;
}

static int stsfc_mx25_config(struct stsfc *sfc)
{
	struct stm_flash_info *info = sfc->info;
	uint32_t *flags = &info->flags;
	uint32_t data_pads;
	uint8_t sta, lock_opcode;
	int ret;
	bool soc_reset;

	/* Configure 'READ' sequence */
	ret = stsfc_search_prepare_rw_seq(sfc, default_read_configs);
	if (ret) {
		printf("failed to prep READ sequence with flags [0x%08x]\n",
		       *flags);
		return ret;
	}

	/* Configure 'WRITE' sequence */
	ret = stsfc_search_prepare_rw_seq(sfc, mx25_write_configs);
	if (ret) {
		printf("failed to prep WRITE sequence with flags [0x%08x]\n",
		       *flags);
		return ret;
	}

	/* Configure 'ERASE_SECTOR' sequence */
	stsfc_prepare_erasesec_seq(sfc);

	/* Configure 32-bit Address Support */
	if (*flags & SFC_FLAG_32BIT_ADDR) {
		/* Configure 'enter_32bitaddr' FSM sequence */
		sfc->enter_32bit_addr = stsfc_mx25_enter_32bit_addr;

		soc_reset = stsfc_can_handle_soc_reset(sfc);
		if (soc_reset || !sfc->booted_from_spi) {
			/*
			 * If we can handle SoC resets, we enable 32-bit address
			 * mode pervasively
			 */
			sfc->enter_32bit_addr(sfc, true);
		} else {
			/*
			 * Else, enable/disable 32-bit addressing before/after
			 * each operation
			 */
			sfc->configuration |= SFC_CFG_READ_TOGGLE_32BIT_ADDR |
					      SFC_CFG_WRITE_TOGGLE_32BIT_ADDR |
					      SFC_CFG_ERASE_TOGGLE_32BIT_ADDR;
		}
	}

	/*
	 * Check WPSEL
	 * WPSEL = 0 => Block Lock protection mode
	 * WPSEL = 1 => Individual block lock protection mode
	 */
	stsfc_read_status(sfc, MX25_CMD_RDSCUR, &sta);
	if (sta & MX25_SCUR_WPSEL) {
		/* Individual block lock protection mode detected */

		/* Read opcode used to lock */
		lock_opcode = 0;
		stsfc_mx25_read_lock_opcode(sfc, &lock_opcode);
		if (lock_opcode == MX25_CMD_SBLK) {
			*flags |= SFC_FLAG_BLK_LOCKING;
		} else if (lock_opcode == MX25_CMD_WRDPB) {
			*flags |= SFC_FLAG_BLK_LOCKING;

			sfc->configuration |= SFC_CFG_RD_WR_LOCK_REG;
		} else
			/* Lock opcode is not supported */
			printf("Lock/unlock command %02x not supported.\n",
			       lock_opcode);
	} else {
		/* BP lock lock protection mode detected */
		*flags |= SFC_FLAG_BPX_LOCKING;
	}

	/* Check status of 'QE' bit, update if required. */
	stsfc_read_status(sfc, CMD_READ_STATUS, &sta);
	data_pads = (sfc->stsfc_seq_read.seq_cfg >> 25) & 0x3;
	if (data_pads == SFC_PADS_4) {
		if (!(sta & MX25_SR_QE)) {
			/* Set 'QE' */
			sta |= MX25_SR_QE;

			stsfc_write_status(sfc, CMD_WRITE_STATUS, sta, 1, true);
		}

		sfc->configuration |= SFC_CFG_MX25_TOGGLE_QE_BIT;
	} else {
		if (sta & MX25_SR_QE) {
			/* Clear 'QE' */
			sta &= ~MX25_SR_QE;

			stsfc_write_status(sfc, CMD_WRITE_STATUS, sta, 1, true);
		}
	}

	return 0;
}

static int stsfc_mx25_resume(struct stsfc *sfc)
{
	struct stm_flash_info *info = sfc->info;
	uint32_t flags = info->flags;
	bool soc_reset;

	/* Configure 32-bit Address Support */
	if (flags & SFC_FLAG_32BIT_ADDR) {
		soc_reset = stsfc_can_handle_soc_reset(sfc);
		if (soc_reset || !sfc->booted_from_spi) {
			/*
			 * If we can handle SoC resets, we enable 32-bit address
			 * mode pervasively
			 */
			sfc->enter_32bit_addr(sfc, true);
		}
	}

	return 0;
}

/*
 * [S25FLxxx] Configuration
 */
#define S25FL_CMD_WRITE4_1_1_4		0x34
#define S25FL_CMD_SE4			0xdc
#define S25FL_CMD_CLSR			0x30
#define S25FL_CMD_WRITE4		0x12
#define S25FL_CMD_DYBWR			0xe1
#define S25FL_CMD_DYBRD			0xe0

#define S25FL_SR1_E_ERR			BIT(5)
#define S25FL_SR1_P_ERR			BIT(6)
#define S25FL_SR2_QE			BIT(1)
#define S25FL_CR1_TBPROT		BIT(5)

/*
 * S25FLxxxS devices provide three ways of supporting 32-bit addressing: Bank
 * Register, Extended Address Modes, and a 32-bit address command set.  The
 * 32-bit address command set is used here, since it avoids any problems with
 * entering a state that is incompatible with the SPIBoot Controller.
 */
static struct seq_rw_config stsfc_s25fl_read4_configs[] = {
	{SFC_FLAG_READ_1_4_4,  CMD_READ4_QUAD_IO_FAST,  0,
		SFC_PADS_4, SFC_PADS_4, 0x00, 2, 4},
	{SFC_FLAG_READ_1_1_4,  CMD_READ4_QUAD,  0,
		SFC_PADS_1, SFC_PADS_4, 0x00, 0, 8},
	{SFC_FLAG_READ_1_2_2,  CMD_READ4_DUAL_IO_FAST,  0,
		SFC_PADS_2, SFC_PADS_2, 0x00, 4, 0},
	{SFC_FLAG_READ_1_1_2,  CMD_READ4_DUAL,  0,
		SFC_PADS_1, SFC_PADS_2, 0x00, 0, 8},
	{SFC_FLAG_READ_FAST,   CMD_READ4_FAST,   0,
		SFC_PADS_1, SFC_PADS_1, 0x00, 0, 8},
	{SFC_FLAG_READ_WRITE,  CMD_READ4,	0,
		SFC_PADS_1, SFC_PADS_1, 0x00, 0, 0},
	{},
};

static struct seq_rw_config stsfc_s25fl_write4_configs[] = {
	{SFC_FLAG_WRITE_1_1_4, S25FL_CMD_WRITE4_1_1_4, 1,
		SFC_PADS_1, SFC_PADS_4, 0x00, 0, 0},
	{SFC_FLAG_READ_WRITE,  S25FL_CMD_WRITE4,       1,
		SFC_PADS_1, SFC_PADS_1, 0x00, 0, 0},
	{},
};

static void stsfc_s25fl_prepare_erasesec_seq_32(struct stsfc *sfc)
{
	struct stsfc_seq *seq = &sfc->stsfc_seq_erase_sector;
	uint32_t *addr_cfg = &sfc->seq_erase_addr_cfg;

	stsfc_prepare_erasesec_seq(sfc);

	seq->opc[1] = SFC_OPC_NO_OF_CYC(SFC_NB_OPCODE_CYCLES - 1) |
		      SFC_OPC_PADS_1 |
		      SFC_OPC_CS_ASSERT |
		      SFC_OPC_TRANSMIT_DATA(S25FL_CMD_SE4);

	*addr_cfg = SFC_ADDR_NO_OF_CYC(SFC_32BIT_ADDR - 1) |
		    SFC_ADDR_PADS_1 |
		    SFC_ADDR_4_BYTE_MODE;
}

static void stsfc_s25fl_clear_flag(struct stsfc *sfc)
{
	uint32_t tcfg_reg;

	tcfg_reg = SFC_TCFG_CDM_NO_OF_CYC(SFC_NB_OPCODE_CYCLES - 1) |
		   SFC_TCFG_CDM_PADS_1 |
		   SFC_TCFG_CDM_TRANSMIT_DATA(S25FL_CMD_CLSR);

	stsfc_load_transmit_config_cdm(sfc, tcfg_reg);

	tcfg_reg = SFC_TCFG_CDM_NO_OF_CYC(SFC_NB_OPCODE_CYCLES - 1) |
		   SFC_TCFG_CDM_PADS_1 |
		   SFC_TCFG_CDM_TRANSMIT_DATA(CMD_WRITE_DISABLE);

	stsfc_load_transmit_config_cdm(sfc, tcfg_reg);
}

static int stsfc_s25fl_config(struct stsfc *sfc)
{
	struct stm_flash_info *info = sfc->info;
	uint32_t flags = info->flags;
	uint32_t data_pads;
	uint16_t sta_wr;
	uint8_t sr1, cr1;
	bool update_sr = false;
	int ret;

	/*
	 * WRSR must always cover CONFIG register to prevent loss of QUAD bit
	 * state
	 */
	sfc->configuration |= SFC_CFG_WRSR_FORCE_16BITS;

	/*
	 * S25FLxxx devices support Program and Error error flags
	 * Configure driver to check flags and clear if necessary.
	 */
	sfc->configuration |= SFC_CFG_S25FL_CHECK_ERROR_FLAGS;

	if (flags & SFC_FLAG_32BIT_ADDR) {
		/*
		 * Prepare Read/Write/Erase sequences according to S25FLxxx
		 * 32-bit address command set
		 */
		ret = stsfc_search_prepare_rw_seq(sfc,
						  stsfc_s25fl_read4_configs);
		if (ret)
			return ret;

		ret = stsfc_search_prepare_rw_seq(sfc,
						  stsfc_s25fl_write4_configs);
		if (ret)
			return ret;

		stsfc_s25fl_prepare_erasesec_seq_32(sfc);
	} else {
		/* Use default configurations for 24-bit addressing */
		ret = stsfc_prepare_rwe_seqs_default(sfc);
		if (ret)
			return ret;
	}

	/* Configure block locking support */
	if (flags & SFC_FLAG_BLK_LOCKING) {
		sfc->configuration |= SFC_CFG_RD_WR_LOCK_REG;
	}

	/* Check status of 'QE' bit, update if required. */
	stsfc_read_status(sfc, CMD_READ_STATUS1, &cr1);
	data_pads = (sfc->stsfc_seq_read.seq_cfg >> 25) & 0x3;
	if (data_pads == SFC_PADS_4) {
		if (!(cr1 & S25FL_SR2_QE)) {
			/* Set 'QE' */
			cr1 |= S25FL_SR2_QE;

			update_sr = true;
		}
	} else {
		if (cr1 & S25FL_SR2_QE) {
			/* Clear 'QE' */
			cr1 &= ~S25FL_SR2_QE;

			update_sr = true;
		}
	}

	if (update_sr) {
		stsfc_read_status(sfc, CMD_READ_STATUS, &sr1);
		sta_wr = ((uint16_t)cr1  << 8) | sr1;
		stsfc_write_status(sfc, CMD_WRITE_STATUS, sta_wr, 2, true);
	}

	return 0;
}

/*
 * [W25Qxxx] Configuration
 */
#define W25Q_SR1_TB			BIT(5)
#define W25Q_SR1_SEC			BIT(6)
#define W25Q_SR2_QE			BIT(1)
#define W25Q_SR2_CMP			BIT(6)

#define W25Q16_DEVICE_ID		0x15
#define W25Q80_DEVICE_ID		0x14

static int stsfc_w25q_config(struct stsfc *sfc)
{
	struct stm_flash_info *info = sfc->info;
	uint32_t *flags = &info->flags;
	uint32_t data_pads;
	uint16_t sr_wr;
	uint8_t sr1, sr2;
	bool update_sr = false;
	int ret;

	/*
	 * WRSR must always cover STATUS register 2 to prevent loss of QUAD bit
	 * and CMP bit state
	 */
	sfc->configuration |= SFC_CFG_WRSR_FORCE_16BITS;

	/* Use default READ/WRITE sequences */
	ret = stsfc_prepare_rwe_seqs_default(sfc);
	if (ret)
		return ret;

	/* Get 'TB' and 'SEC' bits */
	stsfc_read_status(sfc, CMD_READ_STATUS, &sr1);

	/* Get 'CMP' bit */
	stsfc_read_status(sfc, CMD_READ_STATUS1, &sr2);

	if (*flags & SFC_FLAG_BPX_LOCKING) {
		if ((sr1 & W25Q_SR1_SEC) || (sr2 & W25Q_SR2_CMP)) {
			/* This scheme is not supported */
			printf("Lock/unlock scheme not supported. Only schemes whith CMP=0 and SEC=0 is supported.\n");

			/* disable BPx locking support */
			*flags &= ~SFC_FLAG_BPX_LOCKING;
		}
	}

	/* Check status of 'QE' bit, update if required. */
	data_pads = (sfc->stsfc_seq_read.seq_cfg >> 25) & 0x3;
	if (data_pads == SFC_PADS_4) {
		if (!(sr2 & W25Q_SR2_QE)) {
			/* Set 'QE' */
			sr2 |= W25Q_SR2_QE;
			update_sr = true;
		}
	} else {
		if (sr2 & W25Q_SR2_QE) {
			/* Clear 'QE' */
			sr2 &= ~W25Q_SR2_QE;
			update_sr = true;
		}
	}

	if (update_sr) {
		/* Write status register */
		sr_wr = ((uint16_t)sr2 << 8) | sr1;
		stsfc_write_status(sfc, CMD_WRITE_STATUS, sr_wr, 2, true);
	}

	return 0;
}

/*
 *
 * SFC command sequence
 *
 */
static int stsfc_wait_busy(struct stsfc *sfc, unsigned int max_time_ms)
{
	uint32_t tcfg_reg;
	uint32_t cfg_reg;
	uint32_t status;
	u32 now = 0;

	tcfg_reg = SFC_TCFG_CDM_NO_OF_CYC(SFC_NB_OPCODE_CYCLES - 1) |
		   SFC_TCFG_CDM_PADS_1 |
		   SFC_TCFG_CDM_CS_ASSERT |
		   SFC_TCFG_CDM_TRANSMIT_DATA(CMD_READ_STATUS);
	stsfc_load_transmit_config_cdm(sfc, tcfg_reg);

	cfg_reg = SFC_CFG_CDM_NO_OF_CYC(SFC_NB_OPCODE_CYCLES - 1) |
		  SFC_CFG_CDM_PADS_1;
	stsfc_load_config_cdm(sfc, cfg_reg);

	/* Repeat until busy bit is deasserted or timeout */
	now = get_timer(0);
	while (get_timer(now) < max_time_ms) {
		/* Read flash status data */
		status = stsfc_read_data_cdm(sfc) & 0xff;

		if (!(status & STATUS_WIP))
			return 0;

		/* S25FL: Check/Clear Error Flags */
		if ((sfc->configuration & SFC_CFG_S25FL_CHECK_ERROR_FLAGS) &&
		    ((status & S25FL_SR1_P_ERR) ||
		     (status & S25FL_SR1_E_ERR))) {
				stsfc_s25fl_clear_flag(sfc);
				return -EPROTO;
		}

		/* Restart */
		stsfc_load_transmit_config_cdm(sfc, tcfg_reg);
	}

	printf("Timeout on wait_busy\n");

	return -ETIMEDOUT;
}

static void stsfc_read_status(struct stsfc *sfc, uint8_t cmd, uint8_t *data)
{
	uint32_t tcfg_reg;
	uint32_t cfg_reg;

	/* Command sent */
	tcfg_reg = SFC_TCFG_CDM_NO_OF_CYC(SFC_NB_OPCODE_CYCLES - 1) |
		   SFC_TCFG_CDM_PADS_1 |
		   SFC_TCFG_CDM_CS_ASSERT |
		   SFC_TCFG_CDM_TRANSMIT_DATA(cmd);
	stsfc_load_transmit_config_cdm(sfc, tcfg_reg);

	/* Read data */
	cfg_reg = SFC_CFG_CDM_NO_OF_CYC(SFC_NB_OPCODE_CYCLES - 1) |
		  SFC_CFG_CDM_PADS_1;
	stsfc_load_config_cdm(sfc, cfg_reg);

	*data = stsfc_read_data_cdm(sfc) & 0xff;
}

static void stsfc_write_status(struct stsfc *sfc, uint8_t cmd,
			       uint16_t data, uint8_t bytes, bool wait_busy)
{
	uint32_t tcfg_reg;
	uint16_t data_to_send = data;
	uint8_t cycles;

	BUG_ON(bytes != 1 && bytes != 2);

	if (cmd == CMD_WRITE_STATUS &&
	    bytes == 1 &&
	    (sfc->configuration & SFC_CFG_WRSR_FORCE_16BITS)) {
		uint8_t cr;

		stsfc_read_status(sfc, CMD_READ_STATUS1, &cr);

		data_to_send = (data & 0xff) | ((uint16_t)cr << 8);
		bytes = 2;
	}

	cycles = bytes * 8;

	/* Commands sent */
	tcfg_reg = SFC_TCFG_CDM_NO_OF_CYC(SFC_NB_OPCODE_CYCLES - 1) |
		   SFC_TCFG_CDM_PADS_1 |
		   SFC_TCFG_CDM_TRANSMIT_DATA(CMD_WRITE_ENABLE);
	stsfc_load_transmit_config_cdm(sfc, tcfg_reg);

	tcfg_reg = SFC_TCFG_CDM_NO_OF_CYC(SFC_NB_OPCODE_CYCLES - 1) |
		   SFC_TCFG_CDM_PADS_1 |
		   SFC_TCFG_CDM_CS_ASSERT |
		   SFC_TCFG_CDM_TRANSMIT_DATA(cmd);
	stsfc_load_transmit_config_cdm(sfc, tcfg_reg);

	/* Data sent */
	tcfg_reg = SFC_TCFG_CDM_NO_OF_CYC(cycles - 1) |
		   SFC_TCFG_CDM_PADS_1 |
		   SFC_TCFG_CDM_TRANSMIT_DATA(data_to_send);
	stsfc_load_transmit_config_cdm(sfc, tcfg_reg);

	/*
	 * No timeout error returned, depending of the device
	 * we have to wait that the command is ended
	 */
	if (wait_busy)
		stsfc_wait_busy(sfc, SFC_FLASH_MAX_STA_WRITE_MS);
}

static int stsfc_read(struct stsfc *sfc, uint8_t *buf, uint32_t size,
		      uint32_t offset)
{
	struct stsfc_seq *seq = &sfc->stsfc_seq_read;
	uint8_t *dma_buf = (uint8_t *)buf;
	uint32_t size_to_read = ALIGN(size, SFC_MIN_DATA_TRANSFER);
	int ret;

	/* Initialise address of dma_buff  */
	dma_buf = sfc->dma_buf;

	/* Set physical address to write in */
	seq->dma_addr = (uintptr_t)dma_buf;

	/* Set data size to read */
	seq->data_size = SFC_DIS_SIZE(size_to_read);

	/* Set sector address to read in */
	seq->addr = offset;

	/* Enter 32-bit address mode, if required */
	if (sfc->configuration & SFC_CFG_READ_TOGGLE_32BIT_ADDR)
		sfc->enter_32bit_addr(sfc, true);

	/* Start sequence */
	ret = stsfc_load_seq(sfc, seq);
	if (ret < 0)
		return ret;

	/* Wait for sequence completion */
	ret = stsfc_wait_seq(sfc, SFC_MAX_WAIT_SEQ_MS);
	if (ret < 0)
		return ret;

	/* Exit 32-bit address mode, if required */
	if (sfc->configuration & SFC_CFG_READ_TOGGLE_32BIT_ADDR)
		sfc->enter_32bit_addr(sfc, false);

	memcpy(buf, dma_buf, size);

	return 0;
}

static int stsfc_write(struct stsfc *sfc, uint8_t *buf,
		       uint32_t size, uint32_t offset)
{
	struct stsfc_seq *seq = &sfc->stsfc_seq_write;
	uint32_t size_to_write = ALIGN(size, SFC_MIN_DATA_TRANSFER);
	uint8_t *dma_buf;
	uint8_t sta;
	uint32_t i;
	int ret;

	debug("STSFC: to 0x%08x, len %d\n", offset, size);

	/* copy client buffer into dma capable buffer */
	dma_buf = sfc->dma_buf;
	memcpy(dma_buf, buf, size);


	/* We have to write in a multiple of 4 bytes */
	if (size != size_to_write) {
		for (i = size; i < size_to_write; i++)
			dma_buf[i] = 0xff;
	}

	/* Set physical address to read in */
	seq->dma_addr = (uintptr_t)dma_buf;

	/* Set data size to write */
	seq->data_size = SFC_DIS_SIZE(size_to_write);

	/* Set sector address to write in */
	seq->addr = offset;

	/* Enter 32-bit address mode, if required */
	if (sfc->configuration & SFC_CFG_WRITE_TOGGLE_32BIT_ADDR) {
		sfc->enter_32bit_addr(sfc, true);
	}

	/* Start sequence */
	ret = stsfc_load_seq(sfc, seq);
	if (ret < 0) {
		return ret;
	}

	/* Wait for sequence completion */
	ret = stsfc_wait_seq(sfc, SFC_MAX_WAIT_SEQ_MS);
	if (ret < 0) {
		return ret;
	}

	/* Wait for device idle state */
	ret = stsfc_wait_busy(sfc, SFC_FLASH_MAX_PAGE_WRITE_MS);

	/* Exit 32-bit address mode, if required */
	if (sfc->configuration & SFC_CFG_WRITE_TOGGLE_32BIT_ADDR)
		sfc->enter_32bit_addr(sfc, false);

	/* N25Q: Check/Clear Error Flags */
	if (sfc->configuration & SFC_CFG_N25Q_CHECK_ERROR_FLAGS) {
		stsfc_read_status(sfc, N25Q_CMD_RFSR, &sta);
		if (sta & N25Q_FLAGS_ERROR) {
			stsfc_n25q_clear_flag(sfc);
			ret = -EPROTO;
		}
	}

	return ret;
}

static int stsfc_erase_sector(struct stsfc *sfc, uint32_t offset)
{
	struct stsfc_seq *seq = &sfc->stsfc_seq_erase_sector;
	uint8_t sta;
	int ret;

	/* Set sector address to erase */
	seq->addr = offset;

	/* Enter 32-bit address mode, if required */
	if (sfc->configuration & SFC_CFG_ERASE_TOGGLE_32BIT_ADDR)
		sfc->enter_32bit_addr(sfc, true);

	/* Start sequence */
	ret = stsfc_load_seq(sfc, seq);
	if (ret < 0) {
		return ret;
	}

	/* Wait for sequence completion */
	ret = stsfc_wait_seq(sfc, SFC_MAX_WAIT_SEQ_MS);
	if (ret < 0) {
		return ret;
	}
	/* Wait for device idle state */
	ret = stsfc_wait_busy(sfc, SFC_FLASH_MAX_SEC_ERASE_MS);

	/* Exit 32-bit address mode, if required */
	if (sfc->configuration & SFC_CFG_ERASE_TOGGLE_32BIT_ADDR)
		sfc->enter_32bit_addr(sfc, false);

	/* N25Q: Check/Clear Error Flags */
	if (sfc->configuration & SFC_CFG_N25Q_CHECK_ERROR_FLAGS) {
		stsfc_read_status(sfc, N25Q_CMD_RFSR, &sta);
		if (sta & N25Q_FLAGS_ERROR) {
			stsfc_n25q_clear_flag(sfc);
			ret = -EPROTO;
		}
	}

	return ret;
}

static int __maybe_unused stsfc_erase_chip(struct stsfc *sfc)
{
	uint32_t tcfg_reg;

	tcfg_reg = SFC_TCFG_CDM_NO_OF_CYC(SFC_NB_OPCODE_CYCLES - 1) |
		   SFC_TCFG_CDM_PADS_1 |
		   SFC_TCFG_CDM_TRANSMIT_DATA(CMD_WRITE_ENABLE);
	stsfc_load_transmit_config_cdm(sfc, tcfg_reg);

	tcfg_reg = SFC_TCFG_CDM_NO_OF_CYC(SFC_NB_OPCODE_CYCLES - 1) |
		   SFC_TCFG_CDM_PADS_1 |
		   SFC_TCFG_CDM_TRANSMIT_DATA(CMD_ERASE_CHIP);
	stsfc_load_transmit_config_cdm(sfc, tcfg_reg);

	return stsfc_wait_busy(sfc, SFC_FLASH_MAX_CHIP_ERASE_MS);
}

static int stsfc_read_jedec(struct stsfc *sfc, uint8_t *jedec)
{
	int ret;
	struct stsfc_seq seq = {
		.data_size = SFC_DIS_SIZE(ALIGN(SFC_MAX_READID_LEN,
						SFC_MIN_DATA_TRANSFER)),
		.opc[0] = (SFC_OPC_NO_OF_CYC(SFC_NB_OPCODE_CYCLES - 1) |
			   SFC_OPC_PADS_1 |
			   SFC_OPC_CS_ASSERT |
			   SFC_OPC_TRANSMIT_DATA(CMD_READ_ID)),
		.seq[0] = STSFC_INST_WR_OPC_0,
		.seq[1] = STSFC_INST_DATA_BDM,
		.seq[2] = STSFC_INST_STOP,
		.seq_cfg = (SFC_SC_START_SEQ |
			    SFC_SC_DMA_ON |
			    SFC_SC_PADS_1),
		.dma_addr = (uintptr_t)sfc->dma_buf,
	};

	memset((void *)(uintptr_t)sfc->dma_buf, 0, 10);

	/* Start sequence */
	ret = stsfc_load_seq(sfc, &seq);
	if (ret < 0)
		return ret;

	/* Wait for sequence completion */
	ret = stsfc_wait_seq(sfc, SFC_MAX_WAIT_SEQ_MS);
	if (ret < 0)
		return ret;

	memcpy(jedec, sfc->dma_buf, SFC_MAX_READID_LEN);

	return 0;
}

static struct stm_flash_info *stsfc_jedec_probe(struct stsfc *sfc)
{
	struct stm_flash_info *info;
	struct stm_flash_info *match = NULL;
	int match_len = 0;
	uint8_t readid[SFC_MAX_READID_LEN];

	if (stsfc_read_jedec(sfc, readid) < 0)
		return NULL;

	/*
	 * The 'readid' may match multiple entries in the table.  To ensure we
	 * retrieve the most specific match, use the match with the longest len.
	 */
	for (info = flash_types; info->name; info++) {
		if (memcmp(info->readid, readid, info->readid_len) == 0 &&
		    info->readid_len > match_len) {
			match = info;
			match_len = info->readid_len;
		}
	}

	if (!match)
		printf("Unrecognized READID [%s]\n", readid);

	return match;
}

static void stsfc_set_mode(struct stsfc *sfc)
{
	uint32_t sfc_mode_select;

	sfc_mode_select = readl(sfc->base + SFC_MODE_SELECT);

	/* Disable boot mode */
	sfc_mode_select &= ~SFC_BOOT_ENABLE;

	/* Only BDM-DMA mode is supported */
	sfc_mode_select &= ~SFC_CPU_MODE;
	sfc_mode_select |= SFC_DMA_MODE;

	/* Disable high speed */
	sfc_mode_select &= ~SFC_WR_HS_NOT_DS;
	sfc_mode_select &= ~SFC_RD_HS_NOT_DS;

	/* CS active */
	if (sfc->cs1_used)
		sfc_mode_select |= SFC_CS_SEL_1_NOT_0;
	else
		sfc_mode_select &= ~SFC_CS_SEL_1_NOT_0;

	/* Set mode */
	writel(sfc_mode_select, sfc->base + SFC_MODE_SELECT);
}

static void stsfc_set_freq(struct stsfc *sfc, uint32_t spi_freq)
{
	uint32_t sfc_freq, pll0_spi_div;
	uint32_t sfc_clock_division;
	int clk_div;

	sfc_freq = SFC_IP_FREQ;

#ifdef CONFIG_STM_GLLCFF
	pll0_spi_div = readl(GLLCFF_FC_BACKBONE_BASE + PLL0_SPI_DIV_OFFSET);
	if (pll0_spi_div != PLL0_SPI_DIVIDER_RESET_VALUE) {
		/*
		 * If value not equal to reset value, TPack has set SPI-CLK to
		 * max frequency
		 */
		sfc_freq = PLL0_SPI_MAX_FREQ;
	}
#endif

	sfc_clock_division = readl(sfc->base + SFC_CLOCK_DIVISION);

	/*if max freq of SPI-NOR is greater than SPI link max freq */
	if (spi_freq > sfc_freq) {
		/* Set clk_div off */
		sfc_clock_division = 0;
		clk_div = 0;

		/*
		 * At high frequency, DDL should be active with both Rx and Tx
		 * delay.
		 */
		writel(0xa,  sfc->base + SFC_RX_DELAY);
		writel(0xd,  sfc->base + SFC_TX_DELAY);
		writel(0x70, sfc->base + SFC_CLOCK_CONTROL);
	} else {
		/* DLL, Rx delay, Tx delay deactivation */
		writel(0x0, sfc->base + SFC_CLOCK_CONTROL);

		/* Set clk_div on */
		sfc_clock_division &= ~SFC_CLK_DIV_BYPASS;

		/* Calculate clk_div - values 2, 4, 6, 8, 10, 12 or 14 */
		sfc_clock_division &= ~SFC_CLK_DIV;
		clk_div = DIV_ROUND_UP(sfc_freq, spi_freq);
		clk_div = max(min(clk_div, CLK_DIV_MAX), CLK_DIV_MIN);
		clk_div = DIV_ROUND_UP(clk_div, 2);
		sfc_clock_division |= SFC_CLK_DIVISION(clk_div);
		clk_div *= 2;
	}

	debug("sfc_clk = %uHZ, spi_freq = %uHZ, clk_div = %u\n",
	      sfc_freq, spi_freq, clk_div);

	writel(sfc_clock_division, sfc->base + SFC_CLOCK_DIVISION);
}

static void stsfc_init(struct stsfc *sfc)
{
	uint32_t reg;

	/* Set clock to 'safe' frequency initially */
	stsfc_set_freq(sfc, SFC_FLASH_SAFE_FREQ);

	/* Switch to BDM DMA mode */
	stsfc_set_mode(sfc);

	/* Enable interruptions */
	reg = SFC_INT_SEQUENCE_COMPLETION;
	stsfc_enable_interrupts(sfc, reg);
}

static int stsfc_fetch_platform_configs(struct stsfc *sfc)
{
	uint32_t boot_device;
	unsigned int *sysconf_reg;
	int i;

	/* Set defaults. */
	sfc->booted_from_spi = false;
	sfc->reset_signal = 0;
	sfc->reset_por = 0;
	sfc->cs1_used = 0;
	sfc->max_freq = 0;

	sysconf_reg = (unsigned int *)SYSCONF((uintptr_t)stsfc_boot_data.reg);
	boot_device = readl(sysconf_reg) & stsfc_boot_data.mask;

	for (i = 0; i < SFC_MAX_MODE_PINS; i++) {
		if (boot_device == stsfc_boot_data.spi[i]) {
			sfc->booted_from_spi = true;
			break;
		}
	}

	return 0;
}

/*
 *
 * SFC mtd entries
 *
 */

/*
 * Read an address range from the flash chip. The address range
 * may be any size provided it is within the physical boundaries.
 */
static int stsfc_mtd_read(struct mtd_info *mtd, loff_t from, size_t len,
			  size_t *retlen, u_char *buf)
{
	struct stsfc *sfc = mtd_to_sfc(mtd);
	uint32_t bytes;
	int ret = 0;

	/* Load address configuration */
	stsfc_load_addr_cfg(sfc, sfc->seq_read_addr_cfg);

	while (len) {
		bytes = min_t(size_t, len, SFC_FLASH_PAGESIZE);

		ret = stsfc_read(sfc, (uint8_t *)buf, bytes, from);
		if (ret < 0)
			goto out;

		buf += bytes;
		from += bytes;
		len -= bytes;
	}

out:
	*retlen = len;

	return ret;
}

/*
 * Write an address range to the flash chip.  Data must be written in
 * SFC_FLASH_PAGESIZE chunks.  The address range may be any size provided
 * it is within the physical boundaries.
 */
static int stsfc_mtd_write(struct mtd_info *mtd, loff_t to, size_t len,
			   size_t *retlen, const u_char *buf)
{
	struct stsfc *sfc = mtd_to_sfc(mtd);
	uint8_t *b = (uint8_t *)buf;
	u32 page_offs;
	u32 bytes;
	int ret = 0;

	/* Offset within page */
	page_offs = to % SFC_FLASH_PAGESIZE;

	/* Load address configuration */
	stsfc_load_addr_cfg(sfc, sfc->seq_write_addr_cfg);

	while (len) {
		/* Write up to page boundary */
		bytes = min_t(size_t, SFC_FLASH_PAGESIZE - page_offs, len);

		ret = stsfc_write(sfc, b, bytes, to);
		if (ret < 0)
			goto out;

		b += bytes;
		len -= bytes;
		to += bytes;

		/* We are now page-aligned */
		page_offs = 0;
	}

out:
	*retlen = len;
	return ret;
}

/*
 * Erase a block on the flash chip. Return an error is there is a problem
 * erasing.
 */
static int stsfc_mtd_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct stsfc *sfc = mtd_to_sfc(mtd);
	struct spi_flash *flash = &sfc->flash;
	int ret = 0;
	u32 offset = instr->addr;
	size_t len = instr->len;

	debug("STSFC: at 0x%08x, len 0x%08x\n", offset, (u32)len);

	if ((offset + len > flash->size) || (len % flash->sector_size) ||
	    (offset % flash->sector_size))
		return -1;

	/* Whole-chip erase? */
	if (len == flash->size && offset == 0) {
		ret = stsfc_erase_chip(sfc);
	} else {
		/* Load address configuration */
		stsfc_load_addr_cfg(sfc, sfc->seq_erase_addr_cfg);

		while (len) {
			ret = stsfc_erase_sector(sfc, offset);
			if (ret < 0)
				return ret;

			offset += flash->erase_size;
			len -= flash->erase_size;
		}
	}

	return ret;
}

struct spi_flash *spi_flash_probe(unsigned int bus,
				  unsigned int cs,
				  unsigned int max_hz,
				  unsigned int spi_mode)
{
	struct stm_flash_info *info;
	struct stsfc *sfc;
	int ret;
	struct mtd_info *mtd;

	sfc = kzalloc(sizeof(*sfc), GFP_KERNEL);
	if (!sfc)
		return NULL;

	switch (bus) {
	case 0:
		/*command 'sf probe' or 'sf probe 0:0'*/
		sfc->base = (void __iomem *)CONFIG_SYS_STM_SPI_FSM_BASE;
		break;
#ifdef CONFIG_SYS_STM_SPI_FSM2_BASE
	case 1:
		/*command 'sf proble 1:0', only cs=0 is currently supported */
		sfc->base = (void __iomem *)CONFIG_SYS_STM_SPI_FSM2_BASE;
		break;
#endif
	default:
		printf("No SPI-NOR bus number %d\n", bus);
		goto cleanup;
	}

	ret = stsfc_fetch_platform_configs(sfc);
	if (ret < 0) {
		printf("Failed to fetch platform configuration\n");
		goto cleanup;
	}

	stsfc_init(sfc);

	if (p_addr == (phys_addr_t)NULL) {
		p_addr = stm_dma_alloc_coherent(SFC_FLASH_PAGESIZE +
						SFC_DMA_ALIGNMENT);
		if (!p_addr)
			goto cleanup;
	}

	/* Align buffer pointer */
	sfc->dma_buf = PTR_ALIGN((uint8_t *)p_addr, SFC_DMA_ALIGNMENT);

	/* Detect SPI FLASH device */
	info = stsfc_jedec_probe(sfc);
	if (!info) {
		goto cleanup;
	}
	sfc->info = info;

	/* Use device size to determine address width */
	if (info->sector_size * info->n_sectors > SZ_16M)
		info->flags |= SFC_FLAG_32BIT_ADDR;

	/*
	 * Configure READ/WRITE/ERASE sequences according to platform and
	 * device flags.
	 */
	if (info->config) {
		ret = info->config(sfc);
	} else {
		ret = stsfc_prepare_rwe_seqs_default(sfc);
	}

	if (ret)
		goto cleanup;

	/* Set operating frequency, from table */
	if (info->max_freq) {
		stsfc_set_freq(sfc, info->max_freq);
		sfc->max_freq = info->max_freq;
	}

	sfc->flash.name = info->name;
	sfc->flash.page_size = 256u;
	sfc->flash.size = info->sector_size * info->n_sectors;
	sfc->flash.sector_size = info->sector_size;
	sfc->flash.erase_size = info->sector_size;

	mtd = &sfc->flash.mtd;

	sfc->flash.mtd.name		= "nor0";
	sfc->flash.mtd.type		= MTD_NORFLASH;
	sfc->flash.mtd.flags		= MTD_CAP_NORFLASH;
	sfc->flash.mtd.size		= sfc->flash.size;
	sfc->flash.mtd.writesize	= 1;
	sfc->flash.mtd.writebufsize	= SFC_FLASH_PAGESIZE;
	mtd->_erase		= stsfc_mtd_erase;
	mtd->_read		= stsfc_mtd_read;
	mtd->_write		= stsfc_mtd_write;
	sfc->flash.mtd.numeraseregions = 0;
	sfc->flash.mtd.erasesize 	= sfc->flash.erase_size;

	printf("Found serial flash device: %s\n", info->name);
	printf("  size = %llx (%lldMiB) erasesize = 0x%08x (%uKiB)\n",
	       (long long)sfc->flash.size, (long long)(sfc->flash.size >> 20),
	       sfc->flash.erase_size, (sfc->flash.erase_size >> 10));

	return &(sfc->flash);

cleanup:
	kfree(sfc);
	return NULL;
}

void spi_flash_free(struct spi_flash *flash)
{
	if (!flash)
		return;

	kfree(to_sfc(flash));
}

