/*
 * Bus initialization common code.
 */

#include "common.h"

#include <common.h>
#include <fdt_support.h>
#include <spacex/bus.h>
#include <stdbool.h>

/**
 * Get an integer property from a node.
 *
 * @fdt:	A pointer to the Flattened Device Tree.
 * @node:	The node to get the property from.
 * @property:	The name of the property to read.
 * @value_ptr:	An optional pointer to store the value of the
 *		property.
 *
 * Return: true if the value exists and was read properly, false
 * otherwise.
 */
static bool fdt_get_int(const void *fdt, int node, const char *property,
			u32 *value_ptr)
{
	assert(fdt != NULL);
	assert(node >= 0);
	assert(property != NULL);

	const u32 *value;
	int size = 0;
	value = fdt_getprop(fdt, node, property, &size);
	if (value != NULL && size == sizeof(u32)) {
		if (value_ptr != NULL)
			*value_ptr = be32_to_cpu(*value);
		return true;
	}
	return false;
}

/**
 * Get the `#address-cells` property from a node.
 *
 * @fdt:	A pointer to the Flattened Device Tree.
 * @node:	The node to get the property from.
 *
 * Return: the value of the `#address-cells` property, or 0 if not
 * found.
 */
static inline u32 fdt_get_address_cells(const void *fdt, int node)
{
	assert(fdt != NULL);

	u32 address_cells;
	if (!fdt_get_int(fdt, node, "#address-cells", &address_cells))
		return 0;
	return address_cells;
}

/**
 * Get the `#size-cells` property from a node.
 *
 * @fdt:	A pointer to the Flattened Device Tree.
 * @node:	The node to get the property from.
 *
 * Return: the value of the `#size-cells` property, or 0 if not found.
 */
static inline u32 fdt_get_size_cells(const void *fdt, int node)
{
	assert(fdt != NULL);

	u32 size_cells;
	if (!fdt_get_int(fdt, node, "#size-cells", &size_cells))
		return 0;
	return size_cells;
}

/**
 * Extract and translate an address from a property (passed as an
 * array of u32), considering the appropriate `#address-cells`.
 *
 * @fdt:		A pointer to the Flattened Device Tree.
 * @node:		The node containing the property.
 * @constraint_node:	The node used to enforce `#address-cells`
 *			(could be the parent node of the node
 *			containing the property).
 * @property:		The name of the property (used for logging).
 * @values:		The value of the property.
 * @length:		The length (in unit of u32) of the property.
 * @translate:		Whether to translate the address using the parents
 *			node or not.
 * @addr_ptr:		A pointer to store the translated address.
 *
 * Return: The number of words consumed in `values`, 0 on error.
 */
static unsigned int fdt_extract_address(const void *fdt, int node,
					int constraint_node,
					const char *property, const u32 *values,
					size_t length, bool translate,
					phys_addr_t *addr_ptr)
{
	assert(fdt != NULL);
	assert(node >= 0);
	assert(constraint_node >= 0);
	assert(values != NULL);
	assert(addr_ptr != NULL);

	*addr_ptr = 0;

	const char *constraint_name = "#address-cells";
	u32 address_cells;
	address_cells = fdt_get_address_cells(fdt, constraint_node);
	if (!address_cells) {
		fdt_error(fdt, node, NULL, "Missing '%s'.\n", constraint_name);
		return 0;
	}

	if (length < address_cells) {
		fdt_error(fdt, node, property, "Expected a value of length "
			  "'%s'.\n", constraint_name);
		return 0;
	}

	if (translate) {
		/*
		 * The fdt_translate_address() functions lacks of
		 * const, however it is safe since it only reads from
		 * the Device Tree.
		 */
		u64 translated;
		translated = fdt_translate_address((void *)fdt, node, values);
		if (translated == (u64)-1) {
			fdt_error(fdt, node, property, "Failed to translate "
				  "address.\n");
			return 0;
		}
		*addr_ptr = (phys_addr_t)translated;
	} else {
		u32 i;
		for (i = 0; i < address_cells; i++) {
			/*
			 * Cast to unsigned long long to make the
			 * compiler happy. At runtime, if phys_addr_t
			 * cannot handle #address-cells > 2, this will
			 * result in an overflow because the device
			 * tree is incorrectly formed.
			 */
			*addr_ptr = (unsigned long long)*addr_ptr << 32;
			*addr_ptr |= be32_to_cpu(*values++);
		}
	}

	return address_cells;
}

