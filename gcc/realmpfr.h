/* Definitions of floating-point conversion from compiler
   internal format to MPFR.
   Copyright (C) 2010, 2012
   Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it under
   the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 3, or (at your option) any later
   version.

   GCC is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
   for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING3.  If not see
   <http://www.gnu.org/licenses/>.  */

#ifndef GCC_REALGMP_H
#define GCC_REALGMP_H

#include <mpfr.h>
#include <mpc.h>
#include "real.h"

/* In builtins.c.  */
extern tree do_mpc_arg2 (tree, tree, tree, int, int (*)(mpc_ptr, mpc_srcptr, mpc_srcptr, mpc_rnd_t));

/* Convert between MPFR and REAL_VALUE_TYPE.  The caller is
   responsible for initializing and clearing the MPFR parameter.  */

extern void real_from_mpfr (REAL_VALUE_TYPE *, mpfr_srcptr, tree, mp_rnd_t);
extern void mpfr_from_real (mpfr_ptr, const REAL_VALUE_TYPE *, mp_rnd_t);

#endif /* ! GCC_REALGMP_H */

