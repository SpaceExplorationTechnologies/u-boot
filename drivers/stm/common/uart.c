/*
 *  Copyright (C) 2014 STMicroelectronics Limited
 *     Ram Dayal <ram.dayal@st.com>
 *
 * SPDX-License-Identifier:     GPL-2.0+
 *
 */

#include <stm/soc.h>

extern void stm_uart_init(struct stm_uart_config *config)
{
	/* Route SBC_UART0 via PIO3 for TX, RX, CTS & RTS (Alternative #1) */
	PIOALT(config->output.port, config->output.pin,
	       config->output.alt,
	       stm_pad_direction_output); /* SBC_UART0-TX */

	PIOALT(config->input.port, config->input.pin,
	       config->input.alt,
	       stm_pad_direction_input); /* SBC_UART0-TX */
}
