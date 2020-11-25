/**
 * Common code for SpaceX boards.
 */

#include <common.h>
#include <fdt_support.h>
#include <rdate.h>
#include <spacex/common.h>
#include <spacex/ecc.h>
#ifdef CONFIG_SPACEX_POR_GPIO
#include <spacex/por-gpios.h>
#endif
#include <spacex/simple_mmap.h>
#include <version.h>


/**
 * Display the SpaceX logo.
 *
 * Return: always 0.
 */
int spacex_splash(void)
{
	char config_name[] = CONFIG_SYS_CONFIG_NAME;
	unsigned int i;

	for (i = 0; config_name[i] != '\0'; i++)
		if (config_name[i] == '_')
			config_name[i] = ' ';

	printf("\n");
	printf("                                                          *\n");
	printf("                                                 +         \n");
	printf("                                       +    +              \n");
	printf("                                +     +                    \n");
	printf("                           +      +                        \n");
	printf("+ + + + +              +     +                             \n");
	printf("  +        +       +     +                                 \n");
	printf("     +       + +      +                                    \n");
	printf("        +   +      +                                       \n");
	printf("          +      + +                                       \n");
	printf("      +      +        +                                    \n");
	printf("   +       +    +        +                                 \n");
	printf(" +       +         +        +                              \n");
	printf("+ + + + +             + + + + +                            \n");
	printf("\n");
	printf("Board: %s\n", config_name);
	printf("\n");

	return 0;
}

/**
 * fdt_error() - Display an error accessing a property. Report the
 * path of the node if possible.
 *
 * @fdt:	A pointer to the Flattened Device Tree.
 * @node:	The node containing the property.
 * @property:	The property name or NULL if not applicable.
 * @message:	The printf-like error message to display.
 * @...:	Arguments for the message.
 */
void fdt_error(const void *fdt, int node, const char *property,
	       const char *message, ...)
{
	assert(message != NULL);

	va_list va;
	va_start(va, message);

	/* Arbitrary buffer size. */
	char path[256] = { 0 };

	/*
	 * Try to display a message with the node path, do without if
	 * it cannot.
	 */
	if (!fdt_get_path(fdt, node, path, sizeof(path))) {
		printf("%s", path);
	}
	if (property) {
		printf("/%s", property);
	}
	printf(": ");
	vprintf(message, va);

	va_end(va);
}

/**
 * spacex_check_reg() - Check that a base address read from a 'reg'
 * node can be represented using a virtual memory pointer.
 *
 * @base_addr:	The base address read from the device tree/
 * @base:	A pointer to store the virtual memory pointer.
 *
 * Return: true if the address can be represented as a virtual memory
 * pointer, false otherwise.
 */
bool spacex_check_reg(phys_addr_t base_addr, void **base)
{
	const uintptr_t virt = (uintptr_t)base_addr;
	if (virt != base_addr)
		return false;

	*base = (void *)virt;
	return true;
}

/**
 * Pass information to Linux via the /chosen node of the device tree.
 *
 * @fdt:	A pointer to the Flattened Device Tree.
 */
