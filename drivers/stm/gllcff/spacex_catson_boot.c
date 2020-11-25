/*
 * Catson shared boot methods used to implement multi-boot.
 */
#include <malloc.h> /* Required before dma-mapping.h */
#include <linux/types.h> /* Required before dma-mapping.h */
#include <asm/cache.h> /* Required before dma-mapping.h */
#include <asm/armv8/mmu.h>
#include <asm/dma-mapping.h>
#include <asm/io.h>
#include <common.h>
#include <command.h>
#include <fdt_support.h>
#include <mmc.h>
#include <spacex/common.h>
#include <spi.h>
#include <spi_flash.h>

#define BOOT_MODE_REGISTER 0x09130048
#define		BOOT_MODE_EMMC_BIT	BIT(4)

#define FIP1_BOOT_ID_REG 0x22400e04
#define FIP2_BOOT_ID_REG 0x22400e0c
#define NUM_LINUX_BOOT_SLOTS 2

#define CATSON_SIP_SVC_SOFT_RESET   0xC2000011
#define CATSON_SIP_SVC_GET_SOFT_RESET_COUNT	0xC2000012
#define CATSON_SIP_SVC_DISABLE_SWDT 0xC2000015

#define CATSON_SIP_SVC_GET_BOARD_TYPE 0xC2000027

/* mmu def mle to check */
static struct mm_region gllcff_mem_map[] = {
	{
		.virt = 0x0UL,
		.phys = 0x0UL,
		.size = CATSON_SYS_SDRAM_PHYS_BASE,
		.attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
			 PTE_BLOCK_NON_SHARE |
			 PTE_BLOCK_PXN | PTE_BLOCK_UXN
	},
	{
		.virt = CONFIG_SYS_SDRAM_BASE,
		.phys = CONFIG_SYS_SDRAM_BASE,
		.size = CONFIG_SYS_SDRAM_SIZE,
		.attrs = PTE_BLOCK_MEMTYPE(MT_NORMAL) |
			 PTE_BLOCK_INNER_SHARE
	},
	{
		.virt = CATSON_DMA_COHERENT_BASE,
		.phys = CATSON_DMA_COHERENT_BASE,
		.size = CATSON_DMA_COHERENT_SIZE,
		.attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
			 PTE_BLOCK_NON_SHARE |
			 PTE_BLOCK_PXN | PTE_BLOCK_UXN
	},
	{
		.virt = CONFIG_SYS_TEXT_BASE,
		.phys = CONFIG_SYS_TEXT_BASE,
		.size = CATSON_SYS_TEXT_SIZE,
		.attrs = PTE_BLOCK_MEMTYPE(MT_NORMAL) |
			 PTE_BLOCK_INNER_SHARE
	},
	{
	}
};

struct mm_region *mem_map = gllcff_mem_map;

/**
 * catson_read_fip2_slot() - Return which fip this uboot came from.
 *
 * Return:	Returns current fip boot slot.
 */
static unsigned long catson_read_fip2_slot(void)
{
	return readl(FIP2_BOOT_ID_REG) & 0xff;
}

int spacex_read_curr_boot_slot(void)
{
	unsigned long fip2_slot = catson_read_fip2_slot();

	if (fip2_slot < BOOT_START_SLOT ||
	    fip2_slot > BOOT_END_SLOT) {
		printf("Invalid FIP2 boot slot %ld!\n", fip2_slot);
		return -1;
	}

	return fip2_slot - BOOT_START_SLOT;
}

/**
 * catson_read_fip1_slot() - Return which bootfip ROM booted.
 *
 * Return:      Returns current bootfip boot.
 */
static unsigned long catson_read_fip1_slot(void)
{
	return readl(FIP1_BOOT_ID_REG);
}

/**
 * catson_disable_swdt() - Disable the secure watchdog timer.
 */
void catson_disable_swdt(void)
{
	struct pt_regs regs = {};

	regs.regs[0] = CATSON_SIP_SVC_DISABLE_SWDT;

	smc_call(&regs);
}

bool catson_booted_from_emmc(void)
{
	return !!(readl(BOOT_MODE_REGISTER) & BOOT_MODE_EMMC_BIT);
}

/**
 * spacex_soft_reboot() - Perform a soft reboot, falling into next slot.
 */
void spacex_soft_reboot(void)
{
	struct pt_regs regs = {};
	regs.regs[0] = CATSON_SIP_SVC_SOFT_RESET;

	smc_call(&regs);

	printf("Soft reboot failed!\n");
	while(1);
}

/**
 * spacex_get_soft_reboot_count() - Returns the soft reboots counter.
 */
long spacex_get_soft_reboot_count(void)
{
	struct pt_regs regs = {};
	regs.regs[0] = CATSON_SIP_SVC_GET_SOFT_RESET_COUNT;

	smc_call(&regs);

	return (regs.regs[0] == 0) ? regs.regs[1] : -1;
}