/**
 * Extract a size from a property (passed as an array of u32),
 * considering the appropriate `#size-cells`.
 *
 * @fdt:		A pointer to the Flattened Device Tree.
 * @node:		The node containing the property.
 * @constraint_node:	The node used to enforce `#size-cells`
 *			(could be the parent node of the node
 *			containing the property).
 * @property:		The name of the property (used for logging).
 * @values:		The value of the property.
 * @length:		The length (in unit of u32) of the property.
 * @size_ptr:		A pointer to store the extracted size.
 *
 * Return: The number of words consumed in `values`, 0 on error.
 */
static unsigned int fdt_extract_size(const void * fdt, int node,
				     int constraint_node,
				     const char *property, const u32 *values,
				     size_t length, unsigned long *size_ptr)
{
	assert(fdt != NULL);
	assert(node >= 0);
	assert(constraint_node >= 0);
	assert(values != NULL);
	assert(size_ptr != NULL);

	*size_ptr = 0;

	const char *constraint_name = "#size-cells";
	u32 size_cells;
	size_cells = fdt_get_size_cells(fdt, constraint_node);
	if (!size_cells) {
		fdt_error(fdt, node, NULL, "Missing '%s'.\n", constraint_name);
		return 0;
	}

	if (length < size_cells) {
		fdt_error(fdt, node, property, "Expected a value of length "
			  "'%s'.\n", constraint_name);
		return 0;
	}

	if (size_cells > 1) {
		fdt_error(fdt, node, property, "Unsupported '%s' value.\n",
			  constraint_name);
		return 0;
	}

	*size_ptr = (unsigned long)be32_to_cpu(*values);
	return size_cells;
}

/**
 * Parse the `reg` property of a node, considering the appropriate
 * `#address-cells` and `#size-cells`. Extract the translated address
 * and size for the first register space.
 *
 * @fdt:	A pointer to the Flattened Device Tree.
 * @node:	The node to parse the `reg` property from.
 * @translate:	Whether to translate the address using the parents node or not.
 * @addr_ptr:	A pointer to store the translated base address
 *		of the first register space.
 * @size_ptr:	An optional pointer to store the size of the
 *		first register space.
 *
 * Return: true when successfully parsed `reg`, false otherwise.
 */
bool spacex_fdt_parse_reg(const void *fdt, int node, bool translate,
			  phys_addr_t *addr_ptr, unsigned long *size_ptr)
{
	assert(fdt != NULL);
	assert(node >= 0);
	assert(addr_ptr != NULL);

	*addr_ptr = 0;
	if (size_ptr != NULL)
		*size_ptr = 0;

	const u32 *reg;
	int size = 0;
	reg = fdt_getprop(fdt, node, "reg", &size);
	if (reg == NULL || (size % sizeof(u32)) != 0 || size == 0) {
		/* The caller will display an error. */
		return false;
	}
	size_t length;
	length = size / sizeof(u32);

	/* See ePAPR, 2.3.6. */

	int parent_node;
	parent_node = fdt_parent_offset(fdt, node);
	assert(parent_node >= 0);

	unsigned int status;
	status = fdt_extract_address(fdt, node, parent_node, "reg", reg, length,
				     translate, addr_ptr);
	if (status == 0)
		return false;

	reg += status;
	length -= status;

	if (size_ptr != NULL) {
		if (length)
			status = fdt_extract_size(fdt, node, parent_node,
						  "reg", reg, length, size_ptr);
		else
			*size_ptr = 0;
	}

	return status > 0;
}

/**
 * Setup the bus clock divisor from the Device Tree.
 *
 * @fdt:	A pointer to the Flattened Device Tree.
 * @ops:	The bus operations descriptor.
 * @bus:	The bus node in the Flattened Device Tree.
 * @base:	The base address of the bus configuration space.
 */
