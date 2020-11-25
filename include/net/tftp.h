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

/* tftp.c */
void tftp_start(enum proto_t protocol);	/* Begin TFTP get/put */

#ifdef CONFIG_CMD_TFTPSRV
void tftp_start_server(void);	/* Wait for incoming TFTP put */
#endif

extern ulong tftp_timeout_ms;
extern int tftp_timeout_count_max;

#ifdef CONFIG_SPACEX
extern int tftp_total_timeout_count;
#endif

/**********************************************************************/

#endif /* __TFTP_H__ */
