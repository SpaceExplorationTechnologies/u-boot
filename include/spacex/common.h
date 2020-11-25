/**
 * Common functions for SpaceX boards.
 */

#ifndef SPACEX_COMMON_H
#define SPACEX_COMMON_H

#include <common.h>

/**
 * struct board_ops - Operations to map/unmap memory at runtime.
 * @virt:     	Default virtual base address.
 * @map:	Function to map a memory region.
 * @unmap:	Function to unmap a memory region.
 * @read:	Function to read a 32-bit integer.
 * @write:	Function to write a 32-bit integer.
 */
struct board_ops {
	void *virt;
	bool (*map)(const void *virt, phys_addr_t phys, size_t size, bool io);
	void (*unmap)(const void *virt, size_t size);
	u32 (*read)(void __iomem *base, unsigned int offset);
	void (*write)(void __iomem *base, unsigned int offset, u32 value);
};

int spacex_splash(void);

void spacex_init(void *fdt);

void spacex_populate_fdt_chosen(void *fdt);
void spacex_board_populate_fdt_chosen(void *fdt);

/* This callback must be defined for all SpaceX boards. */
const struct board_ops *spacex_board_init(void *fdt);

/* This callback is for Intel ATOM processors. */
const int spacex_fsp_mdp_config(const void *fdt, int fsp_node);

#ifdef CONFIG_SPACEX_ZYNQMP
void spacex_zynqmp_pre_sem_core_init(void);
void spacex_zynqmp_sem_core_init(uintptr_t sem_core_baseaddr);
#endif

#endif  /* !SPACEX_COMMON_H */
