/* Reed Solomon Coding for glyphs
 * Copyright Henry Minsky (hqm@alum.mit.edu) 1991-2009
 *
 * This software library is licensed under terms of the GNU GENERAL
 * PUBLIC LICENSE
 *
 * RSCODE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * RSCODE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Rscode.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Source code is available at http://rscode.sourceforge.net
 *
 * Commercial licensing is available under a separate license, please
 * contact author for details.
 *
 */

#ifndef RSCODE_ECC_H
#define RSCODE_ECC_H

#ifndef TOOLS_BUILD
#include <common.h>
#else
#include <stdint.h>
#endif /* TOOLS_BUILD */

/**
 * The number of parity bytes which will be appended to your data to
 * create a codeword.
 *
 * Note that the maximum codeword size is 255, so the sum of your
 * message length plus parity should be less than or equal to this
 * maximum limit.
 *
 * In practice, you will get slow error correction and decoding if you
 * use more than a reasonably small number of parity bytes (say, 10 or
 * 20).
 */
#define NPAR	32

/**
 * Maximum degree of various polynomials.
 */
#define MAXDEG	(NPAR * 2)

typedef struct rs_state
{
	/**
	 * Encoder parity bytes.
	 */
	uint8_t pBytes[MAXDEG];

	/**
	 * Decoder syndrome bytes.
	 */
	uint8_t synBytes[MAXDEG];

	/**
	 * Generator polynomial.
	 */
	int genPoly[MAXDEG * 2];

	/**
	 * The Error Locator Polynomial, also known as Lambda or
	 * Sigma. Lambda[0] == 1.
	 */
	int Lambda[MAXDEG];

	/**
	 *The Error Evaluator Polynomial.
	 */
	int Omega[MAXDEG];

	/**
	 * Error locations found using Chien's search.
	 */
	uint8_t ErrorLocs[256];

	/**
	 * The number of errors in the ErrorLocs array.
	 */
	uint8_t NErrors;

	/**
	 * Erasure flags.
	 */
	uint8_t ErasureLocs[256];

	/**
	 * The number of erasures in the ErasureLocs array.
	 */
	uint8_t NErasures;
} rs_state;

void initialize_ecc(rs_state *rs);
int check_syndrome(rs_state *rs);

/*
 * Reed Solomon encode/decode routines.
 */
void decode_data(rs_state * rs, unsigned char data[], int nbytes);
void encode_data(rs_state * rs, unsigned char msg[], int nbytes,
		 unsigned char dst[]);

/*
 * Error location routines.
 */
int correct_errors_erasures(rs_state * rs, unsigned char codeword[], int csize,
			    uint8_t nerasures, uint8_t erasures[]);

/*
 * Polynomial arithmetic.
 */
void mult_polys(int dst[], int p1[], uint8_t p2[]);

#define copy_poly(dst,src) { int _i = 0;	\
		for (; _i < MAXDEG; _i++)	\
			dst[_i] = src[_i]; }

#define zero_poly(poly) { int _i = 0; for (; _i < MAXDEG; _i++) poly[_i] = 0; }

/*
 * Precalculated galois arithmetic tables.
 */
extern const uint8_t gexp[];
extern const uint8_t glog[];

/*
 * Multiplication using logarithms.
 */
static inline int gmult(int a, int b)
{
	int i, j;

	if (a == 0 || b == 0)
		return (0);
	i = glog[a];
	j = glog[b];
	return (gexp[(i + j) % 255]);
}

#endif /* !RSCODE_ECC_H */
