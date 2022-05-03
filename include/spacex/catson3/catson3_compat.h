/*
 * Header containing Catson Cut3 compatibility declarations.
 */
#ifndef __SPACEX_CATSON3_COMPAT_H
#define __SPACEX_CATSON3_COMPAT_H
#include "configs/spacex_catson_boot.h"

void configure_modem_v3(int enable_tx, int enable_rx);
void configure_scp_v3(int scp_mapping);

#ifdef CONFIG_SPACEX_CATSON_MODEMLINK
int modemlink_check_status_v3(bool upstream);
void print_mdml_status_v3(bool upstream_miphy_enabled, bool downstream_miphy_enabled);
void configure_cfe_upstream_v3(const struct panel_entry *catson_entry);
void configure_cfe_downstream_v3(void);
#endif /* CONFIG_SPACEX_CATSON_MODEMLINK */

#endif /*__SPACEX_CATSON3_COMPAT_H*/