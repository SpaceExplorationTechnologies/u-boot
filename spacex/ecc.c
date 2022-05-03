/**
 * Glue code to the rscode library for ECC support.
 */

#ifndef TOOLS_BUILD
#include <common.h>
#else
#include <endian.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#endif /* TOOLS_BUILD */
#include <rscode/ecc.h>
#include <spacex/ecc.h>
#include <u-boot/md5.h>

/**
 * Global ECC errors counter. Used to keep track of the total number
 * of errors encountered when decoding multiple files.
 */
unsigned int num_ecc_errors = 0;

/**
 * Corrects a single codeword in place.
 *
 * @rs:			The reed-solomon state.
 * @data:		The data to correct.
 * @length:		The length of the data block.
 * @error_count[out]:	The number of decode errors is added to this value. Can
 *			be NULL.
 *
 * Return: 1 if the block has been fully corrected, 0 otherwise.
 */
static int correct_block(rs_state *rs, void *data, unsigned int length,
			 unsigned int *error_count)
{
	decode_data(rs, data, length);

	int has_an_error = check_syndrome(rs);
	if (has_an_error) {
		if (error_count != NULL)
			*error_count += 1;

		if (!correct_errors_erasures(rs, data, length, 0, 0))
			return 0;
	}

	return 1;
}

/**
 * Decodes the first block of an ECC protected file.
 *
 * @in_buffer:	 The first block.
 *
 * Return: 1 if the file is an ECC file with a good header, 0 otherwise.
 */
static int decode_header(const file_block_t *in_buffer)
{
	if (in_buffer->block_type != ECC_BLOCK_TYPE_DATA &&
	    in_buffer->block_type != ECC_BLOCK_TYPE_LAST)
		return 0;

	if (memcmp(in_buffer->d.s.header.magic, ECC_FILE_MAGIC,
		   ECC_FILE_MAGIC_LEN))
		return 0;

	if (in_buffer->d.s.header.version != ECC_FILE_VERSION)
		return 0;

	return 1;
}

/**
 * Decodes an ECC protected block of memory. If the enable_correction
 * parameter is zero, it will use the MD5 checksum to detect errors and will
 * ignore the ECC bits. Otherwise, it will use the ECC bits to correct any
 * errors and still use the MD5 checksum to detect remaining problems.
 *
 * @data:		Pointer to the input data.
 * @size:		The length of the input data, or 0 to read until the
 *			end of the ECC stream.
 * @dest:		The destination for the decoded data.
 * @decoded[out]:	An optional pointer to store the length of the decoded
 *			data.
 * @silent:		Whether to call print routines or not.
 * @enable_correction:	Indicates that the ECC data should be used
 *			to correct errors. Otherwise the MD5 checksum
 *			will be used to check for an error.
 * @error_count[out]:	Pointer to an integer that will be incremented
 *			by the number of errors found. May be NULL.
 *			Unused if !enable_correction.
 *
 * Return: 1 if the block was successfully decoded, 0 if we had a
 * failure, -1 if the very first block didn't decode (i.e. probably
 * not an ECC file)
 */
