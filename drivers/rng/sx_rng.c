// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022, SpaceX
 * Sergey Negrashov
 */

#include <common.h>
#include <dm.h>
#include <rng.h>
#include <asm/io.h>

#include <linux/string.h>
#include <linux/arm-smccc.h>
#include <linux/delay.h>

/* ZynqMP SMCs */
#define GET_TRNG_STATUS_SVC	0xC3001007

/* Offset into the base address to pull from */
#define SX_ZYNQMP_RNG_DATA_REG		0xC0

#define RNG_RETRY 10
#define SX_RNG_FIFO_EMPTY	BIT(16)
#define SX_RNG_BAD_ENTROPY	BIT(17)
struct sx_rng_plat {
	u64 base;
};

/**
 * smc_read_trng_status - helper function that makes an uncached SMC
 * to fetch the TRNG status.
 *
 * @param status pointer to a u64 where the trng status will be saved
 * @param base_addr pointer to a u64 where the trng base addr will be saved
 *
 * Returns 0 on success, or -EIO on failure.
 */
static int smc_read_trng_status(u64 *status, u64 *base_addr)
{
	struct arm_smccc_res res = {};

	arm_smccc_smc(GET_TRNG_STATUS_SVC, 0, 0, 0, 0, 0, 0, 0, &res);
	if (res.a0 != 0) {
		return -EIO;
	}

	*status = res.a1;
	*base_addr = res.a2;
	return 0;
}

/**
 * Read the st rng. We can't do the smc call in probe since the fpga
 * is not loaded.
 *
 * @param dev uboot device handle.
 * @param data buffer to store random value to.
 * @param len length of the buffer.
 *
 * Returns 0 on success, or error code on failure.
 */
static int sx_rng_read(struct udevice *dev, void *data, size_t len)
{
	size_t i, bytes_read = 0;
	u32 rd;
	u64 status, base_addr;
	int res;

	if (len < sizeof(u16))
		return -EINVAL;

	res = smc_read_trng_status(&status, &base_addr);
	if (res)
		return -ENODEV;
	if (status)
		return -ENXIO;

	/*
	 * The RNG provides 16 bit values,
	 * so we read back as many u16s as will fit in data.
	 */
	while (bytes_read < (len - 1)) {
		rd = readl(base_addr + SX_ZYNQMP_RNG_DATA_REG);
		if (rd & SX_RNG_FIFO_EMPTY) {
			for (i = 0; i < RNG_RETRY; i++) {
				udelay(1);
				rd = readl(base_addr +
							SX_ZYNQMP_RNG_DATA_REG);
				if (!(rd & SX_RNG_FIFO_EMPTY))
					break;
			}
		}

		/* Consider the BAD_ENTROPY as an error */
		if (rd & SX_RNG_BAD_ENTROPY) {
			return -EIO;
		}

		/* If fifo is empty */
		if (rd & SX_RNG_FIFO_EMPTY)
			return -EAGAIN;

		/*
		 * The random value is the lower 16 bits
		 * of the data register
		 */
		((char *)data)[bytes_read]   = (u8)((rd >> 0) & 0xFF);
		((char *)data)[bytes_read + 1] = (u8)((rd >> 8) & 0xFF);

		bytes_read += sizeof(u16);
	}
	return 0;
}

static const struct dm_rng_ops sx_rng_ops = {
	.read = sx_rng_read,
};

static const struct udevice_id sx_rng_match[] = {
	{
		.compatible = "sx,zynqmp_trng-1.00.a",
	},
	{},
};

U_BOOT_DRIVER(sandbox_rng) = {
	.name = "sx-rng",
	.id = UCLASS_RNG,
	.of_match = sx_rng_match,
	.ops = &sx_rng_ops,
};
