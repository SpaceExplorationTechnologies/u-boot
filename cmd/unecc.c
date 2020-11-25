/**
 * Decode ECC-encoded volumes.
 */

#include <common.h>
#include <command.h>
#include <spacex/ecc.h>

/**
 * do_unecc() - Handler for the "unecc" command.
 *
 * @cmdtp:	A pointer to the command structure in the command table.
 * @flag:	Flags, e.g. if the command was repeated with ENTER.
 * @argc:	The number of arguments.
 * @argv:	The command's arguments, as an array of strings.
 *
 * Return: CMD_RET_SUCCESS on success, CMD_RET_USAGE to display the
 * usage, CMD_RET_FAILURE otherwise.
 */
int do_unecc(cmd_tbl_t *cmdtp, int flag, int argc, char *const argv[])
{
	void *data, *dest;
	unsigned long decoded = (unsigned long)-1;

	if (argc != 3)
		return CMD_RET_USAGE;

	data = (void *)simple_strtoul(argv[1], NULL, 16);
	dest = (void *)simple_strtoul(argv[2], NULL, 16);

	if (ecc_decode(data, 0, dest, &decoded, 0, &num_ecc_errors) <= 0)
		return CMD_RET_FAILURE;

	env_set_hex("filesize", decoded);

	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(
	unecc, 3, 0, do_unecc,
	"Unpacks an ECC volume; increments internal ECC error counter on error",
	"<source> <target>\n"
	"\tReturns successfully if the given source was successfully\n"
	"\tunpacked to the target. This will fail if the given source\n"
	"\tis not an ECC volume. It will succeed if bit errors were\n"
	"\tsuccessfully fixed.\n"
	"\t<source> and <target> should both be in hexadecimal.\n"
);
