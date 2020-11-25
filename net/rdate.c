/**
 * BETTER RDATE (based off RFC 868) protocol support.
 *
 * This code is heavily based on the SNTP implementation in sntp.c.
 */

#include <common.h>
#include <command.h>
#include <net.h>
#include <rdate.h>
#include <div64.h>
#include <rtc.h>
#include <dm.h>
#include <asm/sx_time.h>

#include "rdate_protocol.h"

/**
 * Timeout in milliseconds.
 * We make this more than a second so that our NetLoop runs long
 * enough to catch an ARP (for the reply) from Linux. Linux normally
 * ARPs only once per second.
 */
#define RDATE_TIMEOUT	1500UL

/**
 * This definition is normally provided by linux/include/time.h.
 */
#define NSEC_PER_SEC	1000000000L

/**
 * The local, randomly chosen, UDP port number used to send and
 * receive messages.
 */
static int rdate_our_port;

/**
 * The time of boot, in nanoseconds, since UNIX epoch.
 */
static uint64_t boot_time_in_ns = 0;

/**
 * UTC-to-TAI/GPS/PTP offset (in seconds), as given to us by the server.
 */
static uint32_t utc_offset = 0;

/**
 * The protocol timeout handler.
 */
static void rdate_timeout_handler(void)
{
	puts("Timeout\n");
	net_set_state(NETLOOP_FAIL);
	return;
}

/**
 * Set the RTC time based on rdate information
 */
#if defined(CONFIG_DM_RTC)
static void set_rtc_date(int seconds_hi, int seconds_lo)
{
	struct udevice *dev;
	struct rtc_time tm;
	int rcode;

	if (seconds_hi) {
		printf("Date is too large: %d:%d\n", seconds_hi, seconds_lo);
		return;
	}

	rcode = rtc_to_tm(seconds_lo, &tm);
	if (rcode) {
		printf("Date conversion failed: err=%d\n", rcode);
		return;
	}

	rcode = uclass_get_device(UCLASS_RTC, 0, &dev);
	if (rcode) {
		printf("Cannot find RTC: err=%d\n", rcode);
		return;
	}

	rcode = dm_rtc_set(dev, &tm);
	if (rcode) {
		printf("Set date failed: err=%d\n", rcode);
		return;
	}
}
#endif

/**
 * The protocol message handler (for received messages).
 *
 * @pkt:	A pointer to the buffer containing the message.
 * @dest:	The destination port number.
 * @sip:	The source IP address.
 * @src:	The source port number.
 * @len:	The length of the message.
 */
static void rdate_handler(uchar *pkt, unsigned dest, struct in_addr sip,
			  unsigned src, unsigned len)
{
	/*
	 * Sample the current CPU time as early as possible in the
	 * process.
	 */
	unsigned long long response_ticks = get_raw_ticks();

	/*
	 * Ignore messages not targeted to us.
	 */
	if (dest != rdate_our_port)
		return;

	/*
	 * Starting platform 6, we only support the "better RDATE"
	 * protocol. Ensure that this is the kind of message that
	 * we've received.
	 */
	if (len < offsetof(struct better_rdate_response, u)) {
		pr_err("Not an RDATE response!");
		return;
	}

	struct better_rdate_response *rdate =
		(struct better_rdate_response *)pkt;

	if (ntohl(rdate->version) != 1 ||
	    len < (offsetof(struct better_rdate_response, u) +
		   sizeof(struct better_rdate_version1))) {
		pr_err("Not a BETTER-RDATE response!");
		return;
	}

	struct better_rdate_version1 *version1 = &rdate->u.version1;

#if defined(CONFIG_DM_RTC)
	set_rtc_date(ntohl(version1->seconds_hi), ntohl(version1->seconds_lo));
#endif

	/*
	 * Note the order of calculation here, so that we avoid
	 * integer division errors; normally one would calculate:
	 *
	 * (NSEC_PER_SEC / get_tbclk()) * response_ticks
	 *
	 * i.e., ns_per_tick * ticks_since_boot.
	 *
	 * But if get_tbclk() returns 66666667, then (NSEC_PER_SEC /
	 * get_tbclk()) becomes 14ns (incorrect) instead of 15ns
	 * (correct) per clock tick.
	 *
	 * Unfortunately, we don't get a lot of uptime before the
	 * multiplication (NSEC_PER_SEC * response_ticks) overflows 64
	 * bits.
	 * For a system clock of 66MHz, that comes out to:
	 * 2**64 / 10**9 / 66666667 = 276 seconds, or just over 4.5
	 * minutes.
	 * To avoid this, we pre-divide NSEC_PER_SEC by a factor of
	 * 1000 (and multiply that back in at the end). This means we
	 * only have microsecond resolution in the ns_since_boot, but
	 * we don't expect to be more accurate than that anyway (due
	 * to OS scheduling and networking delays). For a 1GHz system
	 * clock, this gives us over 5 hours of U-Boot uptime before
	 * the calculation rolls over.
	 */
	uint64_t ns_since_boot = lldiv((NSEC_PER_SEC/1000)*response_ticks, get_tbclk()/1000);
	debug("response_ticks = %llu, tbclk = %llu\n", response_ticks,
	      (unsigned long long) get_tbclk());
	uint64_t remote_timestamp_in_ns;

	/*
	 * Convert the remote timestamp into nanoseconds since the
	 * Unix epoch.
	 */
	remote_timestamp_in_ns = (((uint64_t)ntohl(version1->seconds_hi) <<
				   32) |
				  ntohl(version1->seconds_lo));
	remote_timestamp_in_ns *= NSEC_PER_SEC;
	remote_timestamp_in_ns += ntohl(version1->nanoseconds);

	debug("remote_timestamp_in_ns = %llu\n",
	      (unsigned long long) remote_timestamp_in_ns);

	boot_time_in_ns = remote_timestamp_in_ns - ns_since_boot;
	utc_offset = ntohl(version1->utc_offset);

	uint64_t sec  = lldiv(boot_time_in_ns, NSEC_PER_SEC);
	uint64_t frac = boot_time_in_ns - (sec*NSEC_PER_SEC);
	printf("Boot time in seconds since Unix epoch: %llu.%09llu\n", sec, frac);
	printf("UTC offset: %u seconds\n", (unsigned)utc_offset);

	/*
	 * Indicate that we do not need to keep waiting for messages.
	 */
	net_set_state(NETLOOP_SUCCESS);
}

/**
 * Initiate query of the time via network using RDATE.
 */
void rdate_start(void)
{
	/*
	 * Setup packets and event handlers.
	 */
	net_set_timeout_handler(RDATE_TIMEOUT, rdate_timeout_handler);
	net_set_udp_handler(rdate_handler);
	memset(net_server_ethaddr, 0, sizeof(net_server_ethaddr));

	/*
	 * Send the initial query message.
	 */
	rdate_our_port = random_port();
	net_send_udp_packet(net_server_ethaddr, net_rdate_server,
			    RDATE_SERVICE_PORT, rdate_our_port, 0);
}

/**
 * Retrieves the time of boot, in nanoseconds, since RDATE epoch.
 *
 * Return: the time of boot, in nanoseconds, since RDATE epoch, or 0
 * if RDATE was not called or did not succeed.
 */
uint64_t get_cpu_boot_time_in_unix_ns(void)
{
	return boot_time_in_ns;
}

/**
 * Retrieves the UTC offset, in seconds.
 *
 * Return: the UTC offset, in seconds, or 0 if RDATE was not called or
 * did not succeed.
 */
uint32_t get_utc_offset(void)
{
	return utc_offset;
}
