/* More subroutines needed by GCC output code on some machines.  */
/* Compile this one with gcc.  */
/* Copyright (C) 1989, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2001, 2002  Free Software Foundation, Inc.

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
Software Foundation, 59 Temple Place - Suite 330, Boston, MA
02111-1307, USA.  */

/* It is incorrect to include config.h here, because this file is being
   compiled for the target, and hence definitions concerning only the host
   do not apply.  */

#include "tconfig.h"
#include "tsystem.h"

/* Don't use `fancy_abort' here even if config.h says to use it.  */
#ifdef abort
#undef abort
#endif

#include "libgcc2.h"

#ifdef DECLARE_LIBRARY_RENAMES
  DECLARE_LIBRARY_RENAMES
#endif

#if defined (L_negdi2)
DWtype
__negdi2 (DWtype u)
{
  DWunion w;
  DWunion uu;

  uu.ll = u;

  w.s.low = -uu.s.low;
  w.s.high = -uu.s.high - ((UWtype) w.s.low > 0);

  return w.ll;
}
#endif

#ifdef L_addvsi3
Wtype
__addvsi3 (Wtype a, Wtype b)
{
  Wtype w;

  w = a + b;

  if (b >= 0 ? w < a : w > a)
    abort ();

  return w;
}
#endif

#ifdef L_addvdi3
DWtype
__addvdi3 (DWtype a, DWtype b)
{
  DWtype w;

  w = a + b;

  if (b >= 0 ? w < a : w > a)
    abort ();

  return w;
}
#endif

#ifdef L_subvsi3
Wtype
__subvsi3 (Wtype a, Wtype b)
{
#ifdef L_addvsi3
  return __addvsi3 (a, (-b));
#else
  DWtype w;

  w = a - b;

  if (b >= 0 ? w > a : w < a)
    abort ();

  return w;
#endif
}
#endif

#ifdef L_subvdi3
DWtype
__subvdi3 (DWtype a, DWtype b)
{
#ifdef L_addvdi3
  return (a, (-b));
#else
  DWtype w;

  w = a - b;

  if (b >= 0 ? w > a : w < a)
    abort ();

  return w;
#endif
}
#endif

#ifdef L_mulvsi3
Wtype
__mulvsi3 (Wtype a, Wtype b)
{
  DWtype w;

  w = a * b;

  if (((a >= 0) == (b >= 0)) ? w < 0 : w > 0)
    abort ();

  return w;
}
#endif

#ifdef L_negvsi2
Wtype
__negvsi2 (Wtype a)
{
  Wtype w;

  w  = -a;

  if (a >= 0 ? w > 0 : w < 0)
    abort ();

   return w;
}
#endif

#ifdef L_negvdi2
DWtype
__negvdi2 (DWtype a)
{
  DWtype w;

  w  = -a;

  if (a >= 0 ? w > 0 : w < 0)
    abort ();

  return w;
}
#endif

#ifdef L_absvsi2
Wtype
__absvsi2 (Wtype a)
{
  Wtype w = a;

  if (a < 0)
#ifdef L_negvsi2
    w = __negvsi2 (a);
#else
    w = -a;

  if (w < 0)
    abort ();
#endif

   return w;
}
#endif

#ifdef L_absvdi2
DWtype
__absvdi2 (DWtype a)
{
  DWtype w = a;

  if (a < 0)
#ifdef L_negvsi2
    w = __negvsi2 (a);
#else
    w = -a;

  if (w < 0)
    abort ();
#endif

  return w;
}
#endif

#ifdef L_mulvdi3
DWtype
__mulvdi3 (DWtype u, DWtype v)
{
  DWtype w;

  w = u * v;

  if (((u >= 0) == (v >= 0)) ? w < 0 : w > 0)
    abort ();

  return w;
}
#endif


/* Unless shift functions are defined whith full ANSI prototypes,
   parameter b will be promoted to int if word_type is smaller than an int.  */
#ifdef L_lshrdi3
DWtype
__lshrdi3 (DWtype u, word_type b)
{
  DWunion w;
  word_type bm;
  DWunion uu;

  if (b == 0)
    return u;

  uu.ll = u;

  bm = (sizeof (Wtype) * BITS_PER_UNIT) - b;
  if (bm <= 0)
    {
      w.s.high = 0;
      w.s.low = (UWtype) uu.s.high >> -bm;
    }
  else
    {
      UWtype carries = (UWtype) uu.s.high << bm;

      w.s.high = (UWtype) uu.s.high >> b;
      w.s.low = ((UWtype) uu.s.low >> b) | carries;
    }

  return w.ll;
}
#endif

#ifdef L_ashldi3
DWtype
__ashldi3 (DWtype u, word_type b)
{
  DWunion w;
  word_type bm;
  DWunion uu;

  if (b == 0)
    return u;

  uu.ll = u;

  bm = (sizeof (Wtype) * BITS_PER_UNIT) - b;
  if (bm <= 0)
    {
      w.s.low = 0;
      w.s.high = (UWtype) uu.s.low << -bm;
    }
  else
    {
      UWtype carries = (UWtype) uu.s.low >> bm;

      w.s.low = (UWtype) uu.s.low << b;
      w.s.high = ((UWtype) uu.s.high << b) | carries;
    }

  return w.ll;
}
#endif

#ifdef L_ashrdi3
DWtype
__ashrdi3 (DWtype u, word_type b)
{
  DWunion w;
  word_type bm;
  DWunion uu;

  if (b == 0)
    return u;

  uu.ll = u;

  bm = (sizeof (Wtype) * BITS_PER_UNIT) - b;
  if (bm <= 0)
    {
      /* w.s.high = 1..1 or 0..0 */
      w.s.high = uu.s.high >> (sizeof (Wtype) * BITS_PER_UNIT - 1);
      w.s.low = uu.s.high >> -bm;
    }
  else
    {
      UWtype carries = (UWtype) uu.s.high << bm;

      w.s.high = uu.s.high >> b;
      w.s.low = ((UWtype) uu.s.low >> b) | carries;
    }

  return w.ll;
}
#endif

#ifdef L_ffsdi2
DWtype
__ffsdi2 (DWtype u)
{
  DWunion uu;
  UWtype word, count, add;

  uu.ll = u;
  if (uu.s.low != 0)
    word = uu.s.low, add = 0;
  else if (uu.s.high != 0)
    word = uu.s.high, add = BITS_PER_UNIT * sizeof (Wtype);
  else
    return 0;

  count_trailing_zeros (count, word);
  return count + add + 1;
}
#endif

#ifdef L_muldi3
DWtype
__muldi3 (DWtype u, DWtype v)
{
  DWunion w;
  DWunion uu, vv;

  uu.ll = u,
  vv.ll = v;

  w.ll = __umulsidi3 (uu.s.low, vv.s.low);
  w.s.high += ((UWtype) uu.s.low * (UWtype) vv.s.high
	       + (UWtype) uu.s.high * (UWtype) vv.s.low);

  return w.ll;
}
#endif

#if (defined (L_udivdi3) || defined (L_divdi3) || \
     defined (L_umoddi3) || defined (L_moddi3))
#if defined (sdiv_qrnnd)
#define L_udiv_w_sdiv
#endif
#endif

#ifdef L_udiv_w_sdiv
#if defined (sdiv_qrnnd)
#if (defined (L_udivdi3) || defined (L_divdi3) || \
     defined (L_umoddi3) || defined (L_moddi3))
