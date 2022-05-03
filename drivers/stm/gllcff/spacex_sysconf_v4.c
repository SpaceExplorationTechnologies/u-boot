/*
 * SpaceX code for initializing Catson Cut 4 sysconf.
 */
#include <asm/io.h>
#include <common.h>
#include <linux/delay.h>

/* Generated registers */
#include <spacex/catson4/network_cfe_common.h>
#include <spacex/catson4/network_common.h>
#include <spacex/catson4/backbone_syscfg_000_799.h>
#include <spacex/catson4/modem_syscfg_2000_2799.h>
#include <spacex/catson4/modem_adc_if.h>
#include <spacex/catson4/modem_dac_if.h>

#include <spacex/catson4/catson4_compat.h>
/* Physical base addresses. */
#define BACKBONE_SYSCFG_000_799_BASE 0x09000000u
#define MODEM_SYSCFG_2000_2799_BASE 0x0D000000u
#define MODEM_DAC_IF_SYSCFG_DAC_INTERFACE_BASE 0x0C300000u
#define MODEM_ADC_IF_SYSCFG_ADC_INTERFACE_BASE 0x0C400000u
#define CUT2_NETWORK_CFE_COMMON_BASE 0x0C210000u
#define CUT2_NETWORK_COMMON_BASE 0x0C208000u

/**
 * Configure the Catson Modem.
 *
 * @enable_tx: Should the modem TX be enabled.
 * @enable_rx: should the modem RX be enabled.
 */
