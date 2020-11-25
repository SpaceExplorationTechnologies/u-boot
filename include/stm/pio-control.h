/*
 * (c) 2010-2013 STMicroelectronics Limited
 *
 * Author: Pawel Moll <pawel.moll@st.com>
 *         Sean McGoogan <Sean.McGoogan@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#ifndef __INCLUDE_STM_PIO_CONTROL_H
#define __INCLUDE_STM_PIO_CONTROL_H

struct stm_pio_control_retime_config {
	int retime	: 2;
	int clk		: 3; /* was previously "clk1notclk0:2" */
	int clknotdata	: 2;
	int double_edge	: 2;
	int invertclk	: 2;
	int delay	: 5; /* was previously "delay_input:3" */
};

#endif /* __INCLUDE_STM_PIO_CONTROL_H */
