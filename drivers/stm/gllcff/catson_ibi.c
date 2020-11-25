/*
 * SpaceX code for initializing Catson IP Plug
 */

#include <asm/io.h>
#include <common.h>

#define IBI_REG_G1_OFFSET	0x0000
#define	MEMORY_PAGE(__val)	((fls(__val) - 7) & 7)
#define		NODE_ENABLE		BIT(8)  /* 0 means Normal */
#define		WRP_ENABLE		BIT(16)
#define		WRP_MODE		BIT(17)
#define		AXI_QOS_MODE		BIT(24)

#define IBI_REG_G2_OFFSET	0x0004
#define		P_START			BIT(0)
#define		P_STOP			0

#define IBI_REG_G3_OFFSET	0x0008
#define		IDLE			BIT(0)

#define CLIENT_ADDR(__ipi_base, __client, __reg)  \
			((__ipi_base) + 0x20 + ((__client) * 0x10) + (__reg))

#define IBI_REG_C1_OFFSET	0x0000
#define		CLIENT_BURST_MAX_SIZE(__val)	(((fls((__val)) - 4) & 0x7))
#define		CLIENT_MAX_OST(__val)	(((__val) ? ((__val) - 1) : 0) << 8)
#define		CLIENT_MAX_OST_NO_LIMIT	CLIENT_MAX_OST(0)
#define IBI_REG_C2_OFFSET	0x0004
#define		CLIENT_N_IVC(__val)	(((__val) > 2) ? ((__val) - 1) : 0)
#define		CLIENT_N_IVC_DEFAULT	CLIENT_N_IVC(1)
#define		CLIENT_N_SVC(__val)	(((__val) & 0xf) << 8)
#define		CLIENT_N_RAT(__val)	((((__val) ? ((__val) - 1) : 0) & 0xf) << 16)
#define		CLIENT_N_RAT_DEFAULT	CLIENT_N_RAT(1)
#define IBI_REG_C3_OFFSET	0x0008
#define		CLIENT_N_DPREG_START(__val)	((__val) & 0xff)
#define		CLIENT_N_DPREG_END(__val)	((__val) << 16)

/*
 * catson_apply_ibi() - Apply an IPPlug Bus Interconnect configuration.
 *
 * @addr:    The address of the interconnect to apply.
 * @config:  The configuration to apply.
 *
 * Return: 0 on success, < 0 on failure.
 */
int catson_apply_ibi(unsigned long addr, struct ibi_config *config)
{
	int i;
	unsigned int dpreg_start;
	unsigned int dpreg_size;

	if (config == NULL)
		return -1;

	/* Start Programming */
	writel(P_START, addr + IBI_REG_G2_OFFSET);

	/* Set G1 Register */
	writel(MEMORY_PAGE(config->memory_page_size), addr + IBI_REG_G1_OFFSET);

	/* Program all the satellites */
	dpreg_start = 0;
	for (i = 0; i < MAX_IBI_CLIENTS; i++) {
		struct ibi_client *client = &config->clients[i];

		if (!client->max_burst_size_bytes)
			continue;

		writel(CLIENT_BURST_MAX_SIZE(client->max_burst_size_bytes) |
				CLIENT_MAX_OST(client->outstanding_packets),
		       CLIENT_ADDR(addr, i, IBI_REG_C1_OFFSET));

		writel(CLIENT_N_IVC_DEFAULT |
				CLIENT_N_SVC(client->svc) |
				CLIENT_N_RAT_DEFAULT,
		       CLIENT_ADDR(addr, i, IBI_REG_C2_OFFSET));

		dpreg_size = (client->dpreg_size * 8) / config->bus_size_bits;

		if (dpreg_start + dpreg_size > config->dpreg_total_size) {
			printf("DPREG (%d) exceeds max (%d)!\n",
			       dpreg_start + dpreg_size,
			       config->dpreg_total_size);
			return -1;
		}

		writel(CLIENT_N_DPREG_START(dpreg_start) |
			       CLIENT_N_DPREG_END(dpreg_start + dpreg_size - 1),
			CLIENT_ADDR(addr, i, IBI_REG_C3_OFFSET));

		dpreg_start += dpreg_size;
	}

	/* End Programing */
	writel(P_STOP, addr + IBI_REG_G2_OFFSET);

	return 0;
}
