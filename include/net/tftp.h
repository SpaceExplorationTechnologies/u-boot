/*
 *	LiMon - BOOTP/TFTP.
 *
 *	Copyright 1994, 1995, 2000 Neil Russell.
 *	Copyright 2011 Comelit Group SpA
 *	               Luca Ceresoli <luca.ceresoli@comelit.it>
 *	(See License)
 */

#ifndef __TFTP_H__
#define __TFTP_H__

/**********************************************************************/
/*
 *	Global functions and variables.
 */

#include <net.h>

/* tftp.c */
void tftp_start(enum proto_t protocol);	/* Begin TFTP get/put */

#ifdef CONFIG_CMD_TFTPSRV
void tftp_start_server(void);	/* Wait for incoming TFTP put */
#endif

extern ulong tftp_timeout_ms;
extern int tftp_timeout_count_max;

#ifdef CONFIG_SPACEX
extern int tftp_total_timeout_count;

#define NET_TFTP_ERROR_NONE                 0
#define NET_TFTP_ERROR_OTHER                -1
#define NET_TFTP_ERROR_TIMEOUT              -2
#define NET_TFTP_ERROR_FILE_NOT_FOUND       -3
#define NET_TFTP_ERROR_ACCESS_DENIED        -4

extern int tftp_error_code;
#endif

/**********************************************************************/

#endif /* __TFTP_H__ */
