#! /usr/bin/env python
"""Creates and applies an SPI flash header to a U-Boot image.
See the P2020 Manual, Section "4.5.2.3 EEPROM Data Structure," for what this
is supposed to look like.
U-Boot's own doc/README.mpc8536ds also has some good information.

So that you don't have to pore through that to get a decent understanding of
this code, here's an executive summary:
There is a boot ROM in the P2020 that can read the SPI flash at a leisurely
speed of 5MHz.
At offset 0x10 words into the SPI flash, the Freescale SPI boot header
begins.  It includes a magic number, how much data to copy from SPI flash,
the source of the copy (e.g. the end of the header), the destination of the
copy, where to jump when the copy is complete, and last but certainly not
least, the number of address/value configuration word-pairs that follow.
These address/values pairs are actually handled first (at each address, the
given value is written) in order to e.g. set up memory for the copy to take
place.  Then the boot ROM does the copy and jumps where the header tells it
to.  And then, with any luck, you're booting!"""

import struct

# Here are the default address/value configuration word pairs.
# I have adapted these from examples found in the U-Boot distribution.
# However, I wanted to justify each of them below.  Some items that were
#  in examples I threw out or fixed based on the documentation in the P2020
#  manual.
DEFAULT_CONFIG = \
[
# This is the L2 Memory-Mapped SRAM Base Address Register 0 of the L2 cache
#  controller. This setting makes the L2 cache, when acting as SRAM (i.e. not
#  cache), map to address 0xf8f80000.
# Note that this is the default value for the P2020.
[0xff720100, 0xf8f80000],
# This is the L2 error disable register (L2ERRDIS). This setting disables
#  multiple- and single-bit errors. (This looks like a bad idea!
#  But it is necessary to use cache-as-RAM.)
[0xff720e44, 0x0000000c],
# This is the L2 Control Register (L2CTL). This setting enables the L2
#  circuitry and sets it to be all (512KiB) used as SRAM.
[0xff720000, 0xa0010000],
# This magic value ends the configuration pairs.
[0x80000001, 0x80000001]
]

P1010_CONFIG = \
[
# This is the L2 Memory-Mapped SRAM Base Address Register 0 of the L2 cache
#  controller. This setting makes the L2 cache, when acting as SRAM (i.e. not
#  cache), map to address 0x00000000 (the trampoline code in start.S relocates
#  this to 0xf8fc0000).
(0xff720100, 0x00000000),
# This is the L2 error disable register (L2ERRDIS). This setting disables
#  multiple- and single-bit errors. (This looks like a bad idea!
#  But it is necessary to use cache-as-RAM.)
(0xff720e44, 0x0000000c),
# This is the L2 Control Register (L2CTL). This setting enables the L2
#  circuitry and sets it to be all (256KiB) used as SRAM.
(0xff720000, 0x90010000),
# This magic value ends the configuration pairs.
(0x80000001, 0x80000001)
]

def apply_header(image, configs):
    """Applies the SPI boot header to the given image and returns it.
    configs should be a sequence of pairs (addr, value) of settings that will
    be applied by the processor's boot ROM.  It should end with the magic
    value (0x80000001, 0x80000001), but doesn't have to (if I'm reading the
    manual correctly).

    image is the raw U-Boot binary without the SPI boot header
    (i.e., u-boot.bin).

    If the image does not end with enough FF's (ignoring the reset vector) to
    apply the header, this function will raise an exception."""
    img_len = len(image)
    num_configs = len(configs)
    CONFIG_FMT = ">LL"
    # This is the length of an instruction in PowerPC; the U-Boot image by
    #  default ends with a backwards jump instruction.
    RESET_VECTOR_LEN = 4
    PRE_CONFIG_HEADER_LEN = 0x80
    header_len = (PRE_CONFIG_HEADER_LEN +
                  (num_configs * struct.calcsize(CONFIG_FMT)))

    copy_address = configs[0][1]
    jump_address = copy_address + len(image) - 0x1000

    # Check to make sure there is enough slack space at the end of the image.
    # Strip off the reset vector (which we don't use, since we have a
    #  jump address).
    if not image[:-RESET_VECTOR_LEN].endswith('\xFF' *
                                              (header_len - RESET_VECTOR_LEN)):
        raise RuntimeError(("Image does not have enough slack space at end " +
                            "(need %d (0x%x) including 4-byte reset vector)") %
                           (header_len, header_len))
    # Trim the input binary image.
    trimmed_image = image[:-header_len]
    copy_length = len(trimmed_image)

    WORD_FMT = ">L"
    BUFFER_WORD = '\x00' * 4
    # Build the header (see the P2020 EEPROM Data Structure referenced above).
    header = BUFFER_WORD * 0x10
    header += "BOOT"
    header += BUFFER_WORD
    header += struct.pack(WORD_FMT, copy_length)
    header += BUFFER_WORD
    header += struct.pack(WORD_FMT, header_len)
    header += BUFFER_WORD
    header += struct.pack(WORD_FMT, copy_address)
    header += BUFFER_WORD
    header += struct.pack(WORD_FMT, jump_address)
    header += BUFFER_WORD
    header += struct.pack(WORD_FMT, num_configs)
    header += (BUFFER_WORD * 5)
    if len(header) != PRE_CONFIG_HEADER_LEN:
        raise RuntimeError("I suck at math *and* following directions!")
    for (addr, value) in configs:
        header += struct.pack(CONFIG_FMT, addr, value)
    if len(header) != header_len:
        raise RuntimeError("I still suck at math...")

    return header + trimmed_image

if __name__ == "__main__":
    import sys
    import errno
    if len(sys.argv) < 2:
        print >>sys.stderr, "Usage: [--p1010] %s <image_file>" % (sys.argv[0])
        sys.exit(errno.EINVAL)
    if sys.argv[1] == '--p1010':
        configs = P1010_CONFIG
        bin_path = sys.argv[2]
    else:
        configs = DEFAULT_CONFIG
        bin_path = sys.argv[1]
    image = open(bin_path, "rb").read()
    sys.stdout.write(apply_header(image, configs))