void configure_modem_v4(int enable_tx, int enable_rx, int enable_l3_cfe, int use_modem_rx_pll)
{
	unsigned int timeout_us;

	volatile MODEM_SYSCFG_2000_2799_SYSCFG_2000_2799_t *syscfg_regs =
		(volatile MODEM_SYSCFG_2000_2799_SYSCFG_2000_2799_t *)
			MODEM_SYSCFG_2000_2799_BASE;

	volatile MODEM_DAC_IF_SYSCFG_DAC_INTERFACE_t *dac_if_regs =
		(volatile MODEM_DAC_IF_SYSCFG_DAC_INTERFACE_t *)
			MODEM_DAC_IF_SYSCFG_DAC_INTERFACE_BASE;

	volatile MODEM_ADC_IF_SYSCFG_ADC_INTERFACE_t *adc_if_regs =
		(volatile MODEM_ADC_IF_SYSCFG_ADC_INTERFACE_t *)
			MODEM_ADC_IF_SYSCFG_ADC_INTERFACE_BASE;

	/* Assert modem resets. */
	writel(0x00000000, &syscfg_regs->SYSTEM_CONFIG2000);
	writel(0x00000000, &syscfg_regs->SYSTEM_CONFIG2002);
	writel(0x00000000, &syscfg_regs->SYSTEM_CONFIG2004);
	writel(0x00000000, &syscfg_regs->SYSTEM_CONFIG2006);
	writel(0x00000000, &syscfg_regs->SYSTEM_CONFIG2008);

	if (enable_rx) {
		/* Removing RX isolation. */
		writel(0x00000000, &syscfg_regs->SYSTEM_CONFIG2024);
		/* Initialize ADC PLL. */
		writel(0x00000486, &adc_if_regs->SYSTEM_CONFIG0);
		writel(0x00000158, &adc_if_regs->SYSTEM_CONFIG1);
		writel(0x00000128, &syscfg_regs->SYSTEM_CONFIG2035);
		writel(0x00000080, &adc_if_regs->SYSTEM_CONFIG3);
		writel(0x00000000, &adc_if_regs->SYSTEM_CONFIG2);
		writel(0x00007a17, &adc_if_regs->SYSTEM_CONFIG5);
		writel(0x00035b58, &adc_if_regs->SYSTEM_CONFIG7);

		/*
		 * A note from ST about the ADC delays required:
		 *
		 * If it comes to real-time, by specification, the PLL power-up
		 * and lock time are resp. 1ms and 100us. For the power-up,
		 * there is no indicator to check that start is ok so I don’t
		 * see other alternative to fixed delay, but for the PLL lock,
		 * it is possible to poll lock detect status bit when it turned
		 * to 1.
		 */
		udelay(1000);

		/* Setup Calibration PLL. */
		writel(0x000000FC, &adc_if_regs->SYSTEM_CONFIG4);
		writel(0x000000FD, &adc_if_regs->SYSTEM_CONFIG4);
		writel(0x00007817, &adc_if_regs->SYSTEM_CONFIG5);
		writel(0x00007A17, &adc_if_regs->SYSTEM_CONFIG5);

		/* Wait for ADC PLL lock.  2 second arbitrary timeout */
		timeout_us = 2000000;
		while (!(readl(&adc_if_regs->SYSTEM_STATUS30) & 0x1) &&
		       timeout_us) {
			udelay(100);
			timeout_us -= 100;
		}
		if (!(readl(&adc_if_regs->SYSTEM_STATUS30) & 0x1)) {
			printf("Failed waiting for ADC PLL lock.\n");
		}

		/* Select clock out of adc_ss. */
		writel(SYSTEM_CONFIG2033__SEL_PLL_ADC_NOT_PLL_RX_MODEM_bm,
		       &syscfg_regs->SYSTEM_CONFIG2033);

		if (use_modem_rx_pll) {
			/* Initialize Modem RX PLL. */
			writel(0x00010000, &syscfg_regs->SYSTEM_CONFIG2027);
			writel(0x00008141, &syscfg_regs->SYSTEM_CONFIG2026);

			/* Wait for Modem RX PLL lock. Two second arbitrary timeout. */
			timeout_us = 2000000;
			while (!(readl(&syscfg_regs->SYSTEM_STATUS2455) & 0x1) &&
			       timeout_us) {
				udelay(100);
				timeout_us -= 100;
			}
			if ((readl(&syscfg_regs->SYSTEM_STATUS2455) & 0x1)) {
				/* Select clock out of mdm_rx_pll5000. */
				writel(0x00000000, &syscfg_regs->SYSTEM_CONFIG2033);

				/* Disable ADC PLL. */
				writel(0x000000C7, &adc_if_regs->SYSTEM_CONFIG0);
			} else {
				printf("Failed waiting for Modem RX PLL lock.\n");

				/* Re-enable PLL_MDM_RX_STRB_BYPASS. */
				writel(0x00008145, &syscfg_regs->SYSTEM_CONFIG2026);
			}
		}

		/* Release ADC Resets */
		writel(0x00000001, &syscfg_regs->SYSTEM_CONFIG2012);
		writel(0x00000002, &syscfg_regs->SYSTEM_CONFIG2008);

		writel(0x00000000, &syscfg_regs->SYSTEM_CONFIG2010);
	}

	/* Enable the DAC PLL. Required even in TX mode, see PLAT-3526*/
	writel(SYSTEM_CONFIG2010__DAC_RESET_N_bm
		       | SYSTEM_CONFIG2010__DAC_IF_RST_CONF_N_bm
		       | SYSTEM_CONFIG2010__DAC_IF_RST_IPL_N_bm,
	       &syscfg_regs->SYSTEM_CONFIG2010);
	writel(SYSTEM_CONFIG0__DACPLL_EN_bm, &dac_if_regs->SYSTEM_CONFIG0);

	/* Wait for DAC PLL lock. 2 second timeout chosen arbitrarily */
	timeout_us = 2000000;
	while (!(readl(&dac_if_regs->SYSTEM_STATUS32) & 0x1) && timeout_us) {
		udelay(100);
		timeout_us -= 100;
	}
	if (!(readl(&dac_if_regs->SYSTEM_STATUS32) & 0x1)) {
		printf("Failed waiting for DAC PLL lock.\n");
	}

	/* EPHY configuration. Must play nice with ADC PLL */
	if (enable_rx) {
		writel((0x5 << SYSTEM_CONFIG2035__XCK_SELVLDO_bp)
			       | SYSTEM_CONFIG2035__XCK_PSREN_bm,
		       &syscfg_regs->SYSTEM_CONFIG2035);
	} else {
		writel((0x5 << SYSTEM_CONFIG2035__XCK_SELVLDO_bp)
			       | SYSTEM_CONFIG2035__XCK_PSREN_bm
			       | SYSTEM_CONFIG2035__XCK_CLKRFPLL_PD_bm,
		       &syscfg_regs->SYSTEM_CONFIG2035);
	}

	/* Release the clocks. */
	writel(0x000000ff, &syscfg_regs->SYSTEM_CONFIG2001);
	if (enable_rx) {
		writel(0x0000000b, &syscfg_regs->SYSTEM_CONFIG2003);
	}
	if (enable_tx) {
		writel(0x0000000f, &syscfg_regs->SYSTEM_CONFIG2005);
	}
	writel(0x0000000f, &syscfg_regs->SYSTEM_CONFIG2007);
	writel(0x00000003, &syscfg_regs->SYSTEM_CONFIG2009);

	/* Release reset to /4 clock divider circuit */
	writel(0x01F00000, &syscfg_regs->SYSTEM_CONFIG2013);  // MODEM_XPHY_0_CLKDIV4_0_RST_N

	/*
	 * Enable clocks from XPHY to AAP Aurora.
	 *
	 * NTW_DBF_XPHY_TX_CLK_CLKEN
	 * NTW_DBF_XPHY_RX_CLK_CLKEN
	 * NTW_DBF_XPHY_CLK_BITRATE_DIV64_CLKEN
	 */
	writel(0x01F00007, &syscfg_regs->SYSTEM_CONFIG2013);

	/* Enable clocks from MiPHY to Modemlink Aurora */
	writel(0x00000007, &syscfg_regs->SYSTEM_CONFIG2015);
	writel(0x00000007, &syscfg_regs->SYSTEM_CONFIG2016);

	/*
	 * Release AAP Aurora resets prior to AAP core.
	 *
	 * NTW_DBF_XPHY_TX_RST_N
	 * NTW_DBF_XPHY_RX_RST_N
	 * NTW_DBF_XPHY_RST_BITRATE_DIV64_N
	 */
	writel(0x01FB0007, &syscfg_regs->SYSTEM_CONFIG2013);

	/* Release AAP Aurora init reset last to trigger core reset sequence */
	writel(0x01FF0007, &syscfg_regs->SYSTEM_CONFIG2013);  // NTW_DBF_XPHY_INIT_RST_N

	/* Release system resets. (Includes AAP core) */
	writel(0x000000ef, &syscfg_regs->SYSTEM_CONFIG2000);

	writel(0x00030007, &syscfg_regs->SYSTEM_CONFIG2015);
	writel(0x00030007, &syscfg_regs->SYSTEM_CONFIG2016);

	if (enable_rx) {
		writel(0x0000001f, &syscfg_regs->SYSTEM_CONFIG2002);
	}
	if (enable_tx) {
		writel(0x00000017, &syscfg_regs->SYSTEM_CONFIG2004);
	}
	writel(0x0000000f, &syscfg_regs->SYSTEM_CONFIG2006);
	writel(0x00000007, &syscfg_regs->SYSTEM_CONFIG2008);

	if (enable_tx) {
		/* Enable DAC clock */
		writel(0x00000110, &dac_if_regs->SYSTEM_CONFIG4);
	}

	/* Configure l3 L2 mode to cfe <-> l2 */
	if (enable_l3_cfe) {
		writel(0x800002, 0xc208000);
	}

	/*
	 * Initialize AAP Ethernet.
	 * AAP Ethernet uses non-standard framing, so default register configs are
	 * not appropriate.
	 * From: https://rtm.spacex.corp/docs/catson-4/en/latest/net_symlink/sub/aap_v2/docs/catson_aap/aap_ethernet.html
	 */
	/* tx.static_cfg */
	writel(0x00010a01, 0x0c257000);
	/* tx.ctrl */
	writel(0x00080f03, 0x0c257004);
	/* rx.static_cfg */
	writel(0x00000001, 0x0c257080);
	/* rx.pcs_ctrl */
	writel(0x00020000, 0x0c25708c);
	/* rx.ctrl */
	writel(0x08000003, 0x0c257084);
	/* clear resets */
	writel(0x00080f02, 0x0c257004);
	writel(0x08000002, 0x0c257084);
}

