/*
 * Copyright 2017 SpaceX
 * Kevin Bosien (kevin.bosien@spacex.com)
 *
 */

#include <common.h>
#include <command.h>
#include <mapmem.h>
#include <spi.h>
#include <spi_flash.h>
#include <asm/io.h>
#include <linux/ctype.h>
#include <u-boot/crc.h>
#include <spacex/sxid.h>

#include <linux/types.h>
#include <asm/dma-mapping.h>

/**
 * sxid_read - read the SXID and set appropriate environmental variables
 *
 * @sxid - SXI structure to decode
 *
 * This function parses the SXID buffer, setting the appropriate environmental
 * variables
 *
 * The environmental variables are only set if they haven't been set already.
 * This ensures that any user-saved variables are never overwritten.
 *
 * This function must be called after relocation.
 *
 * Return: CMD_RET_SUCCESS on successful decode, CMD_RET_FAILURE on error
 */
static int sxid_read(const sxid_t *sxid)
{
	u32 crc;
	int i;

	if (sxid == NULL)
		return CMD_RET_FAILURE;

	/*
	 * Check that this is a valid SXID structure
	 */
	if (memcmp(sxid->id, "SXID", sizeof(sxid->id))) {
		/*
		 * Do not log here, as this may be tried multiple times while
		 * scanning different blocks.
		 */
		return CMD_RET_FAILURE;
	}

	/*
	 * Verify the CRC
	 */
	crc = crc32(0, (void *)sxid, offsetof(sxid_t, crc));
	if (crc != le32_to_cpu(sxid->crc)) {
		printf("SXID: CRC mismatch (%08x != %08x)\n",
		       crc, le32_to_cpu(sxid->crc));
		return CMD_RET_FAILURE;
	}

	/*
	 * Print (but don't fail) if the header has an unexpected version
	 */
	if (le32_to_cpu(sxid->version) != SXID_VERSION_CUR) {
		printf("SXID: Unexpected version (%d != %d)\n",
		       SXID_VERSION_CUR, le32_to_cpu(sxid->version));
	}

	for (i = 0; i < MAX_NUM_PORTS; i++) {
		if (memcmp(&sxid->mac[i], "\0\0\0\0\0\0", 6) &&
		    memcmp(&sxid->mac[i], "\xFF\xFF\xFF\xFF\xFF\xFF", 6)) {
			/*
			 * Ethernet address is 6 octets (12 bytes) + 5
			 * colons + null terminator
			 */
			char ethaddr[18];
			/*
			 * Env is "ethaddr" + up to 2 bytes for number + null
			 */
			char enetvar[10];

			snprintf(ethaddr, sizeof(ethaddr),
				 "%02X:%02X:%02X:%02X:%02X:%02X",
				 sxid->mac[i][0], sxid->mac[i][1],
				 sxid->mac[i][2], sxid->mac[i][3],
				 sxid->mac[i][4], sxid->mac[i][5]);
			snprintf(enetvar, sizeof(enetvar),
				 i ? "eth%daddr" : "ethaddr", i);
			/*
			 * Only initialize environment variables that are blank
			 * (i.e-> have not yet been set)
			 */
			if (!env_get(enetvar))
				env_set(enetvar, ethaddr);
		}
	}

	/*
	 * Use temporary buffer to ensure it's null terminated
	 */
	if (!env_get("sxid_sn")) {
		char sntmp[sizeof(sxid->sn) + 1] = { 0 };
		memcpy(sntmp, (const char *)sxid->sn, sizeof(sxid->sn));
		env_set("sxid_sn", sntmp);
	}

	if (!env_get("sxid_asn")) {
		char asntmp[sizeof(sxid->asn) + 1] = { 0 };
		memcpy(asntmp, (const char *)sxid->asn, sizeof(sxid->asn));
		env_set("sxid_asn", asntmp);
	}

	if (!env_get("vehicleid")) {
		/*
		 * 32-bit value to decimal is up to 10 bytes, + null
		 */
		char vidtmp[11] = { 0 };
		snprintf(vidtmp, sizeof(vidtmp), "%u",
			 le32_to_cpu(sxid->vehicle_id));
		env_set("vehicleid", vidtmp);
	}

	if (!env_get("slotid")) {
		/*
		 * 32-bit value to decimal is up to 10 bytes + null
		 */
		char sidtmp[11] = { 0 };
		snprintf(sidtmp, sizeof(sidtmp), "%u",
			le32_to_cpu(sxid->slot_id));
		env_set("slotid", sidtmp);
	}

	return CMD_RET_SUCCESS;
}

#ifdef CONFIG_SPI_FLASH
/**
 * sxid_read_from_flash - search for a valid SXID in region of flash
 *
 * @offset - the flash offset
 * @length - the length of the region to search
 * @sxid - the buffer to write the SXID to if found
 *
 * Return: CMD_RET_SUCCESS if a valid SXID structure is found,
 * CMD_RET_FAILURE or error code otherwise.
 */