/**
 * catson_get_board_type_fuse() - Returns the fuse based board identifier.
 */
int catson_get_board_type_fuse(void)
{
	struct pt_regs regs = {};

	regs.regs[0] = CATSON_SIP_SVC_GET_BOARD_TYPE;

	smc_call(&regs);

	return (regs.regs[0] == 0) ? regs.regs[1] : -1;
}

/**
 * catson_mark_boot_attempt() - Mark boot attempt in persistent storage and
 *				    set enviornmental with total boot attempts.
 *
 * @update:	Bool to mark persistent storage or just count slots.
 *
 * Return:	previous boot count on success, <0 on failure.
 */
#ifdef CONFIG_CATSON_EMMC_BOOT
static int catson_mark_boot_attempt(void)
{
	struct mmc *mmc = find_mmc_device(CATS_MMC_BOOT_DEV);
	int cnt;
	uint block_size;
	int boot_count;
	unsigned long handle = 0;
	u32 *data = NULL;
	char env_string[32];

	if (!mmc) {
		printf("Unable to find mmc device %d\n", CATS_MMC_BOOT_DEV);
		return -ENODEV;
	}

	if (mmc_init(mmc) != 0) {
		printf("Failed to init mmc device %d\n", CATS_MMC_BOOT_DEV);
		return -EIO;
	}

	if (blk_select_hwpart_devnum(IF_TYPE_MMC, CATS_MMC_BOOT_DEV, CATS_MMC_BOOT_PART) != 0) {
		printf("Failed to select mmc partition %d\n", CATS_MMC_BOOT_PART);
		return -EIO;
	}

	block_size = min(mmc->read_bl_len, mmc->write_bl_len);

	dma_alloc_coherent(block_size, &handle);
	data = (u32 *)handle;
	if (data == NULL) {
		return -ENOMEM;
	}

	cnt = blk_dread(&mmc->block_dev,
			CATS_BOOTMASK1_OFFSET / mmc->read_bl_len, 1, data);
	if (cnt != 1) {
		printf("Failed to read boot count\n");
		boot_count = -EIO;
		goto out;
	}

	boot_count = *data;
	*data = (boot_count + 1) & 0xffff;

	cnt = blk_dwrite(&mmc->block_dev,
			 CATS_BOOTMASK1_OFFSET / mmc->write_bl_len, 1, data);
	if (cnt != 1) {
		printf("Failed to write boot count\n");
		boot_count = -EIO;
		goto out;
	}

	cnt = blk_dwrite(&mmc->block_dev,
			 CATS_BOOTMASK2_OFFSET / mmc->write_bl_len, 1, data);
	if (cnt != 1) {
		printf("Failed to write boot count\n");
		boot_count = -EIO;
		goto out;
	}

out:
	dma_free_coherent(data);
	snprintf(env_string, sizeof(env_string), "%d", boot_count);
	env_set("boot_count", env_string);

	return boot_count;
}
#else
#ifdef CONFIG_SPACEX_STARLINK_TFTP
static int catson_mark_boot_attempt(void)
{
	struct spi_flash *flash = NULL;
	int ret = -1;

	flash = spi_flash_probe(CONFIG_SF_DEFAULT_BUS, CONFIG_SF_DEFAULT_CS,
				CONFIG_SF_DEFAULT_SPEED,
				CONFIG_SF_DEFAULT_MODE);
	if (flash == NULL) {
		printf("Unable to probe flash.\n");
		return -1;
	}

	ret = spi_flash_erase(flash, CATS_BOOTMASK1_OFFSET,
			      flash->erase_size);
	if (ret) {
		printf("Failed to erase flash\n");
		ret = -1;
		goto cleanup;
	}
	ret = spi_flash_erase(flash, CATS_BOOTMASK2_OFFSET,
			      flash->erase_size);
	if (ret) {
		printf("Failed to erase flash\n");
		ret = -1;
		goto cleanup;
	}

cleanup:
	env_set("boot_count", "0");
	if (flash) {
		spi_flash_free(flash);
	}

	return ret;
}
#else /* SPI BOOT */
#define BOOTMAP_BAD_VAL 0xcafefeed
#define INVALID_BOOT_SLOT (~0)
static int catson_mark_boot_attempt(void)
{
	/*
	 * This function could be made more efficient if needed by increasing
	 * the flash read size.  At this time does not seem necessary.
	 */
	u64 boot_value;
	int i, j;
	int ret = -1;
	struct spi_flash *flash = NULL;
	unsigned long first_boot_slot = INVALID_BOOT_SLOT;
	char env_string[32];

	flash = spi_flash_probe(CONFIG_SF_DEFAULT_BUS, CONFIG_SF_DEFAULT_CS,
				CONFIG_SF_DEFAULT_SPEED,
				CONFIG_SF_DEFAULT_MODE);
	if (flash == NULL) {
		printf("Unable to probe flash.\n");
		return -1;
	}

	/* Sanity checks */
	if (!flash->erase_size || CATS_BOOTMASK1_OFFSET % flash->erase_size ||
	    CATS_BOOTMASK2_OFFSET % flash->erase_size ||
	    CATS_BOOTMASK_SIZE % flash->erase_size) {
		printf("Parameters do not align with flash.\n");
		goto cleanup;
	}

	/* Scan through flash looking for the first non-zero word. */
	for (i = 0; i < flash->erase_size; i += sizeof(boot_value)) {
		boot_value = BOOTMAP_BAD_VAL;
		ret = spi_flash_read(flash, CATS_BOOTMASK1_OFFSET + i,
				     sizeof(boot_value), &boot_value);
		/* Validate that the read actually worked. */
		if (ret || boot_value == BOOTMAP_BAD_VAL) {
			printf("Failed to read flash 0x%x\n",
			       CATS_BOOTMASK1_OFFSET + i);
			ret = -1;
			goto cleanup;
		}

		/* Find the first non-zero value and then count zero bits */
		if (boot_value) {
			for (j = 0; j < sizeof(boot_value) * 8; j++) {
				if (boot_value & (1ULL << j)) {
					first_boot_slot =
						i * sizeof(boot_value) + j;
					boot_value = boot_value & ~(1ULL << j);
					break;
				}
			}

			/* Flip first one to a zero and bump the boot count */
			/*
			 * Increment boot slot to reflect whats in flash
			 * when the function returns.
			 */
			ret = spi_flash_write(
				flash, CATS_BOOTMASK1_OFFSET + i,
				sizeof(boot_value), &boot_value);
			if (ret) {
				printf("Failed to mark boot attempt\n");
				ret = -1;
				goto set_env;
			}
			/*
			 * BL20 finds the bootmask region by searching
			 * for the term entry in hardware.  For
			 * redunancy, a second term region is included
			 * as Catson will bootloop without a term.  This
			 * keeps A/B working even if the TERM signal
			 * is damaged.
			 */
			ret = spi_flash_write(
				flash, CATS_BOOTMASK2_OFFSET + i,
				sizeof(boot_value), &boot_value);
			if (ret) {
				printf("Failed to mark boot attempt\n");
				ret = -1;
				goto set_env;
			}
			break;
		}
	}

	/*
	 * If entire block is empty, reset the block.  This prevents forever
	 * increasing boot times.  A block was chosen as the reset point
	 * somewhat arbitrarily.
	 */
	if (first_boot_slot == INVALID_BOOT_SLOT) {
		first_boot_slot = 0;
		ret = spi_flash_erase(flash, CATS_BOOTMASK1_OFFSET,
				      flash->erase_size);
		if (ret) {
			printf("Failed to erase flash\n");
			ret = -1;
			goto set_env;
		}
		ret = spi_flash_erase(flash, CATS_BOOTMASK2_OFFSET,
				      flash->erase_size);
		if (ret) {
			printf("Failed to erase flash\n");
			ret = -1;
			goto set_env;
		}

		/* Need to write a bit to prevent double image. */
		boot_value = ~1;
		ret = spi_flash_write(flash, CATS_BOOTMASK1_OFFSET,
				      sizeof(boot_value), &boot_value);
		if (ret) {
			printf("Failed to mark boot attempt after erase\n");
			ret = -1;
			goto set_env;
		}
		ret = spi_flash_write(flash, CATS_BOOTMASK2_OFFSET,
				      sizeof(boot_value), &boot_value);
		if (ret) {
			printf("Failed to mark boot attempt after erase\n");
			ret = -1;
			goto set_env;
		}
	}

	ret = 0;
set_env:
	snprintf(env_string, sizeof(env_string), "%ld", first_boot_slot);
	env_set("boot_count", env_string);
cleanup:
	if (flash) {
		spi_flash_free(flash);
	}

	return ret;
}
#endif
#endif

