/**
 * Common functions for SpaceX boards.
 */

#ifndef __SPACEX_COMMON_H
#define __SPACEX_COMMON_H

#include <common.h>
#include <fdt_support.h>
#include <stdarg.h>

#ifdef __GNUC__
__attribute__((format (printf, 4, 5)))
#endif /* __GNUC__ */
void fdt_error(const void *fdt, int node, const char *property,
               const char *message, ...);
bool spacex_check_reg(phys_addr_t base_addr, void **base);

#endif /* !__SPACEX_COMMON_H */