void spacex_populate_fdt_chosen(void *fdt)
{
	/*
	 * Pass network parameters, collected from a few environment
	 * variables.
	 *
	 * If for some reason no prime Ethernet device was found, try
	 * passing the currently active device.
	 */
	fdt_add_env_to_chosen(fdt, "ipaddr", NULL);

	if (env_get("hostname"))
		fdt_add_env_to_chosen(fdt, "hostname", "hostname");

	if (env_get("ethprime") != NULL)
		fdt_add_env_to_chosen(fdt, "ethprime", NULL);
	else
		fdt_add_env_to_chosen(fdt, "ethprime", "ethact");

	/*
	 * Pass time parameters obtained via RDATE.
	 */
	uint64_t cpu_boot_time_in_unix_ns = get_cpu_boot_time_in_unix_ns();
	if (cpu_boot_time_in_unix_ns > 0) {
		fdt_add_uint64_to_chosen(fdt, "cpu-boot-time-unix-ns",
					 cpu_boot_time_in_unix_ns);
		fdt_add_uint32_to_chosen(fdt, "utc-offset", get_utc_offset());
	}

	/*
	 * Pass the number of ECC errors detected during loading of
	 * the environment, device tree, and kernel image.
	 */
	fdt_add_uint32_to_chosen(fdt, "uboot-ecc-errors",
				 num_ecc_errors);

	/*
	 * Pass version information for version_info.
	 */
	fdt_add_string_to_chosen(fdt, "uboot-version", PLAIN_VERSION);
#ifdef CONFIG_SYS_CONFIG_NAME
	fdt_add_string_to_chosen(fdt, "uboot-config", CONFIG_SYS_CONFIG_NAME);
#endif
	fdt_add_string_to_chosen(fdt, "sx-uboot-commit", SPACEX_COMMIT);
	fdt_add_string_to_chosen(fdt, "sx-uboot-branch", SPACEX_BRANCH);
	fdt_add_string_to_chosen(fdt, "sx-uboot-build", SPACEX_BUILD);

	if (env_get("serialnum"))
		fdt_add_env_to_chosen(fdt, "assembly_serialnum", "serialnum");

	/*
	 * Add board serial if it exists.
	 */
	if (env_get("boardserialnum"))
		fdt_add_env_to_chosen(fdt, "board_serialnum", "boardserialnum");

	/*
	 * Add vehicleid if it exists.
	 */
	if (env_get("vehicleid"))
		fdt_add_env_to_chosen(fdt, "vehicleid", "vehicleid");

	if (env_get("slotid"))
		fdt_add_env_to_chosen(fdt, "slotid", "slotid");

	/*
	 * Add any board specific data.
	 */
	spacex_board_populate_fdt_chosen(fdt);
}

/**
 * Pass board specific information to Linux via the /chosen node of the device
 * tree.
 *
 * This default implementation does nothing, but is defined with weak linkage to
 * allow it to be overridden by board specific implementations.
 *
 * @fdt:	A pointer to the Flattened Device Tree.
 */
__weak void spacex_board_populate_fdt_chosen(void *fdt)
{
}

__weak const struct board_ops *spacex_board_init(void *fdt)
{
    return NULL;
}
/**
 * Perform initializations that specificly require the the device tree
 * such as external bus setup, FPGA reset, and init-writes.
 *
 * @fdt:	A pointer to the Flattened Device Tree.
 *
 * Return: always 0.
 */
void spacex_init(void *fdt)
{
	if (fdt == NULL) {
		pr_err("Invalid device tree address.");
		return;
	}

	/*
	 * Display the platform description from the Device Tree.
	 */
	int root_node;
	root_node = fdt_path_offset(fdt, "/");

	const char *description;
	description = fdt_getprop(fdt, root_node, "description", NULL);
	if (description != NULL)
		printf("Loaded Device Tree for '%s'.\n", description);

	/*
	 * Call the board-specific initialization, setting up things
	 * such as the bus controller.
	 */
	const struct board_ops *board_ops;
	board_ops = spacex_board_init(fdt);

#ifdef CONFIG_SPACEX_POR_GPIO
	/*
	 * Execute Power-on Reset (PoR) GPIO actions. This is done
	 * after the bus controller is setup in case we need to reset
	 * devices that are on the bus.
	 */
	spacex_por_gpios(fdt, "por-gpios");
#endif

	/*
	 * Perform simple-mmap initial reads/writes. This is done last
	 * since it may depend on the bus controller being setup and
	 * the FPGA(s) being reset.
	 */
	spacex_simple_mmap_init(fdt, board_ops, "init-writes", "init-reads");

	/* Other common initialization can go here. */
}

#ifdef CONFIG_SPACEX_ZYNQMP

/**
 * Invoked to perform the required SEM core initialization steps prior to
 * loading the FPGA bitstream.
 */
__weak void spacex_zynqmp_pre_sem_core_init(void)
{
}

/**
 * Invoked to initialize the SEM core once the FPGA bitstream has been loaded.
 *
 * @sem_core_baseaddr: The SEM core base address.
 */
__weak void spacex_zynqmp_sem_core_init(uintptr_t sem_core_baseaddr)
{
}

#endif