static inline __attribute__ ((__always_inline__))
#endif
UWtype
__udiv_w_sdiv (UWtype *rp, UWtype a1, UWtype a0, UWtype d)
{
  UWtype q, r;
  UWtype c0, c1, b1;

  if ((Wtype) d >= 0)
    {
      if (a1 < d - a1 - (a0 >> (W_TYPE_SIZE - 1)))
	{
	  /* dividend, divisor, and quotient are nonnegative */
	  sdiv_qrnnd (q, r, a1, a0, d);
	}
      else
	{
	  /* Compute c1*2^32 + c0 = a1*2^32 + a0 - 2^31*d */
	  sub_ddmmss (c1, c0, a1, a0, d >> 1, d << (W_TYPE_SIZE - 1));
	  /* Divide (c1*2^32 + c0) by d */
	  sdiv_qrnnd (q, r, c1, c0, d);
	  /* Add 2^31 to quotient */
	  q += (UWtype) 1 << (W_TYPE_SIZE - 1);
	}
    }
  else
    {
      b1 = d >> 1;			/* d/2, between 2^30 and 2^31 - 1 */
      c1 = a1 >> 1;			/* A/2 */
      c0 = (a1 << (W_TYPE_SIZE - 1)) + (a0 >> 1);

      if (a1 < b1)			/* A < 2^32*b1, so A/2 < 2^31*b1 */
	{
	  sdiv_qrnnd (q, r, c1, c0, b1); /* (A/2) / (d/2) */

	  r = 2*r + (a0 & 1);		/* Remainder from A/(2*b1) */
	  if ((d & 1) != 0)
	    {
	      if (r >= q)
		r = r - q;
	      else if (q - r <= d)
		{
		  r = r - q + d;
		  q--;
		}
	      else
		{
		  r = r - q + 2*d;
		  q -= 2;
		}
	    }
	}
      else if (c1 < b1)			/* So 2^31 <= (A/2)/b1 < 2^32 */
	{
	  c1 = (b1 - 1) - c1;
	  c0 = ~c0;			/* logical NOT */

	  sdiv_qrnnd (q, r, c1, c0, b1); /* (A/2) / (d/2) */

	  q = ~q;			/* (A/2)/b1 */
	  r = (b1 - 1) - r;

	  r = 2*r + (a0 & 1);		/* A/(2*b1) */

	  if ((d & 1) != 0)
	    {
	      if (r >= q)
		r = r - q;
	      else if (q - r <= d)
		{
		  r = r - q + d;
		  q--;
		}
	      else
		{
		  r = r - q + 2*d;
		  q -= 2;
		}
	    }
	}
      else				/* Implies c1 = b1 */
	{				/* Hence a1 = d - 1 = 2*b1 - 1 */
	  if (a0 >= -d)
	    {
	      q = -1;
	      r = a0 + d;
	    }
	  else
	    {
	      q = -2;
	      r = a0 + 2*d;
	    }
	}
    }

  *rp = r;
  return q;
}
#else
/* If sdiv_qrnnd doesn't exist, define dummy __udiv_w_sdiv.  */
UWtype
__udiv_w_sdiv (UWtype *rp __attribute__ ((__unused__)),
	       UWtype a1 __attribute__ ((__unused__)),
	       UWtype a0 __attribute__ ((__unused__)),
	       UWtype d __attribute__ ((__unused__)))
{
  return 0;
}
#endif
#endif

#if (defined (L_udivdi3) || defined (L_divdi3) || \
     defined (L_umoddi3) || defined (L_moddi3))
#define L_udivmoddi4
#endif

#ifdef L_clz
const UQItype __clz_tab[] =
{
  0,1,2,2,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
  6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
  8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
  8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
  8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
  8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
};
#endif

#ifdef L_udivmoddi4

#if (defined (L_udivdi3) || defined (L_divdi3) || \
     defined (L_umoddi3) || defined (L_moddi3))
static inline __attribute__ ((__always_inline__))
#endif
UDWtype
__udivmoddi4 (UDWtype n, UDWtype d, UDWtype *rp)
{
  DWunion ww;
  DWunion nn, dd;
  DWunion rr;
  UWtype d0, d1, n0, n1, n2;
  UWtype q0, q1;
  UWtype b, bm;

  nn.ll = n;
  dd.ll = d;

  d0 = dd.s.low;
  d1 = dd.s.high;
  n0 = nn.s.low;
  n1 = nn.s.high;

#if !UDIV_NEEDS_NORMALIZATION
  if (d1 == 0)
    {
      if (d0 > n1)
	{
	  /* 0q = nn / 0D */

	  udiv_qrnnd (q0, n0, n1, n0, d0);
	  q1 = 0;

	  /* Remainder in n0.  */
	}
      else
	{
	  /* qq = NN / 0d */

	  if (d0 == 0)
	    d0 = 1 / d0;	/* Divide intentionally by zero.  */

	  udiv_qrnnd (q1, n1, 0, n1, d0);
	  udiv_qrnnd (q0, n0, n1, n0, d0);

	  /* Remainder in n0.  */
	}

      if (rp != 0)
	{
	  rr.s.low = n0;
	  rr.s.high = 0;
	  *rp = rr.ll;
	}
    }

#else /* UDIV_NEEDS_NORMALIZATION */

  if (d1 == 0)
    {
      if (d0 > n1)
	{
	  /* 0q = nn / 0D */

	  count_leading_zeros (bm, d0);

	  if (bm != 0)
	    {
	      /* Normalize, i.e. make the most significant bit of the
		 denominator set.  */

	      d0 = d0 << bm;
	      n1 = (n1 << bm) | (n0 >> (W_TYPE_SIZE - bm));
	      n0 = n0 << bm;
	    }

	  udiv_qrnnd (q0, n0, n1, n0, d0);
	  q1 = 0;

	  /* Remainder in n0 >> bm.  */
	}
      else
	{
	  /* qq = NN / 0d */

	  if (d0 == 0)
	    d0 = 1 / d0;	/* Divide intentionally by zero.  */

	  count_leading_zeros (bm, d0);

	  if (bm == 0)
	    {
	      /* From (n1 >= d0) /\ (the most significant bit of d0 is set),
		 conclude (the most significant bit of n1 is set) /\ (the
		 leading quotient digit q1 = 1).

		 This special case is necessary, not an optimization.
		 (Shifts counts of W_TYPE_SIZE are undefined.)  */

	      n1 -= d0;
	      q1 = 1;
	    }
	  else
	    {
	      /* Normalize.  */

	      b = W_TYPE_SIZE - bm;

	      d0 = d0 << bm;
	      n2 = n1 >> b;
	      n1 = (n1 << bm) | (n0 >> b);
	      n0 = n0 << bm;

	      udiv_qrnnd (q1, n1, n2, n1, d0);
	    }

	  /* n1 != d0...  */

	  udiv_qrnnd (q0, n0, n1, n0, d0);

	  /* Remainder in n0 >> bm.  */
	}

      if (rp != 0)
	{
	  rr.s.low = n0 >> bm;
	  rr.s.high = 0;
	  *rp = rr.ll;
	}
    }
#endif /* UDIV_NEEDS_NORMALIZATION */

  else
    {
      if (d1 > n1)
	{
	  /* 00 = nn / DD */

	  q0 = 0;
	  q1 = 0;

	  /* Remainder in n1n0.  */
	  if (rp != 0)
	    {
	      rr.s.low = n0;
	      rr.s.high = n1;
	      *rp = rr.ll;
	    }
	}
      else
	{
	  /* 0q = NN / dd */

	  count_leading_zeros (bm, d1);
	  if (bm == 0)
	    {
	      /* From (n1 >= d1) /\ (the most significant bit of d1 is set),
		 conclude (the most significant bit of n1 is set) /\ (the
		 quotient digit q0 = 0 or 1).

		 This special case is necessary, not an optimization.  */

	      /* The condition on the next line takes advantage of that
		 n1 >= d1 (true due to program flow).  */
	      if (n1 > d1 || n0 >= d0)
		{
		  q0 = 1;
		  sub_ddmmss (n1, n0, n1, n0, d1, d0);
		}
	      else
		q0 = 0;

	      q1 = 0;

	      if (rp != 0)
		{
		  rr.s.low = n0;
		  rr.s.high = n1;
		  *rp = rr.ll;
		}
	    }
	  else
	    {
	      UWtype m1, m0;
	      /* Normalize.  */

	      b = W_TYPE_SIZE - bm;

	      d1 = (d1 << bm) | (d0 >> b);
	      d0 = d0 << bm;
	      n2 = n1 >> b;
	      n1 = (n1 << bm) | (n0 >> b);
	      n0 = n0 << bm;

	      udiv_qrnnd (q0, n1, n2, n1, d1);
	      umul_ppmm (m1, m0, q0, d0);

	      if (m1 > n1 || (m1 == n1 && m0 > n0))
		{
		  q0--;
		  sub_ddmmss (m1, m0, m1, m0, d1, d0);
		}

	      q1 = 0;

	      /* Remainder in (n1n0 - m1m0) >> bm.  */
	      if (rp != 0)
		{
		  sub_ddmmss (n1, n0, n1, n0, m1, m0);
		  rr.s.low = (n1 << b) | (n0 >> bm);
		  rr.s.high = n1 >> bm;
		  *rp = rr.ll;
		}
	    }
	}
    }

  ww.s.low = q0;
  ww.s.high = q1;
  return ww.ll;
}
#endif

