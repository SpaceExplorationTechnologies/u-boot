/*
 * Support for SDHCI on STMicroelectronics SoCs
 *
 * Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
 * Stuart Menefy <stuart.menefy@st.com>
 * Sean McGoogan <Sean.McGoogan@st.com>
 * Pawel Moll <pawel.moll@st.com>
 * Youssef TRIKI <youssef.triki@st.com>
 * Imran Khan <imran.khan@st.com>
 * Copyright(C) 2013 STMicroelectronics Ltd
 *
 * Based on sdhci-cns3xxx.c
 */

#include <common.h>
#include <command.h>
#include <stm/soc.h>
#include <asm/io.h>
#include <linux/bug.h>
#include <linux/delay.h>
#include <stm/pio.h>
#include <stm/stm-sdhci.h>
#include <stm/sysconf.h>
#include <stm/pad.h>
#include <stm/pio-control.h>
#include <mmc.h>
#include <malloc.h>
#include <sdhci.h>
#include <errno.h>

/* MMCSS glue logic to setup the HC on some ST SoCs */
#define CFG_EMMC_CTRL				0x400

#define	ST_MMC_CCONFIG_REG_1			(CFG_EMMC_CTRL + 0x0)
#define ST_MMC_CCONFIG_TIMEOUT_CLK_UNIT		BIT(24)
#define ST_MMC_CCONFIG_TIMEOUT_CLK_FREQ		BIT(12)
#define ST_MMC_CCONFIG_TUNING_COUNT_DEFAULT	BIT(8)
#define ST_MMC_CCONFIG_ASYNC_WAKEUP		BIT(0)
#define ST_MMC_CCONFIG_1_DEFAULT	\
				((ST_MMC_CCONFIG_TIMEOUT_CLK_UNIT) | \
				 (ST_MMC_CCONFIG_TIMEOUT_CLK_FREQ) | \
				 (ST_MMC_CCONFIG_TUNING_COUNT_DEFAULT) | \
				 (ST_MMC_CCONFIG_ASYNC_WAKEUP))

#define ST_MMC_CCONFIG_REG_2			CFG_EMMC_CTRL + 0x04
#define ST_MMC_CCONFIG_HIGH_SPEED		BIT(28)
#define ST_MMC_CCONFIG_ADMA2			BIT(24)
#define ST_MMC_CCONFIG_8BIT			BIT(20)
#define ST_MMC_CCONFIG_MAX_BLK_LEN		16
#define  MAX_BLK_LEN_1024			1
#define  MAX_BLK_LEN_2048			2
#define BASE_CLK_FREQ_200			0xc8
#define BASE_CLK_FREQ_100			0x64
#define BASE_CLK_FREQ_50			0x32
#define ST_MMC_CCONFIG_2_DEFAULT \
	(ST_MMC_CCONFIG_HIGH_SPEED | ST_MMC_CCONFIG_ADMA2 | \
	 ST_MMC_CCONFIG_8BIT | \
	 (MAX_BLK_LEN_1024 << ST_MMC_CCONFIG_MAX_BLK_LEN))

#define ST_MMC_CCONFIG_REG_3			(CFG_EMMC_CTRL + 0x08)
#define ST_MMC_CCONFIG_EMMC_SLOT_TYPE		BIT(28)
#define ST_MMC_CCONFIG_64BIT			BIT(24)
#define ST_MMC_CCONFIG_ASYNCH_INTR_SUPPORT	BIT(20)
#define ST_MMC_CCONFIG_1P8_VOLT			BIT(16)
#define ST_MMC_CCONFIG_3P0_VOLT			BIT(12)
#define ST_MMC_CCONFIG_3P3_VOLT			BIT(8)
#define ST_MMC_CCONFIG_SUSP_RES_SUPPORT		BIT(4)
#define ST_MMC_CCONFIG_SDMA			BIT(0)
#define ST_MMC_CCONFIG_3_DEFAULT	\
			 (ST_MMC_CCONFIG_ASYNCH_INTR_SUPPORT	| \
			  ST_MMC_CCONFIG_3P3_VOLT		| \
			  ST_MMC_CCONFIG_SUSP_RES_SUPPORT	| \
			  ST_MMC_CCONFIG_SDMA)

