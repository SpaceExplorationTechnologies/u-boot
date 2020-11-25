/*
 *
 * Imran Khan <imran.khan@st.com>
 * Copyright (c) 2014-2015 STMicroelectronics.
 *
 * SPDX-License-Identifier:	GPL-2.0+
 *
 */

int timer_init(void)
{
	unsigned long cntp_ctl_el0;
	unsigned long cntp_tval_el0 = 0xffffffffffffffff;

	/* TimeStamp generator already enabled by BL31 */

	/* First set the compare register to 0xfffffffffffff  */
	asm volatile("msr cntp_tval_el0, %0" : : "r"(cntp_tval_el0));

	/* Enable the timer */
	asm volatile("mrs %0, cntp_ctl_el0" : "=r"(cntp_ctl_el0));
	cntp_ctl_el0 |= 1;
	asm volatile("msr cntp_ctl_el0, %0" : : "r"(cntp_ctl_el0));

	return 0;
}
