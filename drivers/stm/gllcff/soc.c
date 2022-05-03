/*
 * Copyright (c) 2015 STMicroelectronics.
 *
 * SPDX-License-Identifier:	GPL-2.0+
 *
 */

#include <asm/io.h>
#include <common.h>
#include <stm/pio-control.h>
#include <stm/soc.h>
#include <linux/bug.h>
#include <linux/compat.h>

void reset_cpu(ulong addr)
{
	unsigned int *sysconfReg;
	unsigned int sysconf;

	printf("Resetting system...\n");
	sysconfReg = (unsigned int *)GLLCFF_SYSCFG(SW_GLOBAL_RESET);
	sysconf = readl((uintptr_t)sysconfReg);
	SET_SYSCONF_BIT(sysconf, 0, 0);
	writel(sysconf, sysconfReg);

	while (1) {
	}
}

/*
 * PIO alternative Function selector
 */
void stm_pioalt_select(int port, const int pin, const int alt)
{
	unsigned int sysconf, *sysconfReg;

	switch (port) {
	case 0 ... 7: /* PIO_FLASH */
		sysconfReg = (unsigned int *)GLLCFF_SYSCFG(SS_FLASH_ALT_FUNC);
		sysconfReg += port;
		break;
	case 10 ... 12: /* PIO_A */
		sysconfReg = (unsigned int *)GLLCFF_SYSCFG(SS_PIO_A_ALT_FUNC);
		sysconfReg += port - 10;
		break;
	case 20 ... 23: /* PIO_B */
		sysconfReg = (unsigned int *)GLLCFF_SYSCFG(SS_PIO_B_ALT_FUNC);
		sysconfReg += port - 20;
		break;
	case 30 ... 32: /* PIO_C */
		sysconfReg = (unsigned int *)GLLCFF_SYSCFG(SS_PIO_C_ALT_FUNC);
		sysconfReg += port - 30;
		break;

	default:
		BUG();
		return;
	}

	sysconf = readl(sysconfReg);
	SET_SYSCONF_BITS(sysconf, 1, pin * 4, (pin * 4) + 3, alt, alt);
	writel(sysconf, sysconfReg);
}

/* Pad configuration */
void stm_pioalt_pad(int port, const int pin,
		    const enum stm_pad_gpio_direction direction)
{
	int bit;
	int oe = 0, pu = 0, od = 0;
	unsigned int sysconf, *sysconfReg;

	/*
	 * NOTE: The PIO configuration for the PIO pins in the
	 * "FLASH Bank" are different from all the other banks!
	 * Specifically, the output-enable pad control register
	 * (SYS_CFG_3040) and the pull-up pad control register
	 * (SYS_CFG_3050), are both classed as being "reserved".
	 * Hence, we do not write to these registers to configure
	 * the OE and PU features for PIOs in this bank. However,
	 * the open-drain pad control register (SYS325G_3060)
	 * follows the style of the other banks, and so we can
	 * treat that register normally.
	 *
	 * Being pedantic, we should configure the PU and PD features
	 * in the "FLASH Bank" explicitly instead using the four
	 * SYS_CFG registers: 3080, 3081, 3085, and 3086. However, this
	 * would necessitate passing in the alternate function number
	 * to this function, and adding some horrible complexity here.
	 * Alternatively, we could just perform 4 32-bit "pokes" to
	 * these four SYS_CFG registers early in the initialization.
	 * In practice, these four SYS_CFG registers are correct
	 * after a reset, and U-Boot does not need to change them, so
	 * we (cheat and) rely on these registers being correct.
	 * WARNING: Please be aware of this (pragmatic) behaviour!
	 */
	int flashSS = 0; /* bool: PIO in the Flash Sub-System ? */

	switch (direction) {
	case stm_pad_direction_input:
		oe = 0;
		pu = 0;
		od = 0;
		break;
	case stm_pad_direction_input_with_pullup:
		oe = 0;
		pu = 1;
		od = 0;
		break;
	case stm_pad_direction_output:
		oe = 1;
		pu = 0;
		od = 0;
		break;
	case stm_pad_direction_bidir_no_pullup:
		oe = 1;
		pu = 0;
		od = 1;
		break;
	default:
		BUG();
		break;
	}

	switch (port) {
	case 0 ... 7: /* PIO_FLASH */
		sysconfReg =
			(unsigned int *)GLLCFF_SYSCFG(SS_FLASH_OUPUT_ENABLE) +
			(port / 4);
		break;

	case 10 ... 12: /* PIO_A */
		port -= 10;
		sysconfReg =
			(unsigned int *)GLLCFF_SYSCFG(SS_PIO_A_OUPUT_ENABLE) +
			(port / 4);
		break;
	case 20 ... 23: /* PIO_B */
		port -= 20;
		sysconfReg =
			(unsigned int *)GLLCFF_SYSCFG(SS_PIO_B_OUPUT_ENABLE) +
			(port / 4);
		break;
	case 30 ... 32: /* PIO_C */
		port -= 30;
		sysconfReg =
			(unsigned int *)GLLCFF_SYSCFG(SS_PIO_C_OUPUT_ENABLE) +
			(port / 4);
		break;

	default:
		BUG();
		return;
	}

	bit = ((port * 8) + pin) % 32;

	/* Set the "Output Enable" pad control unless in FlashSS. */
	if (!flashSS) {
		sysconf = readl(sysconfReg);
		SET_SYSCONF_BIT(sysconf, oe, bit);
		writel(sysconf, sysconfReg);
	}

	/* skip to next set (40 bytes of OE) of syscfg registers */
	sysconfReg += 10;

	/* set the "Pull Up" pad control unless in FlashSS */
	if (!flashSS) {
		sysconf = readl(sysconfReg);
		SET_SYSCONF_BIT(sysconf, pu, bit);
		writel(sysconf, sysconfReg);
	}

	/* skip to next set (40 bytes from PU) of syscfg registers */
	sysconfReg += 10;

	/* set the "Open Drain Enable" pad control */
	sysconf = readl(sysconfReg);
	SET_SYSCONF_BIT(sysconf, od, bit);
	writel(sysconf, sysconfReg);
}

