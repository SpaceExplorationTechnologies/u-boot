/*
 * simple-mmap driver code.
 */

#include "common.h"

#include <common.h>
#include <fdt_support.h>
#include <spacex/bus.h>
#include <spacex/common.h>
#include <spacex/simple_mmap.h>
#include <stdbool.h>

/**
 * The 'compatible' string for the simple-mmap driver.
 */
#define SIMPLE_MMAP_COMPATIBLE	"sx,simple-mmap"

/**
 * init_action_t - A function pointer to a callback for parsing init actions.
 *
 * @fdt:	A pointer to the Flattened Device Tree.
 * @node:	The node where the actions are read from.
 * @base:	The base address of the region mapped for the actions.
 * @offset:	The offset from the base address to perform the action.
 * @size:	The size of the region for the actions.
 * @action:	The property to read the actions from.
 * @values:	The parameters for the action.
 * @length:	The number of values for the action.
 * @data:	An opaque pointer passed to the callback.
 */
typedef int (*init_action_t)(const void *fdt, int node, void *base,
			     unsigned int offset, size_t size,
			     const char *action, const u32 *values,
			     size_t length, void *data);

/**
 * Lookup a memory region of index `region` in `node`. Return the base
 * and size of the region.
 *
 * @fdt:	A pointer to the Flattened Device Tree.
 * @node:	The node to lookup the region into.
 * @node_type:	A string representing the type of node (for error output).
 * @region:	The index of the region to lookup.
 * @base:	The base address of the driver.
 * @base_ptr:	A pointer to store the base address of the region.
 * @size_ptr:	A pointer to store the size of the region.
 *
 * Return: true if the region was found, false otherwise.
 */
static bool lookup_region(const void *fdt, int node, const char *node_type,
			  u32 region, void *base, void **base_ptr,
			  unsigned long *size_ptr)
{
	assert(fdt != NULL);
	assert(node >= 0);
	assert(base_ptr != NULL);
	assert(size_ptr != NULL);

	*base_ptr = NULL;
	*size_ptr = 0;

	u32 index = region + 1;
	int child = node;
	int depth = 0;
	do {
		/*
		 * Traverse the following nodes in order. The depth is
		 * relative and is used to detect whether we are going
		 * down into a child node or up to a parent node.
		 */
		child = fdt_next_node(fdt, child, &depth);

		/*
		 * We only count the children of the current node, not
		 * their own children.
		 */
		if (depth == 1) {
			index--;
		}
	} while (child >= 0 && depth > 0 && index);

	/*
	 * If we encountered the end of the tree or got one level up,
	 * then the region does not exist.
	 */
	if (child < 0 || depth < 0) {
		fdt_error(fdt, node, node_type, "Region %u out of range.\n",
			  region);
		return false;
	}

	/*
	 * There is the region. Parse its 'reg', but do not perform
	 * address translation, since we've mapped the driver at a
	 * different address. Instead, just add the offset of the
	 * region to the address of the driver.
	 */
	phys_addr_t offset = 0;
	if (!spacex_fdt_parse_reg(fdt, child, false, &offset, size_ptr)) {
		fdt_error(fdt, child, "reg", "Malformed region.\n");
		return false;
	}
	*base_ptr = (char *)base + offset;

	return true;
}

/**
 * Execute a single 'init-writes' action from the specified node in the
 * specified region. See do_init_writes() for more details.
 *
 * @fdt:	A pointer to the Flattened Device Tree.
 * @node:	The node where the write action is read from.
 * @base:	The base address of the region mapped for the write.
 * @offset:	The offset from the base address to perform the write.
 * @size:	The size of the region for the write.
 * @action:	The property to read the actions from.
 * @writes:	The parameters for the write.
 * @length:	The number of values for the write.
 * @data:	An opaque pointer passed to the callback.
 *
 * Return: a position number of values consumed on success, -1 otherwise.
 */