static void setup_bus_clock(void *fdt, const struct bus_ops *ops,
			    int bus, void *base)
{
	assert(fdt != NULL);
	assert(ops != NULL);
	assert(bus >= 0);
	assert(base != NULL);

	/*
	 * Setup the clock divisor. If there is no value specified in
	 * the device tree, or the value is invalid, or CLKDIV is
	 * already set to the same value, then do nothing.
	 */
	u32 divisor = 0;
	if (!fdt_get_int(fdt, bus, "clock-divisor", &divisor))
		return;

	unsigned int desired_freq;
	desired_freq = DIV_ROUND_CLOSEST(ops->system_bus_freq / divisor,
					 1000000);

	u32 clkdiv = 0;
	if (ops->calc_clkdiv(divisor, &clkdiv)) {
		bool set = false;
		if (ops->get_clkdiv(base) != clkdiv) {
			set = true;
			ops->set_clkdiv(base, clkdiv);
		}
		do_fixup_by_compat_u32(fdt, ops->compatible, "bus-frequency",
				       ops->system_bus_freq / divisor, 1);
		printf("Bus clock %s to %u MHz.\n", set ? "changed" :
		       "already set", desired_freq);
	} else
		fdt_error(fdt, bus, "clock-divisor", "Unsupported value.\n");
}

/**
 * Parse the 'ranges' for the bus. This function exits on the first
 * error, but still puts the correctly parsed ranges in `mappings'.
 *
 * @fdt:		A pointer to the Flattened Device Tree.
 * @ops:		The bus operations descriptor.
 * @bus:		The bus node in the Flattened Device Tree.
 * @mappings:		The descriptor for each memory mapping parsed.
 * @num_mappings_ptr:	A pointer to store the number of memory mappings
 *			successfully parsed.
 */
static void parse_ranges(const void *fdt, const struct bus_ops *ops, int bus,
			 struct mapping_param *mappings,
			 unsigned int *num_mappings_ptr)
{
	assert(fdt != NULL);
	assert(ops != NULL);
	assert(bus >= 0);
	assert(mappings != NULL);
	assert(num_mappings_ptr != NULL);

	*num_mappings_ptr = 0;

	const u32 *ranges;
	int size_ranges = 0;
	ranges = fdt_getprop(fdt, bus, "ranges", &size_ranges);
	if (ranges == NULL)
		return;
	if ((size_ranges % sizeof(u32)) != 0 || size_ranges == 0) {
		fdt_error(fdt, bus, "ranges", "Malformed property.\n");
		return;
	}

	/* Detect duplication using a mask. */
	unsigned int cs_mask = (1 << ops->num_cs) - 1;

	/*
	 * See ePAPR 2.3.5, "The #address-cells and #size-cells
	 * properties are not inherited from ancestors in the device
	 * tree. They shall be explicitly defined.".
	 */
	u32 this_node_address_cells;
	this_node_address_cells = fdt_get_address_cells(fdt, bus);
	if (!this_node_address_cells) {
		fdt_error(fdt, bus, NULL, "Missing '%s'.\n", "#address-cells");
		return;
	}

	int parent_node;
	parent_node = fdt_parent_offset(fdt, bus);
	/* The bus node is never the root node, so we expect success here. */
	assert(parent_node >= 0);

	size_t length_ranges;
	length_ranges = size_ranges / sizeof(u32);
	while (length_ranges) {
		unsigned int status;

		/*
		 * See ePAPR, 2.3.8 and http://devicetree.org/Device_Tree_Usage#Ranges_.28Address_Translation.29
		 */

		/*
		 * The 1st value is the index of the memory bank. But
		 * per Device Tree rules, it is of length
		 * #address-cells. Only account for the 1st word.
		 */
		u32 cs;
		cs = be32_to_cpu(*ranges);
		if (cs >= ops->num_cs) {
			fdt_error(fdt, bus, "ranges", "Chip Select %u is out "
				  "of range.\n", cs);
			return;
		}
		ranges++;
		length_ranges--;

		/* Check for duplicates. */
		if (!((1 << cs) & cs_mask)) {
			fdt_error(fdt, bus, "ranges", "Chip Select %u is "
				  "already set.\n", cs);
			return;
		}
		cs_mask &= ~(1 << cs);

		/* All subsequent words must be 0. */
		u32 i;
		for (i = 1; i < this_node_address_cells; i++) {
			if (*ranges) {
				fdt_error(fdt, bus, "ranges", "Offset for Chip "
					  "Select %u must be 0.\n", cs);
				return;
			}
			ranges++;
			length_ranges--;
		}

		/*
		 * The 2nd value is an address, with length relative
		 * to the parent's #address-cells.
		 */
		phys_addr_t addr = 0;
		status = fdt_extract_address(fdt, bus, parent_node, "ranges",
					     ranges, length_ranges, true,
					     &addr);
		if (!status) {
			/* fdt_extract_address() prints the error */
			return;
		}
		ranges += status;
		length_ranges -= status;

		/* The 3rd is the size, with length #size-cells. */
		unsigned long size = 0;
		status = fdt_extract_size(fdt, bus, bus, "ranges", ranges,
					  length_ranges, &size);
		if (!status) {
			/* fdt_extract_size() prints the error */
			return;
		}
		ranges += status;
		length_ranges -= status;

		/* Record the mapping. */
		mappings[*num_mappings_ptr].cs = cs;
		mappings[*num_mappings_ptr].addr = addr;
		mappings[*num_mappings_ptr].size = size;
		mappings[*num_mappings_ptr].timing =
			ops->default_timings[cs];
		(*num_mappings_ptr)++;
	}
}