#define ST_MMC_CCONFIG_REG_4			(CFG_EMMC_CTRL + 0xc)
#define ST_MMC_CCONFIG_D_DRIVER			BIT(20)
#define ST_MMC_CCONFIG_C_DRIVER			BIT(16)
#define ST_MMC_CCONFIG_A_DRIVER			BIT(12)
#define ST_MMC_CCONFIG_DDR50			BIT(8)
#define ST_MMC_CCONFIG_SDR104			BIT(4)
#define ST_MMC_CCONFIG_SDR50			BIT(0)
#define ST_MMC_CCONFIG_4_DEFAULT		0

#define ST_MMC_CCONFIG_REG_5			(CFG_EMMC_CTRL + 0x10)
#define ST_MMC_CCONFIG_TUNING_FOR_SDR50		BIT(8)
#define RETUNING_TIMER_CNT_MAX			0xf
#define ST_MMC_CCONFIG_5_DEFAULT		0

/* I/O configuration for Arasan IP */
#define	ST_MMC_GP_OUTPUT			(CFG_EMMC_CTRL + 0x50)
#define ST_MMC_GP_OUTPUT_CD			BIT(12)

#define ST_MMC_STATUS_R				(CFG_EMMC_CTRL + 0x60)
#define ST_MMC_STATUS_DLL_LOCK			BIT(0)

/* TOP config registers to manage static and dynamic delay */
#define	ST_TOP_MMC_TX_CLK_DLY			0x8
#define	ST_TOP_MMC_RX_CLK_DLY			0xc
/* MMC delay control register */
#define	ST_TOP_MMC_DLY_CTRL			0x18
#define	ST_TOP_MMC_DLY_CTRL_DLL_BYPASS_CMD	BIT(0)
#define	ST_TOP_MMC_DLY_CTRL_DLL_BYPASS_PH_SEL	BIT(1)
#define	ST_TOP_MMC_DLY_CTRL_DLL_FUNC_CLK_SEL	BIT(2)	/* FlashSS v2.0 */
#define	ST_TOP_MMC_DLY_CTRL_TX_DLL_ENABLE	BIT(8)
#define	ST_TOP_MMC_DLY_CTRL_RX_DLL_ENABLE	BIT(9)
#define	ST_TOP_MMC_DLY_CTRL_ATUNE_NOT_CFG_DLY	BIT(10)
#define	ST_TOP_MMC_START_DLL_LOCK		BIT(11)

/* register to provide the phase-shift value for DLL */
#define	ST_TOP_MMC_TX_DLL_STEP_DLY		0x1c
#define	ST_TOP_MMC_RX_DLL_STEP_DLY		0x20
#define	ST_TOP_MMC_RX_CMD_STEP_DLY		0x24

/* phase shift delay on the tx clk 2.188ns */
#define	ST_TOP_MMC_TX_DLL_STEP_DLY_DEFAULT	0x6
#define	ST_TOP_MMC_TX_DLL_STEP_DLY_SDR104	0x4
#define	ST_TOP_MMC_TX_DLL_STEP_DLY_SDR50	0x8
#define	ST_TOP_MMC_TX_DLL_STEP_DLY_DDR50	0xf

#define	ST_TOP_MMC_DLY_MAX			0xf

/*
 * For clock bigger than 90MHz, it is needed to checks if the DLL procedure
 * is finished before switching to ultra-speed modes.
 */
#define	CLK_TO_CHECL_DLL_LOCK	90000000

/**
 * st_mmcss_cconfig: configure the Arasan HC inside the flashSS.
 * @np: dt device node.
 * @ioaddr: base address
 * Description: this function is to configure the Arasan host controller.
 * On some ST SoCs, the MMC devices inside a dedicated flashSS
 * sub-system need to be configured to be compliant to eMMC 4.5 or eMMC4.3.
 * This has to be done before registering the sdhci host.
 * There are some default settings for the core-config registers (cconfig) and
 * others can be provided from device-tree to fix and tuning the IP according to
 * what has been validated.
 */