#ifdef L_divdi3
DWtype
__divdi3 (DWtype u, DWtype v)
{
  word_type c = 0;
  DWunion uu, vv;
  DWtype w;

  uu.ll = u;
  vv.ll = v;

  if (uu.s.high < 0)
    c = ~c,
    uu.ll = -uu.ll;
  if (vv.s.high < 0)
    c = ~c,
    vv.ll = -vv.ll;

  w = __udivmoddi4 (uu.ll, vv.ll, (UDWtype *) 0);
  if (c)
    w = -w;

  return w;
}
#endif

#ifdef L_moddi3
DWtype
__moddi3 (DWtype u, DWtype v)
{
  word_type c = 0;
  DWunion uu, vv;
  DWtype w;

  uu.ll = u;
  vv.ll = v;

  if (uu.s.high < 0)
    c = ~c,
    uu.ll = -uu.ll;
  if (vv.s.high < 0)
    vv.ll = -vv.ll;

  (void) __udivmoddi4 (uu.ll, vv.ll, &w);
  if (c)
    w = -w;

  return w;
}
#endif

#ifdef L_umoddi3
UDWtype
__umoddi3 (UDWtype u, UDWtype v)
{
  UDWtype w;

  (void) __udivmoddi4 (u, v, &w);

  return w;
}
#endif

#ifdef L_udivdi3
UDWtype
__udivdi3 (UDWtype n, UDWtype d)
{
  return __udivmoddi4 (n, d, (UDWtype *) 0);
}
#endif

#ifdef L_cmpdi2
word_type
__cmpdi2 (DWtype a, DWtype b)
{
  DWunion au, bu;

  au.ll = a, bu.ll = b;

  if (au.s.high < bu.s.high)
    return 0;
  else if (au.s.high > bu.s.high)
    return 2;
  if ((UWtype) au.s.low < (UWtype) bu.s.low)
    return 0;
  else if ((UWtype) au.s.low > (UWtype) bu.s.low)
    return 2;
  return 1;
}
#endif

#ifdef L_ucmpdi2
word_type
__ucmpdi2 (DWtype a, DWtype b)
{
  DWunion au, bu;

  au.ll = a, bu.ll = b;

  if ((UWtype) au.s.high < (UWtype) bu.s.high)
    return 0;
  else if ((UWtype) au.s.high > (UWtype) bu.s.high)
    return 2;
  if ((UWtype) au.s.low < (UWtype) bu.s.low)
    return 0;
  else if ((UWtype) au.s.low > (UWtype) bu.s.low)
    return 2;
  return 1;
}
#endif

#if defined(L_fixunstfdi) && (LIBGCC2_LONG_DOUBLE_TYPE_SIZE == 128)
#define WORD_SIZE (sizeof (Wtype) * BITS_PER_UNIT)
#define HIGH_WORD_COEFF (((UDWtype) 1) << WORD_SIZE)

DWtype
__fixunstfDI (TFtype a)
{
  TFtype b;
  UDWtype v;

  if (a < 0)
    return 0;

  /* Compute high word of result, as a flonum.  */
  b = (a / HIGH_WORD_COEFF);
  /* Convert that to fixed (but not to DWtype!),
     and shift it into the high word.  */
  v = (UWtype) b;
  v <<= WORD_SIZE;
  /* Remove high part from the TFtype, leaving the low part as flonum.  */
  a -= (TFtype)v;
  /* Convert that to fixed (but not to DWtype!) and add it in.
     Sometimes A comes out negative.  This is significant, since
     A has more bits than a long int does.  */
  if (a < 0)
    v -= (UWtype) (- a);
  else
    v += (UWtype) a;
  return v;
}
#endif

#if defined(L_fixtfdi) && (LIBGCC2_LONG_DOUBLE_TYPE_SIZE == 128)
DWtype
__fixtfdi (TFtype a)
{
  if (a < 0)
    return - __fixunstfDI (-a);
  return __fixunstfDI (a);
}
#endif

#if defined(L_fixunsxfdi) && (LIBGCC2_LONG_DOUBLE_TYPE_SIZE == 96)
#define WORD_SIZE (sizeof (Wtype) * BITS_PER_UNIT)
#define HIGH_WORD_COEFF (((UDWtype) 1) << WORD_SIZE)

DWtype
__fixunsxfDI (XFtype a)
{
  XFtype b;
  UDWtype v;

  if (a < 0)
    return 0;

  /* Compute high word of result, as a flonum.  */
  b = (a / HIGH_WORD_COEFF);
  /* Convert that to fixed (but not to DWtype!),
     and shift it into the high word.  */
  v = (UWtype) b;
  v <<= WORD_SIZE;
  /* Remove high part from the XFtype, leaving the low part as flonum.  */
  a -= (XFtype)v;
  /* Convert that to fixed (but not to DWtype!) and add it in.
     Sometimes A comes out negative.  This is significant, since
     A has more bits than a long int does.  */
  if (a < 0)
    v -= (UWtype) (- a);
  else
    v += (UWtype) a;
  return v;
}
#endif

#if defined(L_fixxfdi) && (LIBGCC2_LONG_DOUBLE_TYPE_SIZE == 96)
DWtype
__fixxfdi (XFtype a)
{
  if (a < 0)
    return - __fixunsxfDI (-a);
  return __fixunsxfDI (a);
}
#endif

#ifdef L_fixunsdfdi
#define WORD_SIZE (sizeof (Wtype) * BITS_PER_UNIT)
#define HIGH_WORD_COEFF (((UDWtype) 1) << WORD_SIZE)

