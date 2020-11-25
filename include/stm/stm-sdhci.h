/*
 * (C) Copyright 2013-2014 STMicroelectronics
 * Youssef TRIKI <youssef.triki@st.com>
 * Sean McGoogan <Sean.McGoogan@st.com>
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __STM_SDHCI_H__
#define __STM_SDHCI_H__

#include <stm/pad.h>
#include <stm/pio.h>
#include <stm/pio-control.h>


struct st_mmc_platform_data {
    void __iomem    *top_ioaddr;
    void __iomem    *regbase;
    void __iomem    *regbase_rst;
    void __iomem    *regbase_clk;
    unsigned long max_frequency;
    unsigned long min_frequency;
    int non_removable;
    int mmc_cap_1p8;
    int mmc_cap_uhs_sdr50;
    int mmc_cap_uhs_sdr104;
    int mmc_cap_uhs_ddr50;
    int mmc_force_tx_dll_step_dly;
    int mmcss_config;
    unsigned int flashss_version;
    unsigned long clock;
    unsigned int quirks;
    unsigned int voltages;
    unsigned int host_caps;
    int gpio_cd;
    int port;
    bool phy_dll;
};

#endif	/* __STM_SDHCI_H__ */

