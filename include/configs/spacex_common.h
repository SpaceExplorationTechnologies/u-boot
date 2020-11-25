/**
 * Common definitions for SpaceX board configuration files.
 */

#ifndef __CONFIG_SPACEX_COMMON_H
#define __CONFIG_SPACEX_COMMON_H

/*
 * Additional commands that aren't enabled by default...
 */
#define CONFIG_MD5 1
// #define CONFIG_CMD_REGINFO /* FIXME: Only applies to PPC now */
#define CONFIG_CMD_NXID 1
#define CONFIG_SPACEX_NXID_BAD_OFFSET 1

/*
 * Prevent network inferface cycling. This prevents the primary
 * network inferface's DMA from being left enabled after U-Boot
 * completes, as described in ticket PLAT-935.
 */
#define CONFIG_NET_DO_NOT_TRY_ANOTHER 1

/*
 * Save space by disabling unneeded defaults.
 */
#undef CONFIG_BOOTM_NETBSD
#undef CONFIG_BOOTM_PLAN9
#undef CONFIG_BOOTM_RTEMS
#undef CONFIG_BOOTM_VXWORKS
#undef CONFIG_GZIP
#undef CONFIG_ZLIB
#undef CONFIG_PARTITIONS

/*
 * Have a default name for the device tree image for ease.
 */
#ifndef SX_DTB_IMAGE_FILE
#define SX_DTB_IMAGE_FILE	"device.dtb.ecc"
#endif

/*
 * Have a default name for the OS image for ease.
 */
#ifndef SX_KERN_IMAGE_FILE
#define SX_KERN_IMAGE_FILE	"uImage.ecc"
#endif

/*
 * Have a default TTY device for the kernel command line.
 */
#ifndef SX_LINUX_CONSOLE
#define SX_LINUX_CONSOLE			"ttyS0"
#endif

/*
 * Default booting to quiet mode.
 */
#ifndef SPACEX_KERNEL_VERBOSITY
#define SPACEX_KERNEL_VERBOSITY		"quiet"
#endif

/*
 * Use rdate to synchronize the time.
 *
 * This command repeatedly attempts 'rdate' to a multicast address.
 * Because rdate's timeout is 1.5 seconds (see RDATE_TIMEOUT in rdate.c),
 * we try 8 times, for a total of 12 seconds, before timing out.
 *
 * We also need two successes, if possible, because the first one may
 * be delayed by ARP (since the *response* is unicast).
 *
 * Note that we save off the 'ethact' variable value below and restore it
 * every time we try to query. This is because a failed query may force
 * U-Boot to move to the next interface. Restoring it also has the nice
 * side-effect of forcing autonegotiation again (if available). (Otherwise,
 * the "retries" may go nowhere.)
 *
 * BIG NOTE: If we want to continue attempting to boot even without
 * getting a good time (probably a good idea), then this command
 * *must* return success (see the '&&' after 'run set_time' in
 * 'bootcmd'). Otherwise, a failure here will cause U-Boot to drop to
 * 'tftpsrv' to wait for external commanding.
 * Therefore, we put 'true' at the end to make sure it always returns success.
 */
#ifndef SPACEX_MAX_RDATE_ATTEMPTS
#define SPACEX_MAX_RDATE_ATTEMPTS	0x8
#endif

#define SX_SET_TIME_COMMAND						\
	"setenv rdate_done 0; "						\
	"setenv rdate_attempts 0; "					\
	"setenv rdate_successes 0; "					\
	"setenv old_ethact ${ethact}; "					\
	"while itest ${rdate_done} == 0; do "				\
	"  setexpr rdate_attempts ${rdate_attempts} + 1; "		\
	"  echo rdate attempt 0x${rdate_attempts}...; "			\
	"  setenv ethact ${old_ethact}; "				\
	"  if rdate 224.0.1.1; then "					\
	"    setexpr rdate_successes ${rdate_successes} + 1; "		\
	"  fi; "							\
	"  if itest ${rdate_successes} -ge 2; then "			\
	"    setenv rdate_done 1; "					\
	"  fi; "							\
	"  if itest ${rdate_attempts} -ge ${max_rdate_attempts}; then "	\
	"    setenv rdate_done 1; "					\
	"  fi; "							\
	"done; "							\
	"itest ${rdate_done} == 1"

/*
 * Unconfigured address from https://confluence/display/FSW/IP+Addressing
 * See also /usr/bin/whatami.sh.
 */
#define SPACEX_UNCONFIGURED_IPADDR	172.16.0.254
#ifndef CONFIG_IPADDR
#define CONFIG_IPADDR			SPACEX_UNCONFIGURED_IPADDR
#endif

#define CONFIG_SERVERIP			172.16.1.1
#define CONFIG_GATEWAYIP		172.16.1.1
#define CONFIG_NETMASK			255.240.0.0

/*
 * Miscellaneous configurable options
 */
