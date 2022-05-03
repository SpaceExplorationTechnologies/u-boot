// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022, SpaceX
 * Sergey Negrashov
 */

#include <common.h>
#include <asm/io.h>
#include <errno.h>
#include <command.h>
#include <linux/delay.h>

#define RNG_RETRY 10
#define ST_RNG_ADDR 0x22500000
#define ST_RNG_OFFT 0x24
#define ST_RNG_GLLCFF_FIFO_EMPTY	BIT(16)
#define ST_RNG_GLLCFF_BAD_ENTROPY	BIT(17)

/**
 * Seed the KASLR with HW RNG.
 *
 * @return 0 on success, error code on error
 */
int starlink_catson_seed_kaslr(u64 *seed)
{
	size_t words, i;
	u32 rd;

	*seed = 0;
	/*
	 * While not documented, testing revealed that the first value
	 * in the Catson TRNG FIFO is always zeros, so we discard it.
	 */
	for (i = 0; i < RNG_RETRY; i++) {
		udelay(1);
		rd = readl(ST_RNG_ADDR + ST_RNG_OFFT);
		if (!(rd & ST_RNG_GLLCFF_FIFO_EMPTY))
			break;
	}
	/* Consider the BAD_ENTROPY as an error */
	if (rd & ST_RNG_GLLCFF_BAD_ENTROPY)
		return -EIO;

	/* If fifo is empty */
	if (rd & ST_RNG_GLLCFF_FIFO_EMPTY)
		return -EIO;

	for (words = 0; words < sizeof(u64) / sizeof(u16); words++) {
		rd = readl(ST_RNG_ADDR + ST_RNG_OFFT);
		for (i = 0; i < RNG_RETRY; i++) {
			udelay(1);
			rd = readl(ST_RNG_ADDR + ST_RNG_OFFT);
			if (!(rd & ST_RNG_GLLCFF_FIFO_EMPTY))
				break;
		}

		/* Consider the BAD_ENTROPY as an error */
		if (rd & ST_RNG_GLLCFF_BAD_ENTROPY)
			return -EIO;

		/* If fifo is empty */
		if (rd & ST_RNG_GLLCFF_FIFO_EMPTY)
			return -EIO;

		*seed |= ((u64)((rd) & 0xFFFF)) << (16 * words);
	}
	return 0;
}

/**
 * CLI interface for sx random number generator
 *
 * @cmdtp:     UBoot command table.
 * @flag:      Flags for this command.
 * @argc:      The number of user arguments provided.
 * @argv:      The user arguments.
 *
 * Return:     0 on success, <0 on error.
 */
static int do_print_sxrng_cmd(struct cmd_tbl *cmdtp, int flag, int argc,
			      char * const argv[])
{
	u64 seed;
	int ret = starlink_catson_seed_kaslr(&seed);

	if (ret) {
		printf("Could not communicate with hardware\n");
	} else {
		printf("0x%llx\n", seed);
	}
	return ret;
}

U_BOOT_CMD(sxrng, 1, 0, do_print_sxrng_cmd,
	   "SpaceX rng command",
	   "sxrng: print a random number.\n");