static void st_mmcss_cconfig(struct st_mmc_platform_data *pdata)
{
	void __iomem *ioaddr = pdata->regbase;
	u32 cconf2, cconf3, cconf4, cconf5;
	int f_max;

	cconf2 = ST_MMC_CCONFIG_2_DEFAULT;
	cconf3 = ST_MMC_CCONFIG_3_DEFAULT;
	cconf4 = ST_MMC_CCONFIG_4_DEFAULT;
	cconf5 = ST_MMC_CCONFIG_5_DEFAULT;

	writel(ST_MMC_CCONFIG_1_DEFAULT, ioaddr + ST_MMC_CCONFIG_REG_1);

	/*
	 * Use 50MHz by default and also in case of max_frequency is not
	 * provided
	 */
	f_max = pdata->max_frequency;

	if (f_max == 200000000)
		cconf2 |= BASE_CLK_FREQ_200;
	else if (f_max == 100000000)
		cconf2 |= BASE_CLK_FREQ_100;
	else
		cconf2 |= BASE_CLK_FREQ_50;
	writel(cconf2, ioaddr + ST_MMC_CCONFIG_REG_2);

	if (pdata->non_removable)
		cconf3 |= ST_MMC_CCONFIG_EMMC_SLOT_TYPE;
	else
		/* CARD _D ET_CTRL */
		writel(ST_MMC_GP_OUTPUT_CD, ioaddr + ST_MMC_GP_OUTPUT);

	if (pdata->mmc_cap_1p8)
		cconf3 |= ST_MMC_CCONFIG_1P8_VOLT;

	if (pdata->mmc_cap_uhs_sdr50) {
		/* use 1.8V */
		cconf3 |= ST_MMC_CCONFIG_1P8_VOLT;
		cconf4 |= ST_MMC_CCONFIG_SDR50;
		/* Use tuning */
		cconf5 |= ST_MMC_CCONFIG_TUNING_FOR_SDR50;
		/* Max timeout for retuning */
		cconf5 |= RETUNING_TIMER_CNT_MAX;
	}

	if (pdata->mmc_cap_uhs_sdr104) {
		/*
		 * SDR104 implies the HC can support HS200 mode, so
		 * it's mandatory to use 1.8V
		 */
		cconf3 |= ST_MMC_CCONFIG_1P8_VOLT;
		cconf4 |= ST_MMC_CCONFIG_SDR104;
		/* Max timeout for retuning */
		cconf5 |= RETUNING_TIMER_CNT_MAX;
	}

	if (pdata->mmc_cap_uhs_ddr50)
		cconf4 |= ST_MMC_CCONFIG_DDR50;

	writel(cconf3, ioaddr + ST_MMC_CCONFIG_REG_3);
	writel(cconf4, ioaddr + ST_MMC_CCONFIG_REG_4);
	writel(cconf5, ioaddr + ST_MMC_CCONFIG_REG_5);
}

static void st_mmcss_set_fixed_delay(struct st_mmc_platform_data *pdata)
{
	u32 set_dll = readl(pdata->top_ioaddr + ST_TOP_MMC_DLY_CTRL);

	if (pdata->phy_dll) {
		/* RX & TX static dll delay */
		set_dll |= ST_TOP_MMC_DLY_CTRL_TX_DLL_ENABLE |
				ST_TOP_MMC_DLY_CTRL_RX_DLL_ENABLE;
		writel(set_dll,
		       pdata->top_ioaddr + ST_TOP_MMC_DLY_CTRL);

		/* TX extra static dll delay */
		writel(ST_TOP_MMC_DLY_MAX,
		       pdata->top_ioaddr + ST_TOP_MMC_TX_DLL_STEP_DLY);
		/* RX no static dll delay */
		writel(0x0, pdata->top_ioaddr + ST_TOP_MMC_RX_DLL_STEP_DLY);

	} else {
		/* PVT static */
		writel(0x0, pdata->top_ioaddr + ST_TOP_MMC_DLY_CTRL);
		/* TX extra static PVT delay */
		writel(ST_TOP_MMC_DLY_MAX,
		       pdata->top_ioaddr + ST_TOP_MMC_TX_CLK_DLY);
	}
}

