/*
 * SpaceX code for initializing Catson sysconf
 */

/*
 * Custom platform initialization.
 *
 * This part of an on-going process to break down and understand the
 * external initialization of IPs not covered by existing ST libraries.
 *
 */

#include <asm/io.h>
#include <common.h>

/* Generated registers */
#include <spacex/network_cfe_common.h>
#include <spacex/network_common.h>
#include <stm/backbone_syscfg_000_799.h>
#include <stm/modem_syscfg_2000_2799.h>
#include <stm/modem_adc_if.h>
#include <stm/modem_dac_if.h>

/* Physical base addresses. */
#define BACKBONE_SYSCFG_000_799_BASE 0x09000000u
#define MODEM_SYSCFG_2000_2799_BASE 0x0D000000u
#define MODEM_DAC_IF_SYSCFG_DAC_INTERFACE_BASE 0x0C300000u
#define MODEM_ADC_IF_SYSCFG_ADC_INTERFACE_BASE 0x0C400000u
#define CUT2_NETWORK_CFE_COMMON_BASE 0x0C210000u
#define CUT2_NETWORK_COMMON_BASE 0x0C208000u

#define SYSTEM_STATUS540_REGISTER 0x091300C8u
#define   SYSTEM_STATUS540_VERSION_SHIFT (28)
#define   SYSTEM_STATUS540_VERSION_MASK	(0xf)

/**
 * Return the hardware based catson cut version embedded in the SYSTEM_STATUS540
 * register.  NOTE: This register is 0 indexed, whereas colloquial usage refers
 * to 1 indexed.  If someone says "Catson Cut 2", they are referring to a value
 * of 1 in this register.
 */
unsigned int catson_get_version(void)
{
	return (readl(SYSTEM_STATUS540_REGISTER) >>
			SYSTEM_STATUS540_VERSION_SHIFT) &
		SYSTEM_STATUS540_VERSION_MASK;
}
unsigned int catson_get_minor_version(void)
{
	return readl(SECURITY_BSEC_DEVICEID0) & 0xf;
}

// Returns true if the PLL locked.
static int start_xphy_pll(int bg_trim_reg)
{
	int pll_locked = 0;

	/* Configure bandgap trim */
	int pll_cfg_ana = readl(0x0C6000C8);
	pll_cfg_ana &= 0xFFFFFFF0;
	pll_cfg_ana |= bg_trim_reg;
	writel(pll_cfg_ana, 0x0C6000C8);

	/* Bring clock slice out of reset to start the PHY */
	writel(0x00000001, 0x0C600000);  // xphycs_sw_rst
	writel(0x00000000, 0x0C600000);

	/* Wait for XPHY pll to lock */
	unsigned int timeout_us = 100000;
	while (timeout_us && !pll_locked) {
		/* Clear the sticky CS_PLL_UNLOCK bit */
		writel(0x0000033C, 0x0C600004);
		writel(0x0000013C, 0x0C600004);

		/*
		 * Wait 1ms for:
		 *  1) The PLL to lock.
		 *  2) The PLL to potentially unlock if it's marginal.
		 */
		udelay(1000);
		timeout_us -= 1000;

		/*
		 * Read the lock status and check that the clock slice is in the
		 * "ON" state.
		 */
		pll_locked = ((readl(0x0C600008) & 0x71) == 0x70);
	}

	return pll_locked;
}

static void configure_xphy(void)
{
	volatile MODEM_SYSCFG_2000_2799_SYSCFG_2000_2799_t *syscfg_regs =
		(volatile MODEM_SYSCFG_2000_2799_SYSCFG_2000_2799_t *)
			MODEM_SYSCFG_2000_2799_BASE;

	/*
	 * Release the resets needed to configure XPHY.
	 *
	 * XPHY_0_PX_RST_TX_DATAPATH_N
	 * XPHY_0_PX_RST_RX_DATAPATH_N
	 * XPHY_0_RST_XPHY_N
	 * XPHY_0_APB_RESET_N
	 */
	writel(0x00F00000, &syscfg_regs->SYSTEM_CONFIG2013);

	/*
	 * Simplified initialization of XPHY. Flight software will re-configure XPHY for mission mode operation.
	 *
	 * Skipping board-specific operating frequency and polarity swaps. We just want to get clocks to Aurora for now.
	 * Skipping setting PLL_CALFREQ_TARGET based on the current die temperature.
	 * Skipping fused bangap trim values.
	 *
	 * See PLAT-4899 for details.
	 */
	writel(0x00000001, 0x0C600000);  // xphycs_sw_rst
	writel(0x0000013C, 0x0C600004);  // XPHYCS_CS_CTRL
	writel(0x001FF8F8, 0x0C600040);  // XPHYCS_COMP_CTRL
	writel(0x2E000000, 0x0C600080);  // XPHYCS_PLL_RATIO
	writel(0x00695000, 0x0C600084);  // XPHYCS_PLL_CFG
	writel(0x1F401F40, 0x0C600088);  // XPHYCS_PLL_CALFREQ
	writel(0x00000000, 0x0C6000C0);  // XPHYCS_CSANA_FUSE_CTRL
	writel(0x00000010, 0x0C6000C8);  // XPHYCS_PLL_CFG_ANA
	writel(0x000000F0, 0x0C600140);  // XPHYCS_CSANA_SPARE_IN
	writel(0x000000FF, 0x0C600404);  // XPHYDS_PLL_CLKREF_FREQ
	writel(0x00000404, 0x0C600408);  // XPHYDS_RX_CONTROL
	writel(0x0004FF04, 0x0C60040C);  // XPHYDS_TX_CONTROL
	writel(0x0025BE75, 0x0C60041C);  // XPHYDS_SPEED_SETTINGS_GEN4
	writel(0x00000009, 0x0C600440);  // XPHYDS_RXTX_BOUNDARY_SEL
	writel(0x000003C3, 0x0C600444);  // XPHYDS_RXTX_BOUNDARY_MAN
	writel(0x00003FC0, 0x0C600474);  // XPHYDS_OFFSET_ALGO_SETTINGS
	writel(0x001C000C, 0x0C6004A8);  // XPHYDS_ANA_FUSE_MAN

	int pll_locked = start_xphy_pll(0x9);

	if (!pll_locked)
	{
		printf("Catson U-Boot XPHY: Retrying PLL Initialization with BG_TRIM=0xF.\n");
		pll_locked = start_xphy_pll(0xF);

		if (!pll_locked)
		{
			printf("Catson U-Boot XPHY: Retrying PLL Initialization with BG_TRIM=0xF, LDO_OUT_SEL=0x2.\n");
			writel(0x00000020, 0x0C6000C8);
			pll_locked = start_xphy_pll(0xF);
		}
	}

	if (!pll_locked) {
		printf("Failed waiting for XPHY PLL lock.\n");
	}
}

