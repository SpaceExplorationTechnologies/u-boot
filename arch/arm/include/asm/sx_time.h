/*
 * This file includes architecture-specific support for SpaceX-specific
 * time bootstrapping (see rdate.c here and sx_time_bootstrap.c in Linux).
 *
 * In order for bootstrapping to work, the bootloader must estimate the
 * time that a counter started incrementing at constant rate (called
 * '/chosen/cpu-boot-time-unix-ns' in the device tree).  That counter
 * must continue incrementing at constant rate (i.e., not reset or roll over)
 * up until the point that the Linux counterpart to this code is called.
 */

#ifndef __ARM64_SX_TIME_H
#define __ARM64_SX_TIME_H

#include <asm/barriers.h>
#include <common.h>
#include <compiler.h>

/**
 * arch_counter_get_cntpct() - return ARM physical counter value.
 */
static inline u64 arch_counter_get_cntpct(void)
{
	u64 cval;

	isb();
	asm volatile("mrs %0, cntpct_el0" : "=r"(cval));

	return cval;
}

/**
 * get_raw_ticks() - Returns a raw tick counter for use in time bootstapping.
 *
 * This counter must not be offset by any U-Boot specific values; in other
 * words, it must be a raw value out of hardware.  As noted above, this
 * counter must not be modified or have its rate changed before the Linux
 * code is called.
 */
static inline uint64_t get_raw_ticks(void)
{
	return arch_counter_get_cntpct();
}

#endif /* __ARM64_SX_TIME_H */