int sxid_read_from_flash(unsigned long offset,
			 unsigned long length,
			 sxid_t *sxid)
{
	int ret;
	int i;
	struct spi_flash *flash;

	if (length < sizeof(sxid_t))
		return CMD_RET_FAILURE;

	flash = spi_flash_probe(CONFIG_SF_DEFAULT_BUS,
				CONFIG_SF_DEFAULT_CS,
				CONFIG_SF_DEFAULT_SPEED,
				CONFIG_SF_DEFAULT_MODE);
	if (!flash) {
		return -ENODEV;
	}

	/*
	 * Search for SXID header one page at a time. Assume the header is
	 * aligned on flash page boundaries.
	 */
	for (i = 0; i <= length - sizeof(sxid_t); i += flash->page_size) {
		ret = spi_flash_read(flash, offset + i, sizeof(sxid_t), sxid);
		if (ret) {
			printf("Error %d reading from flash\n", ret);
			break;
		}

		if (sxid_read(sxid) == 0) {
			spi_flash_free(flash);
			return 0;
		}
	}

	spi_flash_free(flash);
	printf("No SXID found\n");
	return CMD_RET_FAILURE;
}
#endif

#ifdef CONFIG_MMC_SDHCI
/**
 * sxid_read_from_mmc - search for a valid SXID in region of flash
 *
 * @dev - the mmc dev part to use
 * @part - the mmc partition to use
 * @offset - the flash offset
 * @length - the length of the region to search
 * @sxid - the buffer to write the SXID to if found
 *
 * Return: CMD_RET_SUCCESS if a valid SXID structure is found,
 * CMD_RET_FAILURE or error code otherwise.
 */
#include "mmc.h"
int sxid_read_from_mmc(unsigned int dev, unsigned int part,
		       unsigned long offset, unsigned long length,
		       sxid_t *sxid)
{
	int i, cnt;
	struct mmc *mmc = find_mmc_device(dev);
	unsigned long handle = 0;
	void *data = NULL;

	if (!mmc) {
		printf("Unable to find mmc device %d\n", CATS_MMC_BOOT_DEV);
		return -ENODEV;
	}

	if (mmc_init(mmc) != 0) {
		printf("Failed to init mmc device %d\n", CATS_MMC_BOOT_DEV);
		return -EIO;
	}

	if (blk_select_hwpart_devnum(IF_TYPE_MMC, dev, part) != 0) {
		printf("Failed to select mmc partition %d\n", CATS_MMC_BOOT_PART);
		return -EIO;
	}

	dma_alloc_coherent(mmc->read_bl_len, &handle);
	data = (void *)handle;
	if (data == NULL) {
		return -ENOMEM;
	}

	/*
	 * Search for SXID header one page at a time. Assume the header is
	 * aligned on flash page boundaries.
	 */
	for (i = offset / mmc->read_bl_len;
	     i <= (offset + length - sizeof(sxid_t)) / mmc->read_bl_len; i++) {
		cnt = blk_dread(&mmc->block_dev, i, 1, data);
		if (cnt != 1) {
			printf("Failed to read sxid\n");
			break;
		}

		if (sxid_read((sxid_t *)data) == 0) {
			memcpy(sxid, data, sizeof(sxid_t));
			dma_free_coherent(data);
			return CMD_RET_SUCCESS;
		}
	}

	dma_free_coherent(data);
	printf("No SXID found\n");
	return -ENODEV;
}
#endif

/**
 * do_sxid() - Handler for the "sxid" command.
 *
 * @cmdtp:  A pointer to the command structure in the command table.
 * @flag:   Flags, e.g. if the command was repeated with ENTER.
 * @argc:   The number of arguments.
 * @argv:   The command's arguments, as an array of strings.
 *
 * Return: CMD_RET_SUCCESS on success, CMD_RET_USAGE to display the
 * usage, CMD_RET_FAILURE otherwise.
 */
int do_sxid(cmd_tbl_t *cmdtp, int flag, int argc, char *const argv[])
{
	unsigned long length;
	int i;
	unsigned char *data;
	int rc = CMD_RET_FAILURE;

	if (argc != 3)
		return CMD_RET_USAGE;

	data = (unsigned char *)simple_strtoul(argv[1], NULL, 16);
	length = simple_strtoul(argv[2], NULL, 16);

	if (length < sizeof(sxid_t))
		return CMD_RET_USAGE;

	/*
	 * Search for SXID header
	 */
	for (i = 0; i <= length - sizeof(sxid_t); i++) {
		if (!memcmp(&data[i], "SXID", strlen("SXID")))
		{
			const sxid_t *sxid = (const sxid_t *)&data[i];
			rc = sxid_read(sxid);
			if (rc == CMD_RET_SUCCESS)
				break;
		}
	}

	if (!env_get("sxid_sn")) {
		printf("SXID: Unable to find valid serial number\n");
		env_set("sxid_sn", "INVALID");
	}

	if (!env_get("sxid_asn")) {
		printf("SXID: Unable to find valid assembly number\n");
		env_set("sxid_asn", "INVALID");
	}

	return rc;
}

U_BOOT_CMD(
	sxid, 3, 0, do_sxid,
	"Set the address where the content of the SXID has been loaded",
	"<address> <length>\n"
);
