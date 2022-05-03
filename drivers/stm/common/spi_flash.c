/*
 *  Copyright (C) 2014 STMicroelectronics Limited
 *     Ram Dayal <ram.dayal@st.com>
 *
 * SPDX-License-Identifier:     GPL-2.0+
 *
 */

#include <stm/soc.h>

extern void stm_configure_spi_flashSS(const int ident)
/*
 * Set flashPHY of flashSS in SPI-NOR mode.
 * Extended to 2 SPI links if requested and physically possible
 */
{
	if (ident & SPI_NOR_1) {
		STM_PIOALT_SELECT(SPI_nCS);  /* SPI_nCS */
		STM_PIOALT_SELECT(SPI_CLK);  /* SPI_CLK */
		STM_PIOALT_SELECT(SPI_MOSI); /* SPI_D0 */
		STM_PIOALT_SELECT(SPI_MISO); /* SPI_D1 */
		/* following only really needed for x4 mode */
		STM_PIOALT_SELECT(SPI_nWP);  /* SPI_D2 */
		STM_PIOALT_SELECT(SPI_HOLD); /* SPI_D3 */
	}

#if defined(CONFIG_SYS_STM_SPI_FSM2_BASE)
	if (ident & SPI_NOR_2) {
		STM_PIOALT_SELECT(SPI2_nCS);  /* SPI_nCS */
		STM_PIOALT_SELECT(SPI2_CLK);  /* SPI_CLK */
		STM_PIOALT_SELECT(SPI2_MOSI); /* SPI_D0 */
		STM_PIOALT_SELECT(SPI2_MISO); /* SPI_D1 */
		/* following only really needed for x4 mode */
		STM_PIOALT_SELECT(SPI2_nWP);  /* SPI_D2 */
		STM_PIOALT_SELECT(SPI2_HOLD); /* SPI_D3 */
	}
#endif
}
