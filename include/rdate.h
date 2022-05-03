/**
 * BETTER RDATE functions.
 */

#ifndef __RDATE_H__
#define __RDATE_H__

#include <common.h>

uint64_t get_cpu_boot_time_in_unix_ns(void);
uint64_t get_current_time_ns(void);
uint32_t get_utc_offset(void);

void rdate_start(void);

#endif /* __RDATE_H__ */