DWtype
__fixunsdfDI (DFtype a)
{
  DFtype b;
  UDWtype v;

  if (a < 0)
    return 0;

  /* Compute high word of result, as a flonum.  */
  b = (a / HIGH_WORD_COEFF);
  /* Convert that to fixed (but not to DWtype!),
     and shift it into the high word.  */
  v = (UWtype) b;
  v <<= WORD_SIZE;
  /* Remove high part from the DFtype, leaving the low part as flonum.  */
  a -= (DFtype)v;
  /* Convert that to fixed (but not to DWtype!) and add it in.
     Sometimes A comes out negative.  This is significant, since
     A has more bits than a long int does.  */
  if (a < 0)
    v -= (UWtype) (- a);
  else
    v += (UWtype) a;
  return v;
}
#endif

#ifdef L_fixdfdi
DWtype
__fixdfdi (DFtype a)
{
  if (a < 0)
    return - __fixunsdfDI (-a);
  return __fixunsdfDI (a);
}
#endif

#ifdef L_fixunssfdi
#define WORD_SIZE (sizeof (Wtype) * BITS_PER_UNIT)
#define HIGH_WORD_COEFF (((UDWtype) 1) << WORD_SIZE)

DWtype
__fixunssfDI (SFtype original_a)
{
  /* Convert the SFtype to a DFtype, because that is surely not going
     to lose any bits.  Some day someone else can write a faster version
     that avoids converting to DFtype, and verify it really works right.  */
  DFtype a = original_a;
  DFtype b;
  UDWtype v;

  if (a < 0)
    return 0;

  /* Compute high word of result, as a flonum.  */
  b = (a / HIGH_WORD_COEFF);
  /* Convert that to fixed (but not to DWtype!),
     and shift it into the high word.  */
  v = (UWtype) b;
  v <<= WORD_SIZE;
  /* Remove high part from the DFtype, leaving the low part as flonum.  */
  a -= (DFtype) v;
  /* Convert that to fixed (but not to DWtype!) and add it in.
     Sometimes A comes out negative.  This is significant, since
     A has more bits than a long int does.  */
  if (a < 0)
    v -= (UWtype) (- a);
  else
    v += (UWtype) a;
  return v;
}
#endif

#ifdef L_fixsfdi
DWtype
__fixsfdi (SFtype a)
{
  if (a < 0)
    return - __fixunssfDI (-a);
  return __fixunssfDI (a);
}
#endif

#if defined(L_floatdixf) && (LIBGCC2_LONG_DOUBLE_TYPE_SIZE == 96)
#define WORD_SIZE (sizeof (Wtype) * BITS_PER_UNIT)
#define HIGH_HALFWORD_COEFF (((UDWtype) 1) << (WORD_SIZE / 2))
#define HIGH_WORD_COEFF (((UDWtype) 1) << WORD_SIZE)

XFtype
__floatdixf (DWtype u)
{
  XFtype d;

  d = (Wtype) (u >> WORD_SIZE);
  d *= HIGH_HALFWORD_COEFF;
  d *= HIGH_HALFWORD_COEFF;
  d += (UWtype) (u & (HIGH_WORD_COEFF - 1));

  return d;
}
#endif

#if defined(L_floatditf) && (LIBGCC2_LONG_DOUBLE_TYPE_SIZE == 128)
#define WORD_SIZE (sizeof (Wtype) * BITS_PER_UNIT)
#define HIGH_HALFWORD_COEFF (((UDWtype) 1) << (WORD_SIZE / 2))
#define HIGH_WORD_COEFF (((UDWtype) 1) << WORD_SIZE)

TFtype
__floatditf (DWtype u)
{
  TFtype d;

  d = (Wtype) (u >> WORD_SIZE);
  d *= HIGH_HALFWORD_COEFF;
  d *= HIGH_HALFWORD_COEFF;
  d += (UWtype) (u & (HIGH_WORD_COEFF - 1));

  return d;
}
#endif

#ifdef L_floatdidf
#define WORD_SIZE (sizeof (Wtype) * BITS_PER_UNIT)
#define HIGH_HALFWORD_COEFF (((UDWtype) 1) << (WORD_SIZE / 2))
#define HIGH_WORD_COEFF (((UDWtype) 1) << WORD_SIZE)

DFtype
__floatdidf (DWtype u)
{
  DFtype d;

  d = (Wtype) (u >> WORD_SIZE);
  d *= HIGH_HALFWORD_COEFF;
  d *= HIGH_HALFWORD_COEFF;
  d += (UWtype) (u & (HIGH_WORD_COEFF - 1));

  return d;
}
#endif

#ifdef L_floatdisf
#define WORD_SIZE (sizeof (Wtype) * BITS_PER_UNIT)
#define HIGH_HALFWORD_COEFF (((UDWtype) 1) << (WORD_SIZE / 2))
#define HIGH_WORD_COEFF (((UDWtype) 1) << WORD_SIZE)

#define DI_SIZE (sizeof (DWtype) * BITS_PER_UNIT)
#define DF_SIZE DBL_MANT_DIG
#define SF_SIZE FLT_MANT_DIG

SFtype
__floatdisf (DWtype u)
{
  /* Do the calculation in DFmode
     so that we don't lose any of the precision of the high word
     while multiplying it.  */
  DFtype f;

  /* Protect against double-rounding error.
     Represent any low-order bits, that might be truncated in DFmode,
     by a bit that won't be lost.  The bit can go in anywhere below the
     rounding position of the SFmode.  A fixed mask and bit position
     handles all usual configurations.  It doesn't handle the case
     of 128-bit DImode, however.  */
  if (DF_SIZE < DI_SIZE
      && DF_SIZE > (DI_SIZE - DF_SIZE + SF_SIZE))
    {
#define REP_BIT ((UDWtype) 1 << (DI_SIZE - DF_SIZE))
      if (! (- ((DWtype) 1 << DF_SIZE) < u
	     && u < ((DWtype) 1 << DF_SIZE)))
	{
	  if ((UDWtype) u & (REP_BIT - 1))
	    {
	      u &= ~ (REP_BIT - 1);
	      u |= REP_BIT;
	    }
	}
    }
  f = (Wtype) (u >> WORD_SIZE);
  f *= HIGH_HALFWORD_COEFF;
  f *= HIGH_HALFWORD_COEFF;
  f += (UWtype) (u & (HIGH_WORD_COEFF - 1));

  return (SFtype) f;
}
#endif

#if defined(L_fixunsxfsi) && LIBGCC2_LONG_DOUBLE_TYPE_SIZE == 96
/* Reenable the normal types, in case limits.h needs them.  */
#undef char
#undef short
#undef int
#undef long
#undef unsigned
#undef float
#undef double
#undef MIN
#undef MAX
#include <limits.h>

UWtype
__fixunsxfSI (XFtype a)
{
  if (a >= - (DFtype) Wtype_MIN)
    return (Wtype) (a + Wtype_MIN) - Wtype_MIN;
  return (Wtype) a;
}
#endif

#ifdef L_fixunsdfsi
/* Reenable the normal types, in case limits.h needs them.  */
#undef char
#undef short
#undef int
#undef long
#undef unsigned
#undef float
#undef double
#undef MIN
#undef MAX
#include <limits.h>

UWtype
__fixunsdfSI (DFtype a)
{
  if (a >= - (DFtype) Wtype_MIN)
    return (Wtype) (a + Wtype_MIN) - Wtype_MIN;
  return (Wtype) a;
}
#endif

#ifdef L_fixunssfsi
/* Reenable the normal types, in case limits.h needs them.  */
#undef char
#undef short
#undef int
#undef long
#undef unsigned
#undef float
#undef double
#undef MIN
#undef MAX
#include <limits.h>

