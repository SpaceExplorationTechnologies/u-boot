/**
 * ECC armoring support.
 */

#ifndef SPACEX_ECC_H
#define SPACEX_ECC_H

#ifndef TOOLS_BUILD
#include <common.h>
#else
#include <stdint.h>
#endif /* TOOLS_BUILD */

#ifdef NPAR
#if NPAR != 32
#error Value of NPAR mismatches
#endif
#else /* !NPAR */
#define NPAR			32
#endif /* NPAR */

/*
 * These options must be synchronized with the userspace "ecc"
 * utility's configuration options. See ecc/trunk/include/ecc.h in the
 * "util" submodule of the platform.
 */
#define ECC_BLOCK_SIZE		255
#define ECC_MD5_LEN		16
#define ECC_EXTENSION		"ecc"
#define ECC_FILE_MAGIC		"SXECCv"
#define ECC_FILE_VERSION	'1'
#define ECC_FILE_MAGIC_LEN	(sizeof(ECC_FILE_MAGIC) - 1)
#define ECC_FILE_FOOTER_LEN	sizeof(file_footer_t)
#define ECC_DAT_SIZE		(ECC_BLOCK_SIZE - NPAR - 1)

#define ECC_BLOCK_TYPE_DATA	'*'
#define ECC_BLOCK_TYPE_LAST	'$'
#define ECC_BLOCK_TYPE_FOOTER	'!'

/**
 * Return the size of the data contained in an ECC encoded buffer of
 * the given size.
 *
 * @size:	Number of encoded bytes.
 *
 * Return: The number of data bytes.
 */
#define ECC_DATA_SIZE(size) \
	(((((unsigned long)(size) - sizeof(file_footer_t)) / ECC_BLOCK_SIZE) * \
	  ECC_DAT_SIZE) - sizeof(file_header_t))

/**
 * struct file_header - Describes the magic header for the ecc file.
 */
typedef struct file_header
{
	/**
	 * The signature that appear at the top of any ecc file.
	 */
	uint8_t magic[ECC_FILE_MAGIC_LEN];

	/**
	 * The ecc file version.
	 */
	char version;
} __attribute__((packed)) file_header_t;

/**
 * struct file_block - Describes any block in the ecc file except for
 * the footer.
 */
typedef struct file_block
{
	/*
	 * A block either has magic numbers at the top (i.e. the
	 * first block) or doesn't. This union keeps you from
	 * having to compute these offsets.
	 */
	union
	{
		struct
		{
			/**
			 * The first block contains the magic header.
			 */
			file_header_t header;

			/**
			 * The length of the data in the first block is
			 * the length of a block minus the header itself.
			 */
			uint8_t short_data[ECC_DAT_SIZE -
					   sizeof(file_header_t)];
		} s;

		/**
		 * Data location for blocks other than the first.
		 */
		uint8_t data[ECC_DAT_SIZE];
	} d;

	/**
	 * The block type.
	 */
	uint8_t block_type;

	/**
	 * The ecc code words.
	 */
	uint8_t ecc[NPAR];
} __attribute__((packed)) file_block_t;

/**
 * struct file_footer - Describes the footer to the ecc file.
 */
typedef struct file_footer
{
	/**
	 * The block type.
	 */
	uint8_t block_type;

	/**
	 * The size of the un-ecc'd payload, in big-endian order.
	 */
	uint32_t payload_size;

	/**
	 * The md5sum of the payload. Note the padding and
	 * ecc symbols are not included in the payload.
	 */
	uint8_t md5sum[ECC_MD5_LEN];

	/**
	 * The ecc code words for the footer.
	 */
	uint8_t ecc[NPAR];
} __attribute__((packed)) file_footer_t;

/**
 * Returns the size of the ECC encoded data for a given data buffer.
 *
 * @size:	The size of the uncencoded data, in bytes.
 *
 * Return: The size of the encoded data, in bytes.
 *
 * This is computed by:
 * 1. Adding the ECC magic header;
 * 2. Converting to the number of data blocks, rounded up (see "Number
 *    Conversion" by Roland Blackhouse (2001) for a paper on the derivation
 *    of the integer ceiling division function);
 * 3. Coverting the number of blocks to bytes, and adding the length of the
 *    footer.
 */
static inline unsigned long ecc_encoded_size(unsigned long size)
{
	return ((size + sizeof(file_header_t) + ECC_DAT_SIZE - 1) /
		ECC_DAT_SIZE) * ECC_BLOCK_SIZE + sizeof(file_footer_t);
}

/**
 * Returns the size of the data contained in an ECC encoded buffer of
 * the given size.
 *
 * @size:	The size of the encoded data, in bytes.
 *
 * Return: The size of the decoded data, in bytes.
 */
static inline unsigned long ecc_decoded_size(unsigned long size)
{
	return ECC_DATA_SIZE(size);
}

int ecc_decode(const void *data, unsigned long size, void *dest,
	       unsigned long *decoded, int silent, unsigned int *error_count);

int ecc_encode(const void *data, unsigned long size, void *dest,
	       unsigned long *encoded);

extern unsigned int num_ecc_errors;

#endif /* !SPACEX_ECC_H */
