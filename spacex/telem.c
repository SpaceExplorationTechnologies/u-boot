/**
 * Telemetry emitter for SpaceX code
 * https://rtm.spacex.corp/docs/flight-software-design/en/latest/common/digital_telemetry.html
 */

#include <asm/sx_time.h>
#include <common.h>
#include <command.h>
#include <net.h>
#include <spacex/common.h>
#include <stdio_dev.h>

DECLARE_GLOBAL_DATA_PTR;

bool processing_output;

static struct in_addr telem_dest;

#ifndef CONFIG_TELEM_DEST_IP
#error CONFIG_TELEM_DEST_IP must be defined
#endif
#define TELEM_DEST_PORT  10017

/* Below fields are spec defined */
#define MAX_BWP_STREAM_DATA     1024

#define BWP_TYPE 108
/* Chosen to be one lower than the standard linux flowid of 99 */
#define BWP_CONSOLE_FLOWID 98
#define BWP_CONSOLE_GROUPID 3
#define BWP_CONSOLE_TYPE 3
/* timestamp is unknown, store on data recorder */
#define BWP_CONSOLE_FLAGS 2

#define BWP_STREAM_HEADER_LEN 12
#define BWP_CONSOLE_MAX_PAYLOAD   (MAX_BWP_STREAM_DATA - BWP_STREAM_HEADER_LEN)

/* Specify a TTL so that this packet will traverse multiple networks */
#define BWP_CONSOLE_TTL		4

#define MAX_LINE_BUFFER_LENGTH	BWP_CONSOLE_MAX_PAYLOAD
char line_buffer[MAX_LINE_BUFFER_LENGTH];
unsigned long line_buffer_length;

struct bwp_telem {
	u32 bwp_type;
	u32 source;
	u16 flow_id;
	u16 group_id;
	u8 telem_type;
	u8 flags;
	u16 length;
	u64 timestamp;
	u32 byte_offset;

	char data[0];
} __packed__;

static u32 byte_offset;

/**
 * Convert a buffer into a BWP console telemetry packet
 *
 * @buf: The string to transfer
 * @len: The length of buf.
 */
static void telem_send_packet(const char *buf, int len)
{
#ifdef CONFIG_DM_ETH
	struct udevice *eth;
#else
	struct eth_device *eth;
#endif
	int inited = 0;
	/* Overwritten by net_send_udp_packet when using multicast */
	uchar ether[6];
	struct bwp_telem *pkt = (struct bwp_telem *)
		(net_tx_packet + net_eth_hdr_size() + IP_UDP_HDR_SIZE);

	pkt->bwp_type = cpu_to_be32(BWP_TYPE);
	pkt->source = net_ip.s_addr;
	pkt->flow_id = cpu_to_be16(BWP_CONSOLE_FLOWID);
	pkt->group_id = cpu_to_be16(BWP_CONSOLE_GROUPID);
	pkt->telem_type = BWP_CONSOLE_TYPE;
	pkt->flags = BWP_CONSOLE_FLAGS;
	pkt->length = cpu_to_be16(BWP_STREAM_HEADER_LEN + len);
	pkt->timestamp = cpu_to_be64(get_raw_ticks());
	pkt->byte_offset = cpu_to_be32(byte_offset);
	memcpy(pkt->data, buf, len);

	byte_offset += len;

	eth = eth_get_dev();
	if (eth == NULL)
		return;

	if (!eth_is_active(eth)) {
		if (eth_is_on_demand_init()) {
			if (eth_init() < 0)
				return;
		} else {
			eth_init_state_only();
		}

		inited = 1;
	}

	net_send_udp_packet_ttl(ether, telem_dest, TELEM_DEST_PORT, TELEM_DEST_PORT,
				sizeof(*pkt) + len, BWP_CONSOLE_TTL);

	if (inited) {
		if (eth_is_on_demand_init())
			eth_halt();
		else
			eth_halt_state_only();
	}
}

/**
 * stdio callback for starting a console device.
 *
 * @dev: The stdio device to start.
 *
 * Return: 0 on success, < 0 on error.
 */
