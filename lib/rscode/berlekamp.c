/***********************************************************************
 * Copyright Henry Minsky (hqm@alum.mit.edu) 1991-2009
 *
 * This software library is licensed under terms of the GNU GENERAL
 * PUBLIC LICENSE
 *
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
 * Commercial licensing is available under a separate license, please
 * contact author for details.
 *
 * Source code is available at http://rscode.sourceforge.net
 * Berlekamp-Peterson and Berlekamp-Massey Algorithms for error-location
 *
 * From Cain, Clark, "Error-Correction Coding For Digital Communications", pp. 205.
 *
 * This finds the coefficients of the error locator polynomial.
 *
 * The roots are then found by looking for the values of a^n
 * where evaluating the polynomial yields zero.
 *
 * Error correction is done using the error-evaluator equation  on pp 207.
 *
 */

#include "ecc.h"

/* multiply by z, i.e., shift right by 1 */
#define mul_z_poly(src) { int _i = MAXDEG-1; \
			  for (; _i > 0; _i--) \
			      src[_i] = src[_i-1]; \
			   src[0] = 0; }

#define scale_poly(k, poly) { int _i = 0; \
			      for (; _i < MAXDEG; _i++) \
				  poly[_i] = gmult(k, poly[_i]); }

/* local ANSI declarations */
static int compute_discrepancy(int lambda[], uint8_t S[], int L, int n);
static void init_gamma(rs_state * rs, int gamma[]);
static void compute_modified_omega (rs_state * rs);
static int ginv (uint8_t elt);
static void add_polys_i(int dst[], int src[]);

/* From  Cain, Clark, "Error-Correction Coding For Digital Communications", pp. 216. */
static void
Modified_Berlekamp_Massey (rs_state * rs)
{
  int n, L, L2, k, d, i;
  int psi[MAXDEG], psi2[MAXDEG], D[MAXDEG];
  int gamma[MAXDEG];

  /* initialize Gamma, the erasure locator polynomial */
  init_gamma(rs, gamma);

  /* initialize to z */
  copy_poly(D, gamma);
  mul_z_poly(D);

  copy_poly(psi, gamma);
  k = -1; L = rs->NErasures;

  for (n = rs->NErasures; n < NPAR; n++) {

    d = compute_discrepancy(psi, rs->synBytes, L, n);

    if (d != 0) {

      /* psi2 = psi - d*D */
      for (i = 0; i < MAXDEG; i++) psi2[i] = psi[i] ^ gmult( d, D[i]);


      if (L < (n-k)) {
	L2 = n-k;
	k = n-L;
	/* D = scale_poly(ginv(d), psi); */
	for (i = 0; i < MAXDEG; i++) D[i] = gmult(psi[i], ginv(d));
	L = L2;
      }

      /* psi = psi2 */
      for (i = 0; i < MAXDEG; i++) psi[i] = psi2[i];
    }

    mul_z_poly(D);
  }

  for(i = 0; i < MAXDEG; i++) rs->Lambda[i] = psi[i];
  compute_modified_omega(rs);


}

int
ginv (uint8_t elt)
{
    return (gexp[(255 - glog[elt]) % 255]);
}

/* given Psi (called Lambda in Modified_Berlekamp_Massey) and synBytes,
   compute the combined erasure/error evaluator polynomial as
   Psi*S mod z^4
  */
void
compute_modified_omega (rs_state * rs)
{
  int i;
  int product[MAXDEG*2];

  mult_polys(product, rs->Lambda, rs->synBytes);
  zero_poly(rs->Omega);
  for(i = 0; i < NPAR; i++) rs->Omega[i] = product[i];

}

/* polynomial multiplication */
void
mult_polys (int dst[], int p1[], uint8_t p2[])
{
  int i, j;
  int tmp1[MAXDEG*2];

  for (i=0; i < (MAXDEG*2); i++) dst[i] = 0;

  for (i = 0; i < MAXDEG; i++) {
    for(j=MAXDEG; j<(MAXDEG*2); j++) tmp1[j]=0;

    /* scale tmp1 by p1[i] */
    for(j=0; j<MAXDEG; j++) tmp1[j]=gmult(p2[j], p1[i]);
    /* and mult (shift) tmp1 right by i */
    for (j = (MAXDEG*2)-1; j >= i; j--) tmp1[j] = tmp1[j-i];
    for (j = 0; j < i; j++) tmp1[j] = 0;

    /* add into partial product */
    for(j=0; j < (MAXDEG*2); j++) dst[j] ^= tmp1[j];
  }
}



