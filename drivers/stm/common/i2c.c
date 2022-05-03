/*
 *  Copyright (C) 2014 STMicroelectronics Limited
 *     Ram Dayal <ram.dayal@st.com>
 *
 * SPDX-License-Identifier:     GPL-2.0+
 *
 */

#include <common.h>
#include <command.h>
#include <stm/soc.h>
#include <stm/pio.h>
#include <asm/io.h>

static struct pio ssc_i2c_scl;
static struct pio ssc_i2c_sda;
bool configured = false;

void stm_configure_i2c(const struct pio *scl, const struct pio *sda)
{
	memcpy(&ssc_i2c_scl, scl, sizeof(ssc_i2c_scl));
	memcpy(&ssc_i2c_sda, sda, sizeof(ssc_i2c_sda));

	/* Route PIO (explicitly alternate #0) */
	stm_pioalt_select(ssc_i2c_scl.port, ssc_i2c_scl.pin, ssc_i2c_scl.alt);
	stm_pioalt_select(ssc_i2c_sda.port, ssc_i2c_sda.pin, ssc_i2c_sda.alt);

	/* set up directionality appropriately */
	SET_PIO_PIN((uintptr_t)STM_PIO_BASE(ssc_i2c_scl.port), ssc_i2c_scl.pin,
		    STPIO_BIDIR);
	SET_PIO_PIN((uintptr_t)STM_PIO_BASE(ssc_i2c_sda.port), ssc_i2c_sda.pin,
		    STPIO_BIDIR);

	configured = true;
}

void stm_i2c_scl(const int val)
{
	if (!configured) {
		return;
	}

	/* SSC's SCLK == I2C's SCL */
	STPIO_SET_PIN((uintptr_t)STM_PIO_BASE(ssc_i2c_scl.port),
		      ssc_i2c_scl.pin, (val) ? 1 : 0);
}

void stm_i2c_sda(const int val)
{
	if (!configured) {
		return;
	}

	/* SSC's MTSR == I2C's SDA */
	STPIO_SET_PIN((uintptr_t)STM_PIO_BASE(ssc_i2c_sda.port),
		      ssc_i2c_sda.pin, (val) ? 1 : 0);
}

int stm_i2c_read(void)
{
	if (!configured) {
		return -1;
	}

	/* SSC's MTSR == I2C's SDA */
	return STPIO_GET_PIN((uintptr_t)STM_PIO_BASE(ssc_i2c_sda.port),
			     ssc_i2c_sda.pin);
}

#if defined(CONFIG_I2C_CMD_TREE)
unsigned int i2c_get_bus_speed(void)
{
	return CONFIG_SYS_I2C_SPEED;
}

int i2c_set_bus_speed(unsigned int speed)
{
	return -1;
}
#endif  /* CONFIG_I2C_CMD_TREE */

