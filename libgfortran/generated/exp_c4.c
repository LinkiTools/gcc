/* Complex exponential functions
   Copyright 2002 Free Software Foundation, Inc.
   Contributed by Paul Brook <paul@nowt.org>

This file is part of the GNU Fortran 95 runtime library (libgfor).

GNU G95 is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

GNU G95 is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with libgfor; see the file COPYING.LIB.  If not,
write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */
#include <math.h>
#include "libgfortran.h"


/* z = a + ib  */
/* Absolute value.  */
G95_REAL_4
cabsf (G95_COMPLEX_4 z)
{
  return hypotf (REALPART (z), IMAGPART (z));
}

/* Complex argument.  The angle made with the +ve real axis.  Range 0-2pi.  */
G95_REAL_4
cargf (G95_COMPLEX_4 z)
{
  G95_REAL_4 arg;

  arg = atan2f (IMAGPART (z), REALPART (z));
  if (arg < 0)
    return arg + 2 * M_PI;
  else
    return arg;
}

/* exp(z) = exp(a)*(cos(b) + isin(b))  */
G95_COMPLEX_4
cexpf (G95_COMPLEX_4 z)
{
  G95_REAL_4 a;
  G95_REAL_4 b;
  G95_COMPLEX_4 v;

  a = REALPART (z);
  b = IMAGPART (z);
  COMPLEX_ASSIGN (v, cosf (b), sinf (b));
  return expf (a) * v;
}

/* log(z) = log (cabs(z)) + i*carg(z)  */
G95_COMPLEX_4
clogf (G95_COMPLEX_4 z)
{
  G95_COMPLEX_4 v;

  COMPLEX_ASSIGN (v, logf (cabsf (z)), cargf (z));
  return v;
}

/* log10(z) = log10 (cabs(z)) + i*carg(z)  */
G95_COMPLEX_4
clog10f (G95_COMPLEX_4 z)
{
  G95_COMPLEX_4 v;

  COMPLEX_ASSIGN (v, log10f (cabsf (z)), cargf (z));
  return v;
}

/* pow(base, power) = cexp (power * clog (base))  */
G95_COMPLEX_4
cpowf (G95_COMPLEX_4 base, G95_COMPLEX_4 power)
{
  return cexpf (power * clogf (base));
}

/* sqrt(z).  Algorithm pulled from glibc.  */
G95_COMPLEX_4
csqrtf (G95_COMPLEX_4 z)
{
  G95_REAL_4 re;
  G95_REAL_4 im;
  G95_COMPLEX_4 v;

  re = REALPART (re);
  im = IMAGPART (im);
  if (im == 0.0)
    {
      if (re < 0.0)
        {
          COMPLEX_ASSIGN (v, 0.0, copysignf (sqrtf (-re), im));
        }
      else
        {
          COMPLEX_ASSIGN (v, fabsf (sqrt (re)),
                          copysignf (0.0, im));
        }
    }
  else if (re == 0.0)
    {
      G95_REAL_4 r;

      r = sqrtf (0.5 * fabs (im));

      COMPLEX_ASSIGN (v, copysignf (r, im), r);
    }
  else
    {
      G95_REAL_4 d, r, s;

      d = hypotf (re, im);
      /* Use the identity   2  Re res  Im res = Im x
         to avoid cancellation error in  d +/- Re x.  */
      if (re > 0)
        {
          r = sqrtf (0.5 * d + 0.5 * re);
          s = (0.5 * im) / r;
        }
      else
        {
          s = sqrtf (0.5 * d - 0.5 * re);
          r = fabsf ((0.5 * im) / s);
        }

      COMPLEX_ASSIGN (v, r, copysignf (s, im));
    }
  return v;
}

