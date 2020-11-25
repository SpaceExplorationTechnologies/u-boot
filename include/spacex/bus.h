/*
 * Header file for the bus initialization common code.
 */

#ifndef SPACEX_BUS_H_
#define SPACEX_BUS_H_

#include <common.h>

#include <stdbool.h>

/**
 * The maximum number of Chip Select per bus. Currently set to 8 to
 * satisfy all of our platforms (P2020 eLBC has 8 Chip Select, P2041
 * eLBC has 4, and P1010 IFC has 4). See the Reference Manuals, in the
 * corresponding chapter (eLBC or IFC).
 */
#define BUS_MAX_NUM_CS		8

/**
 * The maximum number of timing parameters for each chip select.
 */
#define BUS_MAX_EXTRA_TIMINGS	3

/**
 * struct mapping_param - The parameters for a memory mapping.
 * @cs:			The memory mapping Chip Select.
 * @addr:		The base address in physical memory.
 * @size:		The size of the memory mapping.
 * @timing:		The value from the device tree "timings" entry.
 * @extra_timing:	Additional parameters parsed from the device tree.
 */
struct mapping_param {
	unsigned int cs;
	phys_addr_t addr;
	size_t size;
	u32 timing;
	struct {
		/*
		 * @count:	The number of 'values' parsed.
		 * @values:	The values parsed from the device tree.
		 */
		int count;
		u32 values[BUS_MAX_EXTRA_TIMINGS];
	} extra_timing;
};

/**
 * struct bus_ops - Operations for setting up generic peripheral bus.
 * @name:		Pretty-printable name for the bus.
 * @compatible:		'compatible' property to match on in the device tree.
 * @base:		The base address of the bus configuration space.
 *			Might be overridden in the device tree.
 * @system_bus_freq:	The frequency of the system bus.
 * @num_cs:		The number of memory mappings supported by the bus.
 * @default_timings:	The default timing value for each memory mapping.
 * @calc_clkdiv:	Function to calculate the desired clock divisor.
 * @get_clkdiv:		Function to retrieve the value of the clock divisor.
 * @set_clkdiv:		Function to set the value of the clock divisor.
 * @validate_cs:	Function to validate memory mapping parameters.
 * @setup_cs:		Function to program a memory mapping on the controller.
 */
struct bus_ops {
	const char *name;
	const char *compatible;
	void *base;
	unsigned int system_bus_freq;
	unsigned int num_cs;
	u32 default_timings[BUS_MAX_NUM_CS];
	struct {
		const char *name;
		int count;
	} extra_timings;
	bool (*calc_clkdiv)(unsigned int divisor, u32 *clkdiv);
	u32 (*get_clkdiv)(void __iomem *base);
	void (*set_clkdiv)(void __iomem *base, u32 clkdiv);
	bool (*validate_cs)(const struct mapping_param *param);
	void (*setup_cs)(void __iomem *base, const struct mapping_param *param);
};

extern void spacex_bus_init(void *fdt, const struct bus_ops *ops);
extern bool spacex_fdt_parse_reg(const void *fdt, int node, bool translate,
				 phys_addr_t *addr_ptr,
				 unsigned long *size_ptr);
extern bool ispow2(unsigned long n);

#endif /* !SPACEX_BUS_H_ */
