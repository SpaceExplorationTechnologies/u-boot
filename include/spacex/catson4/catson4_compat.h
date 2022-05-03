/*
 * Header containing Catson Cut4 compatibility declarations.
 */
#ifndef __SPACEX_CATSON4_COMPAT_H
#define __SPACEX_CATSON4_COMPAT_H
#include "configs/spacex_catson_boot.h"

void configure_modem_v4(int enable_tx, int enable_rx, int enable_l3_cfe, int use_modem_rx_pll);
void configure_scp_v4(int scp_mapping);

#ifdef CONFIG_SPACEX_CATSON_MODEMLINK
int modemlink_check_status_v4(bool upstream);
void print_mdml_status_v4(bool upstream_miphy_enabled, bool downstream_miphy_enabled);
void configure_cfe_upstream_v4(const struct panel_entry *catson_entry);
void configure_cfe_downstream_v4(void);
#endif /* CONFIG_SPACEX_CATSON_MODEMLINK */

#endif /*__SPACEX_CATSON4_COMPAT_H*/
