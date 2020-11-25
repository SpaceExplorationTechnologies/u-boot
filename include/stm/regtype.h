/*
 * Copyright (C) STMicroelectronics Ltd. 2002, 2013
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

#ifndef __INCLUDE_STM_REGTYPE_H
#define __INCLUDE_STM_REGTYPE_H

#ifndef __ASSEMBLY__

typedef volatile unsigned char *const stm_u8_reg_t;
typedef volatile unsigned short *const stm_u16_reg_t;
typedef volatile unsigned int *const stm_u32_reg_t;
typedef volatile unsigned long long *const stm_u64_reg_t;

#define STM_U8_REG(address) ((stm_u8_reg_t)(address))
#define STM_U16_REG(address) ((stm_u16_reg_t)(address))
#define STM_U32_REG(address) ((stm_u32_reg_t)(address))
#define STM_U64_REG(address) ((stm_u64_reg_t)(address))

#else /* __ASSEMBLY__ */

#define STM_U8_REG(address) (address)
#define STM_U16_REG(address) (address)
#define STM_U32_REG(address) (address)
#define STM_U64_REG(address) (address)

#endif /* __ASSEMBLY__ */

#endif /* __INCLUDE_STM_REGTYPE_H */