static inline void st_mmcss_start_dll(struct st_mmc_platform_data *pdata)
{
	u32 set_dll = ST_TOP_MMC_START_DLL_LOCK;

	if (pdata->phy_dll)
		set_dll |= ST_TOP_MMC_DLY_CTRL_DLL_FUNC_CLK_SEL;

	writel(set_dll, pdata->top_ioaddr + ST_TOP_MMC_DLY_CTRL);
}

static int st_mmcss_lock_dll(void __iomem *ioaddr)
{
	unsigned long value;
	unsigned long finish = 1000;

	/* Checks if the DLL procedure is finished */
	do {
		value = readl(ioaddr + ST_MMC_STATUS_R);
		if (value & ST_MMC_STATUS_DLL_LOCK)
			return 0;
		mdelay(10);
	} while (finish--);

	return -EBUSY;
}

static int sdhci_st_enable_dll(struct st_mmc_platform_data *pdata)
{
	void __iomem *ioaddr = pdata->regbase;
	int value = readl(ioaddr + ST_MMC_STATUS_R);
	int ret = 0;

	if (value & ST_MMC_STATUS_DLL_LOCK)
		return 0;

	if ((pdata->clock > CLK_TO_CHECL_DLL_LOCK) || (pdata->phy_dll)) {
		st_mmcss_start_dll(pdata);
		ret = st_mmcss_lock_dll(ioaddr);
	}

	return ret;
}

static int sdhci_st_probe(struct sdhci_host *host)
{
	int ret = 0;
	struct st_mmc_platform_data *pdata = host->mmc->priv;

	host->host_caps |= MMC_MODE_8BIT;

	if (pdata->non_removable)
		host->host_caps |= MMC_CAP_NONREMOVABLE;

	if (pdata->mmcss_config)
		st_mmcss_cconfig(pdata);

	sdhci_writew(host, 0, SDHCI_TRANSFER_MODE);
	sdhci_writew(host, 0, SDHCI_COMMAND);
	/*
	 * For electrical reasons the extra delay has to be provided, on
	 * platforms able to switch to UHS modes, so when signaling voltage.
	 */
	if (pdata->mmc_cap_uhs_sdr104) {
		ret = sdhci_st_enable_dll(pdata);
		if (ret) {
			printf("Could not lock DLL!\n");
			return 0;
		}
		st_mmcss_set_fixed_delay(pdata);
	}

	return 0;
}

#if defined(CONFIG_STM_SDHCI)

/*
 * Returns the *raw* "CD" (Card Detect) siginal
 * for the appropriate MMC port #.
 * Note: The PIO should have a pull-up attached.
 * returns:	0 for card present, 1 for no card.
 */
static int stm_mmc_getcd(const int port)
{
	if (mmc_pio_getcd[port].pio_port)
		return STPIO_GET_PIN((uintptr_t)STM_PIO_BASE(mmc_pio_getcd[port].pio_port),
				     mmc_pio_getcd[port].pio_pin);
	else
		return 0;
}

#if defined(CONFIG_STM_GLLCFF)
static u32 force_load_address(volatile u32 *volatile ptr)
{
	/*
	 * *ptr is volatile to tell the compiler not to optimize anything around
	 * *ptr : even if this value is known (already loaded), the load should
	 * be performed.
	 */

	return *ptr;
}
#endif

