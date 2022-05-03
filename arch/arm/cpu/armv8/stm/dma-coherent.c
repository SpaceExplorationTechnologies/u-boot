/*
 * Copyright (C) 2018 STMicroelectronics
 *
 * SPDX-License-Identifier:     GPL-2.0+
 */

#include <common.h>
#include <linux/compat.h>

static phys_addr_t dma_offset = CATSON_DMA_COHERENT_BASE;

phys_addr_t stm_dma_alloc_coherent(size_t size)
{
	phys_addr_t alloc;

	if (size + dma_offset > (phys_addr_t)CATSON_DMA_COHERENT_BASE +
				(phys_addr_t)CATSON_DMA_COHERENT_SIZE) {
		printf("Failed to allocate DMA region of size %lx\n", size);
		return 0;
	}

	alloc = dma_offset;
	dma_offset += ALIGN(size, PAGE_SIZE);

	return alloc;
}