static int ecc_decode_one_pass(const void *data, unsigned long size, void *dest,
			       unsigned long *decoded, int silent,
			       int enable_correction, unsigned int *error_count)
{
	int rc = 0;

	struct MD5Context md5_context;
	MD5Init(&md5_context);

	rs_state rs;
	if (enable_correction)
		initialize_ecc(&rs);

	file_block_t in_buffer;
	int blocknum = 0;
	const file_block_t *pos = data;
	uint8_t *outpos = dest;
	const uint8_t *in_data;
	uint32_t in_size;
	int unrecoverable = 0;
	unsigned int decode_errors = 0;
	do {
		const void *const addr = pos;
		const file_block_t *in;

		/*
		 * If we are not streaming, make sure we have enough
		 * data left to finish the file.
		 */
		if (size && (uintptr_t)(pos + 1) + sizeof(file_footer_t) >
		    (uintptr_t)data + size) {
			if (!silent)
				printf("%sData is truncated\n",
				       (blocknum % 8192) > 128 ? "\n" : "");
			goto out;
		}

		if (!enable_correction)
			in = pos;
		else {
			memcpy(&in_buffer, pos, sizeof(file_block_t));
			unrecoverable = !correct_block(&rs, &in_buffer,
						       ECC_BLOCK_SIZE,
						       &decode_errors);
			in = &in_buffer;
		}
		pos++;

		if (!blocknum) {
			/*
			 * Take a peek at the header and ensure we are
			 * reading an ECC file.
			 */
			if (!decode_header(in)) {
				if (!silent)
					printf("Bad ECC header in buffer at %p"
					       "\n", addr);
				if (enable_correction)
					rc = -1;
				goto out;
			}

			/*
			 * The first block has a slightly smaller
			 * payload due to the magic numbers at the
			 * beginning of a file.
			 */
			in_data = in->d.s.short_data;
			in_size = sizeof(in->d.s.short_data);
		} else {
			in_data = in->d.data;
			in_size = sizeof(in->d.data);
		}

		/*
		 * If correction is enabled, but failed, then exit
		 * with an error.
		 */
		if (unrecoverable) {
			if (!silent)
				printf("%sUnrecoverable error\n",
				       (blocknum % 8192) > 128 ? "\n" : "");
			goto out;
		}

		blocknum++;
		if (!(blocknum % 128) && !silent)
			printf("%s", (blocknum % 8192) ? "#" : "\n");

		if (in->block_type == ECC_BLOCK_TYPE_LAST ||
		    in->block_type == ECC_BLOCK_TYPE_FOOTER) {
			/*
			 * If we hit the last block or footer, exit the loop.
			 */
			break;
		} else if (in->block_type != ECC_BLOCK_TYPE_DATA) {
			if (!silent)
				printf("%sBad ECC block in buffer at %p\n",
				       (blocknum % 8192) > 128 ? "\n" : "",
				       addr);
			goto out;
		}

		MD5Update(&md5_context, in_data, in_size);
		memmove(outpos, in_data, in_size);
		outpos += in_size;
	} while (1);

	/*
	 * We should be at the footer now. Note that in_data & in_size
	 * are still valid from the loop above.
	 */
	file_footer_t footer;
	memcpy(&footer, pos, sizeof(file_footer_t));

	if (enable_correction) {
		/*
		 * Correct the footer.
		 */
		unrecoverable = !correct_block(&rs, (uint8_t*)&footer,
					       sizeof(file_footer_t),
					       &decode_errors);
	}

	if (footer.block_type != ECC_BLOCK_TYPE_FOOTER) {
		if (!silent)
			printf("%sBad footer in buffer at %p\n",
			       (blocknum % 8192) > 128 ? "\n" : "", pos);
		goto out;
	}

	/*
	 * If correction is enabled, but the footer failed to decode,
	 * then exit with an error.
	 */
	if (unrecoverable) {
		if (!silent)
			printf("%sUnrecoverable error\n",
			       (blocknum % 8192) > 128 ? "\n" : "");
		goto out;
	}

	/*
	 * Figure out how many bytes we have left to write.
	 */
	uint32_t total_bytes_expected = be32_to_cpu(footer.payload_size);
	uint32_t bytes_left = total_bytes_expected - (outpos - (uint8_t *)dest);

	/*
	 * And write them.
	 */
	MD5Update(&md5_context, in_data, bytes_left);
	memmove(outpos, in_data, bytes_left);
	outpos += bytes_left;

	/*
	 * Finalize and compute the checksum of what we wrote.
	 */
	uint8_t checksum[ECC_MD5_LEN] = { 0 };
	MD5Final(checksum, &md5_context);

	/*
	 * Ensure the checksum of the data we wrote matches the
	 * checksum embedded in the file.
	 */
	if (memcmp(checksum, footer.md5sum, ECC_MD5_LEN)) {
		if (!silent)
			printf("%sMD5 sum mismatch\n",
			       (blocknum % 8192) > 128 ? "\n" : "");
		goto out;
	}

	if (decoded != NULL)
		*decoded = (outpos - (uint8_t *)dest);
	rc = 1;

out:
	if (!silent && (blocknum % 8192) > 128)
		puts("\n");

	if (enable_correction && error_count != NULL) {
		if (decode_errors > 0)
			*error_count += decode_errors;
		else if (rc != 1)
			*error_count += 1;
	}

	return rc;
}

/**
 * Tries a copy with MD5 checksum verification only, falling back to ECC error
 * correction if it fails.
 *
 * If called with the same source and destination (i.e., in-place
 * decoding), we must fall back to cautious/slower one-pass
 * decoding. If called with a null input size, assume streaming of the
 * ECC data, and do not enforce input/output size.
 *
 * @data:		Pointer to the input data.
 * @size:		The length of the input data, or 0 to read until the
 *			end of the ECC stream.
 * @dest:		The destination for the decoded data.
 * @decoded[inout]:	An optional pointer to limit the length of the decoded
 *			data. Also used to return the actual length of the
 *			decoded data.
 * @silent:		Whether log errors or not.
 * @error_count[out]:	Pointer to an integer that will be incremented
 *			by the number of errors found. May be NULL.
 *
 * Return: 1 if the block was successfully decoded, 0 if we had a
 * failure, -1 if the very first block didn't decode (i.e. probably
 * not an ECC file).
 */
