/*
 * (C) Copyright 2008-2014 STMicroelectronics.
 *
 * Sean McGoogan <Sean.McGoogan@st.com>
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __INCLUDE_STM_SOC_H
#define __INCLUDE_STM_SOC_H

#include <linux/types.h>
#include <stm/pad.h>
#include <stm/socregs.h>
#include <stm/sysconf.h>

struct pio {
	const unsigned char port, pin;
	unsigned char alt;
};

struct stm_uart_config {
	struct pio output;
	struct pio input;
};

struct stm_mmc_pio_getcd {
    int pio_port;
    int pio_pin;
};

void stm_pioalt_pad(int port, const int pin,
		    const enum stm_pad_gpio_direction direction);
void stm_pioalt_select(const int port, const int pin, const int alt);
#define STM_PIOALT_SELECT(TRIPLE) stm_pioalt_select(TRIPLE)

/*
 * SPI_NOR_1 use CS=0 of SFC connected on bus=0. Use 'sf probe 0:0' or
 * 'sf probe' to probe it.
 */
#define SPI_NOR_1 (1 << 1)
/*
 * SPI_NOR_1 use CS=0 of SFC connected on bus=1. Use 'sf probe 1:0' to probe
 * it.
 */
#define SPI_NOR_2 (1 << 2)

void stm_configure_spi_flashSS(const int ident);

/*
 * Common functions for STMicroelectronics' SoCs.
 */
int arch_cpu_init(void);

void stm_uart_init(struct stm_uart_config *config);
void stm_configure_pios(const struct stm_pad_pin *const pad_config,
			const size_t num_pads);
void stm_configure_i2c(const struct pio *scl, const struct pio *sda);

extern struct stm_uart_config stm_uart_config[];
extern struct stm_mmc_pio_getcd mmc_pio_getcd[];
extern const struct stm_pad_pin stm_mmc_pad_configs[][10];

#endif /* __INCLUDE_STM_SOC_H */