/**
 * Parse the 'timings' for the bus. This function exits on the first
 * error, but still puts the correctly parsed timings in `mappings'.
 *
 * @fdt:		A pointer to the Flattened Device Tree.
 * @ops:		The bus operations descriptor.
 * @bus:		The bus node in the Flattened Device Tree.
 * @mappings:		The descriptor for each memory mapping.
 * @num_mappings:	The number of memory mappings.
 * @extra_timings:	If NULL, parse the "timings" device tree entry.
 *			Otherwise, parse the field named by 'extra_timings'
 *			into the 'mappings->extra'.
 * @count:		If 'extra_timings' is not NULL, this is the number of
 *			values for each chip select.
 */
static void parse_timings(const void *fdt, const struct bus_ops *ops, int bus,
			  struct mapping_param *mappings,
			  unsigned int num_mappings, const char *extra_timings,
			  int count)
{
	assert(fdt != NULL);
	assert(ops != NULL);
	assert(bus >= 0);
	assert(mappings != NULL);

	const u32 *timings;
	int size_timings = 0;
	const char *name;

	if (extra_timings != NULL) {
		/* Parse the extra timings field. */
		name = extra_timings;
		assert(count > 0);
		assert(count <= ARRAY_SIZE(mappings[0].extra_timing.values));
	} else {
		/* Parse the regular timings field. */
		name = "timings";
		count = 1;
	}

	timings = fdt_getprop(fdt, bus, name, &size_timings);
	if (timings == NULL)
		return;
	if ((size_timings % sizeof(u32)) != 0 || size_timings == 0) {
		fdt_error(fdt, bus, "timings", "Malformed property.\n");
		return;
	}

	/* Detect duplication using a mask. */
	unsigned int cs_mask = (1 << ops->num_cs) - 1;

	size_t length_timings;
	length_timings = size_timings / sizeof(u32);

	/*
	 * Each entry is count + 1 words: the chip select number and
	 * its parameters.
	 */
	size_t i;
	for (i = 0; i + count + 1 <= length_timings; i += count + 1) {
		u32 cs;
		cs = be32_to_cpu(timings[i]);

		/* Lookup the corresponding Chip Select. */
		unsigned int index;
		for (index = 0; index < num_mappings &&
			     mappings[index].cs != cs; index++)
			;
		if (index == num_mappings) {
			fdt_error(fdt, bus, name, "Chip Select %u not found in "
				  "'ranges'.\n", cs);
			return;
		}

		/* Check for duplicates. */
		if (!((1 << cs) & cs_mask)) {
			fdt_error(fdt, bus, name, "Chip Select %u already set."
				  "\n", cs);
			return;
		}
		cs_mask &= ~(1 << cs);

		/* Record the new timing. */
		if (extra_timings) {
			int j;
			for (j = 0; j < count; j++) {
				mappings[index].extra_timing.values[j] =
					be32_to_cpu(timings[i + j + 1]);
			}
			mappings[index].extra_timing.count = count;
		} else {
			/* The regular "timings" field has only one
			 * parameter for each chip select. */
			mappings[index].timing = be32_to_cpu(timings[i + 1]);
		}
	}

	if (i != length_timings) {
		fdt_error(fdt, bus, name, "Malformed property.\n");
	}
}

