/* Generic helper function for repacking arrays.
   Copyright 2003, 2004, 2005, 2007  Free Software Foundation, Inc.
   Contributed by Paul Brook <paul@nowt.org>

This file is part of the GNU Fortran 95 runtime library (libgfortran).

Libgfortran is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file into combinations with other programs,
and to distribute those combinations without any restriction coming
from the use of this file.  (The General Public License restrictions
do apply in other respects; for example, they cover modification of
the file, and distribution when not linked into a combine
executable.)

Libgfortran is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public
License along with libgfortran; see the file COPYING.  If not,
write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#include "libgfortran.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>

extern void *internal_pack (gfc_array_char *);
export_proto(internal_pack);

void *
internal_pack (gfc_array_char * source)
{
  index_type count[GFC_MAX_DIMENSIONS];
  index_type extent[GFC_MAX_DIMENSIONS];
  index_type stride[GFC_MAX_DIMENSIONS];
  index_type stride0;
  index_type dim;
  index_type ssize;
  const char *src;
  char *dest;
  void *destptr;
  int n;
  int packed;
  index_type size;
  int type;

  if (source->dim[0].stride == 0)
    {
      source->dim[0].stride = 1;
      return source->data;
    }

  type = GFC_DESCRIPTOR_TYPE (source);
  size = GFC_DESCRIPTOR_SIZE (source);
  switch (type)
    {
    case GFC_DTYPE_INTEGER:
    case GFC_DTYPE_LOGICAL:
      switch (size)
	{
	case sizeof (GFC_INTEGER_1):
	  return internal_pack_1 ((gfc_array_i1 *) source);

	case sizeof (GFC_INTEGER_2):
	  return internal_pack_2 ((gfc_array_i2 *) source);

	case sizeof (GFC_INTEGER_4):
	  return internal_pack_4 ((gfc_array_i4 *) source);
	  
	case sizeof (GFC_INTEGER_8):
	  return internal_pack_8 ((gfc_array_i8 *) source);

#if defined(HAVE_GFC_INTEGER_16)
	case sizeof (GFC_INTEGER_16):
	  return internal_pack_16 ((gfc_array_i16 *) source);
#endif
	}
      break;

    case GFC_DTYPE_REAL:
      switch (size)
	{
	case sizeof (GFC_REAL_4):
	  return internal_pack_r4 ((gfc_array_r4 *) source);

	case sizeof (GFC_REAL_8):
	  return internal_pack_r8 ((gfc_array_r8 *) source);

#if defined (HAVE_GFC_REAL_10)
	case sizeof (GFC_REAL_10):
	  return internal_pack_r10 ((gfc_array_r10 *) source);
#endif

#if defined (HAVE_GFC_REAL_16)
	case sizeof (GFC_REAL_16):
	  return internal_pack_r16 ((gfc_array_r16 *) source);
#endif
	}
    case GFC_DTYPE_COMPLEX:
      switch (size)
	{
	case sizeof (GFC_COMPLEX_4):
	  return internal_pack_c4 ((gfc_array_c4 *) source);
	  
	case sizeof (GFC_COMPLEX_8):
	  return internal_pack_c8 ((gfc_array_c8 *) source);

#if defined (HAVE_GFC_COMPLEX_10)
	case sizeof (GFC_COMPLEX_10):
	  return internal_pack_c10 ((gfc_array_c10 *) source);
#endif

#if defined (HAVE_GFC_COMPLEX_16)
	case sizeof (GFC_COMPLEX_16):
	  return internal_pack_c16 ((gfc_array_c16 *) source);
#endif

	}
      break;

    default:
      break;
    }

  dim = GFC_DESCRIPTOR_RANK (source);
  ssize = 1;
  packed = 1;
  for (n = 0; n < dim; n++)
    {
      count[n] = 0;
      stride[n] = source->dim[n].stride;
      extent[n] = source->dim[n].ubound + 1 - source->dim[n].lbound;
      if (extent[n] <= 0)
        {
          /* Do nothing.  */
          packed = 1;
          break;
        }

      if (ssize != stride[n])
        packed = 0;

      ssize *= extent[n];
    }

  if (packed)
    return source->data;

   /* Allocate storage for the destination.  */
  destptr = internal_malloc_size (ssize * size);
  dest = (char *)destptr;
  src = source->data;
  stride0 = stride[0] * size;

  while (src)
    {
      /* Copy the data.  */
      memcpy(dest, src, size);
      /* Advance to the next element.  */
      dest += size;
      src += stride0;
      count[0]++;
      /* Advance to the next source element.  */
      n = 0;
      while (count[n] == extent[n])
        {
          /* When we get to the end of a dimension, reset it and increment
             the next dimension.  */
          count[n] = 0;
          /* We could precalculate these products, but this is a less
             frequently used path so probably not worth it.  */
          src -= stride[n] * extent[n] * size;
          n++;
          if (n == dim)
            {
              src = NULL;
              break;
            }
          else
            {
              count[n]++;
              src += stride[n] * size;
            }
        }
    }
  return destptr;
}
