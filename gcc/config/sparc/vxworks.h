/* Definitions of target machine for GNU compiler,
   for SPARC targeting the VxWorks run time environment.
   Copyright (C) 2007 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

#define TARGET_OS_CPP_BUILTINS()		\
  do						\
    {						\
      builtin_define ("__sparc");		\
      builtin_define ("CPU=SIMSPARCSOLARIS");	\
      VXWORKS_OS_CPP_BUILTINS ();		\
    }						\
  while (0)

#undef OVERRIDE_OPTIONS
#define OVERRIDE_OPTIONS			\
  do						\
    {						\
      VXWORKS_OVERRIDE_OPTIONS;			\
      sparc_override_options ();		\
    }						\
  while (0)

#undef CPP_SUBTARGET_SPEC
#define CPP_SUBTARGET_SPEC VXWORKS_ADDITIONAL_CPP_SPEC

#undef LIB_SPEC
#define LIB_SPEC VXWORKS_LIB_SPEC
#undef LINK_SPEC
#define LINK_SPEC VXWORKS_LINK_SPEC
#undef STARTFILE_SPEC
#define STARTFILE_SPEC VXWORKS_STARTFILE_SPEC
#undef ENDFILE_SPEC
#define ENDFILE_SPEC VXWORKS_ENDFILE_SPEC

#undef TARGET_VERSION
#define TARGET_VERSION fputs (" (SPARC/VxWorks)", stderr);

#undef FUNCTION_PROFILER
#define FUNCTION_PROFILER VXWORKS_FUNCTION_PROFILER

/* Use standard numbered ctors/dtors sections.  */
#undef CTORS_SECTION_ASM_OP
#undef DTORS_SECTION_ASM_OP

/* We cannot use PC-relative accesses for VxWorks PIC because there is no
   fixed gap between segments.  */
#undef ASM_PREFERRED_EH_DATA_FORMAT
