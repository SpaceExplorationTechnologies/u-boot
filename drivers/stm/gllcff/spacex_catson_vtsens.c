/*
 * (C) Copyright 2021 SpaceX.
 *
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <common.h>
#include <asm/io.h>
#include <errno.h>
#include <command.h>
#include <linux/delay.h>

/*
 * Maximum number of retries before giving up reading the cluster register.
 */
#define MAX_RETRY 100

/*
 * Offset into BSEC used to read the calibration information.
 */
#define ST_BSEC_OFFSET 0x22400000ULL
#define ST_BSEC_VTSENS_OFFT 0x6C
#define ST_BSEC_TRIMVBE_MASK 0x1FF

#define read_bsec_vtsen() readl(ST_BSEC_OFFSET + ST_BSEC_VTSENS_OFFT)

/*
 * VTSENS specific offsets and registers.
 */
#define VTSENS_OFFSET 0x8031000ULL
#define VTSENS_DYNAMIC_CONFIG_OFFT 0x0
#define VTSENS_CALIBRATION_CONFIG_OFFT 0xC
#define VTSENS_CLUSTER0_OFFT 0x20

#define VTSENS_CLUSTER_DATA_MASK 0x1FFF
#define VTSENS_CLUSTER_OVERFLOW_MASK 0x40000
#define VTSENS_CLUSTER_READY_MASK 0x20000

#define read_vtsens_reg(a) readl(VTSENS_OFFSET + (a))
#define write_vtsens_reg(v, a) writel(v, VTSENS_OFFSET + (a))

/*
 * Temperature value can be calculated as follow:
 * T = (DATA[13:0] * A) / 2^14 - B
 * A = 731, B = 273.4
 */
#define TEMP_FACTOR_A	731
#define TEMP_FACTOR_B	273

/**
 * Reads the die temperature using VTSENS.
 *
 * @temp:	preallocated memory to store temperature in Celsius.
 *
 * Return: -EOVERFLOW if the value did not become ready.
 *         -BUSY if the ready flag never went high.
 *         0 on success.
 */
int read_vtsens_temperature(int *temp)
{
	int retry = 0;
	int ret = -EBUSY;
	*temp = 0;
	const u32 dynamic_config_old_state =
				read_vtsens_reg(VTSENS_DYNAMIC_CONFIG_OFFT);
	const u32 calibration_config_old_state =
				read_vtsens_reg(VTSENS_CALIBRATION_CONFIG_OFFT);

	/*
	 * Read the calibration value from BSEC, and write to vtsens.
	 */
	const u32 vtsens_cal = read_bsec_vtsen() & ST_BSEC_TRIMVBE_MASK;

	write_vtsens_reg(vtsens_cal, VTSENS_CALIBRATION_CONFIG_OFFT);

	/*
	 * Set the dynamic config to enable vtsens.
	 */
	const u32 dynamic_config = 0x20009;

	write_vtsens_reg(dynamic_config, VTSENS_DYNAMIC_CONFIG_OFFT);

	/*
	 * If we read immediately, we get a stale value.
	 */
	mdelay(10);

	while (retry < MAX_RETRY) {
		u32 cluster_value = read_vtsens_reg(VTSENS_CLUSTER0_OFFT);

		if (cluster_value & VTSENS_CLUSTER_OVERFLOW_MASK) {
			/*
			 * ADC overflowed.
			 */
			ret = -EOVERFLOW;
			break;
		}
		if (cluster_value & VTSENS_CLUSTER_READY_MASK) {
			int raw_adc = cluster_value & VTSENS_CLUSTER_DATA_MASK;
			int kelvin = (raw_adc * TEMP_FACTOR_A) >> 14;
			*temp =  kelvin - TEMP_FACTOR_B;
			ret = 0;
			break;
		}
		udelay(1000);
		retry += 1;
	}
	/*
	 * Return the vtsens registers to previous values for Linux.
	 */
	write_vtsens_reg(dynamic_config_old_state,
			 VTSENS_DYNAMIC_CONFIG_OFFT);
	write_vtsens_reg(calibration_config_old_state,
			 VTSENS_CALIBRATION_CONFIG_OFFT);

	return ret;
}

/**
 * CLI interface for checking the die temperature
 *
 * @cmdtp:     UBoot command table.
 * @flag:      Flags for this command.
 * @argc:      The number of user arguments provided.
 * @argv:      The user arguments.
 *
 * Return:     0 on success, <0 on error.
 */
static int do_print_temp_cmd(struct cmd_tbl *cmdtp, int flag, int argc,
			     char * const argv[])
{
	int temp;
	int ret = read_vtsens_temperature(&temp);

	switch (ret) {
	case -EBUSY:
	printf("VTSens did not become ready.\n");
	break;

	case -EOVERFLOW:
	printf("VTSens ADC overflowed.\n");
	break;

	case 0:
	printf("VTSens: %dC.\n", temp);
	break;

	default:
	printf("VTSens unknown error occurred.\n");
	break;
	}
	return ret;
}

U_BOOT_CMD(vtsens, 1, 0, do_print_temp_cmd,
	   "SpaceX VTsens command",
	   "vtsens: Prints the currend die temperature.\n");
