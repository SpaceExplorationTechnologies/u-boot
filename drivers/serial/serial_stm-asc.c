/*
 * drivers/serial/stm-asc.c
 *
 * Support for Serial I/O using STMicroelectronics' on-chip ASC.
 *
 *  Copyright (c) 2004,2008-2013  STMicroelectronics Limited
 *  Sean McGoogan <Sean.McGoogan@st.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 *
 */

#include "asm/io.h"
#include "common.h"
#include <serial.h>
#include <stm/socregs.h>

DECLARE_GLOBAL_DATA_PTR;

/*
 * This is an arbitrary set of options in the form of a bitmask.  ST has a
 * common function that each serial controller and configuration calls for
 * device specific initialization.  Even though we only have one controller,
 * keep this setting for ease of integrating future code and controllers.
 */
#define CS7		0x0000400 /* 7 bit (with parity) */
#define CS8		0x0000060 /* 8 bit mode */
#define CSIZE		0x0000060 /* Mask of 7 or 8 bit mode */
#define CSTOPB		0x0000100 /* Use stop bit */
#define PARENB		0x0000400 /* Use parity */
#define PARODD		0x0001000 /* Odd parity */
#define FIFOEN		0x0010000 /* Use fifo for transfer */

#define BAUDMODE	0x00001000
#define CTSENABLE	0x00000800
#define FIFOENABLE	0x00000400
#define RXENABLE	0x00000100
#define RUN		0x00000080
#define STOPBIT		0x00000008
#define MODE		0x00000001
#define MODE_7BIT_PAR	0x0003
#define MODE_8BIT_PAR	0x0007
#define MODE_8BIT	0x0001
#define STOP_1BIT	0x0008
#define PARITYODD	0x0020

#define STA_NKD		0x0400
#define STA_TF		0x0200
#define STA_RHF		0x0100
#define STA_TOI		0x0080
#define STA_TNE		0x0040
#define STA_OE		0x0020
#define STA_FE		0x0010
#define STA_PE		0x0008
#define	STA_THE		0x0004
#define STA_TE		0x0002
#define STA_RBF		0x0001

#define UART_BAUDRATE_OFFSET	0x00
#define UART_TXBUFFER_OFFSET	0x04
#define UART_RXBUFFER_OFFSET	0x08
#define UART_CONTROL_OFFSET	0x0C
#define UART_INTENABLE_OFFSET	0x10
#define UART_STATUS_OFFSET	0x14
#define UART_GUARDTIME_OFFSET	0x18
#define UART_TIMEOUT_OFFSET	0x1C
#define UART_TXRESET_OFFSET	0x20
#define UART_RXRESET_OFFSET	0x24
#define UART_RETRIES_OFFSET	0x28

#define UART_BAUDRATE_REG	(SYS_STM_ASC_BASE + UART_BAUDRATE_OFFSET)
#define UART_TXBUFFER_REG	(SYS_STM_ASC_BASE + UART_TXBUFFER_OFFSET)
#define UART_RXBUFFER_REG	(SYS_STM_ASC_BASE + UART_RXBUFFER_OFFSET)
#define UART_CONTROL_REG	(SYS_STM_ASC_BASE + UART_CONTROL_OFFSET)
#define UART_INTENABLE_REG	(SYS_STM_ASC_BASE + UART_INTENABLE_OFFSET)
#define UART_STATUS_REG		(SYS_STM_ASC_BASE + UART_STATUS_OFFSET)
#define UART_GUARDTIME_REG	(SYS_STM_ASC_BASE + UART_GUARDTIME_OFFSET)
#define UART_TIMEOUT_REG	(SYS_STM_ASC_BASE + UART_TIMEOUT_OFFSET)
#define UART_TXRESET_REG	(SYS_STM_ASC_BASE + UART_TXRESET_OFFSET)
#define UART_RXRESET_REG	(SYS_STM_ASC_BASE + UART_RXRESET_OFFSET)
#define UART_RETRIES_REG	(SYS_STM_ASC_BASE + UART_RETRIES_OFFSET)

/*
 * Values for the BAUDRATE Register:
 * MODE 0
 *                       ICCLK
 * ASCBaudRate =   ----------------
 *                   baudrate * 16
 *
 * MODE 1
 *                   baudrate * 16 * 2^16
 * ASCBaudRate =   ------------------------
 *                          ICCLK
 *
 * The 16 in this math comes from the a 4 bit counter.  Each time that counter
 * rolls, the serial counter increments.
 *
 * NOTE:
 * Mode 1 should be used for baudrates of 19200, and above, as it
 * has a lower deviation error than Mode 0 for higher frequencies.
 * Mode 0 should be used for all baudrates below 19200.
 */
