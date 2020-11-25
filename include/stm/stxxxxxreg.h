/*
 * Copyright (C) STMicroelectronics Ltd. 2002, 2003, 2007-2014
 *
 * andy.sturges@st.com
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

/*
 * This is derived from STMicroelectronics gnu toolchain example:
 *   sh-superh-elf/examples/bare/sh4reg/st40reg.h
 */

#ifndef __INCLUDE_STM_STXXXXX_H
#define __INCLUDE_STM_STXXXXX_H

#include <stm/regtype.h>

/*
 * STMicroelectronics control registers
 */

/* Parallel I/O control registers */
#define STM_PIO_POUT(n) STM_U32_REG(STM_PIO##n##_REGS_BASE + 0x00)
#define STM_PIO_PIN(n) STM_U32_REG(STM_PIO##n##_REGS_BASE + 0x10)
#define STM_PIO_PC0(n) STM_U32_REG(STM_PIO##n##_REGS_BASE + 0x20)
#define STM_PIO_PC1(n) STM_U32_REG(STM_PIO##n##_REGS_BASE + 0x30)
#define STM_PIO_PC2(n) STM_U32_REG(STM_PIO##n##_REGS_BASE + 0x40)
#define STM_PIO_PCOMP(n) STM_U32_REG(STM_PIO##n##_REGS_BASE + 0x50)
#define STM_PIO_PMASK(n) STM_U32_REG(STM_PIO##n##_REGS_BASE + 0x60)

/* PIO pseudo registers */
#define STM_PIO_SET_POUT(n) STM_U32_REG(STM_PIO##n##_REGS_BASE + 0x04)
#define STM_PIO_CLEAR_POUT(n) STM_U32_REG(STM_PIO##n##_REGS_BASE + 0x08)
#define STM_PIO_SET_PC0(n) STM_U32_REG(STM_PIO##n##_REGS_BASE + 0x24)
#define STM_PIO_CLEAR_PC0(n) STM_U32_REG(STM_PIO##n##_REGS_BASE + 0x28)
#define STM_PIO_SET_PC1(n) STM_U32_REG(STM_PIO##n##_REGS_BASE + 0x34)
#define STM_PIO_CLEAR_PC1(n) STM_U32_REG(STM_PIO##n##_REGS_BASE + 0x38)
#define STM_PIO_SET_PC2(n) STM_U32_REG(STM_PIO##n##_REGS_BASE + 0x44)
#define STM_PIO_CLEAR_PC2(n) STM_U32_REG(STM_PIO##n##_REGS_BASE + 0x48)
#define STM_PIO_SET_PCOMP(n) STM_U32_REG(STM_PIO##n##_REGS_BASE + 0x54)
#define STM_PIO_CLEAR_PCOMP(n) STM_U32_REG(STM_PIO##n##_REGS_BASE + 0x58)
#define STM_PIO_SET_PMASK(n) STM_U32_REG(STM_PIO##n##_REGS_BASE + 0x64)
#define STM_PIO_CLEAR_PMASK(n) STM_U32_REG(STM_PIO##n##_REGS_BASE + 0x68)

/* Asynchronous Serial Controller control registers */
#define STM_ASC_BAUDRATE(n) STM_U32_REG(STM_ASC##n##_REGS_BASE + 0x00)
#define STM_ASC_TXBUFFER(n) STM_U32_REG(STM_ASC##n##_REGS_BASE + 0x04)
#define STM_ASC_RXBUFFER(n) STM_U32_REG(STM_ASC##n##_REGS_BASE + 0x08)
#define STM_ASC_CONTROL(n) STM_U32_REG(STM_ASC##n##_REGS_BASE + 0x0c)
#define STM_ASC_INTENABLE(n) STM_U32_REG(STM_ASC##n##_REGS_BASE + 0x10)
#define STM_ASC_STATUS(n) STM_U32_REG(STM_ASC##n##_REGS_BASE + 0x14)
#define STM_ASC_GUARDTIME(n) STM_U32_REG(STM_ASC##n##_REGS_BASE + 0x18)
#define STM_ASC_TIMEOUT(n) STM_U32_REG(STM_ASC##n##_REGS_BASE + 0x1c)
#define STM_ASC_TXRESET(n) STM_U32_REG(STM_ASC##n##_REGS_BASE + 0x20)
#define STM_ASC_RXRESET(n) STM_U32_REG(STM_ASC##n##_REGS_BASE + 0x24)
#define STM_ASC_RETRIES(n) STM_U32_REG(STM_ASC##n##_REGS_BASE + 0x28)

#endif /* __INCLUDE_STM_STXXXXX_H */
