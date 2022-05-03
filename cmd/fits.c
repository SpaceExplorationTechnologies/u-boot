/**
 * Access subimage data inside FIT images.
 */

#include <common.h>
#include <command.h>
#include <image.h>

/**
 * do_fits() - Handler for the "fits" command.
 *
 * @cmdtp:	A pointer to the command structure in the command table.
 * @flag:	Flags, e.g. if the command was repeated with ENTER.
 * @argc:	The number of arguments.
 * @argv:	The command's arguments, as an array of strings.
 *
 * Return: CMD_RET_SUCCESS on success, CMD_RET_USAGE to display the
 * usage, CMD_RET_FAILURE (or other non-zero error code) otherwise.
 */
int do_fits(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	const void *fit;
	ulong fit_addr = 0;
	const char *fit_uname = NULL;
	int noffset, cfg_noffset;
	int rc;
	const void *addr;
	size_t size;

	if (argc != 2)
		return CMD_RET_USAGE;

	if (!fit_parse_subimage(argv[1], 0,
				&fit_addr, &fit_uname))
		return CMD_RET_USAGE;

	fit = (const void *)fit_addr;
	debug("%s: subimage '%s' from FIT image at 0x%016lx\n",
	      __func__, fit_uname, fit_addr);
	if (fit_uname == NULL) {
		puts("No FIT subimage unit name\n");
		return CMD_RET_USAGE;
	}

	if (fit_check_format(fit, IMAGE_SIZE_INVAL)) {
		puts("Bad FIT image format\n");
		return CMD_RET_FAILURE;
	}

	cfg_noffset = fit_conf_get_node(fit, NULL);
	if (cfg_noffset < 0) {
		puts("Could not find configuration node\n");
		return CMD_RET_FAILURE;
	}

	debug("   Verifying Hash Integrity ... ");
	if (fit_config_verify(fit, cfg_noffset)) {
		puts("Bad Data Hash\n");
		return CMD_RET_FAILURE;
	}
	debug("OK\n");

	noffset = fit_conf_get_prop_node(fit, cfg_noffset,
					 fit_uname);
	if (noffset < 0) {
		printf("%s: Can't find '%s' FIT subimage\n",
		       __func__, fit_uname);
		return CMD_RET_FAILURE;
	}

	/* verify integrity */
	if (!fit_image_verify(fit, noffset)) {
		printf("%s: Bad hash for '%s' FIT subimage\n",
		       __func__, fit_uname);
		return CMD_RET_FAILURE;
	}

	rc = fit_image_get_data(fit, noffset, &addr, &size);
	if (rc != 0) {
		printf("%s: No 'data' property for '%s' FIT subimage?\n",
		       __func__, fit_uname);
		return rc;
	}

	env_set_addr("fileaddr", addr);
	env_set_hex("filesize", size);

	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(
	fits, 3, 0, do_fits,
	"Provides access to data inside a FIT image.",
	"<addr>:<subimg_uname>\n"
	"\tReturns successfully if the subimage of name <subimg_uname> was\n"
	"\tfound inside the FIT image at address <addr>. <addr> should be in\n"
	"\thexadecimal.\n"
	"\tUpon success, the environment variables 'fileaddr' and 'filesize'\n"
	"\twill be set to the hexadecimal address and hexadecimal size of\n"
	"\tthe subimage in memory, respectively.\n"
);