UWtype
__fixunssfSI (SFtype a)
{
  if (a >= - (SFtype) Wtype_MIN)
    return (Wtype) (a + Wtype_MIN) - Wtype_MIN;
  return (Wtype) a;
}
#endif

/* From here on down, the routines use normal data types.  */

#define SItype bogus_type
#define USItype bogus_type
#define DItype bogus_type
#define UDItype bogus_type
#define SFtype bogus_type
#define DFtype bogus_type
#undef Wtype
#undef UWtype
#undef HWtype
#undef UHWtype
#undef DWtype
#undef UDWtype

#undef char
#undef short
#undef int
#undef long
#undef unsigned
#undef float
#undef double

#ifdef L__gcc_bcmp

/* Like bcmp except the sign is meaningful.
   Result is negative if S1 is less than S2,
   positive if S1 is greater, 0 if S1 and S2 are equal.  */

int
__gcc_bcmp (const unsigned char *s1, const unsigned char *s2, size_t size)
{
  while (size > 0)
    {
      unsigned char c1 = *s1++, c2 = *s2++;
      if (c1 != c2)
	return c1 - c2;
      size--;
    }
  return 0;
}

#endif

/* __eprintf used to be used by GCC's private version of <assert.h>.
   We no longer provide that header, but this routine remains in libgcc.a
   for binary backward compatibility.  Note that it is not included in
   the shared version of libgcc.  */
#ifdef L_eprintf
#ifndef inhibit_libc

#undef NULL /* Avoid errors if stdio.h and our stddef.h mismatch.  */
#include <stdio.h>

void
__eprintf (const char *string, const char *expression,
	   unsigned int line, const char *filename)
{
  fprintf (stderr, string, expression, line, filename);
  fflush (stderr);
  abort ();
}

#endif
#endif

#ifdef L_gcov

/* Gcov profile dumper. Requires atexit and stdio.  */

#undef NULL /* Avoid errors if stdio.h and our stddef.h mismatch.  */
#include <stdio.h>

#include "gcov-io.h"
#include <string.h>
#if defined (TARGET_HAS_F_SETLKW)
#include <fcntl.h>
#include <errno.h>
#endif

/* Chain of per-object gcov structures.  */
static struct gcov_info *gcov_list;

/* A program checksum allows us to distinguish program data for an
   object file included in multiple programs.  */
static unsigned gcov_crc32;

static void
gcov_version_mismatch (struct gcov_info *ptr, unsigned version)
{
  unsigned expected = GCOV_VERSION;
  unsigned ix;
  char e[4], v[4];

  for (ix = 4; ix--; expected >>= 8, version >>= 8)
    {
      e[ix] = expected;
      v[ix] = version;
    }
  
  fprintf (stderr,
	   "profiling:%s:Version mismatch - expected %.4s got %.4s\n",
	   ptr->filename, e, v);
}

/* Dump the coverage counts. We merge with existing counts when
   possible, to avoid growing the .da files ad infinitum. We use this
   program's checksum to make sure we only accumulate whole program
   statistics to the correct summary. An object file might be embedded
   in two separate programs, and we must keep the two program
   summaries separate. */