#define PCLK			(SYS_STM_ASC_CLK)
#define BAUDRATE_VAL_M0(bps)	(PCLK / (16 * (bps)))
#define BAUDRATE_VAL_M1(bps)	((((bps * (1 << 14))) / (PCLK / (1 << 6))))

/* Note: the argument order for "outl()" is swapped, w.r.t. writel() */
#define p2_inl(addr)		__raw_readl(addr)
#define p2_outl(addr, v)	__raw_writel(v, addr)

/* busy wait until it is safe to send a char */
static inline void tx_char_wait_ready(void)
{
	while(p2_inl(UART_STATUS_REG) & STA_TF) {
	}
}

/* initialize the ASC */
static int stm_asc_serial_init_common(const int cflag)
{
	unsigned long val = 0;
	int baud = gd->baudrate;
	int baud_reg, mode;

	switch (baud) {
	case 9600:
		baud_reg = BAUDRATE_VAL_M0(9600);
		mode = 0;
		break;
	case 19200:
	case 38400:
	case 57600:
		baud_reg = BAUDRATE_VAL_M1(baud);
		mode = 1;
		break;
	default:
		printf("ASC: unsupported baud rate: %d, using 115200 instead.\n",
		       baud);
		/* Fall through */
	case 115200:
		baud_reg = BAUDRATE_VAL_M1(115200);
		mode = 1;
		break;
	}

	/* wait for end of current transmission */
	tx_char_wait_ready();

	/* disable the baudrate generator */
	val = p2_inl(UART_CONTROL_REG);
	p2_outl(UART_CONTROL_REG, (val & ~RUN));

	/* set baud generator reload value */
	p2_outl(UART_BAUDRATE_REG, baud_reg);

	/* reset the RX & TX buffers */
	p2_outl(UART_TXRESET_REG, 1);
	p2_outl(UART_RXRESET_REG, 1);

	/* build up the value to be written to CONTROL */
	val = RXENABLE | RUN;

	/* set character length */
	if ((cflag & CSIZE) == CS7) {
		val |= MODE_7BIT_PAR;
	} else {
		if (cflag & PARENB)
			val |= MODE_8BIT_PAR;
		else
			val |= MODE_8BIT;
	}

	/* stop bit */
	if (cflag & CSTOPB)
		val |= STOP_1BIT;

	/* odd parity */
	if (cflag & PARODD)
		val |= PARITYODD;

	/* fifo enable */
	if (cflag & FIFOEN)
		val |= FIFOENABLE;

	/* set baud generator mode */
	if (mode)
		val |= BAUDMODE;

	/* finally, write value and enable ASC */
	p2_outl(UART_CONTROL_REG, val);

	return 0;
}

/* Default initializaiton parameters */
static int stm_asc_serial_init(void)
{
	const int cflag = CSTOPB | CS8 | PARODD | FIFOEN;

	return stm_asc_serial_init_common(cflag);
}

/* returns TRUE if a char is available to be read */
static int stm_asc_serial_tstc(void)
{
	return p2_inl(UART_STATUS_REG) & STA_RBF;
}

/* blocking function that returns next char */
static int stm_asc_serial_getc(void)
{
	/* polling wait: for a char to be read */
	while (!stm_asc_serial_tstc()) {
	}

	/* consumed and return char to the caller */
	return p2_inl(UART_RXBUFFER_REG);
}

/* write out a single char */
static void stm_asc_serial_putc(char ch)
{
	/* Stream-LF to CR+LF conversion */
	if (ch == '\n')
		stm_asc_serial_putc('\r');

	/* wait till safe to write next char */
	tx_char_wait_ready();

	/* finally, write next char */
	p2_outl(UART_TXBUFFER_REG, ch);
}

/* called to adjust baud-rate */
static void stm_asc_serial_setbrg(void)
{
	/* just re-initialize ASC */
	stm_asc_serial_init();
}

static struct serial_device stm_asc_serial_drv = {
	.name	= "stm_asc",
	.start	= stm_asc_serial_init,
	.stop	= NULL,
	.setbrg	= stm_asc_serial_setbrg,
	.putc	= stm_asc_serial_putc,
	.puts	= default_serial_puts,
	.getc	= stm_asc_serial_getc,
	.tstc	= stm_asc_serial_tstc,
};

void stm_asc_serial_initialize(void)
{
	serial_register(&stm_asc_serial_drv);
}

/*
 * If we are also using DTF (JTAG), we probably want that driver
 * to dominate, hence we define this function as "__weak".
 */
__weak struct serial_device *default_serial_console(void)
{
	return &stm_asc_serial_drv;
}