static int do_init_write(const void *fdt, int node, void *base,
			 unsigned int offset, size_t size, const char *action,
			 const u32 *writes, size_t length, void *data)
{
	assert(fdt != NULL);
	assert(base != NULL);
	assert(size > 0);
	assert(action != NULL);
	assert(writes != NULL);

	const struct board_ops *ops = data;

	/*
	 * The next word is the value to bitwise AND the content of
	 * the register with. The final word is the value to bitwise
	 * OR the result from the previous operation with.
	 */
	if (length < 2) {
		fdt_error(fdt, node, action, "Malformed write.\n");
		return -1;
	}

	u32 and_mask, or_mask;
	and_mask = be32_to_cpu(*writes++);
	or_mask = be32_to_cpu(*writes++);

	/* Write the register. */
	u32 reg;
	reg = ops->read(base, offset);
	reg = (reg & and_mask) | or_mask;
	printf("'init-writes': 0x%p <- 0x%08x.\n", (u8 *)base + offset, reg);
	ops->write(base, offset, reg);

	/* We've consumed 2 words. */
	return 2;
}

/**
 * Execute a single 'init-reads' action from the specified node in the
 * specified region. See do_init_reads() for more details.
 *
 * @fdt:	A pointer to the Flattened Device Tree.
 * @node:	The node where the read action is read from.
 * @base:	The base address of the region mapped for the read.
 * @offset:	The offset from the base address to perform the read.
 * @size:	The size of the region for the read.
 * @action:	The property to read the actions from.
 * @reads:	The parameters for the read.
 * @length:	The number of values for the read.
 * @data:	An opaque pointer passed to the callback.
 *
 * Return: a position number of values consumed on success, -1 otherwise.
 */
static int do_init_read(const void *fdt, int node, void *base,
			unsigned int offset, size_t size, const char *action,
			const u32 *reads, size_t length, void *data)
{
	assert(fdt != NULL);
	assert(base != NULL);
	assert(size > 0);
	assert(action != NULL);
	assert(reads != NULL);

	const struct board_ops *ops = data;

	const char *variables;
	int size_variables = 0;
	variables = fdt_getprop(fdt, node, "variables", &size_variables);
	if (variables == NULL) {
		pr_err("No 'variables' property.");
		return -1;
	}

	/*
	 * The next word is the value to bitwise AND the content of
	 * the register with. The following word is the number of bits
	 * to shift the value right. The final word is the variable
	 * index.
	 */
	if (length < 3) {
		fdt_error(fdt, node, action, "Malformed read.\n");
		return -1;
	}

	u32 and_mask, right_shift, variable_index;
	and_mask = be32_to_cpu(*reads++);
	right_shift = be32_to_cpu(*reads++);
	variable_index = be32_to_cpu(*reads++);

	/* Check the right shift. */
	if (right_shift >= 32) {
		fdt_error(fdt, node, action, "Shift of %d bits is "
			  "invalid.\n", right_shift);
		return -1;
	}

	/* Lookup the variable name. */
	const char *variable = NULL;
	size_t i;
	u32 j;
	for (i = 0, j = 0; i < size_variables;
	     i += (strnlen(&variables[i], size_variables - i) + 1), j++) {
		if (j == variable_index) {
			variable = &variables[i];
			break;
		}
	}
	if (variable == NULL) {
		fdt_error(fdt, node, action, "Variable %u not found\n",
			  variable_index);
		return -1;
	}

	/* Read the register and store the variable. */
	u32 reg, value;
	reg = ops->read(base, offset);
	value = (reg & and_mask) >> right_shift;
	printf("'init-reads': 0x%p 0x%08x, \"%s\" <- 0x%08x.\n",
	       (u8 *)base + offset, reg, variable, value);
	char reg_str[11] = { 0 };
	snprintf(reg_str, sizeof(reg_str), "0x%08x", value);
	env_set(variable, reg_str);

	/* We've consumed 3 words. */
	return 3;
}

/**
 * Execute several actions described in the specified node, for the
 * given region and through a callback.
 *
 * @fdt:		A pointer to the Flattened Device Tree.
 * @node:		The node to read the actions from.
 * @base:		The base address of the region mapped for the actions.
 * @size:		The size of the region for the actions.
 * @action:		The property to read the actions from.
 * @handle_regions:	Whether or not to use regions.
 * @callback:		The callback to use for execution each action.
 * @data:		An opaque pointer passed to the callback.
 */