static void
gcov_exit (void)
{
  struct gcov_info *ptr;
  unsigned ix, jx;
  struct gcov_summary program;
  gcov_type program_max_one = 0;
  gcov_type program_max_sum = 0;
  gcov_type program_sum = 0;
  unsigned program_arcs = 0;
  
#if defined (TARGET_HAS_F_SETLKW)
  struct flock s_flock;

  s_flock.l_type = F_WRLCK;
  s_flock.l_whence = SEEK_SET;
  s_flock.l_start = 0;
  s_flock.l_len = 0; /* Until EOF.  */
  s_flock.l_pid = getpid ();
#endif

  memset (&program, 0, sizeof (program));
  program.checksum = gcov_crc32;
  
  for (ptr = gcov_list; ptr; ptr = ptr->next)
    {
      FILE *da_file;
      struct gcov_summary object;
      struct gcov_summary local_prg;
      int merging = 0;
      long base;
      const struct function_info *fn_info;
      gcov_type **counters;
      gcov_type *count_ptr;
      gcov_type object_max_one = 0;
      gcov_type count;
      unsigned tag, length, flength, checksum;
      unsigned arc_data_index, f_sect_index, sect_index;

      ptr->wkspc = 0;
      if (!ptr->filename)
	continue;

      counters = malloc (sizeof (gcov_type *) * ptr->n_counter_sections);
      for (ix = 0; ix < ptr->n_counter_sections; ix++)
	counters[ix] = ptr->counter_sections[ix].counters;

      for (arc_data_index = 0;
	   arc_data_index < ptr->n_counter_sections
	   && ptr->counter_sections[arc_data_index].tag != GCOV_TAG_ARC_COUNTS;
	   arc_data_index++)
	continue;

      if (arc_data_index == ptr->n_counter_sections)
	{
	  /* For now; later we may want to just measure other profiles,
	     but now I am lazy to check for all consequences.  */
	  abort ();
	}
      for (ix = ptr->counter_sections[arc_data_index].n_counters,
	   count_ptr = ptr->counter_sections[arc_data_index].counters; ix--;)
	{
	  gcov_type count = *count_ptr++;

	  if (count > object_max_one)
	    object_max_one = count;
	}
      if (object_max_one > program_max_one)
	program_max_one = object_max_one;
      
      memset (&local_prg, 0, sizeof (local_prg));
      memset (&object, 0, sizeof (object));
      
      /* Open for modification */
      if ((da_file = fopen (ptr->filename, "r+b")))
	merging = 1;
      else if ((da_file = fopen (ptr->filename, "w+b")))
	;
      else
	{
	  fprintf (stderr, "profiling:%s:Cannot open\n", ptr->filename);
	  ptr->filename = 0;
	  continue;
	}

#if defined (TARGET_HAS_F_SETLKW)
      /* After a fork, another process might try to read and/or write
         the same file simultanously.  So if we can, lock the file to
         avoid race conditions.  */
      while (fcntl (fileno (da_file), F_SETLKW, &s_flock)
	     && errno == EINTR)
	continue;
#endif
      if (merging)
	{
	  /* Merge data from file.  */
	      
	  if (gcov_read_unsigned (da_file, &tag) || tag != GCOV_DATA_MAGIC)
	    {
	      fprintf (stderr, "profiling:%s:Not a gcov data file\n",
		       ptr->filename);
	    read_fatal:;
	      fclose (da_file);
	      ptr->filename = 0;
	      continue;
	    }
	  if (gcov_read_unsigned (da_file, &length) || length != GCOV_VERSION)
	    {
	      gcov_version_mismatch (ptr, length);
	      goto read_fatal;
	    }
	  
	  /* Merge execution counts for each function.  */
	  for (ix = ptr->n_functions, fn_info = ptr->functions;
	       ix--; fn_info++)
	    {
	      if (gcov_read_unsigned (da_file, &tag)
		  || gcov_read_unsigned (da_file, &length))
		{
		read_error:;
		  fprintf (stderr, "profiling:%s:Error merging\n",
			   ptr->filename);
		  goto read_fatal;
		}

	      /* Check function */
	      if (tag != GCOV_TAG_FUNCTION)
		{
		read_mismatch:;
		  fprintf (stderr, "profiling:%s:Merge mismatch at %s\n",
			   ptr->filename, fn_info->name);
		  goto read_fatal;
		}

	      if (gcov_read_unsigned (da_file, &flength)
		  || gcov_skip_string (da_file, flength)
		  || gcov_read_unsigned (da_file, &checksum))
		goto read_error;
	      if (flength != strlen (fn_info->name)
		  || checksum != fn_info->checksum)
		goto read_mismatch;

	      /* Counters.  */
	      for (f_sect_index = 0;
		   f_sect_index < fn_info->n_counter_sections;
		   f_sect_index++)
		{
		  if (gcov_read_unsigned (da_file, &tag)
		      || gcov_read_unsigned (da_file, &length))
		    goto read_error;
		  for (sect_index = 0;
		       sect_index < ptr->n_counter_sections;
		       sect_index++)
		    if (ptr->counter_sections[sect_index].tag == tag)
		      break;
		  if (fn_info->counter_sections[f_sect_index].tag != tag
		      || sect_index == ptr->n_counter_sections
		      || length / 8 != fn_info->counter_sections[f_sect_index].n_counters)
		    goto read_mismatch;
		  
		  for (jx = fn_info->counter_sections[f_sect_index].n_counters;
		       jx--; counters[sect_index]++)
		    if (gcov_read_counter (da_file, &count))
		      goto read_error;
		    else
		      *counters[sect_index] += count;
		}
	    }

	  /* Check object summary */
	  if (gcov_read_unsigned (da_file, &tag)
	      || gcov_read_unsigned (da_file, &length))
	    goto read_error;
	  if (tag != GCOV_TAG_OBJECT_SUMMARY)
	    goto read_mismatch;
	  if (gcov_read_summary (da_file, &object))
	    goto read_error;

	  /* Check program summary */
	  while (1)
	    {
	      long base = ftell (da_file);
	      
	      if (gcov_read_unsigned (da_file, &tag)
		  || gcov_read_unsigned (da_file, &length))
		{
		  if (feof (da_file))
		    break;
		  goto read_error;
		}
	      if (tag != GCOV_TAG_PROGRAM_SUMMARY
		  && tag != GCOV_TAG_PLACEHOLDER_SUMMARY
		  && tag != GCOV_TAG_INCORRECT_SUMMARY)
		goto read_mismatch;
	      if (gcov_read_summary (da_file, &local_prg))
		goto read_error;
	      if (local_prg.checksum != program.checksum)
		continue;
	      if (tag == GCOV_TAG_PLACEHOLDER_SUMMARY)
		{
		  fprintf (stderr,
			   "profiling:%s:Concurrent race detected\n",
			   ptr->filename);
		  goto read_fatal;
		}
	      merging = -1;
	      if (tag != GCOV_TAG_PROGRAM_SUMMARY)
		break;
	      
	      if (program.runs
		  && memcmp (&program, &local_prg, sizeof (program)))
		{
		  fprintf (stderr, "profiling:%s:Invocation mismatch\n",
			   ptr->filename);
		  local_prg.runs = 0;
		}
	      else
		memcpy (&program, &local_prg, sizeof (program));
	      ptr->wkspc = base;
	      break;
	    }
	  fseek (da_file, 0, SEEK_SET);
	}

      object.runs++;
      object.arcs = ptr->counter_sections[arc_data_index].n_counters;
      object.arc_sum = 0;
      if (object.arc_max_one < object_max_one)
	object.arc_max_one = object_max_one;
      object.arc_sum_max += object_max_one;
      
      /* Write out the data.  */
      if (/* magic */
	  gcov_write_unsigned (da_file, GCOV_DATA_MAGIC)
	  /* version number */
	  || gcov_write_unsigned (da_file, GCOV_VERSION))
	{
	write_error:;
	  fclose (da_file);
	  fprintf (stderr, "profiling:%s:Error writing\n", ptr->filename);
	  ptr->filename = 0;
	  continue;
	}
      
      /* Write execution counts for each function.  */
      for (ix = 0; ix < ptr->n_counter_sections; ix++)
	counters[ix] = ptr->counter_sections[ix].counters;
      for (ix = ptr->n_functions, fn_info = ptr->functions; ix--; fn_info++)
	{
	  /* Announce function. */
	  if (gcov_write_unsigned (da_file, GCOV_TAG_FUNCTION)
	      || !(base = gcov_reserve_length (da_file))
	      /* function name */
	      || gcov_write_string (da_file, fn_info->name,
				    strlen (fn_info->name))
	      /* function checksum */
	      || gcov_write_unsigned (da_file, fn_info->checksum)
	      || gcov_write_length (da_file, base))
	    goto write_error;

	  /* counters.  */
	  for (f_sect_index = 0;
	       f_sect_index < fn_info->n_counter_sections;
	       f_sect_index++)
	    {
	      tag = fn_info->counter_sections[f_sect_index].tag;
	      for (sect_index = 0;
    		   sect_index < ptr->n_counter_sections;
		   sect_index++)
		if (ptr->counter_sections[sect_index].tag == tag)
		  break;
	      if (sect_index == ptr->n_counter_sections)
		abort ();

	      if (gcov_write_unsigned (da_file, tag)
		  || !(base = gcov_reserve_length (da_file)))
		goto write_error;
	  
    	      for (jx = fn_info->counter_sections[f_sect_index].n_counters; jx--;)
		{
		  gcov_type count = *counters[sect_index]++;
	      
		  if (tag == GCOV_TAG_ARC_COUNTS)
		    {
		      object.arc_sum += count;
		      if (object.arc_max_sum < count)
			object.arc_max_sum = count;
		    }
		  if (gcov_write_counter (da_file, count))
		    goto write_error; /* RIP Edsger Dijkstra */
		}
	      if (gcov_write_length (da_file, base))
		goto write_error;
	    }
	}

      /* Object file summary. */
      if (gcov_write_summary (da_file, GCOV_TAG_OBJECT_SUMMARY, &object))
	goto write_error;

      if (merging >= 0)
	{
	  if (fseek (da_file, 0, SEEK_END))
	    goto write_error;
	  ptr->wkspc = ftell (da_file);
	  if (gcov_write_summary (da_file, GCOV_TAG_PLACEHOLDER_SUMMARY,
				  &program))
	    goto write_error;
	}
      else if (ptr->wkspc)
	{
	  /* Zap trailing program summary */
	  if (fseek (da_file, ptr->wkspc, SEEK_SET))
	    goto write_error;
	  if (!local_prg.runs)
	    ptr->wkspc = 0;
	  if (gcov_write_unsigned (da_file,
			     local_prg.runs ? GCOV_TAG_PLACEHOLDER_SUMMARY
			     : GCOV_TAG_INCORRECT_SUMMARY))
	    goto write_error;
	}
      if (fflush (da_file))
	goto write_error;

      if (fclose (da_file))
	{
	  fprintf (stderr, "profiling:%s:Error closing\n", ptr->filename);
	  ptr->filename = 0;
	}
      else
	{
	  program_arcs += ptr->counter_sections[arc_data_index].n_counters;
	  program_sum += object.arc_sum;
	  if (program_max_sum < object.arc_max_sum)
	    program_max_sum = object.arc_max_sum;
	}
      free(counters);
    }

  /* Generate whole program statistics.  */
  program.runs++;
  program.arcs = program_arcs;
  program.arc_sum = program_sum;
  if (program.arc_max_one < program_max_one)
    program.arc_max_one = program_max_one;
  if (program.arc_max_sum < program_max_sum)
    program.arc_max_sum = program_max_sum;
  program.arc_sum_max += program_max_one;
  
  /* Upate whole program statistics.  */
  for (ptr = gcov_list; ptr; ptr = ptr->next)
    if (ptr->filename && ptr->wkspc)
      {
	FILE *da_file;
	
	da_file = fopen (ptr->filename, "r+b");
	if (!da_file)
	  {
	    fprintf (stderr, "profiling:%s:Cannot open\n", ptr->filename);
	    continue;
	  }
	
#if defined (TARGET_HAS_F_SETLKW)
	while (fcntl (fileno (da_file), F_SETLKW, &s_flock)
	       && errno == EINTR)
	  continue;
#endif
	if (fseek (da_file, ptr->wkspc, SEEK_SET)
 	    || gcov_write_summary (da_file, GCOV_TAG_PROGRAM_SUMMARY, &program)
 	    || fflush (da_file))
 	  fprintf (stderr, "profiling:%s:Error writing\n", ptr->filename);
	if (fclose (da_file))
	  fprintf (stderr, "profiling:%s:Error closing\n", ptr->filename);
      }
}

