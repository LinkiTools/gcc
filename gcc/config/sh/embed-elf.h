/* Definitions of target machine for GNU compiler for Renesas / SuperH SH 
   non-Linux embedded targets.
   Copyright (C) 2002, 2003 Free Software Foundation, Inc.
   Contributed by J"orn Rennecke <joern.rennecke@superh.com>

This file is part of GNU CC.

GNU CC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU CC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU CC; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

#undef USER_LABEL_PREFIX
#define USER_LABEL_PREFIX "_"

#undef TARGET_HAS_F_SETLKW
