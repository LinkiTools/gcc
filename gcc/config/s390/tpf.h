/* Definitions for target OS TPF for GNU compiler, for IBM S/390 hardware
   Copyright (C) 2003, 2004 Free Software Foundation, Inc.
   Contributed by P.J. Darcy (darcypj@us.ibm.com),
                  Hartmut Penner (hpenner@de.ibm.com), and
                  Ulrich Weigand (uweigand@de.ibm.com).

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 59 Temple Place - Suite 330, Boston, MA
02111-1307, USA.  */

#ifndef _TPF_H
#define _TPF_H

/* TPF wants the following macros defined/undefined as follows.  */
#undef TARGET_TPF
#define TARGET_TPF 1
#undef ASM_APP_ON
#define ASM_APP_ON "#APP\n"
#undef ASM_APP_OFF
#define ASM_APP_OFF "#NO_APP\n"
#define NO_IMPLICIT_EXTERN_C
#define TARGET_HAS_F_SETLKW
#undef MD_EXEC_PREFIX
#undef MD_STARTFILE_PREFIX

#undef  SIZE_TYPE
#define SIZE_TYPE ("long unsigned int")
#undef  PTRDIFF_TYPE
#define PTRDIFF_TYPE ("long int")
#undef  WCHAR_TYPE
#define WCHAR_TYPE "int"
#undef  WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 32


/* Basic record keeping for the TPF OS name.  */
#undef  TARGET_VERSION
#define TARGET_VERSION fprintf (stderr, " (TPF: zSeries)");

/* TPF OS specific stack-pointer offset.  */
#undef STACK_POINTER_OFFSET
#define STACK_POINTER_OFFSET 		448
/* TPF stack placeholder offset.  */
#undef TPF_LOC_DIFF_OFFSET
#define TPF_LOC_DIFF_OFFSET             168

/* When building for TPF, set a generic default target that is 64 bits.  */
#undef TARGET_DEFAULT
#define TARGET_DEFAULT             0xb3

/* Exception handling.  */

/* Select a format to encode pointers in exception handling data.  */
#undef ASM_PREFERRED_EH_DATA_FORMAT
#define ASM_PREFERRED_EH_DATA_FORMAT(CODE, GLOBAL) DW_EH_PE_absptr

/* TPF OS specific compiler settings.  */
#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()                \
  do                                            \
    {                                           \
      builtin_define_std ("tpf");               \
      builtin_assert ("system=tpf");            \
      builtin_define ("__ELF__");               \
      if (flag_pic)                             \
        {                                       \
          builtin_define ("__PIC__");           \
          builtin_define ("__pic__");           \
        }                                       \
    }                                           \
  while (0)


/* Make TPF specific spec file settings here.  */

#undef  STARTFILE_SPEC
#define STARTFILE_SPEC \
  "%{!shared: \
     %{pg:gcrt1.o%s} %{!pg:%{p:gcrt1.o%s} \
                       %{!p:%{profile:gcrt1.o%s} \
                         %{!profile:crt1.o%s}}}} \
   crti.o%s %{static:crtbeginT.o%s} \
   %{!static:%{!shared:crtbegin.o%s} %{shared:crtbeginS.o%s}}"

#undef  ENDFILE_SPEC
#define ENDFILE_SPEC \
  "%{!shared:crtend.o%s} %{shared:crtendS.o%s} crtn.o%s"

/* The GNU C++ standard library requires that these macros be defined.  */
#undef CPLUSPLUS_CPP_SPEC
#define CPLUSPLUS_CPP_SPEC "-D_GNU_SOURCE %(cpp)"

#undef  LIB_SPEC
#define LIB_SPEC "%{pthread:-lpthread} -lc"

#undef TARGET_C99_FUNCTIONS
#define TARGET_C99_FUNCTIONS 1

#undef LINK_SPEC
#define LINK_SPEC \
  "-m elf64_s390 \
   %{shared:-shared} \
   %{!shared: \
      %{static:-static} \
      %{!static: \
        %{rdynamic:-export-dynamic} \
        %{!dynamic-linker:-dynamic-linker /lib/ld64.so}}}"

extern unsigned int __isPATrange (void *);

/* Exceptions macro defined for TPF so that functions without 
   dwarf frame information can be used with exceptions.  */
#define MD_FALLBACK_FRAME_STATE_FOR(CONTEXT, FS, SUCCESS)               \
  do                                                                    \
    {                                                                   \
      unsigned long int regs_;                                          \
      unsigned long int new_cfa_;                                       \
      int i_;                                                           \
                                                                        \
      if ((CONTEXT)->cfa == NULL)                                       \
        goto SUCCESS;                                                   \
                                                                        \
      /* Are we going through special linkage code?  */                 \
      if (__isPATrange((CONTEXT)->ra))                                  \
        {                                                               \
          /* No stack frame.   */                                       \
          (FS)->cfa_how = CFA_REG_OFFSET;                               \
          (FS)->cfa_reg = 15;                                           \
          (FS)->cfa_offset = STACK_POINTER_OFFSET;                      \
                                                                        \
          /* All registers remain unchanged ...  */                     \
          for (i_ = 0; i_ < 32; i_++)                                   \
            {                                                           \
              (FS)->regs.reg[i_].how = REG_SAVED_REG;                   \
              (FS)->regs.reg[i_].loc.reg = i_;                          \
            }                                                           \
                                                                        \
          /* ... except for %r14, which is stored at CFA-112            \
             and used as return address.  */                            \
          (FS)->regs.reg[14].how = REG_SAVED_OFFSET;                    \
          (FS)->regs.reg[14].loc.offset =                               \
            TPF_LOC_DIFF_OFFSET - STACK_POINTER_OFFSET;                 \
          (FS)->retaddr_column = 14;                                    \
                                                                        \
          goto SUCCESS;                                                 \
                                                                        \
        }                                                               \
                                                                        \
      regs_ = *((unsigned long int *)                                   \
        (((unsigned long int) (CONTEXT)->cfa) - STACK_POINTER_OFFSET)); \
      new_cfa_ = regs_ + STACK_POINTER_OFFSET;                          \
      (FS)->cfa_how = CFA_REG_OFFSET;                                   \
      (FS)->cfa_reg = 15;                                               \
      (FS)->cfa_offset = new_cfa_ -                                     \
        (unsigned long int) (CONTEXT)->cfa + STACK_POINTER_OFFSET;      \
                                                                        \
      for (i_ = 0; i_ < 16; i_++)                                       \
        {                                                               \
          (FS)->regs.reg[i_].how = REG_SAVED_OFFSET;                    \
          (FS)->regs.reg[i_].loc.offset =                               \
            (regs_+(i_*8)) - new_cfa_;                                  \
        }                                                               \
                                                                        \
      for (i_ = 0; i_ < 4; i_++)                                        \
        {                                                               \
          (FS)->regs.reg[16+i_].how = REG_SAVED_OFFSET;                 \
          (FS)->regs.reg[16+i_].loc.offset =                            \
            (regs_+(16*8)+(i_*8)) - new_cfa_;                           \
        }                                                               \
                                                                        \
      (FS)->retaddr_column = 14;                                        \
                                                                        \
      goto SUCCESS;                                                     \
                                                                        \
    } while (0)

#endif /* ! _TPF_H */