/**
 * Configure the modem SCP.
 *
 * @scp_mapping: scp mapping to use.
 */
void configure_scp_v4(int scp_mapping)
{
	volatile MODEM_SYSCFG_2000_2799_SYSCFG_2000_2799_t *syscfg_regs =
		(volatile MODEM_SYSCFG_2000_2799_SYSCFG_2000_2799_t *)
			MODEM_SYSCFG_2000_2799_BASE;

	volatile BACKBONE_SYSCFG_000_799_t *backbone_regs =
		(volatile BACKBONE_SYSCFG_000_799_t *)
			BACKBONE_SYSCFG_000_799_BASE;

	/* NDIV = 60, BYPASS = 1 */
	writel((60 << SYSTEM_CONFIG2030__PLL_SCP_NDIV_bp) |
	       SYSTEM_CONFIG2030__PLL_SCP_STRB_BYPASS_bm,
	       &syscfg_regs->SYSTEM_CONFIG2030);

	/*
	 * IDF = 2
	 * SCP Mapping from board config
	 */
	writel((2 << SYSTEM_CONFIG2031__PLL_SCP_IDF_bp) |
	       (scp_mapping << SYSTEM_CONFIG2031__SEL_SCP_MAPPING_bp),
	       &syscfg_regs->SYSTEM_CONFIG2031);

	/* Disable fail-safe bits when using SCP-lite */
	if (scp_mapping != scp_lvds_io) {
		writel(SYSTEM_CONFIG17__PAD_ABF_DBF_BANDGAP_LVDS_1V8_2_ENABLE_BG_bm,
		       &backbone_regs->SYSTEM_CONFIG17);
	} else {
		writel(SYSTEM_CONFIG17__PAD_ABF_DBF_BANDGAP_LVDS_1V8_2_ENABLE_BG_bm |
		       SYSTEM_CONFIG17__PD_ABF_DBF_CTRL_DAT_FS_ENABLE_bm |
		       SYSTEM_CONFIG17__PD_ABF_DBF_CTRL_CLK_FS_ENABLE_bm,
		       &backbone_regs->SYSTEM_CONFIG17);
	}

	/* N/A */
	writel(0x00000000, &syscfg_regs->SYSTEM_CONFIG2032);

	/* Release SCP reset. */
	writel(SYSTEM_CONFIG2017__SCP_MASTER_AXICLK_CLKEN_bm |
	       SYSTEM_CONFIG2017__SCP_PLL_CLKDIV8_RST_N_bm |
	       SYSTEM_CONFIG2017__SCP_MASTER_RST_N_bm,
	       &syscfg_regs->SYSTEM_CONFIG2017);
}
