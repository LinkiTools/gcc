/* Configuration file for Symbian OS on ARM processors.
   Copyright (C) 2004
   Free Software Foundation, Inc.
   Contributed by CodeSourcery, LLC   

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 2, or (at your
   option) any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to
   the Free Software Foundation, 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

/* Do not expand builtin functions (unless explicitly prefixed with
   "__builtin").  Symbian OS code relies on properties of the standard
   library that go beyond those guaranteed by the ANSI/ISO standard.
   For example, "memcpy" works even with overlapping memory, like
   "memmove".  We cannot simply set flag_no_builtin in arm.c because
   (a) flag_no_builtin is not declared in language-independent code,
   and (b) that would prevent users from explicitly overriding the
   default with -fbuiltin, which may sometimes be useful.

   Make all symbols hidden by default.  Symbian OS expects that all
   exported symbols will be explicitly marked with
   "__declspec(dllexport)".  */
#define CC1_SPEC						\
  "%{!fbuiltin:%{!fno-builtin:-fno-builtin}} "			\
  "%{!fvisibility=*:-fvisibility=hidden} "			\
  "%{!fshort-enums:%{!fno-short-enums:-fno-short-enums}} "	\
  "%{!fshort-wchar:%{!fno-short-wchar:-fshort-wchar}} "
#define CC1PLUS_SPEC CC1_SPEC

/* Symbian OS does not use crt0.o, unlike the generic unknown-elf
   configuration.  */
#undef STARTFILE_SPEC
#define STARTFILE_SPEC "crti%O%s crtbegin%O%s"

/* Support the "dllimport" attribute.  */
#define TARGET_DLLIMPORT_DECL_ATTRIBUTES 1

/* Symbian OS assumes ARM V5 or above.  Since -march=armv5 is
   equivalent to making the ARM 10TDMI core the default, we can set
   SUBTARGET_CPU_DEFAULT and get an equivalent effect.  */
#undef SUBTARGET_CPU_DEFAULT
#define SUBTARGET_CPU_DEFAULT TARGET_CPU_arm10tdmi

/* The assembler should assume the VFP FPU format when the hard-float
   ABI is in use.  */
#undef SUBTARGET_ASM_FLOAT_SPEC
#define SUBTARGET_ASM_FLOAT_SPEC \
  "%{!mfpu=*:%{mfloat-abi=hard:-mfpu=vfp}}"
  