static void do_init_action(const void *fdt, int node, void *base, size_t size,
			   const char *action, bool handle_regions,
			   init_action_t callback, void *data)
{
	assert(fdt != NULL);
	assert(node >= 0);
	assert(base != NULL);
	assert(size > 0);
	assert(action != NULL);

	const u32 *values;
	int size_values = 0;
	values = fdt_getprop(fdt, node, action, &size_values);
	if (values == NULL)
		return;
	if ((size_values % sizeof(u32)) != 0 || size_values == 0) {
		fdt_error(fdt, node, action, "Malformed property.\n");
		return;
	}

	size_t length_values;
	length_values = size_values / sizeof(u32);
	while (length_values) {
		void *region_base = NULL;
		unsigned long region_size = 0;

		if (handle_regions) {
			/*
			 * When handling regions in the simple-mmap
			 * driver, the 1st word is the region index to
			 * lookup the region within the node.
			 */
			u32 region;
			region = be32_to_cpu(*values++);
			length_values--;

			if (!lookup_region(fdt, node, action, region, base,
					   &region_base, &region_size)) {
				/* The error was displayed in lookup_region(). */
				break;
			}

			/* Make sure the region is valid. */
			if ((uintptr_t)region_base < (uintptr_t)base ||
			    (uintptr_t)region_base + region_size >
			    (uintptr_t)base + size) {
				pr_err("Region %u for driver '%s' is invalid.",
				      region, fdt_get_name(fdt, node, NULL));
				break;
			}
		} else {
			region_base = base;
			region_size = size;
		}

		/*
		 * The next word is the offset of the register in the
		 * region.
		 */
		if (length_values == 0) {
			fdt_error(fdt, node, action, "Malformed read.\n");
			break;
		}

		u32 offset;
		offset = be32_to_cpu(*values++);
		length_values--;

		/*
		 * Check the value for alignment and upper bound of
		 * the register space.
		 */
		if ((offset % sizeof(u32))) {
			fdt_error(fdt, node, action, "Offset 0x%x is unaligned."
				  "\n", offset);
			break;
		}
		if (region_size && offset >= region_size) {
			fdt_error(fdt, node, action, "Offset 0x%x is out of "
				  "range.\n", offset);
			break;
		}

		int ret;
		ret = callback(fdt, node, region_base, offset, region_size,
			       action, values, length_values, data);
		if (ret < 0) {
			/* The error was displayed in the callback. */
			break;
		}
		assert(ret <= length_values);
		values += ret;
		length_values -= ret;
	}
}

/**
 * Execute the 'init-reads' in the specified node, using the given
 * register space. When `handle_regions` is false, for example when
 * processing the Global Utilites initial reads, the 'init-reads'
 * property specifies an offset, followed by the AND mask, the right
 * shift and a variable index from the 'variables' list. When
 * `handle_regions` is set to true, in the case of the simple-mmap
 * drivers, the 'init-reads` property specifies a region and an
 * offset, followed by the AND mask, the right shift and variable
 * index. The region is the index of the child node following the
 * 'init-reads' node, and this node contains a smaller register
 * window.
 *
 * @fdt:		A pointer to the Flattened Device Tree.
 * @node:		The node to read the 'init-reads' property from.
 * @base:		The base of the register space.
 * @size:		The size of the register space.
 * @action:		The property to read the actions from.
 * @handle_regions:	Whether or not to use regions.
 * @data:		An opaque pointer to pass to the callback.

 */
static void do_init_reads(const void *fdt, int node, void *base, size_t size,
			  const char *action, bool handle_regions, void *data)
{
	assert(fdt != NULL);
	assert(base != NULL);
	assert(size > 0);
	assert(action != NULL);

	do_init_action(fdt, node, base, size, action, handle_regions,
		       do_init_read, data);
}

/**
 * Execute the 'init-writes' in the specified node, using the given
 * register space. When `handle_regions` is false, for example when
 * processing the Global Utilites initial writes, the 'init-writes'
 * property specifies an offset, followed by the AND mask and the OR
 * mask. When `handle_regions` is set to true, in the case of the
 * simple-mmap drivers, the 'init-writes` property specifies a region
 * and an offset, followed by the AND mask and the OR mask. The region
 * is the index of the child node following the 'init-writes' node,
 * and this node contains a smaller register window.
 *
 * @fdt:		A pointer to the Flattened Device Tree.
 * @node:		The node to read the 'init-writes' property from.
 * @base:		The base of the register space.
 * @size:		The size of the register space.
 * @action:		The property to read the actions from.
 * @handle_regions:	Whether or not to use regions.
 * @data:		An opaque pointer to pass to the callback.
 */
