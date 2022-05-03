/*
 * If you update these two variables, you must update these variable in these locations:
 * 1. ZYNQMP_ATF_MEM_BASE and ZYNQMP_ATF_MEM_SIZE in xilinx-arm-trusted-firmware/Makefile
 * 2. BL31_LOCATION and BL31_SIZE in sx_xmpu.c (in xilinx/embeddedsw)
 */
#define BL31_LOCATION 0x3b00000
#define BL31_SIZE 0x100000