/* PIO retiming setup */
static void stm_pioalt_retime(int port, const int pin,
			      const struct stm_pio_control_retime_config *const cfg,
			      const enum stm_pad_gpio_direction direction)
{
	unsigned int sysconf, *sysconfReg;
	unsigned int innotout = 0;

	switch (direction) {
	case stm_pad_direction_input:
	case stm_pad_direction_input_with_pullup:
		innotout = 1;
		break;
	case stm_pad_direction_output:
	case stm_pad_direction_bidir_no_pullup:
		innotout = 0;
		break;
	default:
		BUG();
		break;
	}

	switch (port) {
	case 10 ... 12:/* PIO_A */
		sysconfReg = (unsigned int *)GLLCFF_SYSCFG(SS_PIO_A_RETIMING);
		sysconfReg += (port - 10) * 8;
		break;
	case 20 ... 23: /* PIO_B */
		sysconfReg = (unsigned int *)GLLCFF_SYSCFG(SS_PIO_B_RETIMING);
		sysconfReg += (port - 20) * 8;
		break;
	case 30 ... 32: /* PIO_C */
		sysconfReg = (unsigned int *)GLLCFF_SYSCFG(SS_PIO_C_RETIMING);
		sysconfReg += (port - 30) * 8;
		break;

	default:
		BUG();
		return;
	}

	sysconfReg += pin;

	/* read the "old" value from the system configuration register */
	sysconf = readl(sysconfReg);

	if (cfg->clk >= 0) { /* map value to 2 adjacent bits */
		SET_SYSCONF_BITS(sysconf, 1, 0, 1, cfg->clk, cfg->clk);
	}

	if (cfg->clknotdata >= 0) {
		SET_SYSCONF_BIT(sysconf, cfg->clknotdata, 2);
	}

	if (cfg->delay >= 0) { /* map value to 4 adjacent bits */
		SET_SYSCONF_BITS(sysconf, 1, 3, 6, cfg->delay, cfg->delay);
	}

	SET_SYSCONF_BIT(sysconf, innotout, 7);

	if (cfg->double_edge >= 0) {
		SET_SYSCONF_BIT(sysconf, cfg->double_edge, 8);
	}

	if (cfg->invertclk >= 0) {
		SET_SYSCONF_BIT(sysconf, cfg->invertclk, 9);
	}

	if (cfg->retime >= 0) {
		SET_SYSCONF_BIT(sysconf, cfg->retime, 10);
	}

	/* write the "new" value to the system configuration register */
	writel(sysconf, sysconfReg);
}

void stm_configure_pios(const struct stm_pad_pin *const pad_config,
			const size_t num_pads)
{
	size_t i;

	/* now configure all the PIOs */
	for (i = 0; i < num_pads; i++) {
		const struct stm_pad_pin *const pad = &pad_config[i];
		const int portno = pad->pio.port;
		const int pinno = pad->pio.pin;

		if (pad->direction == stm_pad_direction_unknown)
			continue; /* skip all "ignored" pads */

		/* select alternate function */
		stm_pioalt_select(portno, pinno, pad->pio.alt);

		if (pad->direction != stm_pad_direction_ignored)
			stm_pioalt_pad(portno, pinno, pad->direction);

		if (pad->retime)
			stm_pioalt_retime(portno, pinno, pad->retime,
					  pad->direction);
	}
}