/* Add a new object file onto the bb chain.  Invoked automatically
   when running an object file's global ctors.  */

void
__gcov_init (struct gcov_info *info)
{
  if (!info->version)
    return;
  if (info->version != GCOV_VERSION)
    gcov_version_mismatch (info, info->version);
  else
    {
      const char *ptr = info->filename;
      unsigned crc32 = gcov_crc32;
  
      do
	{
	  unsigned ix;
	  unsigned value = *ptr << 24;

	  for (ix = 8; ix--; value <<= 1)
	    {
	      unsigned feedback;

	      feedback = (value ^ crc32) & 0x80000000 ? 0x04c11db7 : 0;
	      crc32 <<= 1;
	      crc32 ^= feedback;
	    }
	}
      while (*ptr++);
      
      gcov_crc32 = crc32;
      
      if (!gcov_list)
	atexit (gcov_exit);
      
      info->next = gcov_list;
      gcov_list = info;
    }
  info->version = 0;
}

/* Called before fork or exec - write out profile information gathered so
   far and reset it to zero.  This avoids duplication or loss of the
   profile information gathered so far.  */

void
__gcov_flush (void)
{
  struct gcov_info *ptr;

  gcov_exit ();
  for (ptr = gcov_list; ptr; ptr = ptr->next)
    {
      unsigned i, j;
      
      for (j = 0; j < ptr->n_counter_sections; j++)
	for (i = ptr->counter_sections[j].n_counters; i--;)
	  ptr->counter_sections[j].counters[i] = 0;
    }
}

#endif /* L_gcov */

#ifdef L_clear_cache
/* Clear part of an instruction cache.  */

#define INSN_CACHE_PLANE_SIZE (INSN_CACHE_SIZE / INSN_CACHE_DEPTH)

void
__clear_cache (char *beg __attribute__((__unused__)),
	       char *end __attribute__((__unused__)))
{
#ifdef CLEAR_INSN_CACHE
  CLEAR_INSN_CACHE (beg, end);
#else
#ifdef INSN_CACHE_SIZE
  static char array[INSN_CACHE_SIZE + INSN_CACHE_PLANE_SIZE + INSN_CACHE_LINE_WIDTH];
  static int initialized;
  int offset;
  void *start_addr
  void *end_addr;
  typedef (*function_ptr) (void);

#if (INSN_CACHE_SIZE / INSN_CACHE_LINE_WIDTH) < 16
  /* It's cheaper to clear the whole cache.
     Put in a series of jump instructions so that calling the beginning
     of the cache will clear the whole thing.  */

  if (! initialized)
    {
      int ptr = (((int) array + INSN_CACHE_LINE_WIDTH - 1)
		 & -INSN_CACHE_LINE_WIDTH);
      int end_ptr = ptr + INSN_CACHE_SIZE;

      while (ptr < end_ptr)
	{
	  *(INSTRUCTION_TYPE *)ptr
	    = JUMP_AHEAD_INSTRUCTION + INSN_CACHE_LINE_WIDTH;
	  ptr += INSN_CACHE_LINE_WIDTH;
	}
      *(INSTRUCTION_TYPE *) (ptr - INSN_CACHE_LINE_WIDTH) = RETURN_INSTRUCTION;

      initialized = 1;
    }

  /* Call the beginning of the sequence.  */
  (((function_ptr) (((int) array + INSN_CACHE_LINE_WIDTH - 1)
		    & -INSN_CACHE_LINE_WIDTH))
   ());

#else /* Cache is large.  */

  if (! initialized)
    {
      int ptr = (((int) array + INSN_CACHE_LINE_WIDTH - 1)
		 & -INSN_CACHE_LINE_WIDTH);

      while (ptr < (int) array + sizeof array)
	{
	  *(INSTRUCTION_TYPE *)ptr = RETURN_INSTRUCTION;
	  ptr += INSN_CACHE_LINE_WIDTH;
	}

      initialized = 1;
    }

  /* Find the location in array that occupies the same cache line as BEG.  */

  offset = ((int) beg & -INSN_CACHE_LINE_WIDTH) & (INSN_CACHE_PLANE_SIZE - 1);
  start_addr = (((int) (array + INSN_CACHE_PLANE_SIZE - 1)
		 & -INSN_CACHE_PLANE_SIZE)
		+ offset);

  /* Compute the cache alignment of the place to stop clearing.  */
#if 0  /* This is not needed for gcc's purposes.  */
  /* If the block to clear is bigger than a cache plane,
     we clear the entire cache, and OFFSET is already correct.  */
  if (end < beg + INSN_CACHE_PLANE_SIZE)
#endif
    offset = (((int) (end + INSN_CACHE_LINE_WIDTH - 1)
	       & -INSN_CACHE_LINE_WIDTH)
	      & (INSN_CACHE_PLANE_SIZE - 1));

#if INSN_CACHE_DEPTH > 1
  end_addr = (start_addr & -INSN_CACHE_PLANE_SIZE) + offset;
  if (end_addr <= start_addr)
    end_addr += INSN_CACHE_PLANE_SIZE;

  for (plane = 0; plane < INSN_CACHE_DEPTH; plane++)
    {
      int addr = start_addr + plane * INSN_CACHE_PLANE_SIZE;
      int stop = end_addr + plane * INSN_CACHE_PLANE_SIZE;

      while (addr != stop)
	{
	  /* Call the return instruction at ADDR.  */
	  ((function_ptr) addr) ();

	  addr += INSN_CACHE_LINE_WIDTH;
	}
    }
#else /* just one plane */
  do
    {
      /* Call the return instruction at START_ADDR.  */
      ((function_ptr) start_addr) ();

      start_addr += INSN_CACHE_LINE_WIDTH;
    }
  while ((start_addr % INSN_CACHE_SIZE) != offset);
#endif /* just one plane */
#endif /* Cache is large */
#endif /* Cache exists */
#endif /* CLEAR_INSN_CACHE */
}

#endif /* L_clear_cache */

#ifdef L_trampoline

/* Jump to a trampoline, loading the static chain address.  */

#if defined(WINNT) && ! defined(__CYGWIN__) && ! defined (_UWIN)

long
getpagesize (void)
{
#ifdef _ALPHA_
  return 8192;
#else
  return 4096;
#endif
}

#ifdef __i386__
extern int VirtualProtect (char *, int, int, int *) __attribute__((stdcall));
#endif

int
mprotect (char *addr, int len, int prot)
{
  int np, op;

  if (prot == 7)
    np = 0x40;
  else if (prot == 5)
    np = 0x20;
  else if (prot == 4)
    np = 0x10;
  else if (prot == 3)
    np = 0x04;
  else if (prot == 1)
    np = 0x02;
  else if (prot == 0)
    np = 0x01;

  if (VirtualProtect (addr, len, np, &op))
    return 0;
  else
    return -1;
}

