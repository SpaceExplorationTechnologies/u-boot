/*
 * (C) Copyright STMicroelectronics 2005, 2008-2011
 * Andy Stugres, <andy.sturges@st.com>
 * Sean McGoogan <Sean.McGoogan@st.com>
 *
 * See file CREDITS for list of people who contributed to this
 * project.
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
 */

#ifndef __INCLUDE_STM_PIO_H
#define __INCLUDE_STM_PIO_H

#define STPIO_NONPIO 0   /* Non-PIO function (ST40 defn) */
#define STPIO_BIDIR_Z1 0 /* Input weak pull-up (arch defn) */
#define STPIO_BIDIR 1    /* Bidirectonal open-drain */
#define STPIO_OUT 2      /* Output push-pull */
/*#define STPIO_BIDIR		3	* Bidirectional open drain */
#define STPIO_IN 4 /* Input Hi-Z */
/*#define STPIO_IN		5	* Input Hi-Z */
#define STPIO_ALT_OUT 6   /* Alt output push-pull (arch defn) */
#define STPIO_ALT_BIDIR 7 /* Alt bidir open drain (arch defn) */

#define STPIO_POUT_OFFSET 0x00
#define STPIO_PIN_OFFSET 0x10
#define STPIO_PC0_OFFSET 0x20
#define STPIO_PC1_OFFSET 0x30
#define STPIO_PC2_OFFSET 0x40
#define STPIO_PCOMP_OFFSET 0x50
#define STPIO_SET_PCOMP_OFFSET 0x54
#define STPIO_CLR_PCOMP_OFFSET 0x58
#define STPIO_PMASK_OFFSET 0x60
#define STPIO_SET_PMASK_OFFSET 0x64
#define STPIO_CLR_PMASK_OFFSET 0x68

#define STPIO_SET_OFFSET 0x4
#define STPIO_CLEAR_OFFSET 0x8

#define STPIO_NO_PIN 0xff /* No pin specified */

#define PIO_PORT(n) (STM_PIO##n##_REGS_BASE)

#define PIN_CX(PIN, DIR, X)                                                    \
	(((PIN) == STPIO_NO_PIN) ? 0 : (((DIR) & (X)) != 0) << (PIN))
#define PIN_C0(PIN, DIR) PIN_CX((PIN), (DIR), 0x01)
#define PIN_C1(PIN, DIR) PIN_CX((PIN), (DIR), 0x02)
#define PIN_C2(PIN, DIR) PIN_CX((PIN), (DIR), 0x04)

#define CLEAR_PIN_C0(PIN, DIR) ((((DIR)&0x1) == 0) << (PIN))
#define CLEAR_PIN_C1(PIN, DIR) ((((DIR)&0x2) == 0) << (PIN))
#define CLEAR_PIN_C2(PIN, DIR) ((((DIR)&0x4) == 0) << (PIN))

#define SET_PIO_PIN(PIO_ADDR, PIN, DIR)                                        \
	do {                                                                   \
		writel(PIN_C0((PIN), (DIR)),                                   \
		       (PIO_ADDR) + STPIO_PC0_OFFSET + STPIO_SET_OFFSET);      \
		writel(PIN_C1((PIN), (DIR)),                                   \
		       (PIO_ADDR) + STPIO_PC1_OFFSET + STPIO_SET_OFFSET);      \
		writel(PIN_C2((PIN), (DIR)),                                   \
		       (PIO_ADDR) + STPIO_PC2_OFFSET + STPIO_SET_OFFSET);      \
		writel(CLEAR_PIN_C0((PIN), (DIR)),                             \
		       (PIO_ADDR) + STPIO_PC0_OFFSET + STPIO_CLEAR_OFFSET);    \
		writel(CLEAR_PIN_C1((PIN), (DIR)),                             \
		       (PIO_ADDR) + STPIO_PC1_OFFSET + STPIO_CLEAR_OFFSET);    \
		writel(CLEAR_PIN_C2((PIN), (DIR)),                             \
		       (PIO_ADDR) + STPIO_PC2_OFFSET + STPIO_CLEAR_OFFSET);    \
	} while (0)