static void stm_enable_mmc(struct sdhci_host *host)
{
	struct st_mmc_platform_data *pdata = host->mmc->priv;
	phys_addr_t regbase = (phys_addr_t)pdata->regbase;
	int port = pdata->port;
	unsigned long bootif = readl(SYSCONF(SS_BOOT_MODE));

	/* Only needs to run on bootable device */
	if (port != 0) {
		return;
	}

	printf("%s:", (bootif & 0x1) ? "Fast boot" : "boot");
	if (((bootif >> 0x1) & 0x08) == 0x08) {
		printf("eMMC: ");
		bootif = (bootif >> 0x1) & 0x07;
		switch (bootif) {
		case 0x0:
			printf("%s\n", "8xbit - div4");
			break;
		case 0x1:
			printf("%s\n", "8xbit - div2");
			break;
		case 0x2:
			printf("%s\n", "8xbit - div4");
			break;
		case 0x3:
			printf("%s\n", "8xbit - div8");
			break;
		case 0x4:
			printf("%s\n", "1xbit - div4");
			break;
		case 0x5:
			printf("%s\n", "1xbit - div2");
			break;
		case 0x6:
			printf("%s\n", "1xbit - div4");
			break;
		case 0x7:
			printf("%s\n", "1xbit - div8");
			break;
		default:
			return;
		}
	} else {
		printf(" non-eMMC.\n");
	}

	/*
	 * Un-map in both spi and mmc boot cases to avoid manipulation
	 * error.  Perform a single "dummy" read to the boot region (@0).
	 * This read only needs to be performed once.
	 * This is only really an issue when booting via GDB/JTAG,
	 * and when the mode-pins are in boot-from-eMMC mode,
	 * when the boot-controller has not been exercised yet.
	 */
	force_load_address(0x0); /* timeout FW */

	/* Now boot disable is safe*/
	writel(0, regbase + TOP_FLASHSS_CONFIG);
}

/*
 * For booting, the standard requires the CMD and DATAx
 * lines all have a pull-up attached. As a cost saving,
 * these pull-ups might not be on the board, so we will
 * explicitly enable the pad's pull-ups in the SoC.
 */
static void set_mmc_pulup_config(int port)
{
	unsigned long sysconf;

	if (port)
		return;

	sysconf = readl(SYSCONF(SS_MMC0_PIO_CONF));

	SET_SYSCONF_BIT(sysconf, 1, SYSCONF_MMC0_PIO_CMD_PIN);/* CMD */
	SET_SYSCONF_BIT(sysconf, 1, SYSCONF_MMC0_PIO_D0_PIN);	/* DATA0 */
	SET_SYSCONF_BIT(sysconf, 1, SYSCONF_MMC0_PIO_D1_PIN);	/* DATA1 */
	SET_SYSCONF_BIT(sysconf, 1, SYSCONF_MMC0_PIO_D2_PIN);	/* DATA2 */
	SET_SYSCONF_BIT(sysconf, 1, SYSCONF_MMC0_PIO_D3_PIN);	/* DATA3 */
	SET_SYSCONF_BIT(sysconf, 1, SYSCONF_MMC0_PIO_D4_PIN);	/* DATA4 */
	SET_SYSCONF_BIT(sysconf, 1, SYSCONF_MMC0_PIO_D5_PIN);	/* DATA5 */
	SET_SYSCONF_BIT(sysconf, 1, SYSCONF_MMC0_PIO_D6_PIN);	/* DATA6 */
	SET_SYSCONF_BIT(sysconf, 1, SYSCONF_MMC0_PIO_D7_PIN);	/* DATA7 */

	writel(sysconf, SYSCONF(SS_MMC0_PIO_CONF));
}

static void stm_configure_mmc(struct sdhci_host *host)
{
	struct st_mmc_platform_data *pdata = host->mmc->priv;
	int port = pdata->port;
	const struct stm_pad_pin *pad_config = stm_mmc_pad_configs[port];
	size_t num_pads = ARRAY_SIZE(stm_mmc_pad_configs[port]);

	/* Configure all the PIOs */
	stm_configure_pios(pad_config, num_pads);

	/* Pullup already present */
	set_mmc_pulup_config(port);
}