int ecc_decode(const void *data, unsigned long size, void *dest,
	       unsigned long *decoded, int silent, unsigned int *error_count)
{
	int rc;

	if (size && decoded != NULL && *decoded < ecc_decoded_size(size))
		return 0;

	if (data == dest) {
		rc = ecc_decode_one_pass(data, size, dest, decoded, silent, 1,
					 error_count);
	} else {
		/*
		 * Avoid double-counting errors: specify NULL counter
		 * during check.
		 */
		rc = ecc_decode_one_pass(data, size, dest, decoded, 1, 0, NULL);
		if (rc != 1) {
			if (!silent)
				printf("MD5 detected corruption of buffer at %p"
				       "; using full ECC to decode\n", data);
			rc = ecc_decode_one_pass(data, size, dest, decoded,
						 silent, 1, error_count);
		}
	}

	return rc;
}

/**
 * Encodes a block of memory.
 *
 * @data:		Pointer to the input data.
 * @size:		The length of the input data.
 * @dest:		The destination for the encoded data.
 * @encoded[inout]:	Takes the length of the output buffer.
 *			Returns the length of the encoded data.
 *
 * Return: 1 if the data was successfully encoded, 0 if there is not
 * enough room in the output buffer.
 */
int ecc_encode(const void *data, unsigned long size, void *dest,
		unsigned long *encoded)
{
	unsigned long encoded_size = ecc_encoded_size(size);
	if (*encoded < encoded_size)
		return 0;

	struct MD5Context md5_context;
	MD5Init(&md5_context);

	rs_state rs;
	initialize_ecc(&rs);

	file_block_t buffer;
	buffer.block_type = ECC_BLOCK_TYPE_DATA;

	const uint8_t *src = data;
	uint8_t *outpos = dest;
	unsigned bytes_left = size;
	do
	{
		void *out_data;
		size_t out_size;
		if (outpos == dest) {
			/*
			 * Fill the header.
			 */
			memcpy(buffer.d.s.header.magic, ECC_FILE_MAGIC,
			       ECC_FILE_MAGIC_LEN);
			buffer.d.s.header.version = ECC_FILE_VERSION;

			out_data = buffer.d.s.short_data;
			out_size = sizeof(buffer.d.s.short_data);
		} else {
			out_data = buffer.d.data;
			out_size = sizeof(buffer.d.data);
		}

		unsigned long bytes_to_copy = out_size;
		if (bytes_to_copy > bytes_left)
			bytes_to_copy = bytes_left;

		memcpy(out_data, src, bytes_to_copy);
		MD5Update(&md5_context, out_data, bytes_to_copy);

		if (bytes_to_copy < out_size) {
			/*
			 * Set the remainder of the block to zeros.
			 */
			memset((uint8_t *)out_data + bytes_to_copy, 0,
			       out_size - bytes_to_copy);
		}

		src += bytes_to_copy;
		bytes_left -= bytes_to_copy;

		if (bytes_left == 0) {
			/*
			 * Set the type of the last block to something
			 * special.  This allows us to read the blocks
			 * from a stream and know before reading the
			 * footer that this block shouldn't be written
			 * whole.
			 */
			buffer.block_type = ECC_BLOCK_TYPE_LAST;
		}

		/*
		 * Add the ECC symbols to the block. Include the
		 * header if necessary.
		 */
		encode_data(&rs, (void *)&buffer, sizeof(file_block_t) - NPAR,
			    (void *)&buffer);

		/*
		 * Write the codeword to the output buffer.
		 */
		memcpy(outpos, &buffer, ECC_BLOCK_SIZE);
		outpos += ECC_BLOCK_SIZE;
	} while (bytes_left > 0);

	/*
	 * Generate the footer. Note the file length is in network order.
	 */
	file_footer_t footer;
	footer.block_type = ECC_BLOCK_TYPE_FOOTER;
	footer.payload_size = cpu_to_be32(size);
	MD5Final(footer.md5sum, &md5_context);

	/*
	 * Add the ECC symbols to the footer.
	 */
	encode_data(&rs, (void *)&footer, sizeof(footer) - NPAR,
		    (void *)&footer);

	/*
	 * Write the footer.
	 */
	memcpy(outpos, &footer, sizeof(footer));
	outpos += sizeof(footer);

	*encoded = encoded_size;

	return 1;
}
