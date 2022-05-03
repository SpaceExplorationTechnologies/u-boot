/*
 * Copyright (C) 2013 STMicroelectronics
 *	Sean McGoogan <Sean.McGoogan@st.com>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <asm/global_data.h>
#include <common.h>

DECLARE_GLOBAL_DATA_PTR;

int dram_init(void)
{
	/*
	 * Note: this function should only called when gd->bd == NULL
	 */
	if (gd->bd) {
		printf("%s() called with gd->bd not NULL (%p).\n",
		       __func__, gd->bd);
		return -1;
	}

	gd->ram_size = CONFIG_SYS_SDRAM_SIZE; /* size of DRAM memory in bytes */

	return 0;
}
