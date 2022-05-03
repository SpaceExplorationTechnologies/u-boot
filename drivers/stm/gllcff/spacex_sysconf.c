/*
 * SpaceX code for initializing Catson sysconf common across devices.
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
#include <configs/spacex_catson_boot.h>
#include <linux/delay.h>
#include <spacex/catson3/catson3_compat.h>
#include <spacex/catson4/catson4_compat.h>

#define SYSTEM_STATUS450_REGISTER 0x091300C8u
#define   SYSTEM_STATUS450_VERSION_SHIFT (28)
#define   SYSTEM_STATUS450_VERSION_MASK	(0xf)

/**
 * Return the hardware based catson cut version embedded in the SYSTEM_STATUS540
 * register.  NOTE: This register is 0 indexed, whereas colloquial usage refers
 * to 1 indexed.  If someone says "Catson Cut 2", they are referring to a value
 * of 1 in this register.
 */
unsigned int catson_get_version(void)
{
	return (readl(SYSTEM_STATUS450_REGISTER) >>
			SYSTEM_STATUS450_VERSION_SHIFT) &
		SYSTEM_STATUS450_VERSION_MASK;
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
	/*
	 * Release the resets needed to configure XPHY.
	 *
	 * XPHY_0_PX_RST_TX_DATAPATH_N
	 * XPHY_0_PX_RST_RX_DATAPATH_N
	 * XPHY_0_RST_XPHY_N
	 * XPHY_0_APB_RESET_N
	 */
	writel(0x00F00000, 0xd000034);

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

	if (!pll_locked) {
		printf("Catson U-Boot XPHY: Retrying PLL Initialization with BG_TRIM=0xF.\n");
		pll_locked = start_xphy_pll(0xF);

		if (!pll_locked) {
			printf("Catson U-Boot XPHY: Retrying PLL Initialization with BG_TRIM=0xF, LDO_OUT_SEL=0x2.\n");
			writel(0x00000020, 0x0C6000C8);
			pll_locked = start_xphy_pll(0xF);
		}
	}

	if (!pll_locked) {
		printf("Failed waiting for XPHY PLL lock.\n");
	}
}

void configure_sysconf(int enable_tx, int enable_rx, int scp_mapping,
		       int enable_xphy, int enable_l3_cfe, int use_modem_rx_pll)
{
	if (enable_xphy) {
		configure_xphy();
	}
	if (catson_get_version() >= CATSON_VERSION_CUT4) {
		configure_modem_v4(enable_tx, enable_rx, enable_l3_cfe, use_modem_rx_pll);
		configure_scp_v4(scp_mapping);
	} else {
		configure_modem_v3(enable_tx, enable_rx);
		configure_scp_v3(scp_mapping);
    }
}
