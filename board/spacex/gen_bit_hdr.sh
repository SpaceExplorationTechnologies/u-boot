#! /bin/sh
# Generates a C header for the specified bitstream (compressed and uncompressed).
# Usage: this_script <lzma'd_bitstream_file> <original_bitstream_file>

filename_to_ld_binary_symbol() {
    echo -n _binary_$(echo $1 | tr /. _)
}

cat <<EOF
#ifndef BIT_HDR_H
#define BIT_HDR_H

extern unsigned char
$(filename_to_ld_binary_symbol $1_start)[];

static const size_t bitstream_lzma_size = $(stat -c %s $1);
static const size_t bitstream_orig_size = $(stat -c %s $2);

#endif /* BIT_HDR_H */
EOF