/**
 * Setup the memory mappings.
 *
 * @ops:		The bus operations descriptor.
 * @base:		The base address of the bus configuration space.
 * @mappings:		The descriptor for each memory mapping to program.
 * @num_mappings:	The number of memory mappings.
 */
static void setup_mappings(const struct bus_ops *ops, void *base,
			   const struct mapping_param *mappings,
			   unsigned int num_mappings)
{
	assert(ops != NULL);
	assert(base != NULL);
	assert(mappings != NULL);

	unsigned int i;
	for (i = 0; i < num_mappings; i++) {
		/*
		 * This function will display the proper error message
		 * upon failure.
		 */
		if (!ops->validate_cs(&mappings[i])) {
			debug("Skipped Chip Select %u.\n", mappings[i].cs);
			continue;
		}

		debug("Setting up Chip Select %u: addr=0x%08llx, size=%zu, "
		      "timing=%08x.\n", mappings[i].cs,
		      (unsigned long long)mappings[i].addr, mappings[i].size,
		      mappings[i].timing);
		ops->setup_cs(base, &mappings[i]);
	}
}


/**
 * Initialize a bus according to the parameters in the Device Tree.
 *
 * @fdt:	A pointer to the Flattened Device Tree.
 * @ops:	The bus operations descriptor.
 */
void spacex_bus_init(void *fdt, const struct bus_ops *ops)
{
	assert(ops != NULL);

	if (fdt == NULL) {
		pr_err("No device tree - cannot initialize the %s.", ops->name);
		return;
	}

	int bus;
	bus = fdt_node_offset_by_compatible(fdt, -1, ops->compatible);
	if (bus < 0)
		return;

	printf("Setting up the %s...\n", ops->name);

	/* Find the base address for the bus configuration space. */
	phys_addr_t base_addr = 0;
	void *base = NULL;
	if (!spacex_fdt_parse_reg(fdt, bus, true, &base_addr, NULL) ||
	    !spacex_check_reg(base_addr, &base)) {
		/*
		 * Use the default if no address is specified in the
		 * Device Tree, or the address does not look
		 * identity-mapped.
		 */
		base = ops->base;
		printf("Using default base address 0x%p for the %s.\n", base,
		       ops->name);
	}

	/*
	 * Setup the bus clock using the divisor specified in the
	 * Device Tree, if any.
	 */
	setup_bus_clock(fdt, ops, bus, base);

	struct mapping_param mappings[ops->num_cs];
	memset(mappings, 0, sizeof(mappings));
	unsigned int num_mappings = 0;

	/* Parse the memory mappings. */
	parse_ranges(fdt, ops, bus, mappings, &num_mappings);

	/* Parse the memory mappings timings. */
	parse_timings(fdt, ops, bus, mappings, num_mappings, NULL, 0);

	if (ops->extra_timings.name != NULL) {
		parse_timings(fdt, ops, bus, mappings, num_mappings,
			      ops->extra_timings.name,
			      ops->extra_timings.count);
	}

	/* Setup the memory mappings. */
	setup_mappings(ops, base, mappings, num_mappings);
}

/**
 * Test if a number if a power of 2.
 *
 * @n:	The number to test.
 *
 * Return: true if the number if a power of 2, false otherwise.
 */
bool ispow2(unsigned long n)
{
	return (n != 0 && ((n & (n - 1)) == 0));
}