#ifdef CONFIG_STM_GLLCFF
#define SDHCI_CLK_MAX 200 * 1000 * 1000		/* max clock 200Mhz */
#define SDHCI_CLK_MIN 400 * 1000		/* min clock 400Khz */
struct st_mmc_platform_data stm_mmc0 = {
	.regbase = (void *)CONFIG_SYS_MMC0_BASE,
	.quirks = SDHCI_QUIRK_BROKEN_VOLTAGE |
		  SDHCI_QUIRK_BROKEN_R1B |
		  SDHCI_QUIRK_WAIT_SEND_CMD |
		  SDHCI_QUIRK_32BIT_DMA_ADDR |
		  SDHCI_QUIRK_NO_HISPD_BIT,
	.voltages = MMC_VDD_165_195,
	.host_caps = MMC_MODE_DDR_52MHz,
	.top_ioaddr = (void __iomem *)(CONFIG_SYS_MMC0_BASE + TOP_FLASHSS_CONFIG),
	.flashss_version = 2,
	.max_frequency = SDHCI_CLK_MAX,
	.min_frequency = SDHCI_CLK_MIN,
	.non_removable = 1,
	.mmc_cap_1p8 = 0,
	.mmc_cap_uhs_sdr50 = 1,
	.mmc_cap_uhs_sdr104 = 0,
	.mmc_cap_uhs_ddr50 = 1,
	.mmc_force_tx_dll_step_dly = 0,
	.mmcss_config = 1,
	.port = 0,
};
#endif

#if defined(CONFIG_STM_SDHCI_1)
struct st_mmc_platform_data stm_mmc1;
#endif

static int stm_sdhci_init(struct st_mmc_platform_data *pdata);

extern int cpu_mmc_init(struct bd_info *bis)
{
	int ret = 0;

#if defined(CONFIG_STM_SDHCI_0)
	ret = stm_sdhci_init(&stm_mmc0);
#endif	/* CONFIG_STM_SDHCI_0 */

#if defined(CONFIG_STM_SDHCI_1)
	ret |= stm_sdhci_init(&stm_mmc1);
#endif	/* CONFIG_STM_SDHCI_1 */

	return ret;
}

#endif	/* CONFIG_STM_SDHCI */

static const char *sdhci_name[] = {
	"stm-sdhci0",   /* MMC #0 */
	"stm-sdhci1",   /* MMC #1 */
	"stm-sdhci2",   /* MMC #2 */
	"stm-sdhci3",   /* MMC #3 */
};

static int stm_sdhci_init(struct st_mmc_platform_data *pdata)
{
	struct sdhci_host *host;
	int port = pdata->port;
	int ret;

	/* paranoia */
	BUG_ON(port >= ARRAY_SIZE(sdhci_name));

	host = (struct sdhci_host *)malloc(sizeof(struct sdhci_host));
	if (!host) {
		printf("stm_sdhci_init() malloc fail for host structure!\n");
		return -1;
	}

	/* fill in the newly allocated host structure */
	memset(host, 0, sizeof(struct sdhci_host));
	host->mmc = (struct mmc *)malloc(sizeof(struct mmc));
	host->mmc->priv = pdata;
	host->name      = sdhci_name[port];
	host->ioaddr    = pdata->regbase;
	host->quirks    = pdata->quirks;
	host->host_caps = pdata->host_caps;

	stm_enable_mmc(host);
	stm_configure_mmc(host);

	ret = sdhci_st_probe(host);
	if (ret < 0) {
		return ret;
	}

	host->version = sdhci_readw(host, SDHCI_HOST_VERSION);

	return add_sdhci(host, pdata->max_frequency, pdata->min_frequency);
}

/*
 * Pointer to SoC-specific mmc_getcd() function.
 */
int board_mmc_getcd(struct mmc *mmc)
{
	if (!strcmp(mmc->cfg->name, sdhci_name[0])) /* MMC #0 */
		return !stm_mmc_getcd(0);

	if (!strcmp(mmc->cfg->name, sdhci_name[1])) /* MMC #1 */
		return !stm_mmc_getcd(1);

	if (!strcmp(mmc->cfg->name, sdhci_name[2])) /* MMC #2 */
		return !stm_mmc_getcd(2);

	if (!strcmp(mmc->cfg->name, sdhci_name[3])) /* MMC #3 */
		return !stm_mmc_getcd(3);

	BUG();		/* should never get here! */
	return -1;
}
