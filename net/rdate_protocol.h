/**
 * BETTER-RDATE (based off RFC 868) protocol support, header file.
 */

#ifndef __RDATE_PROTOCOL_H__
#define __RDATE_PROTOCOL_H__

#include <common.h>

/**
 * The UDP port number on the server.
 */
#define RDATE_SERVICE_PORT	37

/**
 * struct better_rdate_version1 - Extended rdate protocol message, version 1.
 *
 * @seconds_hi:		The number of seconds since UNIX epoch, high word.
 * @seconds_lo:		The number of seconds since UNIX epoch, low word.
 * @nanoseconds:	The number of nanoseconds, to complement the number of
 *			second since UNIX epoch.
 * @utc_offset:		The UTC offset, in seconds.
 *
 * This is version 1 of the extended rdate information.
 */
struct better_rdate_version1 {
	uint32_t seconds_hi;
	uint32_t seconds_lo;
	uint32_t nanoseconds;
	uint32_t utc_offset;
} __attribute__((packed));

/**
 * struct better_rdate_response - Extended rdate protocol message.
 *
 * @rdate_time:	The number of seconds since RDATE epoch.
 * @version:	The version of the protocol.
 * @u:		The extended protocol message.
 *
 * This structure allows the rdate server to provide additional
 * information that will allow us to more precisely set our clock.
 */
struct better_rdate_response {
	uint32_t rdate_time;
	uint32_t version;
	union {
		struct better_rdate_version1 version1;
	} u;
} __attribute__((packed));

#endif /* __RDATE_PROTOCOL_H__ */