/* gamma = product (1-z*a^Ij) for erasure locs Ij */
void
init_gamma (rs_state * rs, int gamma[])
{
  int e, tmp[MAXDEG];

  zero_poly(gamma);
  zero_poly(tmp);
  gamma[0] = 1;

  for (e = 0; e < rs->NErasures; e++) {
    copy_poly(tmp, gamma);
    scale_poly(gexp[(rs->ErasureLocs[e])%255], tmp);
    mul_z_poly(tmp);
    add_polys_i(gamma, tmp);
  }
}



void
compute_next_omega (int d, int A[], int dst[], int src[])
{
  int i;
  for ( i = 0; i < MAXDEG;  i++) {
    dst[i] = src[i] ^ gmult(d, A[i]);
  }
}



int
compute_discrepancy (int lambda[], uint8_t S[], int L, int n)
{
  int i, sum=0;

  for (i = 0; i <= L; i++)
    sum ^= gmult(lambda[i], S[n-i]);
  return (sum);
}

/********** polynomial arithmetic *******************/

void add_polys_i(int dst[], int src[])
{
  int i;
  for (i = 0; i < MAXDEG; i++) dst[i] ^= src[i];
}


/* Finds all the roots of an error-locator polynomial with coefficients
 * Lambda[j] by evaluating Lambda at successive values of alpha.
 *
 * This can be tested with the decoder's equations case.
 */


void
Find_Roots (rs_state * rs)
{
  int sum, r, k;
  rs->NErrors = 0;

  for (r = 1; r < 256; r++) {
    sum = 0;
    /* evaluate lambda at r */
    for (k = 0; k < NPAR+1; k++) {
      sum ^= gmult(gexp[(k*r)%255], rs->Lambda[k]);
    }
    if (sum == 0)
      {
	rs->ErrorLocs[rs->NErrors] = (255-r); rs->NErrors++;
      }
  }
}

/* Combined Erasure And Error Magnitude Computation
 *
 * Pass in the codeword, its size in bytes, as well as
 * an array of any known erasure locations, along the number
 * of these erasures.
 *
 * Evaluate Omega(actually Psi)/Lambda' at the roots
 * alpha^(-i) for error locs i.
 *
 * Returns 1 if everything ok, or 0 if an out-of-bounds error is found
 *
 */

int
correct_errors_erasures (rs_state * rs,
			 unsigned char codeword[],
			 int csize,
			 uint8_t nerasures,
			 uint8_t erasures[])
{
  int r, i, j, err;

  /* If you want to take advantage of erasure correction, be sure to
     set NErasures and ErasureLocs[] with the locations of erasures.
     */
  rs->NErasures = nerasures;
  for (i = 0; i < rs->NErasures; i++) rs->ErasureLocs[i] = erasures[i];

  Modified_Berlekamp_Massey(rs);
  Find_Roots(rs);

  if ((rs->NErrors <= NPAR) && rs->NErrors > 0) {

    /* first check for illegal error locs */
    for (r = 0; r < rs->NErrors; r++) {
      if (rs->ErrorLocs[r] >= csize) {
	return(0);
      }
    }

    for (r = 0; r < rs->NErrors; r++) {
      int num, denom;
      i = rs->ErrorLocs[r];
      /* evaluate Omega at alpha^(-i) */

      num = 0;
      for (j = 0; j < MAXDEG; j++)
	num ^= gmult(rs->Omega[j], gexp[((255-i)*j)%255]);

      /* evaluate Lambda' (derivative) at alpha^(-i) ; all odd powers disappear */
      denom = 0;
      for (j = 1; j < MAXDEG; j += 2) {
	denom ^= gmult(rs->Lambda[j], gexp[((255-i)*(j-1)) % 255]);
      }

      err = gmult(num, ginv(denom));

      codeword[csize-i-1] ^= err;
    }
    return(1);
  }
  else {
    return(0);
  }
}