#undef CONFIG_CLOCKS_IN_MHZ
#define CONFIG_SYS_LONGHELP	1		/* undef to save memory	*/
#define CONFIG_CMDLINE_EDITING 1			/* Command-line editing */

#ifndef CONFIG_SYS_LOAD_ADDR
#define CONFIG_SYS_LOAD_ADDR	0x4000000	/* default load address */
#endif

#ifdef CONFIG_CMD_KGDB
#define CONFIG_SYS_CBSIZE	1024		/* Console I/O Buffer Size */
#else
#define CONFIG_SYS_CBSIZE	256		/* Console I/O Buffer Size */
#endif
/* Print Buffer Size */
#define CONFIG_SYS_PBSIZE	(CONFIG_SYS_CBSIZE + \
				sizeof(CONFIG_SYS_PROMPT)+16)
#define CONFIG_SYS_MAXARGS	16		/* max number of command args */
/* Boot Argument Buffer Size */
#define CONFIG_SYS_BARGSIZE	CONFIG_SYS_CBSIZE

/*
 * For booting Linux, the board info and command line data
 * have to be in the first 64 MB of memory, since this is
 * the maximum mapped by the Linux kernel during initialization.
 */

#ifndef CONFIG_SYS_BOOTMAPSZ
#define CONFIG_SYS_BOOTMAPSZ	(64 << 20)	/* Initial Memory for Linux */
#endif

#define CONFIG_SYS_BOOTM_LEN	(64 << 20)	/* Increase max gunzip size */

#ifdef CONFIG_CMD_KGDB
#define CONFIG_KGDB_BAUDRATE	230400	/* speed to run kgdb serial port */
#endif

/* allow overwrites of e.g. 'ethaddr' variable */
#define CONFIG_ENV_OVERWRITE

/* default location for tftp and bootm */
#define CONFIG_LOADADDR		CONFIG_SYS_LOAD_ADDR

#define CONFIG_BOOTDELAY	0	/* -1 disables auto-boot */

/*
 * Forces the user to type 'falcon' (a highly unlikely string) to
 * enter the bootloader.
 */
#define CONFIG_AUTOBOOT_KEYED		1
#define CONFIG_AUTOBOOT_STOP_STR	"falcon"
#define CONFIG_AUTOBOOT_PROMPT						\
	"======================================\n"			\
	"= Type 'falcon' to stop boot process =\n"			\
	"======================================\n"
#define SPACEX_AUTOBOOT_MOTD						\
	"\n"								\
	"  Run 'load_fdt' to configure the FPGA(s) (if applicable).\n"	\
	"  Run 'load_fpgabs' to bootup the FPGA(s) (if applicable).\n"	\
	"  Run 'load_nxid' to load the board identification (including"	\
	" MAC addresses).\n"						\
	"\n"

#define CONFIG_BAUDRATE		115200

#ifndef CONFIG_REGEX
/*
 * Some boards need regex support to be disabled in order to save some
 * space on the U-Boot binary image. This has the unfortunate
 * side-effect of breaking the behavior of the "eth*addr" environment
 * variables, which are updated once we parse the NXID
 * blob. Statically list the list of variables that call the
 * on_ethaddr callback. We support up to 5 Ethernet devices, and this
 * definition must be extended in order to support more.
 */
#define CONFIG_ENV_CALLBACK_LIST_STATIC					\
	"eth1addr:ethaddr,"						\
	"eth2addr:ethaddr,"						\
	"eth3addr:ethaddr,"						\
	"eth4addr:ethaddr,"
#endif /* !CONFIG_REGEX */

/*
 * Define the base boot arguments without serial options, so platforms
 * with different serial implementations can not take them.
 */
#define SPACEX_BOOTARGS							\
	"rdinit=/usr/sbin/sxruntime_start "				\
	"mtdoops.mtddev=mtdoops "					\
	"console=" SX_LINUX_CONSOLE "," __stringify(CONFIG_BAUDRATE) " "\
	SPACEX_KERNEL_VERBOSITY " "					\
	"alloc_snapshot "						\
	"trace_buf_size=5M "

#define SPACEX_BASE_ENV_SETTINGS					\
	"max_rdate_attempts=" __stringify(SPACEX_MAX_RDATE_ATTEMPTS) "\0" \
	"set_time=" SX_SET_TIME_COMMAND "\0"

#define SPACEX_ENV_SETTINGS						\
	SPACEX_BASE_ENV_SETTINGS					\
	"consoledev=" SX_LINUX_CONSOLE "\0"				\
	"stdin=serial\0"						\
	"stdout=serial\0"						\
	"stderr=serial\0"

/*
 * Fits an IPv4 address and the terminating \0.
 */
#define SPACEX_REMOTE_NAME_LEN		16

/*
 * Triple stringed vehicle node + proxy.
 */
#define SPACEX_MAX_REMOTES		4
#define SPACEX_REMOTE_DELIM		','

#endif /* !__CONFIG_SPACEX_COMMON_H */