static int telem_stdio_start(struct stdio_dev *dev)
{
	/*
	 * Starlink TFTP nodes rapidly reset, causing the byte_offset to reset
	 * to 0.  This confuses the telemetry proxy and causes many useful
	 * messages to be dropped.
	 */
#ifdef CONFIG_SPACEX_ZYNQMP_SOFT_REBOOT
	byte_offset = spacex_get_soft_reboot_count() * 1024 * 1024;
#endif

	/*
	 * Initialize the static IP settings and buffer pointers
	 * in case we call net_send_udp_packet before net_loop
	 */
	net_init();

	return 0;
}

/**
 * stdio callback for inserting a single character.  This input is buffered
 * until a newline is inserted or a puts is called.
 *
 * @dev: The stdio device to insert on.
 * @c:   The character to insert.
 */
static void telem_stdio_putc(struct stdio_dev *dev, char c)
{
	if (processing_output)
		return;
	processing_output = true;

	line_buffer[line_buffer_length] = c;
	line_buffer_length++;

	if (line_buffer_length == MAX_LINE_BUFFER_LENGTH || c == '\n') {
		telem_send_packet(line_buffer, line_buffer_length);
		line_buffer_length = 0;
	}

	processing_output = false;
}

/**
 * stdio callback for inserting a string
 *
 * @dev: The stdio device to start.
 * @s:   The null terminated string to transmit.
 */
static void telem_stdio_puts(struct stdio_dev *dev, const char *s)
{
	int len;

	if (processing_output)
		return;
	processing_output = true;

	/*
	 * This is unlikely to happen in practice, so do not bother "optimizing"
	 * by bouncing puts through the local buffer.
	 */
	if (line_buffer_length) {
		telem_send_packet(line_buffer, line_buffer_length);
		line_buffer_length = 0;
	}

	len = strlen(s);
	while (len) {
		int send_len = min(len, (int)BWP_CONSOLE_MAX_PAYLOAD);
		telem_send_packet(s, send_len);
		len -= send_len;
		s += send_len;
	}

	processing_output = false;
}

/**
 * Registration for telemetry stdio device.
 *
 * Return: 1 on success, <0 on failure.
 */
int drv_telem_init(void)
{
	struct stdio_dev dev;
	int rc;

	telem_dest = string_to_ip(CONFIG_TELEM_DEST_IP);

	/*
	 * Allowing unicast addresses would require a more complicated
	 * implementation allowing for ARP packets.
	 */
	if ((0xF0000000 & ntohl(telem_dest.s_addr)) != 0xE0000000) {
		pr_err("Telemetry destination '%s' %08x is not multicast\n",
		      CONFIG_TELEM_DEST_IP, ntohl(telem_dest.s_addr));
		return -1;
	}

	memset(&dev, 0, sizeof(dev));

	strcpy(dev.name, "telem");
	dev.flags = DEV_FLAGS_OUTPUT;
	dev.start = telem_stdio_start;
	dev.putc = telem_stdio_putc;
	dev.puts = telem_stdio_puts;

	/* This return pattern matches convention of other stdio devices */
	rc = stdio_register(&dev);
	return (rc == 0) ? 1 : rc;
}

/**
 * UBoot command callback to generate a telemetry packet.
 */
static int do_telem_cmd(cmd_tbl_t *cmdtp, int flag, int argc,
			  char * const argv[])
{
	char data[BWP_CONSOLE_MAX_PAYLOAD];
	int len = 0;
	int i;

	telem_stdio_start(NULL);

	len += scnprintf(data + len, sizeof(data) - len, "UBoot Console: ");
	for (i = 1; i < argc; i++) {
		len += scnprintf(data + len, sizeof(data) - len, "%s ", argv[i]);
	}
	len += scnprintf(data + len, sizeof(data) - len, "\n");

	telem_send_packet(data, len);

	return 0;

}

U_BOOT_CMD(
	telem, 99,  1,  do_telem_cmd,
	"SpaceX telemetry console output",
	"[args]                     - console to emit\n");