static void configure_modem(int enable_tx, int enable_rx)
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
	volatile CFE_COMMON_t *cfe_common_regs =
		(volatile CFE_COMMON_t *)CUT2_NETWORK_CFE_COMMON_BASE;
	volatile NETWORK__NETWORK_COMMON_t *net_common_regs =
		(volatile NETWORK__NETWORK_COMMON_t *)CUT2_NETWORK_COMMON_BASE;

	/* Assert modem resets. */
	writel(0x00000000, &syscfg_regs->SYSTEM_CONFIG2000);
	writel(0x00000000, &syscfg_regs->SYSTEM_CONFIG2002);
	writel(0x00000000, &syscfg_regs->SYSTEM_CONFIG2004);
	writel(0x00000000, &syscfg_regs->SYSTEM_CONFIG2006);
	writel(0x00000000, &syscfg_regs->SYSTEM_CONFIG2008);

	/* Removing RX isolation. */
	writel(0x00000000, &syscfg_regs->SYSTEM_CONFIG2024);

	if (enable_rx) {
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
	}

	/* Select clock out of adc_ss. */
	writel(SYSTEM_CONFIG2033__SEL_PLL_ADC_NOT_PLL_RX_MODEM_bm,
	       &syscfg_regs->SYSTEM_CONFIG2033);

	/* Release ADC Resets */
	writel(0x00000001, &syscfg_regs->SYSTEM_CONFIG2012);
	writel(0x00000002, &syscfg_regs->SYSTEM_CONFIG2008);

	writel(0x00000000, &syscfg_regs->SYSTEM_CONFIG2010);

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
	writel(0x0000003f, &syscfg_regs->SYSTEM_CONFIG2001);
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
	writel(0x0000002f, &syscfg_regs->SYSTEM_CONFIG2000);

	/*
	 * Disable all traffic from downstream to avoid garbage during MiPhy
	 * bringup.
	 */
	writel(0x00000000, &cfe_common_regs->RX_ARB_DWNSTRM_RX_QUANTUM);

	/*
	 * Keep Aurora interfaces in reset until userland is up and can configure
	 * them in line with Catson position in ModemLink chain.
	 */
	writel(0x0000001D, &net_common_regs->FCNET_AURORA_CTRL);
	writel(0x0000001D, &net_common_regs->CATSON_AURORA_CTRL);

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
}

static void configure_scp(int scp_lite)
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
	 * SCP Mapping = 0 (PIO20[2], for scp-lite)
	 *             = 2 (LVDS_IO, for normal-scp)
	 */
	if (scp_lite) {
		writel((2 << SYSTEM_CONFIG2031__PLL_SCP_IDF_bp),
		       &syscfg_regs->SYSTEM_CONFIG2031);
	} else {
		writel((2 << SYSTEM_CONFIG2031__PLL_SCP_IDF_bp) |
		       (2 << SYSTEM_CONFIG2031__SEL_SCP_MAPPING_bp),
		       &syscfg_regs->SYSTEM_CONFIG2031);
	}

	/* Disable fail-safe bits when using SCP-lite */
	if (scp_lite) {
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

void configure_sysconf(int enable_tx, int enable_rx, int scp_lite, int enable_xphy)
{
	if (enable_xphy) {
		configure_xphy();
	}
	configure_modem(enable_tx, enable_rx);
	configure_scp(scp_lite);
}
