/* Copyright (C) 2007  Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file into combinations with other programs,
and to distribute those combinations without any restriction coming
from the use of this file.  (The General Public License restrictions
do apply in other respects; for example, they cover modification of
the file, and distribution when not linked into a combine
executable.)

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

#include "bid_internal.h"

/*****************************************************************************
 *  BID64 minimum function - returns greater of two numbers
 *****************************************************************************/

static const UINT64 mult_factor[16] = {
  1ull, 10ull, 100ull, 1000ull,
  10000ull, 100000ull, 1000000ull, 10000000ull,
  100000000ull, 1000000000ull, 10000000000ull, 100000000000ull,
  1000000000000ull, 10000000000000ull,
  100000000000000ull, 1000000000000000ull
};

#if DECIMAL_CALL_BY_REFERENCE
void
__bid64_minnum (UINT64 * pres, UINT64 * px, UINT64 * py) {
  UINT64 x = *px;
  UINT64 y = *py;
#else
UINT64
__bid64_minnum (UINT64 x, UINT64 y) {
#endif

  UINT64 res;
  int exp_x, exp_y;
  UINT64 sig_x, sig_y;
  UINT128 sig_n_prime;
  char x_is_zero = 0, y_is_zero = 0, non_canon_x, non_canon_y;

  // NaN (CASE1)
  // if x is NAN, then return y
  if ((x & MASK_NAN) == MASK_NAN) {
    if ((x & 0x0200000000000000ull) == 0x0200000000000000ull) {
      ; // *pfpsf |= INVALID_EXCEPTION;        // set exception if sNaN
    }
    // check if y is SNaN
    if ((y & MASK_SNAN) == MASK_SNAN) {
      ; // *pfpsf |= INVALID_EXCEPTION; // set invalid status flag if sNaN 
      // return quiet (SNaN) 
      ; // y = y & 0xfdffffffffffffffull;
    }
    res = y;
    BID_RETURN (res);
  }
  // if y is NAN, then return x
  else if ((y & MASK_NAN) == MASK_NAN) {
    if ((y & 0x0200000000000000ull) == 0x0200000000000000ull) {
      ; // *pfpsf |= INVALID_EXCEPTION;        // set exception if sNaN
    }
    res = x;
    BID_RETURN (res);
  }
  // SIMPLE (CASE2)
  // if all the bits are the same, these numbers are equal, return either number
  if (x == y) {
    res = x;
    BID_RETURN (res);
  }
  // INFINITY (CASE3)
  if ((x & MASK_INF) == MASK_INF) {
    // if x is neg infinity, there is no way it is greater than y, return x
    if (((x & MASK_SIGN) == MASK_SIGN)) {
      res = x;
      BID_RETURN (res);
    }
    // x is pos infinity, return y
    else {
      res = y;
      BID_RETURN (res);
    }
  } else if ((y & MASK_INF) == MASK_INF) {
    // x is finite, so if y is positive infinity, then x is less, return y
    //                 if y is negative infinity, then x is greater, return x
    res = ((y & MASK_SIGN) == MASK_SIGN) ? y : x;
    BID_RETURN (res);
  }
  // if steering bits are 11 (condition will be 0), then exponent is G[0:w+1] =>
  if ((x & MASK_STEERING_BITS) == MASK_STEERING_BITS) {
    exp_x = (x & MASK_BINARY_EXPONENT2) >> 51;
    sig_x = (x & MASK_BINARY_SIG2) | MASK_BINARY_OR2;
    if (sig_x > 9999999999999999ull) {
      non_canon_x = 1;
    } else {
      non_canon_x = 0;
    }
  } else {
    exp_x = (x & MASK_BINARY_EXPONENT1) >> 53;
    sig_x = (x & MASK_BINARY_SIG1);
    non_canon_x = 0;
  }

  // if steering bits are 11 (condition will be 0), then exponent is G[0:w+1] =>
  if ((y & MASK_STEERING_BITS) == MASK_STEERING_BITS) {
    exp_y = (y & MASK_BINARY_EXPONENT2) >> 51;
    sig_y = (y & MASK_BINARY_SIG2) | MASK_BINARY_OR2;
    if (sig_y > 9999999999999999ull) {
      non_canon_y = 1;
    } else {
      non_canon_y = 0;
    }
  } else {
    exp_y = (y & MASK_BINARY_EXPONENT1) >> 53;
    sig_y = (y & MASK_BINARY_SIG1);
    non_canon_y = 0;
  }

  // ZERO (CASE4)
  // some properties:
  //    (+ZERO == -ZERO) => therefore 
  //        ignore the sign, and neither number is greater
  //    (ZERO x 10^A == ZERO x 10^B) for any valid A, B => 
  //        ignore the exponent field
  //    (Any non-canonical # is considered 0)
  if (non_canon_x || sig_x == 0) {
    x_is_zero = 1;
  }
  if (non_canon_y || sig_y == 0) {
    y_is_zero = 1;
  }

  if (x_is_zero && y_is_zero) {
    // if both numbers are zero, neither is greater => return either
    res = y;
    BID_RETURN (res);
  } else if (x_is_zero) {
    // is x is zero, it is greater if Y is negative
    res = ((y & MASK_SIGN) == MASK_SIGN) ? y : x;
    BID_RETURN (res);
  } else if (y_is_zero) {
    // is y is zero, X is greater if it is positive
    res = ((x & MASK_SIGN) != MASK_SIGN) ? y : x;;
    BID_RETURN (res);
  }
  // OPPOSITE SIGN (CASE5)
  // now, if the sign bits differ, x is greater if y is negative
  if (((x ^ y) & MASK_SIGN) == MASK_SIGN) {
    res = ((y & MASK_SIGN) == MASK_SIGN) ? y : x;
    BID_RETURN (res);
  }
  // REDUNDANT REPRESENTATIONS (CASE6)

  // if both components are either bigger or smaller, 
  // it is clear what needs to be done
  if (sig_x > sig_y && exp_x >= exp_y) {
    res = ((x & MASK_SIGN) != MASK_SIGN) ? y : x;
    BID_RETURN (res);
  }
  if (sig_x < sig_y && exp_x <= exp_y) {
    res = ((x & MASK_SIGN) == MASK_SIGN) ? y : x;
    BID_RETURN (res);
  }
  // if exp_x is 15 greater than exp_y, no need for compensation
  if (exp_x - exp_y > 15) {
    res = ((x & MASK_SIGN) != MASK_SIGN) ? y : x; // difference cannot be >10^15
    BID_RETURN (res);
  }
  // if exp_x is 15 less than exp_y, no need for compensation
  if (exp_y - exp_x > 15) {
    res = ((x & MASK_SIGN) == MASK_SIGN) ? y : x;
    BID_RETURN (res);
  }
  // if |exp_x - exp_y| < 15, it comes down to the compensated significand
  if (exp_x > exp_y) { // to simplify the loop below,

    // otherwise adjust the x significand upwards
    __mul_64x64_to_128MACH (sig_n_prime, sig_x,
			    mult_factor[exp_x - exp_y]);
    // if postitive, return whichever significand is larger 
    // (converse if negative)
    if (sig_n_prime.w[1] == 0 && (sig_n_prime.w[0] == sig_y)) {
      res = y;
      BID_RETURN (res);
    }

    res = (((sig_n_prime.w[1] > 0)
	    || sig_n_prime.w[0] > sig_y) ^ ((x & MASK_SIGN) ==
					    MASK_SIGN)) ? y : x;
    BID_RETURN (res);
  }
  // adjust the y significand upwards
  __mul_64x64_to_128MACH (sig_n_prime, sig_y,
			  mult_factor[exp_y - exp_x]);

  // if postitive, return whichever significand is larger (converse if negative)
  if (sig_n_prime.w[1] == 0 && (sig_n_prime.w[0] == sig_x)) {
    res = y;
    BID_RETURN (res);
  }
  res = (((sig_n_prime.w[1] == 0)
	  && (sig_x > sig_n_prime.w[0])) ^ ((x & MASK_SIGN) ==
					    MASK_SIGN)) ? y : x;
  BID_RETURN (res);
}

/*****************************************************************************
 *  BID64 minimum magnitude function - returns greater of two numbers
 *****************************************************************************/

#if DECIMAL_CALL_BY_REFERENCE
void
__bid64_minnum_mag (UINT64 * pres, UINT64 * px, UINT64 * py) {
  UINT64 x = *px;
  UINT64 y = *py;
#else
UINT64
__bid64_minnum_mag (UINT64 x, UINT64 y) {
#endif

  UINT64 res;
  int exp_x, exp_y;
  UINT64 sig_x, sig_y;
  UINT128 sig_n_prime;
  char non_canon_x, non_canon_y;

  // NaN (CASE1)
  if ((x & MASK_NAN) == MASK_NAN) {
    // if x is NAN, then return y
    if ((x & 0x0200000000000000ull) == 0x0200000000000000ull) {
      ; // *pfpsf |= INVALID_EXCEPTION;        // set exception if sNaN
    }
    // check if y is SNaN
    if ((y & MASK_SNAN) == MASK_SNAN) {
      ; // *pfpsf |= INVALID_EXCEPTION; // set invalid status flag if sNaN
      // return quiet (SNaN)
      ; // y = y & 0xfdffffffffffffffull;
    }
    res = y;
    BID_RETURN (res);
  } else if ((y & MASK_NAN) == MASK_NAN) {
    // if y is NAN, then return x
    if ((y & 0x0200000000000000ull) == 0x0200000000000000ull) {
      ; // *pfpsf |= INVALID_EXCEPTION;        // set exception if sNaN
    }
    res = x;
    BID_RETURN (res);
  }
  // SIMPLE (CASE2)
  // if all the bits are the same, these numbers are equal, return either number
  if (x == y) {
    res = x;
    BID_RETURN (res);
  }
  // INFINITY (CASE3)
  if ((x & MASK_INF) == MASK_INF) {
    // x is infinity, its magnitude is greater than or equal to y
    // return x only if y is infinity and x is negative
    res = ((x & MASK_SIGN) == MASK_SIGN
	   && (y & MASK_INF) == MASK_INF) ? x : y;
    BID_RETURN (res);
  } else if ((y & MASK_INF) == MASK_INF) {
    // y is infinity, then it must be greater in magnitude, return x
    res = x;
    BID_RETURN (res);
  }
  // if steering bits are 11 (condition will be 0), then exponent is G[0:w+1] =>
  if ((x & MASK_STEERING_BITS) == MASK_STEERING_BITS) {
    exp_x = (x & MASK_BINARY_EXPONENT2) >> 51;
    sig_x = (x & MASK_BINARY_SIG2) | MASK_BINARY_OR2;
    if (sig_x > 9999999999999999ull) {
      non_canon_x = 1;
    } else {
      non_canon_x = 0;
    }
  } else {
    exp_x = (x & MASK_BINARY_EXPONENT1) >> 53;
    sig_x = (x & MASK_BINARY_SIG1);
    non_canon_x = 0;
  }

  // if steering bits are 11 (condition will be 0), then exponent is G[0:w+1] =>
  if ((y & MASK_STEERING_BITS) == MASK_STEERING_BITS) {
    exp_y = (y & MASK_BINARY_EXPONENT2) >> 51;
    sig_y = (y & MASK_BINARY_SIG2) | MASK_BINARY_OR2;
    if (sig_y > 9999999999999999ull) {
      non_canon_y = 1;
    } else {
      non_canon_y = 0;
    }
  } else {
    exp_y = (y & MASK_BINARY_EXPONENT1) >> 53;
    sig_y = (y & MASK_BINARY_SIG1);
    non_canon_y = 0;
  }

  // ZERO (CASE4)
  // some properties:
  //    (+ZERO == -ZERO) => therefore 
  //        ignore the sign, and neither number is greater
  //    (ZERO x 10^A == ZERO x 10^B) for any valid A, B => 
  //        ignore the exponent field
  //    (Any non-canonical # is considered 0)
  if (non_canon_x || sig_x == 0) {
    res = x; // x_is_zero, its magnitude must be smaller than y
    BID_RETURN (res);
  }
  if (non_canon_y || sig_y == 0) {
    res = y; // y_is_zero, its magnitude must be smaller than x
    BID_RETURN (res);
  }
  // REDUNDANT REPRESENTATIONS (CASE6)
  // if both components are either bigger or smaller, 
  // it is clear what needs to be done
  if (sig_x > sig_y && exp_x >= exp_y) {
    res = y;
    BID_RETURN (res);
  }
  if (sig_x < sig_y && exp_x <= exp_y) {
    res = x;
    BID_RETURN (res);
  }
  // if exp_x is 15 greater than exp_y, no need for compensation
  if (exp_x - exp_y > 15) {
    res = y; // difference cannot be greater than 10^15
    BID_RETURN (res);
  }
  // if exp_x is 15 less than exp_y, no need for compensation
  if (exp_y - exp_x > 15) {
    res = x;
    BID_RETURN (res);
  }
  // if |exp_x - exp_y| < 15, it comes down to the compensated significand
  if (exp_x > exp_y) { // to simplify the loop below,
    // otherwise adjust the x significand upwards
    __mul_64x64_to_128MACH (sig_n_prime, sig_x,
			    mult_factor[exp_x - exp_y]);
    // now, sig_n_prime has: sig_x * 10^(exp_x-exp_y), this is 
    // the compensated signif.
    if (sig_n_prime.w[1] == 0 && (sig_n_prime.w[0] == sig_y)) {
      // two numbers are equal, return minNum(x,y)
      res = ((y & MASK_SIGN) == MASK_SIGN) ? y : x;
      BID_RETURN (res);
    }
    // now, if compensated_x (sig_n_prime) is greater than y, return y,  
    // otherwise return x
    res = ((sig_n_prime.w[1] != 0) || sig_n_prime.w[0] > sig_y) ? y : x;
    BID_RETURN (res);
  }
  // exp_y must be greater than exp_x, thus adjust the y significand upwards
  __mul_64x64_to_128MACH (sig_n_prime, sig_y,
			  mult_factor[exp_y - exp_x]);

  if (sig_n_prime.w[1] == 0 && (sig_n_prime.w[0] == sig_x)) {
    res = ((y & MASK_SIGN) == MASK_SIGN) ? y : x; 
        // two numbers are equal, return either
    BID_RETURN (res);
  }

  res = ((sig_n_prime.w[1] == 0) && (sig_x > sig_n_prime.w[0])) ? y : x;
  BID_RETURN (res);
}

/*****************************************************************************
 *  BID64 maximum function - returns greater of two numbers
 *****************************************************************************/

#if DECIMAL_CALL_BY_REFERENCE
void
__bid64_maxnum (UINT64 * pres, UINT64 * px, UINT64 * py) {
  UINT64 x = *px;
  UINT64 y = *py;
#else
UINT64
__bid64_maxnum (UINT64 x, UINT64 y) {
#endif

  UINT64 res;
  int exp_x, exp_y;
  UINT64 sig_x, sig_y;
  UINT128 sig_n_prime;
  char x_is_zero = 0, y_is_zero = 0, non_canon_x, non_canon_y;

  // NaN (CASE1)
  if ((x & MASK_NAN) == MASK_NAN) {
    // if x is NAN, then return y
    if ((x & 0x0200000000000000ull) == 0x0200000000000000ull) {
      ; // *pfpsf |= INVALID_EXCEPTION;        // set exception if sNaN
    }
    // check if y is SNaN
    if ((y & MASK_SNAN) == MASK_SNAN) {
      ; // *pfpsf |= INVALID_EXCEPTION; // set invalid status flag if sNaN
      // return quiet (SNaN)
      ; // y = y & 0xfdffffffffffffffull;
    }
    res = y;
    BID_RETURN (res);
  } else if ((y & MASK_NAN) == MASK_NAN) {
    // if y is NAN, then return x
    if ((y & 0x0200000000000000ull) == 0x0200000000000000ull) {
      ; // *pfpsf |= INVALID_EXCEPTION;        // set exception if sNaN
    }
    res = x;
    BID_RETURN (res);
  }
  // SIMPLE (CASE2)
  // if all the bits are the same, these numbers are equal (not Greater).
  if (x == y) {
    res = x;
    BID_RETURN (res);
  }
  // INFINITY (CASE3)
  if ((x & MASK_INF) == MASK_INF) {
    // if x is neg infinity, there is no way it is greater than y, return y
    // x is pos infinity, it is greater, unless y is positive infinity => 
    // return y!=pos_infinity
    if (((x & MASK_SIGN) == MASK_SIGN)) {
      res = y;
      BID_RETURN (res);
    } else {
      res = (((y & MASK_INF) != MASK_INF)
	     || ((y & MASK_SIGN) == MASK_SIGN)) ? x : y;
      BID_RETURN (res);
    }
  } else if ((y & MASK_INF) == MASK_INF) {
    // x is finite, so if y is positive infinity, then x is less, return y
    //                 if y is negative infinity, then x is greater, return x
    res = ((y & MASK_SIGN) == MASK_SIGN) ? x : y;
    BID_RETURN (res);
  }
  // if steering bits are 11 (condition will be 0), then exponent is G[0:w+1] =>
  if ((x & MASK_STEERING_BITS) == MASK_STEERING_BITS) {
    exp_x = (x & MASK_BINARY_EXPONENT2) >> 51;
    sig_x = (x & MASK_BINARY_SIG2) | MASK_BINARY_OR2;
    if (sig_x > 9999999999999999ull) {
      non_canon_x = 1;
    } else {
      non_canon_x = 0;
    }
  } else {
    exp_x = (x & MASK_BINARY_EXPONENT1) >> 53;
    sig_x = (x & MASK_BINARY_SIG1);
    non_canon_x = 0;
  }

  // if steering bits are 11 (condition will be 0), then exponent is G[0:w+1] =>
  if ((y & MASK_STEERING_BITS) == MASK_STEERING_BITS) {
    exp_y = (y & MASK_BINARY_EXPONENT2) >> 51;
    sig_y = (y & MASK_BINARY_SIG2) | MASK_BINARY_OR2;
    if (sig_y > 9999999999999999ull) {
      non_canon_y = 1;
    } else {
      non_canon_y = 0;
    }
  } else {
    exp_y = (y & MASK_BINARY_EXPONENT1) >> 53;
    sig_y = (y & MASK_BINARY_SIG1);
    non_canon_y = 0;
  }

  // ZERO (CASE4)
  // some properties:
  //    (+ZERO == -ZERO) => therefore 
  //        ignore the sign, and neither number is greater
  //    (ZERO x 10^A == ZERO x 10^B) for any valid A, B => 
  //        ignore the exponent field
  //    (Any non-canonical # is considered 0)
  if (non_canon_x || sig_x == 0) {
    x_is_zero = 1;
  }
  if (non_canon_y || sig_y == 0) {
    y_is_zero = 1;
  }

  if (x_is_zero && y_is_zero) {
    // if both numbers are zero, neither is greater => return NOTGREATERTHAN
    res = y;
    BID_RETURN (res);
  } else if (x_is_zero) {
    // is x is zero, it is greater if Y is negative
    res = ((y & MASK_SIGN) == MASK_SIGN) ? x : y;
    BID_RETURN (res);
  } else if (y_is_zero) {
    // is y is zero, X is greater if it is positive
    res = ((x & MASK_SIGN) != MASK_SIGN) ? x : y;;
    BID_RETURN (res);
  }
  // OPPOSITE SIGN (CASE5)
  // now, if the sign bits differ, x is greater if y is negative
  if (((x ^ y) & MASK_SIGN) == MASK_SIGN) {
    res = ((y & MASK_SIGN) == MASK_SIGN) ? x : y;
    BID_RETURN (res);
  }
  // REDUNDANT REPRESENTATIONS (CASE6)

  // if both components are either bigger or smaller, 
  //     it is clear what needs to be done
  if (sig_x > sig_y && exp_x >= exp_y) {
    res = ((x & MASK_SIGN) != MASK_SIGN) ? x : y;
    BID_RETURN (res);
  }
  if (sig_x < sig_y && exp_x <= exp_y) {
    res = ((x & MASK_SIGN) == MASK_SIGN) ? x : y;
    BID_RETURN (res);
  }
  // if exp_x is 15 greater than exp_y, no need for compensation
  if (exp_x - exp_y > 15) {
    res = ((x & MASK_SIGN) != MASK_SIGN) ? x : y; 
        // difference cannot be > 10^15
    BID_RETURN (res);
  }
  // if exp_x is 15 less than exp_y, no need for compensation
  if (exp_y - exp_x > 15) {
    res = ((x & MASK_SIGN) == MASK_SIGN) ? x : y;
    BID_RETURN (res);
  }
  // if |exp_x - exp_y| < 15, it comes down to the compensated significand
  if (exp_x > exp_y) { // to simplify the loop below,
    // otherwise adjust the x significand upwards
    __mul_64x64_to_128MACH (sig_n_prime, sig_x,
			    mult_factor[exp_x - exp_y]);
    // if postitive, return whichever significand is larger 
    // (converse if negative)
    if (sig_n_prime.w[1] == 0 && (sig_n_prime.w[0] == sig_y)) {
      res = y;
      BID_RETURN (res);
    }
    res = (((sig_n_prime.w[1] > 0)
	    || sig_n_prime.w[0] > sig_y) ^ ((x & MASK_SIGN) ==
					    MASK_SIGN)) ? x : y;
    BID_RETURN (res);
  }
  // adjust the y significand upwards
  __mul_64x64_to_128MACH (sig_n_prime, sig_y,
			  mult_factor[exp_y - exp_x]);

  // if postitive, return whichever significand is larger (converse if negative)
  if (sig_n_prime.w[1] == 0 && (sig_n_prime.w[0] == sig_x)) {
    res = y;
    BID_RETURN (res);
  }
  res = (((sig_n_prime.w[1] == 0)
	  && (sig_x > sig_n_prime.w[0])) ^ ((x & MASK_SIGN) ==
					    MASK_SIGN)) ? x : y;
  BID_RETURN (res);
}

/*****************************************************************************
 *  BID64 maximum magnitude function - returns greater of two numbers
 *****************************************************************************/

#if DECIMAL_CALL_BY_REFERENCE
void
__bid64_maxnum_mag (UINT64 * pres, UINT64 * px, UINT64 * py) {
  UINT64 x = *px;
  UINT64 y = *py;
#else
UINT64
__bid64_maxnum_mag (UINT64 x, UINT64 y) {
#endif

  UINT64 res;
  int exp_x, exp_y;
  UINT64 sig_x, sig_y;
  UINT128 sig_n_prime;
  char non_canon_x, non_canon_y;

  // NaN (CASE1)
  if ((x & MASK_NAN) == MASK_NAN) {
    // if x is NAN, then return y
    if ((x & 0x0200000000000000ull) == 0x0200000000000000ull) {
      ; // *pfpsf |= INVALID_EXCEPTION;        // set exception if sNaN
    }
    // check if y is SNaN
    if ((y & MASK_SNAN) == MASK_SNAN) {
      ; // *pfpsf |= INVALID_EXCEPTION; // set invalid status flag if sNaN
      // return quiet (SNaN)
      ; // y = y & 0xfdffffffffffffffull;
    }
    res = y;
    BID_RETURN (res);
  } else if ((y & MASK_NAN) == MASK_NAN) {
    // if y is NAN, then return x
    if ((y & 0x0200000000000000ull) == 0x0200000000000000ull) {
      ; // *pfpsf |= INVALID_EXCEPTION;        // set exception if sNaN
    }
    res = x;
    BID_RETURN (res);
  }
  // SIMPLE (CASE2)
  // if all the bits are the same, these numbers are equal, return either number
  if (x == y) {
    res = x;
    BID_RETURN (res);
  }
  // INFINITY (CASE3)
  if ((x & MASK_INF) == MASK_INF) {
    // x is infinity, its magnitude is greater than or equal to y
    // return y as long as x isn't negative infinity
    res = ((x & MASK_SIGN) == MASK_SIGN
	   && (y & MASK_INF) == MASK_INF) ? y : x;
    BID_RETURN (res);
  } else if ((y & MASK_INF) == MASK_INF) {
    // y is infinity, then it must be greater in magnitude
    res = y;
    BID_RETURN (res);
  }
  // if steering bits are 11 (condition will be 0), then exponent is G[0:w+1] =>
  if ((x & MASK_STEERING_BITS) == MASK_STEERING_BITS) {
    exp_x = (x & MASK_BINARY_EXPONENT2) >> 51;
    sig_x = (x & MASK_BINARY_SIG2) | MASK_BINARY_OR2;
    if (sig_x > 9999999999999999ull) {
      non_canon_x = 1;
    } else {
      non_canon_x = 0;
    }
  } else {
    exp_x = (x & MASK_BINARY_EXPONENT1) >> 53;
    sig_x = (x & MASK_BINARY_SIG1);
    non_canon_x = 0;
  }

  // if steering bits are 11 (condition will be 0), then exponent is G[0:w+1] =>
  if ((y & MASK_STEERING_BITS) == MASK_STEERING_BITS) {
    exp_y = (y & MASK_BINARY_EXPONENT2) >> 51;
    sig_y = (y & MASK_BINARY_SIG2) | MASK_BINARY_OR2;
    if (sig_y > 9999999999999999ull) {
      non_canon_y = 1;
    } else {
      non_canon_y = 0;
    }
  } else {
    exp_y = (y & MASK_BINARY_EXPONENT1) >> 53;
    sig_y = (y & MASK_BINARY_SIG1);
    non_canon_y = 0;
  }

  // ZERO (CASE4)
  // some properties:
  //    (+ZERO == -ZERO) => therefore 
  //        ignore the sign, and neither number is greater
  //    (ZERO x 10^A == ZERO x 10^B) for any valid A, B => 
  //        ignore the exponent field
  //    (Any non-canonical # is considered 0)
  if (non_canon_x || sig_x == 0) {
    res = y; // x_is_zero, its magnitude must be smaller than y
    BID_RETURN (res);
  }
  if (non_canon_y || sig_y == 0) {
    res = x; // y_is_zero, its magnitude must be smaller than x
    BID_RETURN (res);
  }
  // REDUNDANT REPRESENTATIONS (CASE6)
  // if both components are either bigger or smaller, 
  // it is clear what needs to be done
  if (sig_x > sig_y && exp_x >= exp_y) {
    res = x;
    BID_RETURN (res);
  }
  if (sig_x < sig_y && exp_x <= exp_y) {
    res = y;
    BID_RETURN (res);
  }
  // if exp_x is 15 greater than exp_y, no need for compensation
  if (exp_x - exp_y > 15) {
    res = x; // difference cannot be greater than 10^15
    BID_RETURN (res);
  }
  // if exp_x is 15 less than exp_y, no need for compensation
  if (exp_y - exp_x > 15) {
    res = y;
    BID_RETURN (res);
  }
  // if |exp_x - exp_y| < 15, it comes down to the compensated significand
  if (exp_x > exp_y) { // to simplify the loop below,
    // otherwise adjust the x significand upwards
    __mul_64x64_to_128MACH (sig_n_prime, sig_x,
			    mult_factor[exp_x - exp_y]);
    // now, sig_n_prime has: sig_x * 10^(exp_x-exp_y), 
    // this is the compensated signif.
    if (sig_n_prime.w[1] == 0 && (sig_n_prime.w[0] == sig_y)) {
      // two numbers are equal, return maxNum(x,y)
      res = ((y & MASK_SIGN) == MASK_SIGN) ? x : y;
      BID_RETURN (res);
    }
    // now, if compensated_x (sig_n_prime) is greater than y return y,  
    // otherwise return x
    res = ((sig_n_prime.w[1] != 0) || sig_n_prime.w[0] > sig_y) ? x : y;
    BID_RETURN (res);
  }
  // exp_y must be greater than exp_x, thus adjust the y significand upwards
  __mul_64x64_to_128MACH (sig_n_prime, sig_y,
			  mult_factor[exp_y - exp_x]);

  if (sig_n_prime.w[1] == 0 && (sig_n_prime.w[0] == sig_x)) {
    res = ((y & MASK_SIGN) == MASK_SIGN) ? x : y; 
        // two numbers are equal, return either
    BID_RETURN (res);
  }

  res = ((sig_n_prime.w[1] == 0) && (sig_x > sig_n_prime.w[0])) ? x : y;
  BID_RETURN (res);
}