#define STPIO_SET_PIN(PIO_ADDR, PIN, V)                                        \
		writel(1 << (PIN), (PIO_ADDR) + STPIO_POUT_OFFSET              \
					   + ((V) ? STPIO_SET_OFFSET           \
						  : STPIO_CLEAR_OFFSET));
#define STPIO_GET_PIN(PIO_ADDR, PIN)                                           \
	((readl((PIO_ADDR) + STPIO_PIN_OFFSET) >> (PIN)) & 0x01)

/*
 * Add versions that allow us to define a convenient macro
 * which defines both the 'PIO' and 'PIN' arguments
 * as a comma-delimited single (macro) argument pair.
 */
#define SET_PIO_PIN2(PAIR, DIR) SET_PIO_PIN3(PAIR, (DIR))
#define STPIO_SET_PIN2(PAIR, V) STPIO_SET_PIN3(PAIR, (V))
#define STPIO_GET_PIN2(PAIR) STPIO_GET_PIN3(PAIR)

/* also, we need a set to map 'PIO' -> 'PIO_ADDR' */
#define SET_PIO_PIN3(PIO, PIN, DIR) SET_PIO_PIN(STM_PIO_BASE(PIO), (PIN), (DIR))
#define STPIO_SET_PIN3(PIO, PIN, V) STPIO_SET_PIN(STM_PIO_BASE(PIO), (PIN), (V))
#define STPIO_GET_PIN3(PIO, PIN) STPIO_GET_PIN(STM_PIO_BASE(PIO), (PIN))

#define SET_PIO_ASC_OUTDIR(PIO_ADDR, TX, RX, CTS, RTS, OUTDIR)                 \
	do {                                                                   \
		writel(PIN_C0((TX), (OUTDIR)) | PIN_C0((RX), STPIO_IN)         \
			       | PIN_C0((CTS), STPIO_IN)                       \
			       | PIN_C0((RTS), (OUTDIR)),                      \
		       (PIO_ADDR) + STPIO_PC0_OFFSET + STPIO_SET_OFFSET);      \
		writel(PIN_C1((TX), (OUTDIR)) | PIN_C1((RX), STPIO_IN)         \
			       | PIN_C1((CTS), STPIO_IN)                       \
			       | PIN_C1((RTS), (OUTDIR)),                      \
		       (PIO_ADDR) + STPIO_PC1_OFFSET + STPIO_SET_OFFSET);      \
		writel(PIN_C2((TX), (OUTDIR)) | PIN_C2((RX), STPIO_IN)         \
			       | PIN_C2((CTS), STPIO_IN)                       \
			       | PIN_C2((RTS), (OUTDIR)),                      \
		       (PIO_ADDR) + STPIO_PC2_OFFSET + STPIO_SET_OFFSET);      \
	} while (0)

#define SET_PIO_PMASK(PIO, PIN, V)                                             \
		writel(1 << (PIN),                                             \
		       STM_PIO_BASE(PIO) + ((V) ? STPIO_SET_PMASK_OFFSET       \
						: STPIO_CLR_PMASK_OFFSET));

#define SET_PIO_PCOMP(PIO, PIN, V)                                             \
		writel(1 << (PIN),                                             \
		       STM_PIO_BASE(PIO) + ((V) ? STPIO_SET_PCOMP_OFFSET       \
						: STPIO_CLR_PCOMP_OFFSET));

#define SET_PIO_ASC(PIO_ADDR, TX, RX, CTS, RTS)                                \
	SET_PIO_ASC_OUTDIR((PIO_ADDR), (TX), (RX), (CTS), (RTS), STPIO_ALT_OUT)

#endif /* __INCLUDE_STM_PIO_H */
