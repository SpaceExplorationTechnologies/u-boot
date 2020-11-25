/*
 * Header file for the bus initialization common code.
 */

#ifndef SPACEX_SIMPLE_MMAP_H_
#define SPACEX_SIMPLE_MMAP_H_

#include <common.h>

/**
 * struct region_props - Properties for initializing a specific region.
 * @name:	Pretty-printable name for the bus.
 * @compatible:	'compatible' property to match on in the device tree.
 * @base:	The base address of the region's register space. Might be
 *		overridden in the device tree.
 */
struct region_props {
	const char *name;
	const char *compatible;
	void *base;
};

extern void spacex_region_init(const void *fdt,
			       const struct board_ops *board_ops,
			       const struct region_props *ops);
extern void spacex_simple_mmap_init(const void *fdt,
				    const struct board_ops *ops,
				    const char *init_writes_name,
				    const char *init_reads_name);

#endif /* !SPACEX_SIMPLE_MMAP_H_ */