/**
 * catson_late_init() - SoC-specific late initialization handler.
 *
 * Update the boot counters, disable the watchdog and determine
 * the boot slot.
 */
void catson_late_init(void)
{
	int ret;
	unsigned long fip1_boot_slot;
	unsigned long fip2_boot_slot;
	char modeboot[16];
#ifndef CONFIG_SPACEX_STARLINK_TFTP
	bool emmc_boot = catson_booted_from_emmc();
	const char *boot_string;
	unsigned long linux_boot_slot;
#endif

	ret = catson_mark_boot_attempt();
	if (ret < 0) {
		/* On failure continue best-effort. */
		printf("ERROR: failed to update SPI boot slot\n");
	}

#ifndef CONFIG_SPACEX_STARLINK_TFTP
#ifdef CONFIG_CATSON_EMMC_BOOT
	if (!emmc_boot) {
		printf("WARNING: Configured for eMMC boot but booted from SPI\n");
	}
	boot_string = "emmcboot";
#else
	if (emmc_boot) {
		printf("WARNING: Configured for SPI boot but booted from eMMC\n");
	}
	boot_string = "qspiboot";
#endif
#endif

	/*
	 * Disable the WDT until Rocket support is added for this.  At this
	 * point, NOR has been updated, so BL20 will pick a different slot on
	 * the next power cycle.
	 */
	catson_disable_swdt();

	env_set_ulong("catson_cut", catson_get_version() + 1);

	/*
	 * Set the FIP slot environment.
	 */
	fip1_boot_slot = catson_read_fip1_slot();
	env_set_ulong("fip1_boot_slot", fip1_boot_slot);

	fip2_boot_slot = catson_read_fip2_slot();
	if (fip2_boot_slot < BOOT_START_SLOT ||
	    fip2_boot_slot > BOOT_END_SLOT) {
		printf("Invalid FIP2 boot slot %ld!\n", fip2_boot_slot);
#ifndef CONFIG_SPACEX_STARLINK_TFTP
		linux_boot_slot = env_get_ulong("boot_count", 10, 0) %
						NUM_LINUX_BOOT_SLOTS;
#endif
		env_set("fip2_boot_slot", "Invalid");
	} else {
		fip2_boot_slot -= BOOT_START_SLOT;
#ifndef CONFIG_SPACEX_STARLINK_TFTP
		linux_boot_slot = fip2_boot_slot % NUM_LINUX_BOOT_SLOTS;
#endif
		env_set_ulong("fip2_boot_slot", fip2_boot_slot);
	}

	printf("FIP1: %ld FIP2: %ld\n", fip1_boot_slot, fip2_boot_slot);

#ifdef CONFIG_SPACEX_STARLINK_TFTP
	puts("TFTP MODE\n");
	env_set("linux_boot_slot", "0");
	snprintf(modeboot, sizeof(modeboot), "mdml_tftp_boot");
#else
	if (linux_boot_slot == 0) {
		puts("BOOT SLOT A\n");
		snprintf(modeboot, sizeof(modeboot), "%s_a", boot_string);
		env_set("linux_boot_slot", "0");
	} else {
		puts("BOOT SLOT B\n");
		snprintf(modeboot, sizeof(modeboot), "%s_b", boot_string);
		env_set("linux_boot_slot", "1");
	}
#endif

	env_set("modeboot", modeboot);
}

