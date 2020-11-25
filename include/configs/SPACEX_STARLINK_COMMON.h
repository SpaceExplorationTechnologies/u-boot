/*
 * Common configuration for SpaceX Starlink Computers.
 */
#ifndef SPACEX_STARLINK_COMMON_H
#define SPACEX_STARLINK_COMMON_H

#define CONFIG_CONSOLE_MUX
#define CONFIG_SPACEX_TELEM

#ifndef CONFIG_TELEM
#define CONFIG_TELEM_DEST_IP "239.26.7.129"
#endif

/*
 * Read stdin, stdout, and stderr device names from environment variables.
 */
#define CONFIG_SYS_CONSOLE_IS_IN_ENV

/*
 * Include nulldev device so that we can use it for stdin below.
 */
#define CONFIG_SYS_DEVICE_NULLDEV

/*
 * Conditionally enables stdin on development hardware, the u-boot command
 * sx_is_prod_hw reads the production fuses on the device and sets is_prod_hw 1
 * if the fuse state indicates production hardware.
 *
 * Note that if sx_is_prod_hw fails for any reason and doesn't define
 * is_prod_hw, the test will compare "x" with "0x" and skip setting stdin.  If
 * and only if sx_is_prod_hw succeeds and the fuse indicates development
 * hardware is stdin enabled.
 */
#define SPACEX_STARLINK_SET_STDIN                                 \
    "sx_is_prod_hw;"                                              \
    "if test \"${is_prod_hw}x\" = \"0x\" -o "                     \
            "\"${modeboot}x\" = \"jtagbootx\"; then "             \
        "setenv stdin serial;"                                    \
    "fi;"

#endif /* SPACEX_STARLINK_COMMON_H */
