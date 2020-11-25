/**
 * Definitions for handling Power-on Reset (PoR) GPIOs action as
 * defined in the device tree.
 */

#ifndef __SPACEX_POR_GPIOS_H
#define __SPACEX_POR_GPIOS_H

void spacex_por_gpios(const void *fdt, const char *por_gpios_name);

#endif /* !__SPACEX_POR_GPIOS_H */