/**
 * Populate the Catson-specific device tree entries.
 *
 * @fdt:	A pointer to the Flattened Device Tree.
 */
void catson_populate_fdt_chosen(void *fdt)
{
	fdt_add_env_to_chosen(fdt, "bootfip_slot", "fip1_boot_slot");
	fdt_add_env_to_chosen(fdt, "boot_slot", "fip2_boot_slot");
	fdt_add_env_to_chosen(fdt, "linux_boot_slot", NULL);
	fdt_add_env_to_chosen(fdt, "boot_count", NULL);
	fdt_add_env_to_chosen(fdt, "board_revision", NULL);
	fdt_add_env_to_chosen(fdt, "catson_cut", NULL);
	fdt_add_env_to_chosen(fdt, "modeboot", NULL);
	fdt_add_env_to_chosen(fdt, "boot_type", NULL);
}

/**
 * Reads the fuse and returns 1 if the fuse has been blown on production
 * hardware.
 *
 * @return 1 if on production hardware, 0 otherwise.
 */
static int is_prod_hw(void)
{
    const uint32_t data = readl(SECURITY_BSEC_PROD_FUSE);
    return (data & 0x80) != 0;
}


/**
 * Callback that sets the 'is_prod_hw' u-boot environment variable. If the
 * hardware is production fused, is_prod_hw=1, else is_prod_hw=0.
 *
 * @return 0 on success, 1 if env_set fails.
 */
static int call_sx_is_prod_hw(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
    printf("Is production hardware? ");
    if (is_prod_hw() == 0)
    {
        printf("NO\n");
        return env_set("is_prod_hw", "0");
    }

    printf("YES\n");
    return env_set("is_prod_hw", "1");
}

U_BOOT_CMD(
    sx_is_prod_hw, 1, 0, call_sx_is_prod_hw,
    "reads the production fused state and sets is_prod_hw",
    "Reads the fused state and calls 'sentenv is_prod_hw 1' if it has been "
    "fused for production, 0 otherwise."
);