#endif /* WINNT && ! __CYGWIN__ && ! _UWIN */

#ifdef TRANSFER_FROM_TRAMPOLINE
TRANSFER_FROM_TRAMPOLINE
#endif

#ifdef __sysV68__

#include <sys/signal.h>
#include <errno.h>

/* Motorola forgot to put memctl.o in the libp version of libc881.a,
   so define it here, because we need it in __clear_insn_cache below */
/* On older versions of this OS, no memctl or MCT_TEXT are defined;
   hence we enable this stuff only if MCT_TEXT is #define'd.  */

#ifdef MCT_TEXT
asm("\n\
	global memctl\n\
memctl:\n\
	movq &75,%d0\n\
	trap &0\n\
	bcc.b noerror\n\
	jmp cerror%\n\
noerror:\n\
	movq &0,%d0\n\
	rts");
#endif

/* Clear instruction cache so we can call trampolines on stack.
   This is called from FINALIZE_TRAMPOLINE in mot3300.h.  */

void
__clear_insn_cache (void)
{
#ifdef MCT_TEXT
  int save_errno;

  /* Preserve errno, because users would be surprised to have
  errno changing without explicitly calling any system-call.  */
  save_errno = errno;

  /* Keep it simple : memctl (MCT_TEXT) always fully clears the insn cache.
     No need to use an address derived from _start or %sp, as 0 works also.  */
  memctl(0, 4096, MCT_TEXT);
  errno = save_errno;
#endif
}

#endif /* __sysV68__ */
#endif /* L_trampoline */

#ifndef __CYGWIN__
#ifdef L__main

#include "gbl-ctors.h"
/* Some systems use __main in a way incompatible with its use in gcc, in these
   cases use the macros NAME__MAIN to give a quoted symbol and SYMBOL__MAIN to
   give the same symbol without quotes for an alternative entry point.  You
   must define both, or neither.  */
#ifndef NAME__MAIN
#define NAME__MAIN "__main"
#define SYMBOL__MAIN __main
#endif

#ifdef INIT_SECTION_ASM_OP
#undef HAS_INIT_SECTION
#define HAS_INIT_SECTION
#endif

#if !defined (HAS_INIT_SECTION) || !defined (OBJECT_FORMAT_ELF)

/* Some ELF crosses use crtstuff.c to provide __CTOR_LIST__, but use this
   code to run constructors.  In that case, we need to handle EH here, too.  */

#ifdef EH_FRAME_SECTION_NAME
#include "unwind-dw2-fde.h"
extern unsigned char __EH_FRAME_BEGIN__[];
#endif

/* Run all the global destructors on exit from the program.  */

void
__do_global_dtors (void)
{
#ifdef DO_GLOBAL_DTORS_BODY
  DO_GLOBAL_DTORS_BODY;
#else
  static func_ptr *p = __DTOR_LIST__ + 1;
  while (*p)
    {
      p++;
      (*(p-1)) ();
    }
#endif
#if defined (EH_FRAME_SECTION_NAME) && !defined (HAS_INIT_SECTION)
  {
    static int completed = 0;
    if (! completed)
      {
	completed = 1;
	__deregister_frame_info (__EH_FRAME_BEGIN__);
      }
  }
#endif
}
#endif

#ifndef HAS_INIT_SECTION
/* Run all the global constructors on entry to the program.  */

void
__do_global_ctors (void)
{
#ifdef EH_FRAME_SECTION_NAME
  {
    static struct object object;
    __register_frame_info (__EH_FRAME_BEGIN__, &object);
  }
#endif
  DO_GLOBAL_CTORS_BODY;
  atexit (__do_global_dtors);
}
#endif /* no HAS_INIT_SECTION */

#if !defined (HAS_INIT_SECTION) || defined (INVOKE__main)
/* Subroutine called automatically by `main'.
   Compiling a global function named `main'
   produces an automatic call to this function at the beginning.

   For many systems, this routine calls __do_global_ctors.
   For systems which support a .init section we use the .init section
   to run __do_global_ctors, so we need not do anything here.  */

void
SYMBOL__MAIN ()
{
  /* Support recursive calls to `main': run initializers just once.  */
  static int initialized;
  if (! initialized)
    {
      initialized = 1;
      __do_global_ctors ();
    }
}
#endif /* no HAS_INIT_SECTION or INVOKE__main */

#endif /* L__main */
#endif /* __CYGWIN__ */

#ifdef L_ctors

#include "gbl-ctors.h"

/* Provide default definitions for the lists of constructors and
   destructors, so that we don't get linker errors.  These symbols are
   intentionally bss symbols, so that gld and/or collect will provide
   the right values.  */

/* We declare the lists here with two elements each,
   so that they are valid empty lists if no other definition is loaded.

   If we are using the old "set" extensions to have the gnu linker
   collect ctors and dtors, then we __CTOR_LIST__ and __DTOR_LIST__
   must be in the bss/common section.

   Long term no port should use those extensions.  But many still do.  */
#if !defined(INIT_SECTION_ASM_OP) && !defined(CTOR_LISTS_DEFINED_EXTERNALLY)
#if defined (TARGET_ASM_CONSTRUCTOR) || defined (USE_COLLECT2)
func_ptr __CTOR_LIST__[2] = {0, 0};
func_ptr __DTOR_LIST__[2] = {0, 0};
#else
func_ptr __CTOR_LIST__[2];
func_ptr __DTOR_LIST__[2];
#endif
#endif /* no INIT_SECTION_ASM_OP and not CTOR_LISTS_DEFINED_EXTERNALLY */
#endif /* L_ctors */

#ifdef L_exit

#include "gbl-ctors.h"

#ifdef NEED_ATEXIT

#ifndef ON_EXIT

# include <errno.h>

static func_ptr *atexit_chain = 0;
static long atexit_chain_length = 0;
static volatile long last_atexit_chain_slot = -1;

int
atexit (func_ptr func)
{
  if (++last_atexit_chain_slot == atexit_chain_length)
    {
      atexit_chain_length += 32;
      if (atexit_chain)
	atexit_chain = (func_ptr *) realloc (atexit_chain, atexit_chain_length
					     * sizeof (func_ptr));
      else
	atexit_chain = (func_ptr *) malloc (atexit_chain_length
					    * sizeof (func_ptr));
      if (! atexit_chain)
	{
	  atexit_chain_length = 0;
	  last_atexit_chain_slot = -1;
	  errno = ENOMEM;
	  return (-1);
	}
    }
  atexit_chain[last_atexit_chain_slot] = func;
  return (0);
}

extern void _cleanup (void);
extern void _exit (int) __attribute__ ((__noreturn__));

void
exit (int status)
{
  if (atexit_chain)
    {
      for ( ; last_atexit_chain_slot-- >= 0; )
	{
	  (*atexit_chain[last_atexit_chain_slot + 1]) ();
	  atexit_chain[last_atexit_chain_slot + 1] = 0;
	}
      free (atexit_chain);
      atexit_chain = 0;
    }
#ifdef EXIT_BODY
  EXIT_BODY;
#else
  _cleanup ();
#endif
  _exit (status);
}

#else /* ON_EXIT */

/* Simple; we just need a wrapper for ON_EXIT.  */
int
atexit (func_ptr func)
{
  return ON_EXIT (func);
}

#endif /* ON_EXIT */
#endif /* NEED_ATEXIT */

#endif /* L_exit */
