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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 */

#ifndef __INCLUDE_STM_PAD_H
#define __INCLUDE_STM_PAD_H

#include <common.h>

enum stm_pad_gpio_direction {
	stm_pad_direction_unknown,	    /* oe=?, pu=?, od=? */
	stm_pad_direction_ignored,	    /* oe=?, pu=?, od=? */
	stm_pad_direction_input,	      /* oe=0, pu=0, od=0 */
	stm_pad_direction_input_with_pullup,  /* oe=0, pu=1, od=0 */
	stm_pad_direction_output,	     /* oe=1, pu=0, od=0 */
	stm_pad_direction_output_with_pullup, /* oe=1, pu=1, od=0 */
	stm_pad_direction_bidir_no_pullup,    /* oe=1, pu=0, od=1 */
	stm_pad_direction_bidir_with_pullup   /* oe=1, pu=1, od=1 */
};

struct stm_pad_pin {
	struct {
		const unsigned char port, pin;
		unsigned char alt;
	} pio;
	const union {
		struct { /* bit-fields for the GMAC */
			const char phy_clock : 1;
			const char tx_clock : 1;
			const char txer : 1;
		} gmac;
	} u;
	enum stm_pad_gpio_direction direction;
	const struct stm_pio_control_retime_config *const retime;
};

#endif /* __INCLUDE_STM_PIO_PAD_H */