static void do_init_writes(const void *fdt, int node, void *base, size_t size,
			   const char *action, bool handle_regions, void *data)
{
	assert(fdt != NULL);
	assert(base != NULL);
	assert(size > 0);
	assert(action != NULL);

	do_init_action(fdt, node, base, size, action, handle_regions,
		       do_init_write, data);
}

/**
 * Initialize a region, executing the 'init-writes' and 'init-reads'
 * described in the Device Tree. This is intended for use with regions
 * of the SoC that are already mapped (like CCSR on mpx85xx).
 *
 * @fdt:	A pointer to the Flattened Device Tree.
 * @ops:	The region operations descriptor.
 */
void spacex_region_init(const void *fdt, const struct board_ops *board_ops,
			const struct region_props *ops)
{
	assert(ops != NULL);

	if (fdt == NULL) {
		pr_err("No device tree - cannot initialize %s.", ops->name);
		return;
	}
	if (board_ops == NULL) {
		pr_err("Board does not support region initialization - "
		      "cannot initialize %s", ops->name);
		return;
	}

	int region;
	region = fdt_node_offset_by_compatible(fdt, -1, ops->compatible);
	if (region < 0)
		return;

	printf("Setting up %s...\n", ops->name);

	/* Find the base address and size for the region. */
	phys_addr_t base_addr = 0;
	void *base = NULL;
	unsigned long size = 0;
	if (!spacex_fdt_parse_reg(fdt, region, true, &base_addr, &size) ||
	    !spacex_check_reg(base_addr, &base)) {
		/*
		 * Use the default if no address is specified in the
		 * Device Tree, or the address does not look
		 * identity-mapped.
		 */
		base = (void *)ops->base;
		printf("Using default base address 0x%p for %s.\n", base,
		       ops->name);
	}

	/*
	 * We assume that the region is either a system region that is
	 * already mapped by U-Boot, or the region is mapped on the
	 * bus.
	 */

	/* Perform the 'init-writes', then 'init-reads'. */
	do_init_writes(fdt, region, base, size, "init-writes", false, (void *)board_ops);
	do_init_reads(fdt, region, base, size, "init-reads", false, (void *)board_ops);
}

/**
 * Look for simple-mmap drivers and execute their 'init-writes'.
 *
 * @fdt:	A pointer to the Flattened Device Tree.
 */
void spacex_simple_mmap_init(const void *fdt,
			     const struct board_ops *ops,
			     const char *init_writes_name,
			     const char *init_reads_name)
{
	assert(init_writes_name != NULL);
	assert(init_reads_name != NULL);

	if (fdt == NULL) {
		pr_err("No device tree - cannot process the simple-mmap "
		      "drivers.");
		return;
	}

	int node = -1;
	while ((node = fdt_node_offset_by_compatible(
			fdt, node, SIMPLE_MMAP_COMPATIBLE)) >= 0) {
		const char *driver;
		driver = fdt_get_name(fdt, node, NULL);

		printf("Found simple-mmap driver '%s'.\n", driver);

		/*
		 * If there are no init-writes nor init-reads to process,
		 * skip the driver.
		 */
		if (fdt_getprop(fdt, node, init_writes_name, NULL) == NULL &&
		    fdt_getprop(fdt, node, init_reads_name, NULL) == NULL)
			continue;

		if (ops == NULL) {
			pr_err("Board does not support driver initialization - "
			      "cannot process the simple-mmap drivers.");
			return;
		}

		/* Find the base address and size for the driver. */
		phys_addr_t base = 0;
		unsigned long size = 0;
		if (!spacex_fdt_parse_reg(fdt, node, true, &base, &size) ||
		    size == 0) {
			fdt_error(fdt, node, "reg", "Invalid mapping for driver"
				  " '%s'.\n", driver);
			continue;
		}

		/* Map the driver temporarily. */
		void *virt_base = 0;
		if (ops->map) {
			virt_base = ops->virt;
			if (!ops->map(virt_base, base, size, true)) {
				pr_err("Unable to map registers for driver '%s'.",
				      driver);
				return;
			}
		} else {
			virt_base = (void *)((unsigned long)base);
		}

		/* Perform the 'init-writes', then 'init-reads'. */
		do_init_writes(fdt, node, virt_base, size, init_writes_name,
			       true, (void *)ops);

		do_init_reads(fdt, node, virt_base, size, init_reads_name,
			      true, (void *)ops);

		if (ops->unmap)
			ops->unmap(virt_base, size);
	}
}
