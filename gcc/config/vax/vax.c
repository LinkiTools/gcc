/* Subroutines for insn-output.c for VAX.
   Copyright (C) 1987, 1994, 1995, 1997, 1998, 1999, 2000, 2001, 2002,
   2004, 2005, 2006, 2007, 2008, 2009, 2010, 2011
   Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "df.h"
#include "tree.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "insn-config.h"
#include "conditions.h"
#include "function.h"
#include "output.h"
#include "insn-attr.h"
#include "recog.h"
#include "expr.h"
#include "optabs.h"
#include "flags.h"
#include "debug.h"
#include "diagnostic-core.h"
#include "reload.h"
#include "tm-preds.h"
#include "tm-constrs.h"
#include "tm_p.h"
#include "target.h"
#include "target-def.h"

static void vax_option_override (void);
static bool vax_legitimate_address_p (enum machine_mode, rtx, bool);
static void vax_output_function_prologue (FILE *, HOST_WIDE_INT);
static void vax_file_start (void);
static void vax_init_libfuncs (void);
static void vax_output_mi_thunk (FILE *, tree, HOST_WIDE_INT,
				 HOST_WIDE_INT, tree);
static int vax_address_cost_1 (rtx);
static int vax_address_cost (rtx, bool);
static bool vax_rtx_costs (rtx, int, int, int *, bool);
static rtx vax_function_arg (cumulative_args_t, enum machine_mode,
			     const_tree, bool);
static void vax_function_arg_advance (cumulative_args_t, enum machine_mode,
				      const_tree, bool);
static rtx vax_struct_value_rtx (tree, int);
static rtx vax_builtin_setjmp_frame_value (void);
static void vax_asm_trampoline_template (FILE *);
static void vax_trampoline_init (rtx, tree, rtx);
static int vax_return_pops_args (tree, tree, int);

/* Initialize the GCC target structure.  */
#undef TARGET_ASM_ALIGNED_HI_OP
#define TARGET_ASM_ALIGNED_HI_OP "\t.word\t"

#undef TARGET_ASM_FUNCTION_PROLOGUE
#define TARGET_ASM_FUNCTION_PROLOGUE vax_output_function_prologue

#undef TARGET_ASM_FILE_START
#define TARGET_ASM_FILE_START vax_file_start
#undef TARGET_ASM_FILE_START_APP_OFF
#define TARGET_ASM_FILE_START_APP_OFF true

#undef TARGET_INIT_LIBFUNCS
#define TARGET_INIT_LIBFUNCS vax_init_libfuncs

#undef TARGET_ASM_OUTPUT_MI_THUNK
#define TARGET_ASM_OUTPUT_MI_THUNK vax_output_mi_thunk
#undef TARGET_ASM_CAN_OUTPUT_MI_THUNK
#define TARGET_ASM_CAN_OUTPUT_MI_THUNK default_can_output_mi_thunk_no_vcall

#undef TARGET_RTX_COSTS
#define TARGET_RTX_COSTS vax_rtx_costs
#undef TARGET_ADDRESS_COST
#define TARGET_ADDRESS_COST vax_address_cost

#undef TARGET_PROMOTE_PROTOTYPES
#define TARGET_PROMOTE_PROTOTYPES hook_bool_const_tree_true

#undef TARGET_FUNCTION_ARG
#define TARGET_FUNCTION_ARG vax_function_arg
#undef TARGET_FUNCTION_ARG_ADVANCE
#define TARGET_FUNCTION_ARG_ADVANCE vax_function_arg_advance

#undef TARGET_STRUCT_VALUE_RTX
#define TARGET_STRUCT_VALUE_RTX vax_struct_value_rtx

#undef TARGET_BUILTIN_SETJMP_FRAME_VALUE
#define TARGET_BUILTIN_SETJMP_FRAME_VALUE vax_builtin_setjmp_frame_value

#undef TARGET_LEGITIMATE_ADDRESS_P
#define TARGET_LEGITIMATE_ADDRESS_P vax_legitimate_address_p

#undef TARGET_FRAME_POINTER_REQUIRED
#define TARGET_FRAME_POINTER_REQUIRED hook_bool_void_true

#undef TARGET_ASM_TRAMPOLINE_TEMPLATE
#define TARGET_ASM_TRAMPOLINE_TEMPLATE vax_asm_trampoline_template
#undef TARGET_TRAMPOLINE_INIT
#define TARGET_TRAMPOLINE_INIT vax_trampoline_init
#undef TARGET_RETURN_POPS_ARGS
#define TARGET_RETURN_POPS_ARGS vax_return_pops_args

#undef TARGET_OPTION_OVERRIDE
#define TARGET_OPTION_OVERRIDE vax_option_override

struct gcc_target targetm = TARGET_INITIALIZER;

/* Set global variables as needed for the options enabled.  */

static void
vax_option_override (void)
{
  /* We're VAX floating point, not IEEE floating point.  */
  if (TARGET_G_FLOAT)
    REAL_MODE_FORMAT (DFmode) = &vax_g_format;

#ifdef SUBTARGET_OVERRIDE_OPTIONS
  SUBTARGET_OVERRIDE_OPTIONS;
#endif
}

/* Generate the assembly code for function entry.  FILE is a stdio
   stream to output the code to.  SIZE is an int: how many units of
   temporary storage to allocate.

   Refer to the array `regs_ever_live' to determine which registers to
   save; `regs_ever_live[I]' is nonzero if register number I is ever
   used in the function.  This function is responsible for knowing
   which registers should not be saved even if used.  */

static void
vax_output_function_prologue (FILE * file, HOST_WIDE_INT size)
{
  int regno;
  int mask = 0;

  for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno++)
    if (df_regs_ever_live_p (regno) && !call_used_regs[regno])
      mask |= 1 << regno;

  fprintf (file, "\t.word 0x%x\n", mask);

  if (dwarf2out_do_frame ())
    {
      const char *label = dwarf2out_cfi_label (false);
      int offset = 0;

      for (regno = FIRST_PSEUDO_REGISTER-1; regno >= 0; --regno)
	if (df_regs_ever_live_p (regno) && !call_used_regs[regno])
	  dwarf2out_reg_save (label, regno, offset -= 4);

      dwarf2out_reg_save (label, PC_REGNUM, offset -= 4);
      dwarf2out_reg_save (label, FRAME_POINTER_REGNUM, offset -= 4);
      dwarf2out_reg_save (label, ARG_POINTER_REGNUM, offset -= 4);
      dwarf2out_def_cfa (label, FRAME_POINTER_REGNUM, -(offset - 4));
    }

  size -= STARTING_FRAME_OFFSET;
  if (size >= 64)
    asm_fprintf (file, "\tmovab %wd(%Rsp),%Rsp\n", -size);
  else if (size)
    asm_fprintf (file, "\tsubl2 $%wd,%Rsp\n", size);
}

/* When debugging with stabs, we want to output an extra dummy label
   so that gas can distinguish between D_float and G_float prior to
   processing the .stabs directive identifying type double.  */
static void
vax_file_start (void)
{
  default_file_start ();

  if (write_symbols == DBX_DEBUG)
    fprintf (asm_out_file, "___vax_%c_doubles:\n", ASM_DOUBLE_CHAR);
}

/* We can use the BSD C library routines for the libgcc calls that are
   still generated, since that's what they boil down to anyways.  When
   ELF, avoid the user's namespace.  */

static void
vax_init_libfuncs (void)
{
  if (TARGET_BSD_DIVMOD)
    {
      set_optab_libfunc (udiv_optab, SImode, TARGET_ELF ? "*__udiv" : "*udiv");
      set_optab_libfunc (umod_optab, SImode, TARGET_ELF ? "*__urem" : "*urem");
    }
}

/* This is like nonimmediate_operand with a restriction on the type of MEM.  */

static void
split_quadword_operands (rtx insn, enum rtx_code code, rtx * operands,
			 rtx * low, int n)
{
  int i;

  for (i = 0; i < n; i++)
    low[i] = 0;

  for (i = 0; i < n; i++)
    {
      if (MEM_P (operands[i])
	  && (GET_CODE (XEXP (operands[i], 0)) == PRE_DEC
	      || GET_CODE (XEXP (operands[i], 0)) == POST_INC))
	{
	  rtx addr = XEXP (operands[i], 0);
	  operands[i] = low[i] = gen_rtx_MEM (SImode, addr);
	}
      else if (optimize_size && MEM_P (operands[i])
	       && REG_P (XEXP (operands[i], 0))
	       && (code != MINUS || operands[1] != const0_rtx)
	       && find_regno_note (insn, REG_DEAD,
				   REGNO (XEXP (operands[i], 0))))
	{
	  low[i] = gen_rtx_MEM (SImode,
				gen_rtx_POST_INC (Pmode,
						  XEXP (operands[i], 0)));
	  operands[i] = gen_rtx_MEM (SImode, XEXP (operands[i], 0));
	}
      else
	{
	  low[i] = operand_subword (operands[i], 0, 0, DImode);
	  operands[i] = operand_subword (operands[i], 1, 0, DImode);
	}
    }
}

void
print_operand_address (FILE * file, rtx addr)
{
  rtx orig = addr;
  rtx reg1, breg, ireg;
  rtx offset;

 retry:
  switch (GET_CODE (addr))
    {
    case MEM:
      fprintf (file, "*");
      addr = XEXP (addr, 0);
      goto retry;

    case REG:
      fprintf (file, "(%s)", reg_names[REGNO (addr)]);
      break;

    case PRE_DEC:
      fprintf (file, "-(%s)", reg_names[REGNO (XEXP (addr, 0))]);
      break;

    case POST_INC:
      fprintf (file, "(%s)+", reg_names[REGNO (XEXP (addr, 0))]);
      break;

    case PLUS:
      /* There can be either two or three things added here.  One must be a
	 REG.  One can be either a REG or a MULT of a REG and an appropriate
	 constant, and the third can only be a constant or a MEM.

	 We get these two or three things and put the constant or MEM in
	 OFFSET, the MULT or REG in IREG, and the REG in BREG.  If we have
	 a register and can't tell yet if it is a base or index register,
	 put it into REG1.  */

      reg1 = 0; ireg = 0; breg = 0; offset = 0;

      if (CONSTANT_ADDRESS_P (XEXP (addr, 0))
	  || MEM_P (XEXP (addr, 0)))
	{
	  offset = XEXP (addr, 0);
	  addr = XEXP (addr, 1);
	}
      else if (CONSTANT_ADDRESS_P (XEXP (addr, 1))
	       || MEM_P (XEXP (addr, 1)))
	{
	  offset = XEXP (addr, 1);
	  addr = XEXP (addr, 0);
	}
      else if (GET_CODE (XEXP (addr, 1)) == MULT)
	{
	  ireg = XEXP (addr, 1);
	  addr = XEXP (addr, 0);
	}
      else if (GET_CODE (XEXP (addr, 0)) == MULT)
	{
	  ireg = XEXP (addr, 0);
	  addr = XEXP (addr, 1);
	}
      else if (REG_P (XEXP (addr, 1)))
	{
	  reg1 = XEXP (addr, 1);
	  addr = XEXP (addr, 0);
	}
      else if (REG_P (XEXP (addr, 0)))
	{
	  reg1 = XEXP (addr, 0);
	  addr = XEXP (addr, 1);
	}
      else
	gcc_unreachable ();

      if (REG_P (addr))
	{
	  if (reg1)
	    ireg = addr;
	  else
	    reg1 = addr;
	}
      else if (GET_CODE (addr) == MULT)
	ireg = addr;
      else
	{
	  gcc_assert (GET_CODE (addr) == PLUS);
	  if (CONSTANT_ADDRESS_P (XEXP (addr, 0))
	      || MEM_P (XEXP (addr, 0)))
	    {
	      if (offset)
		{
		  if (CONST_INT_P (offset))
		    offset = plus_constant (XEXP (addr, 0), INTVAL (offset));
		  else
		    {
		      gcc_assert (CONST_INT_P (XEXP (addr, 0)));
		      offset = plus_constant (offset, INTVAL (XEXP (addr, 0)));
		    }
		}
	      offset = XEXP (addr, 0);
	    }
	  else if (REG_P (XEXP (addr, 0)))
	    {
	      if (reg1)
		ireg = reg1, breg = XEXP (addr, 0), reg1 = 0;
	      else
		reg1 = XEXP (addr, 0);
	    }
	  else
	    {
	      gcc_assert (GET_CODE (XEXP (addr, 0)) == MULT);
	      gcc_assert (!ireg);
	      ireg = XEXP (addr, 0);
	    }

	  if (CONSTANT_ADDRESS_P (XEXP (addr, 1))
	      || MEM_P (XEXP (addr, 1)))
	    {
	      if (offset)
		{
		  if (CONST_INT_P (offset))
		    offset = plus_constant (XEXP (addr, 1), INTVAL (offset));
		  else
		    {
		      gcc_assert (CONST_INT_P (XEXP (addr, 1)));
		      offset = plus_constant (offset, INTVAL (XEXP (addr, 1)));
		    }
		}
	      offset = XEXP (addr, 1);
	    }
	  else if (REG_P (XEXP (addr, 1)))
	    {
	      if (reg1)
		ireg = reg1, breg = XEXP (addr, 1), reg1 = 0;
	      else
		reg1 = XEXP (addr, 1);
	    }
	  else
	    {
	      gcc_assert (GET_CODE (XEXP (addr, 1)) == MULT);
	      gcc_assert (!ireg);
	      ireg = XEXP (addr, 1);
	    }
	}

      /* If REG1 is nonzero, figure out if it is a base or index register.  */
      if (reg1)
	{
	  if (breg
	      || (flag_pic && GET_CODE (addr) == SYMBOL_REF)
	      || (offset
		  && (MEM_P (offset)
		      || (flag_pic && symbolic_operand (offset, SImode)))))
	    {
	      gcc_assert (!ireg);
	      ireg = reg1;
	    }
	  else
	    breg = reg1;
	}

      if (offset != 0)
	{
	  if (flag_pic && symbolic_operand (offset, SImode))
	    {
	      if (breg && ireg)
		{
		  debug_rtx (orig);
		  output_operand_lossage ("symbol used with both base and indexed registers");
		}

#ifdef NO_EXTERNAL_INDIRECT_ADDRESS
	      if (flag_pic > 1 && GET_CODE (offset) == CONST
		  && GET_CODE (XEXP (XEXP (offset, 0), 0)) == SYMBOL_REF
		  && !SYMBOL_REF_LOCAL_P (XEXP (XEXP (offset, 0), 0)))
		{
		  debug_rtx (orig);
		  output_operand_lossage ("symbol with offset used in PIC mode");
		}
#endif

	      /* symbol(reg) isn't PIC, but symbol[reg] is.  */
	      if (breg)
		{
		  ireg = breg;
		  breg = 0;
		}

	    }

	  output_address (offset);
	}

      if (breg != 0)
	fprintf (file, "(%s)", reg_names[REGNO (breg)]);

      if (ireg != 0)
	{
	  if (GET_CODE (ireg) == MULT)
	    ireg = XEXP (ireg, 0);
	  gcc_assert (REG_P (ireg));
	  fprintf (file, "[%s]", reg_names[REGNO (ireg)]);
	}
      break;

    default:
      output_addr_const (file, addr);
    }
}

void
print_operand (FILE *file, rtx x, int code)
{
  if (code == '#')
    fputc (ASM_DOUBLE_CHAR, file);
  else if (code == '|')
    fputs (REGISTER_PREFIX, file);
  else if (code == 'c')
    fputs (cond_name (x), file);
  else if (code == 'C')
    fputs (rev_cond_name (x), file);
  else if (code == 'D' && CONST_INT_P (x) && INTVAL (x) < 0)
    fprintf (file, "$" NEG_HWI_PRINT_HEX16, INTVAL (x));
  else if (code == 'P' && CONST_INT_P (x))
    fprintf (file, "$" HOST_WIDE_INT_PRINT_DEC, INTVAL (x) + 1);
  else if (code == 'N' && CONST_INT_P (x))
    fprintf (file, "$" HOST_WIDE_INT_PRINT_DEC, ~ INTVAL (x));
  /* rotl instruction cannot deal with negative arguments.  */
  else if (code == 'R' && CONST_INT_P (x))
    fprintf (file, "$" HOST_WIDE_INT_PRINT_DEC, 32 - INTVAL (x));
  else if (code == 'H' && CONST_INT_P (x))
    fprintf (file, "$%d", (int) (0xffff & ~ INTVAL (x)));
  else if (code == 'h' && CONST_INT_P (x))
    fprintf (file, "$%d", (short) - INTVAL (x));
  else if (code == 'B' && CONST_INT_P (x))
    fprintf (file, "$%d", (int) (0xff & ~ INTVAL (x)));
  else if (code == 'b' && CONST_INT_P (x))
    fprintf (file, "$%d", (int) (0xff & - INTVAL (x)));
  else if (code == 'M' && CONST_INT_P (x))
    fprintf (file, "$%d", ~((1 << INTVAL (x)) - 1));
  else if (REG_P (x))
    fprintf (file, "%s", reg_names[REGNO (x)]);
  else if (MEM_P (x))
    output_address (XEXP (x, 0));
  else if (GET_CODE (x) == CONST_DOUBLE && GET_MODE (x) == SFmode)
    {
      char dstr[30];
      real_to_decimal (dstr, CONST_DOUBLE_REAL_VALUE (x),
		       sizeof (dstr), 0, 1);
      fprintf (file, "$0f%s", dstr);
    }
  else if (GET_CODE (x) == CONST_DOUBLE && GET_MODE (x) == DFmode)
    {
      char dstr[30];
      real_to_decimal (dstr, CONST_DOUBLE_REAL_VALUE (x),
		       sizeof (dstr), 0, 1);
      fprintf (file, "$0%c%s", ASM_DOUBLE_CHAR, dstr);
    }
  else
    {
      if (flag_pic > 1 && symbolic_operand (x, SImode))
	{
	  debug_rtx (x);
	  output_operand_lossage ("symbol used as immediate operand");
	}
      putc ('$', file);
      output_addr_const (file, x);
    }
}

const char *
cond_name (rtx op)
{
  switch (GET_CODE (op))
    {
    case NE:
      return "neq";
    case EQ:
      return "eql";
    case GE:
      return "geq";
    case GT:
      return "gtr";
    case LE:
      return "leq";
    case LT:
      return "lss";
    case GEU:
      return "gequ";
    case GTU:
      return "gtru";
    case LEU:
      return "lequ";
    case LTU:
      return "lssu";

    default:
      gcc_unreachable ();
    }
}

const char *
rev_cond_name (rtx op)
{
  switch (GET_CODE (op))
    {
    case EQ:
      return "neq";
    case NE:
      return "eql";
    case LT:
      return "geq";
    case LE:
      return "gtr";
    case GT:
      return "leq";
    case GE:
      return "lss";
    case LTU:
      return "gequ";
    case LEU:
      return "gtru";
    case GTU:
      return "lequ";
    case GEU:
      return "lssu";

    default:
      gcc_unreachable ();
    }
}

static bool
vax_float_literal (rtx c)
{
  enum machine_mode mode;
  REAL_VALUE_TYPE r, s;
  int i;

  if (GET_CODE (c) != CONST_DOUBLE)
    return false;

  mode = GET_MODE (c);

  if (c == const_tiny_rtx[(int) mode][0]
      || c == const_tiny_rtx[(int) mode][1]
      || c == const_tiny_rtx[(int) mode][2])
    return true;

  REAL_VALUE_FROM_CONST_DOUBLE (r, c);

  for (i = 0; i < 7; i++)
    {
      int x = 1 << i;
      bool ok;
      REAL_VALUE_FROM_INT (s, x, 0, mode);

      if (REAL_VALUES_EQUAL (r, s))
	return true;
      ok = exact_real_inverse (mode, &s);
      gcc_assert (ok);
      if (REAL_VALUES_EQUAL (r, s))
	return true;
    }
  return false;
}


/* Return the cost in cycles of a memory address, relative to register
   indirect.

   Each of the following adds the indicated number of cycles:

   1 - symbolic address
   1 - pre-decrement
   1 - indexing and/or offset(register)
   2 - indirect */


static int
vax_address_cost_1 (rtx addr)
{
  int reg = 0, indexed = 0, indir = 0, offset = 0, predec = 0;
  rtx plus_op0 = 0, plus_op1 = 0;
 restart:
  switch (GET_CODE (addr))
    {
    case PRE_DEC:
      predec = 1;
    case REG:
    case SUBREG:
    case POST_INC:
      reg = 1;
      break;
    case MULT:
      indexed = 1;	/* 2 on VAX 2 */
      break;
    case CONST_INT:
      /* byte offsets cost nothing (on a VAX 2, they cost 1 cycle) */
      if (offset == 0)
	offset = (unsigned HOST_WIDE_INT)(INTVAL(addr)+128) > 256;
      break;
    case CONST:
    case SYMBOL_REF:
      offset = 1;	/* 2 on VAX 2 */
      break;
    case LABEL_REF:	/* this is probably a byte offset from the pc */
      if (offset == 0)
	offset = 1;
      break;
    case PLUS:
      if (plus_op0)
	plus_op1 = XEXP (addr, 0);
      else
	plus_op0 = XEXP (addr, 0);
      addr = XEXP (addr, 1);
      goto restart;
    case MEM:
      indir = 2;	/* 3 on VAX 2 */
      addr = XEXP (addr, 0);
      goto restart;
    default:
      break;
    }

  /* Up to 3 things can be added in an address.  They are stored in
     plus_op0, plus_op1, and addr.  */

  if (plus_op0)
    {
      addr = plus_op0;
      plus_op0 = 0;
      goto restart;
    }
  if (plus_op1)
    {
      addr = plus_op1;
      plus_op1 = 0;
      goto restart;
    }
  /* Indexing and register+offset can both be used (except on a VAX 2)
     without increasing execution time over either one alone.  */
  if (reg && indexed && offset)
    return reg + indir + offset + predec;
  return reg + indexed + indir + offset + predec;
}

static int
vax_address_cost (rtx x, bool speed ATTRIBUTE_UNUSED)
{
  return (1 + (REG_P (x) ? 0 : vax_address_cost_1 (x)));
}

/* Cost of an expression on a VAX.  This version has costs tuned for the
   CVAX chip (found in the VAX 3 series) with comments for variations on
   other models.

   FIXME: The costs need review, particularly for TRUNCATE, FLOAT_EXTEND
   and FLOAT_TRUNCATE.  We need a -mcpu option to allow provision of
   costs on a per cpu basis.  */

static bool
vax_rtx_costs (rtx x, int code, int outer_code, int *total,
	       bool speed ATTRIBUTE_UNUSED)
{
  enum machine_mode mode = GET_MODE (x);
  int i = 0;				   /* may be modified in switch */
  const char *fmt = GET_RTX_FORMAT (code); /* may be modified in switch */

  switch (code)
    {
      /* On a VAX, constants from 0..63 are cheap because they can use the
	 1 byte literal constant format.  Compare to -1 should be made cheap
	 so that decrement-and-branch insns can be formed more easily (if
	 the value -1 is copied to a register some decrement-and-branch
	 patterns will not match).  */
    case CONST_INT:
      if (INTVAL (x) == 0)
	{
	  *total = 0;
	  return true;
	}
      if (outer_code == AND)
	{
	  *total = ((unsigned HOST_WIDE_INT) ~INTVAL (x) <= 077) ? 1 : 2;
	  return true;
	}
      if ((unsigned HOST_WIDE_INT) INTVAL (x) <= 077
	  || (outer_code == COMPARE
	      && INTVAL (x) == -1)
	  || ((outer_code == PLUS || outer_code == MINUS)
	      && (unsigned HOST_WIDE_INT) -INTVAL (x) <= 077))
	{
	  *total = 1;
	  return true;
	}
      /* FALLTHRU */

    case CONST:
    case LABEL_REF:
    case SYMBOL_REF:
      *total = 3;
      return true;

    case CONST_DOUBLE:
      if (GET_MODE_CLASS (GET_MODE (x)) == MODE_FLOAT)
	*total = vax_float_literal (x) ? 5 : 8;
      else
	*total = ((CONST_DOUBLE_HIGH (x) == 0
		   && (unsigned HOST_WIDE_INT) CONST_DOUBLE_LOW (x) < 64)
		  || (outer_code == PLUS
		      && CONST_DOUBLE_HIGH (x) == -1
		      && (unsigned HOST_WIDE_INT)-CONST_DOUBLE_LOW (x) < 64))
		 ? 2 : 5;
      return true;

    case POST_INC:
      *total = 2;
      return true;		/* Implies register operand.  */

    case PRE_DEC:
      *total = 3;
      return true;		/* Implies register operand.  */

    case MULT:
      switch (mode)
	{
	case DFmode:
	  *total = 16;		/* 4 on VAX 9000 */
	  break;
	case SFmode:
	  *total = 9;		/* 4 on VAX 9000, 12 on VAX 2 */
	  break;
	case DImode:
	  *total = 16;		/* 6 on VAX 9000, 28 on VAX 2 */
	  break;
	case SImode:
	case HImode:
	case QImode:
	  *total = 10;		/* 3-4 on VAX 9000, 20-28 on VAX 2 */
	  break;
	default:
	  *total = MAX_COST;	/* Mode is not supported.  */
	  return true;
	}
      break;

    case UDIV:
      if (mode != SImode)
	{
	  *total = MAX_COST;	/* Mode is not supported.  */
	  return true;
	}
      *total = 17;
      break;

    case DIV:
      if (mode == DImode)
	*total = 30;		/* Highly variable.  */
      else if (mode == DFmode)
	/* divide takes 28 cycles if the result is not zero, 13 otherwise */
	*total = 24;
      else
	*total = 11;		/* 25 on VAX 2 */
      break;

    case MOD:
      *total = 23;
      break;

    case UMOD:
      if (mode != SImode)
	{
	  *total = MAX_COST;	/* Mode is not supported.  */
	  return true;
	}
      *total = 29;
      break;

    case FLOAT:
      *total = (6		/* 4 on VAX 9000 */
		+ (mode == DFmode) + (GET_MODE (XEXP (x, 0)) != SImode));
      break;

    case FIX:
      *total = 7;		/* 17 on VAX 2 */
      break;

    case ASHIFT:
    case LSHIFTRT:
    case ASHIFTRT:
      if (mode == DImode)
	*total = 12;
      else
	*total = 10;		/* 6 on VAX 9000 */
      break;

    case ROTATE:
    case ROTATERT:
      *total = 6;		/* 5 on VAX 2, 4 on VAX 9000 */
      if (CONST_INT_P (XEXP (x, 1)))
	fmt = "e"; 		/* all constant rotate counts are short */
      break;

    case PLUS:
    case MINUS:
      *total = (mode == DFmode) ? 13 : 8; /* 6/8 on VAX 9000, 16/15 on VAX 2 */
      /* Small integer operands can use subl2 and addl2.  */
      if ((CONST_INT_P (XEXP (x, 1)))
	  && (unsigned HOST_WIDE_INT)(INTVAL (XEXP (x, 1)) + 63) < 127)
	fmt = "e";
      break;

    case IOR:
    case XOR:
      *total = 3;
      break;

    case AND:
      /* AND is special because the first operand is complemented.  */
      *total = 3;
      if (CONST_INT_P (XEXP (x, 0)))
	{
	  if ((unsigned HOST_WIDE_INT)~INTVAL (XEXP (x, 0)) > 63)
	    *total = 4;
	  fmt = "e";
	  i = 1;
	}
      break;

    case NEG:
      if (mode == DFmode)
	*total = 9;
      else if (mode == SFmode)
	*total = 6;
      else if (mode == DImode)
	*total = 4;
      else
	*total = 2;
      break;

    case NOT:
      *total = 2;
      break;

    case ZERO_EXTRACT:
    case SIGN_EXTRACT:
      *total = 15;
      break;

    case MEM:
      if (mode == DImode || mode == DFmode)
	*total = 5;		/* 7 on VAX 2 */
      else
	*total = 3;		/* 4 on VAX 2 */
      x = XEXP (x, 0);
      if (!REG_P (x) && GET_CODE (x) != POST_INC)
	*total += vax_address_cost_1 (x);
      return true;

    case FLOAT_EXTEND:
    case FLOAT_TRUNCATE:
    case TRUNCATE:
      *total = 3;		/* FIXME: Costs need to be checked  */
      break;

    default:
      return false;
    }

  /* Now look inside the expression.  Operands which are not registers or
     short constants add to the cost.

     FMT and I may have been adjusted in the switch above for instructions
     which require special handling.  */

  while (*fmt++ == 'e')
    {
      rtx op = XEXP (x, i);

      i += 1;
      code = GET_CODE (op);

      /* A NOT is likely to be found as the first operand of an AND
	 (in which case the relevant cost is of the operand inside
	 the not) and not likely to be found anywhere else.  */
      if (code == NOT)
	op = XEXP (op, 0), code = GET_CODE (op);

      switch (code)
	{
	case CONST_INT:
	  if ((unsigned HOST_WIDE_INT)INTVAL (op) > 63
	      && GET_MODE (x) != QImode)
	    *total += 1;	/* 2 on VAX 2 */
	  break;
	case CONST:
	case LABEL_REF:
	case SYMBOL_REF:
	  *total += 1;		/* 2 on VAX 2 */
	  break;
	case CONST_DOUBLE:
	  if (GET_MODE_CLASS (GET_MODE (op)) == MODE_FLOAT)
	    {
	      /* Registers are faster than floating point constants -- even
		 those constants which can be encoded in a single byte.  */
	      if (vax_float_literal (op))
		*total += 1;
	      else
		*total += (GET_MODE (x) == DFmode) ? 3 : 2;
	    }
	  else
	    {
	      if (CONST_DOUBLE_HIGH (op) != 0
		  || (unsigned HOST_WIDE_INT)CONST_DOUBLE_LOW (op) > 63)
		*total += 2;
	    }
	  break;
	case MEM:
	  *total += 1;		/* 2 on VAX 2 */
	  if (!REG_P (XEXP (op, 0)))
	    *total += vax_address_cost_1 (XEXP (op, 0));
	  break;
	case REG:
	case SUBREG:
	  break;
	default:
	  *total += 1;
	  break;
	}
    }
  return true;
}

/* Output code to add DELTA to the first argument, and then jump to FUNCTION.
   Used for C++ multiple inheritance.
	.mask	^m<r2,r3,r4,r5,r6,r7,r8,r9,r10,r11>  #conservative entry mask
	addl2	$DELTA, 4(ap)	#adjust first argument
	jmp	FUNCTION+2	#jump beyond FUNCTION's entry mask
*/

static void
vax_output_mi_thunk (FILE * file,
		     tree thunk ATTRIBUTE_UNUSED,
		     HOST_WIDE_INT delta,
		     HOST_WIDE_INT vcall_offset ATTRIBUTE_UNUSED,
		     tree function)
{
  fprintf (file, "\t.word 0x0ffc\n\taddl2 $" HOST_WIDE_INT_PRINT_DEC, delta);
  asm_fprintf (file, ",4(%Rap)\n");
  fprintf (file, "\tjmp ");
  assemble_name (file,  XSTR (XEXP (DECL_RTL (function), 0), 0));
  fprintf (file, "+2\n");
}

static rtx
vax_struct_value_rtx (tree fntype ATTRIBUTE_UNUSED,
		      int incoming ATTRIBUTE_UNUSED)
{
  return gen_rtx_REG (Pmode, VAX_STRUCT_VALUE_REGNUM);
}

static rtx
vax_builtin_setjmp_frame_value (void)
{
  return hard_frame_pointer_rtx;
}

/* Worker function for NOTICE_UPDATE_CC.  */

void
vax_notice_update_cc (rtx exp, rtx insn ATTRIBUTE_UNUSED)
{
  if (GET_CODE (exp) == SET)
    {
      if (GET_CODE (SET_SRC (exp)) == CALL)
	CC_STATUS_INIT;
      else if (GET_CODE (SET_DEST (exp)) != ZERO_EXTRACT
	       && GET_CODE (SET_DEST (exp)) != PC)
	{
	  cc_status.flags = 0;
	  /* The integer operations below don't set carry or
	     set it in an incompatible way.  That's ok though
	     as the Z bit is all we need when doing unsigned
	     comparisons on the result of these insns (since
	     they're always with 0).  Set CC_NO_OVERFLOW to
	     generate the correct unsigned branches.  */
	  switch (GET_CODE (SET_SRC (exp)))
	    {
	    case NEG:
	      if (GET_MODE_CLASS (GET_MODE (exp)) == MODE_FLOAT)
		break;
	    case AND:
	    case IOR:
	    case XOR:
	    case NOT:
	    case MEM:
	    case REG:
	      cc_status.flags = CC_NO_OVERFLOW;
	      break;
	    default:
	      break;
	    }
	  cc_status.value1 = SET_DEST (exp);
	  cc_status.value2 = SET_SRC (exp);
	}
    }
  else if (GET_CODE (exp) == PARALLEL
	   && GET_CODE (XVECEXP (exp, 0, 0)) == SET)
    {
      if (GET_CODE (SET_SRC (XVECEXP (exp, 0, 0))) == CALL)
	CC_STATUS_INIT;
      else if (GET_CODE (SET_DEST (XVECEXP (exp, 0, 0))) != PC)
	{
	  cc_status.flags = 0;
	  cc_status.value1 = SET_DEST (XVECEXP (exp, 0, 0));
	  cc_status.value2 = SET_SRC (XVECEXP (exp, 0, 0));
	}
      else
	/* PARALLELs whose first element sets the PC are aob,
	   sob insns.  They do change the cc's.  */
	CC_STATUS_INIT;
    }
  else
    CC_STATUS_INIT;
  if (cc_status.value1 && REG_P (cc_status.value1)
      && cc_status.value2
      && reg_overlap_mentioned_p (cc_status.value1, cc_status.value2))
    cc_status.value2 = 0;
  if (cc_status.value1 && MEM_P (cc_status.value1)
      && cc_status.value2
      && MEM_P (cc_status.value2))
    cc_status.value2 = 0;
  /* Actual condition, one line up, should be that value2's address
     depends on value1, but that is too much of a pain.  */
}

/* Output integer move instructions.  */

const char *
vax_output_int_move (rtx insn ATTRIBUTE_UNUSED, rtx *operands,
		     enum machine_mode mode)
{
  rtx hi[3], lo[3];
  const char *pattern_hi, *pattern_lo;

  switch (mode)
    {
    case DImode:
      if (operands[1] == const0_rtx)
	return "clrq %0";
      if (TARGET_QMATH && optimize_size
	  && (CONST_INT_P (operands[1])
	      || GET_CODE (operands[1]) == CONST_DOUBLE))
	{
	  unsigned HOST_WIDE_INT hval, lval;
	  int n;

	  if (GET_CODE (operands[1]) == CONST_DOUBLE)
	    {
	      gcc_assert (HOST_BITS_PER_WIDE_INT != 64);

	      /* Make sure only the low 32 bits are valid.  */
	      lval = CONST_DOUBLE_LOW (operands[1]) & 0xffffffff;
	      hval = CONST_DOUBLE_HIGH (operands[1]) & 0xffffffff;
	    }
	  else
	    {
	      lval = INTVAL (operands[1]);
	      hval = 0;
	    }

	  /* Here we see if we are trying to see if the 64bit value is really
	     a 6bit shifted some arbitrary amount.  If so, we can use ashq to
	     shift it to the correct value saving 7 bytes (1 addr-mode-byte +
	     8 bytes - 1 shift byte - 1 short literal byte.  */
	  if (lval != 0
	      && (n = exact_log2 (lval & (- lval))) != -1
	      && (lval >> n) < 64)
	    {
	      lval >>= n;

	      /* On 32bit platforms, if the 6bits didn't overflow into the
		 upper 32bit value that value better be 0.  If we have
		 overflowed, make sure it wasn't too much.  */
	      if (HOST_BITS_PER_WIDE_INT == 32 && hval != 0)
		{
		  if (n <= 26 || hval >= ((unsigned)1 << (n - 26)))
		    n = 0;	/* failure */
		  else
		    lval |= hval << (32 - n);
		}
	      /*  If n is 0, then ashq is not the best way to emit this.  */
	      if (n > 0)
		{
		  operands[1] = GEN_INT (lval);
		  operands[2] = GEN_INT (n);
		  return "ashq %2,%1,%0";
		}
#if HOST_BITS_PER_WIDE_INT == 32
	    }
	  /* On 32bit platforms, if the low 32bit value is 0, checkout the
	     upper 32bit value.  */
	  else if (hval != 0
		   && (n = exact_log2 (hval & (- hval)) - 1) != -1
		   && (hval >> n) < 64)
	    {
	      operands[1] = GEN_INT (hval >> n);
	      operands[2] = GEN_INT (n + 32);
	      return "ashq %2,%1,%0";
#endif
	    }
	}

      if (TARGET_QMATH
	  && (!MEM_P (operands[0])
	      || GET_CODE (XEXP (operands[0], 0)) == PRE_DEC
	      || GET_CODE (XEXP (operands[0], 0)) == POST_INC
	      || !illegal_addsub_di_memory_operand (operands[0], DImode))
	  && ((CONST_INT_P (operands[1])
	       && (unsigned HOST_WIDE_INT) INTVAL (operands[1]) >= 64)
	      || GET_CODE (operands[1]) == CONST_DOUBLE))
	{
	  hi[0] = operands[0];
	  hi[1] = operands[1];

	  split_quadword_operands (insn, SET, hi, lo, 2);

	  pattern_lo = vax_output_int_move (NULL, lo, SImode);
	  pattern_hi = vax_output_int_move (NULL, hi, SImode);

	  /* The patterns are just movl/movl or pushl/pushl then a movq will
	     be shorter (1 opcode byte + 1 addrmode byte + 8 immediate value
	     bytes .vs. 2 opcode bytes + 2 addrmode bytes + 8 immediate value
	     value bytes.  */
	  if ((!strncmp (pattern_lo, "movl", 4)
	      && !strncmp (pattern_hi, "movl", 4))
	      || (!strncmp (pattern_lo, "pushl", 5)
		  && !strncmp (pattern_hi, "pushl", 5)))
	    return "movq %1,%0";

	  if (MEM_P (operands[0])
	      && GET_CODE (XEXP (operands[0], 0)) == PRE_DEC)
	    {
	      output_asm_insn (pattern_hi, hi);
	      operands[0] = lo[0];
	      operands[1] = lo[1];
	      operands[2] = lo[2];
	      return pattern_lo;
	    }
	  else
	    {
	      output_asm_insn (pattern_lo, lo);
	      operands[0] = hi[0];
	      operands[1] = hi[1];
	      operands[2] = hi[2];
	      return pattern_hi;
	    }
	}
      return "movq %1,%0";

    case SImode:
      if (symbolic_operand (operands[1], SImode))
	{
	  if (push_operand (operands[0], SImode))
	    return "pushab %a1";
	  return "movab %a1,%0";
	}

      if (operands[1] == const0_rtx)
	{
	  if (push_operand (operands[1], SImode))
	    return "pushl %1";
	  return "clrl %0";
	}

      if (CONST_INT_P (operands[1])
	  && (unsigned HOST_WIDE_INT) INTVAL (operands[1]) >= 64)
	{
	  HOST_WIDE_INT i = INTVAL (operands[1]);
	  int n;
	  if ((unsigned HOST_WIDE_INT)(~i) < 64)
	    return "mcoml %N1,%0";
	  if ((unsigned HOST_WIDE_INT)i < 0x100)
	    return "movzbl %1,%0";
	  if (i >= -0x80 && i < 0)
	    return "cvtbl %1,%0";
	  if (optimize_size
	      && (n = exact_log2 (i & (-i))) != -1
	      && ((unsigned HOST_WIDE_INT)i >> n) < 64)
	    {
	      operands[1] = GEN_INT ((unsigned HOST_WIDE_INT)i >> n);
	      operands[2] = GEN_INT (n);
	      return "ashl %2,%1,%0";
	    }
	  if ((unsigned HOST_WIDE_INT)i < 0x10000)
	    return "movzwl %1,%0";
	  if (i >= -0x8000 && i < 0)
	    return "cvtwl %1,%0";
	}
      if (push_operand (operands[0], SImode))
	return "pushl %1";
      return "movl %1,%0";

    case HImode:
      if (CONST_INT_P (operands[1]))
	{
	  HOST_WIDE_INT i = INTVAL (operands[1]);
	  if (i == 0)
	    return "clrw %0";
	  else if ((unsigned HOST_WIDE_INT)i < 64)
	    return "movw %1,%0";
	  else if ((unsigned HOST_WIDE_INT)~i < 64)
	    return "mcomw %H1,%0";
	  else if ((unsigned HOST_WIDE_INT)i < 256)
	    return "movzbw %1,%0";
	  else if (i >= -0x80 && i < 0)
	    return "cvtbw %1,%0";
	}
      return "movw %1,%0";

    case QImode:
      if (CONST_INT_P (operands[1]))
	{
	  HOST_WIDE_INT i = INTVAL (operands[1]);
	  if (i == 0)
	    return "clrb %0";
	  else if ((unsigned HOST_WIDE_INT)~i < 64)
	    return "mcomb %B1,%0";
	}
      return "movb %1,%0";

    default:
      gcc_unreachable ();
    }
}

/* Output integer add instructions.

   The space-time-opcode tradeoffs for addition vary by model of VAX.

   On a VAX 3 "movab (r1)[r2],r3" is faster than "addl3 r1,r2,r3",
   but it not faster on other models.

   "movab #(r1),r2" is usually shorter than "addl3 #,r1,r2", and is
   faster on a VAX 3, but some VAXen (e.g. VAX 9000) will stall if
   a register is used in an address too soon after it is set.
   Compromise by using movab only when it is shorter than the add
   or the base register in the address is one of sp, ap, and fp,
   which are not modified very often.  */

const char *
vax_output_int_add (rtx insn, rtx *operands, enum machine_mode mode)
{
  switch (mode)
    {
    case DImode:
      {
	rtx low[3];
	const char *pattern;
	int carry = 1;
	bool sub;

	if (TARGET_QMATH && 0)
	  debug_rtx (insn);

	split_quadword_operands (insn, PLUS, operands, low, 3);

	if (TARGET_QMATH)
	  {
	    gcc_assert (rtx_equal_p (operands[0], operands[1]));
#ifdef NO_EXTERNAL_INDIRECT_ADDRESSS
	    gcc_assert (!flag_pic || !external_memory_operand (low[2], SImode));
	    gcc_assert (!flag_pic || !external_memory_operand (low[0], SImode));
#endif

	    /* No reason to add a 0 to the low part and thus no carry, so just
	       emit the appropriate add/sub instruction.  */
	    if (low[2] == const0_rtx)
	      return vax_output_int_add (NULL, operands, SImode);

	    /* Are we doing addition or subtraction?  */
	    sub = CONST_INT_P (operands[2]) && INTVAL (operands[2]) < 0;

	    /* We can't use vax_output_int_add since some the patterns don't
	       modify the carry bit.  */
	    if (sub)
	      {
		if (low[2] == constm1_rtx)
		  pattern = "decl %0";
		else
		  pattern = "subl2 $%n2,%0";
	      }
	    else
	      {
		if (low[2] == const1_rtx)
		  pattern = "incl %0";
		else
		  pattern = "addl2 %2,%0";
	      }
	    output_asm_insn (pattern, low);

	    /* In 2's complement, -n = ~n + 1.  Since we are dealing with
	       two 32bit parts, we complement each and then add one to
	       low part.  We know that the low part can't overflow since
	       it's value can never be 0.  */
	    if (sub)
		return "sbwc %N2,%0";
	    return "adwc %2,%0";
	  }

	/* Add low parts.  */
	if (rtx_equal_p (operands[0], operands[1]))
	  {
	    if (low[2] == const0_rtx)
	/* Should examine operand, punt if not POST_INC.  */
	      pattern = "tstl %0", carry = 0;
	    else if (low[2] == const1_rtx)
	      pattern = "incl %0";
	    else
	      pattern = "addl2 %2,%0";
	  }
	else
	  {
	    if (low[2] == const0_rtx)
	      pattern = "movl %1,%0", carry = 0;
	    else
	      pattern = "addl3 %2,%1,%0";
	  }
	if (pattern)
	  output_asm_insn (pattern, low);
	if (!carry)
	  /* If CARRY is 0, we don't have any carry value to worry about.  */
	  return get_insn_template (CODE_FOR_addsi3, insn);
	/* %0 = C + %1 + %2 */
	if (!rtx_equal_p (operands[0], operands[1]))
	  output_asm_insn ((operands[1] == const0_rtx
			    ? "clrl %0"
			    : "movl %1,%0"), operands);
	return "adwc %2,%0";
      }

    case SImode:
      if (rtx_equal_p (operands[0], operands[1]))
	{
	  if (operands[2] == const1_rtx)
	    return "incl %0";
	  if (operands[2] == constm1_rtx)
	    return "decl %0";
	  if (CONST_INT_P (operands[2])
	      && (unsigned HOST_WIDE_INT) (- INTVAL (operands[2])) < 64)
	    return "subl2 $%n2,%0";
	  if (CONST_INT_P (operands[2])
	      && (unsigned HOST_WIDE_INT) INTVAL (operands[2]) >= 64
	      && REG_P (operands[1])
	      && ((INTVAL (operands[2]) < 32767 && INTVAL (operands[2]) > -32768)
		   || REGNO (operands[1]) > 11))
	    return "movab %c2(%1),%0";
	  if (REG_P (operands[0]) && symbolic_operand (operands[2], SImode))
	    return "movab %a2[%0],%0";
	  return "addl2 %2,%0";
	}

      if (rtx_equal_p (operands[0], operands[2]))
	{
	  if (REG_P (operands[0]) && symbolic_operand (operands[1], SImode))
	    return "movab %a1[%0],%0";
	  return "addl2 %1,%0";
	}

      if (CONST_INT_P (operands[2])
	  && INTVAL (operands[2]) < 32767
	  && INTVAL (operands[2]) > -32768
	  && REG_P (operands[1])
	  && push_operand (operands[0], SImode))
	return "pushab %c2(%1)";

      if (CONST_INT_P (operands[2])
	  && (unsigned HOST_WIDE_INT) (- INTVAL (operands[2])) < 64)
	return "subl3 $%n2,%1,%0";

      if (CONST_INT_P (operands[2])
	  && (unsigned HOST_WIDE_INT) INTVAL (operands[2]) >= 64
	  && REG_P (operands[1])
	  && ((INTVAL (operands[2]) < 32767 && INTVAL (operands[2]) > -32768)
	       || REGNO (operands[1]) > 11))
	return "movab %c2(%1),%0";

      /* Add this if using gcc on a VAX 3xxx:
      if (REG_P (operands[1]) && REG_P (operands[2]))
	return "movab (%1)[%2],%0";
      */

      if (REG_P (operands[1]) && symbolic_operand (operands[2], SImode))
	{
	  if (push_operand (operands[0], SImode))
	    return "pushab %a2[%1]";
	  return "movab %a2[%1],%0";
	}

      if (REG_P (operands[2]) && symbolic_operand (operands[1], SImode))
	{
	  if (push_operand (operands[0], SImode))
	    return "pushab %a1[%2]";
	  return "movab %a1[%2],%0";
	}

      if (flag_pic && REG_P (operands[0])
	  && symbolic_operand (operands[2], SImode))
	return "movab %a2,%0;addl2 %1,%0";

      if (flag_pic
	  && (symbolic_operand (operands[1], SImode)
	      || symbolic_operand (operands[1], SImode)))
	debug_rtx (insn);

      return "addl3 %1,%2,%0";

    case HImode:
      if (rtx_equal_p (operands[0], operands[1]))
	{
	  if (operands[2] == const1_rtx)
	    return "incw %0";
	  if (operands[2] == constm1_rtx)
	    return "decw %0";
	  if (CONST_INT_P (operands[2])
	      && (unsigned HOST_WIDE_INT) (- INTVAL (operands[2])) < 64)
	    return "subw2 $%n2,%0";
	  return "addw2 %2,%0";
	}
      if (rtx_equal_p (operands[0], operands[2]))
	return "addw2 %1,%0";
      if (CONST_INT_P (operands[2])
	  && (unsigned HOST_WIDE_INT) (- INTVAL (operands[2])) < 64)
	return "subw3 $%n2,%1,%0";
      return "addw3 %1,%2,%0";

    case QImode:
      if (rtx_equal_p (operands[0], operands[1]))
	{
	  if (operands[2] == const1_rtx)
	    return "incb %0";
	  if (operands[2] == constm1_rtx)
	    return "decb %0";
	  if (CONST_INT_P (operands[2])
	      && (unsigned HOST_WIDE_INT) (- INTVAL (operands[2])) < 64)
	    return "subb2 $%n2,%0";
	  return "addb2 %2,%0";
	}
      if (rtx_equal_p (operands[0], operands[2]))
	return "addb2 %1,%0";
      if (CONST_INT_P (operands[2])
	  && (unsigned HOST_WIDE_INT) (- INTVAL (operands[2])) < 64)
	return "subb3 $%n2,%1,%0";
      return "addb3 %1,%2,%0";

    default:
      gcc_unreachable ();
    }
}

const char *
vax_output_int_subtract (rtx insn, rtx *operands, enum machine_mode mode)
{
  switch (mode)
    {
    case DImode:
      {
	rtx low[3];
	const char *pattern;
	int carry = 1;

	if (TARGET_QMATH && 0)
	  debug_rtx (insn);

	split_quadword_operands (insn, MINUS, operands, low, 3);

	if (TARGET_QMATH)
	  {
	    if (operands[1] == const0_rtx && low[1] == const0_rtx)
	      {
		/* Negation is tricky.  It's basically complement and increment.
		   Negate hi, then lo, and subtract the carry back.  */
		if ((MEM_P (low[0]) && GET_CODE (XEXP (low[0], 0)) == POST_INC)
		    || (MEM_P (operands[0])
			&& GET_CODE (XEXP (operands[0], 0)) == POST_INC))
		  fatal_insn ("illegal operand detected", insn);
		output_asm_insn ("mnegl %2,%0", operands);
		output_asm_insn ("mnegl %2,%0", low);
		return "sbwc $0,%0";
	      }
	    gcc_assert (rtx_equal_p (operands[0], operands[1]));
	    gcc_assert (rtx_equal_p (low[0], low[1]));
	    if (low[2] == const1_rtx)
	      output_asm_insn ("decl %0", low);
	    else
	      output_asm_insn ("subl2 %2,%0", low);
	    return "sbwc %2,%0";
	  }

	/* Subtract low parts.  */
	if (rtx_equal_p (operands[0], operands[1]))
	  {
	    if (low[2] == const0_rtx)
	      pattern = 0, carry = 0;
	    else if (low[2] == constm1_rtx)
	      pattern = "decl %0";
	    else
	      pattern = "subl2 %2,%0";
	  }
	else
	  {
	    if (low[2] == constm1_rtx)
	      pattern = "decl %0";
	    else if (low[2] == const0_rtx)
	      pattern = get_insn_template (CODE_FOR_movsi, insn), carry = 0;
	    else
	      pattern = "subl3 %2,%1,%0";
	  }
	if (pattern)
	  output_asm_insn (pattern, low);
	if (carry)
	  {
	    if (!rtx_equal_p (operands[0], operands[1]))
	      return "movl %1,%0;sbwc %2,%0";
	    return "sbwc %2,%0";
	    /* %0 = %2 - %1 - C */
	  }
	return get_insn_template (CODE_FOR_subsi3, insn);
      }

    default:
      gcc_unreachable ();
  }
}

/* True if X is an rtx for a constant that is a valid address.  */

bool
legitimate_constant_address_p (rtx x)
{
  if (GET_CODE (x) == LABEL_REF || GET_CODE (x) == SYMBOL_REF
	  || CONST_INT_P (x) || GET_CODE (x) == HIGH)
    return true;
  if (GET_CODE (x) != CONST)
    return false;
#ifdef NO_EXTERNAL_INDIRECT_ADDRESS
  if (flag_pic
      && GET_CODE (XEXP (XEXP (x, 0), 0)) == SYMBOL_REF
      && !SYMBOL_REF_LOCAL_P (XEXP (XEXP (x, 0), 0)))
    return false;
#endif
   return true;
}

/* The other macros defined here are used only in legitimate_address_p ().  */

/* Nonzero if X is a hard reg that can be used as an index
   or, if not strict, if it is a pseudo reg.  */
#define	INDEX_REGISTER_P(X, STRICT) \
(REG_P (X) && (!(STRICT) || REGNO_OK_FOR_INDEX_P (REGNO (X))))

/* Nonzero if X is a hard reg that can be used as a base reg
   or, if not strict, if it is a pseudo reg.  */
#define	BASE_REGISTER_P(X, STRICT) \
(REG_P (X) && (!(STRICT) || REGNO_OK_FOR_BASE_P (REGNO (X))))

#ifdef NO_EXTERNAL_INDIRECT_ADDRESS

/* Re-definition of CONSTANT_ADDRESS_P, which is true only when there
   are no SYMBOL_REFs for external symbols present.  */

static bool
indirectable_constant_address_p (rtx x, bool indirect)
{
  if (GET_CODE (x) == SYMBOL_REF)
    return !flag_pic || SYMBOL_REF_LOCAL_P (x) || !indirect;

  if (GET_CODE (x) == CONST)
    return !flag_pic
	   || GET_CODE (XEXP (XEXP (x, 0), 0)) != SYMBOL_REF
	   || SYMBOL_REF_LOCAL_P (XEXP (XEXP (x, 0), 0));

  return CONSTANT_ADDRESS_P (x);
}

#else /* not NO_EXTERNAL_INDIRECT_ADDRESS */

static bool
indirectable_constant_address_p (rtx x, bool indirect ATTRIBUTE_UNUSED)
{
  return CONSTANT_ADDRESS_P (x);
}

#endif /* not NO_EXTERNAL_INDIRECT_ADDRESS */

/* True if X is an address which can be indirected.  External symbols
   could be in a sharable image library, so we disallow those.  */

static bool
indirectable_address_p (rtx x, bool strict, bool indirect)
{
  if (indirectable_constant_address_p (x, indirect)
      || BASE_REGISTER_P (x, strict))
    return true;
  if (GET_CODE (x) != PLUS
      || !BASE_REGISTER_P (XEXP (x, 0), strict)
      || (flag_pic && !CONST_INT_P (XEXP (x, 1))))
    return false;
  return indirectable_constant_address_p (XEXP (x, 1), indirect);
}

/* Return true if x is a valid address not using indexing.
   (This much is the easy part.)  */
static bool
nonindexed_address_p (rtx x, bool strict)
{
  rtx xfoo0;
  if (REG_P (x))
    {
      if (! reload_in_progress
	  || reg_equiv_mem (REGNO (x)) == 0
	  || indirectable_address_p (reg_equiv_mem (REGNO (x)), strict, false))
	return true;
    }
  if (indirectable_constant_address_p (x, false))
    return true;
  if (indirectable_address_p (x, strict, false))
    return true;
  xfoo0 = XEXP (x, 0);
  if (MEM_P (x) && indirectable_address_p (xfoo0, strict, true))
    return true;
  if ((GET_CODE (x) == PRE_DEC || GET_CODE (x) == POST_INC)
      && BASE_REGISTER_P (xfoo0, strict))
    return true;
  return false;
}

/* True if PROD is either a reg times size of mode MODE and MODE is less
   than or equal 8 bytes, or just a reg if MODE is one byte.  */

static bool
index_term_p (rtx prod, enum machine_mode mode, bool strict)
{
  rtx xfoo0, xfoo1;

  if (GET_MODE_SIZE (mode) == 1)
    return BASE_REGISTER_P (prod, strict);

  if (GET_CODE (prod) != MULT || GET_MODE_SIZE (mode) > 8)
    return false;

  xfoo0 = XEXP (prod, 0);
  xfoo1 = XEXP (prod, 1);

  if (CONST_INT_P (xfoo0)
      && INTVAL (xfoo0) == (int)GET_MODE_SIZE (mode)
      && INDEX_REGISTER_P (xfoo1, strict))
    return true;

  if (CONST_INT_P (xfoo1)
      && INTVAL (xfoo1) == (int)GET_MODE_SIZE (mode)
      && INDEX_REGISTER_P (xfoo0, strict))
    return true;

  return false;
}

/* Return true if X is the sum of a register
   and a valid index term for mode MODE.  */
static bool
reg_plus_index_p (rtx x, enum machine_mode mode, bool strict)
{
  rtx xfoo0, xfoo1;

  if (GET_CODE (x) != PLUS)
    return false;

  xfoo0 = XEXP (x, 0);
  xfoo1 = XEXP (x, 1);

  if (BASE_REGISTER_P (xfoo0, strict) && index_term_p (xfoo1, mode, strict))
    return true;

  if (BASE_REGISTER_P (xfoo1, strict) && index_term_p (xfoo0, mode, strict))
    return true;

  return false;
}

/* Return true if xfoo0 and xfoo1 constitute a valid indexed address.  */
static bool
indexable_address_p (rtx xfoo0, rtx xfoo1, enum machine_mode mode, bool strict)
{
  if (!CONSTANT_ADDRESS_P (xfoo0))
    return false;
  if (BASE_REGISTER_P (xfoo1, strict))
    return !flag_pic || mode == QImode;
  if (flag_pic && symbolic_operand (xfoo0, SImode))
    return false;
  return reg_plus_index_p (xfoo1, mode, strict);
}

/* legitimate_address_p returns true if it recognizes an RTL expression "x"
   that is a valid memory address for an instruction.
   The MODE argument is the machine mode for the MEM expression
   that wants to use this address.  */
bool
vax_legitimate_address_p (enum machine_mode mode, rtx x, bool strict)
{
  rtx xfoo0, xfoo1;

  if (nonindexed_address_p (x, strict))
    return true;

  if (GET_CODE (x) != PLUS)
    return false;

  /* Handle <address>[index] represented with index-sum outermost */

  xfoo0 = XEXP (x, 0);
  xfoo1 = XEXP (x, 1);

  if (index_term_p (xfoo0, mode, strict)
      && nonindexed_address_p (xfoo1, strict))
    return true;

  if (index_term_p (xfoo1, mode, strict)
      && nonindexed_address_p (xfoo0, strict))
    return true;

  /* Handle offset(reg)[index] with offset added outermost */

  if (indexable_address_p (xfoo0, xfoo1, mode, strict)
      || indexable_address_p (xfoo1, xfoo0, mode, strict))
    return true;

  return false;
}

/* Return true if x (a legitimate address expression) has an effect that
   depends on the machine mode it is used for.  On the VAX, the predecrement
   and postincrement address depend thus (the amount of decrement or
   increment being the length of the operand) and all indexed address depend
   thus (because the index scale factor is the length of the operand).  */

bool
vax_mode_dependent_address_p (rtx x)
{
  rtx xfoo0, xfoo1;

  /* Auto-increment cases are now dealt with generically in recog.c.  */
  if (GET_CODE (x) != PLUS)
    return false;

  xfoo0 = XEXP (x, 0);
  xfoo1 = XEXP (x, 1);

  if (CONST_INT_P (xfoo0) && REG_P (xfoo1))
    return false;
  if (CONST_INT_P (xfoo1) && REG_P (xfoo0))
    return false;
  if (!flag_pic && CONSTANT_ADDRESS_P (xfoo0) && REG_P (xfoo1))
    return false;
  if (!flag_pic && CONSTANT_ADDRESS_P (xfoo1) && REG_P (xfoo0))
    return false;

  return true;
}

static rtx
fixup_mathdi_operand (rtx x, enum machine_mode mode)
{
  if (illegal_addsub_di_memory_operand (x, mode))
    {
      rtx addr = XEXP (x, 0);
      rtx temp = gen_reg_rtx (Pmode);
      rtx offset = 0;
#ifdef NO_EXTERNAL_INDIRECT_ADDRESS
      if (GET_CODE (addr) == CONST && flag_pic)
	{
	  offset = XEXP (XEXP (addr, 0), 1);
	  addr = XEXP (XEXP (addr, 0), 0);
	}
#endif
      emit_move_insn (temp, addr);
      if (offset)
	temp = gen_rtx_PLUS (Pmode, temp, offset);
      x = gen_rtx_MEM (DImode, temp);
    }
  return x;
}

void
vax_expand_addsub_di_operands (rtx * operands, enum rtx_code code)
{
  int hi_only = operand_subword (operands[2], 0, 0, DImode) == const0_rtx;
  rtx temp;

  rtx (*gen_old_insn)(rtx, rtx, rtx);
  rtx (*gen_si_insn)(rtx, rtx, rtx);
  rtx (*gen_insn)(rtx, rtx, rtx);

  if (code == PLUS)
    {
      gen_old_insn = gen_adddi3_old;
      gen_si_insn = gen_addsi3;
      gen_insn = gen_adcdi3;
    }
  else if (code == MINUS)
    {
      gen_old_insn = gen_subdi3_old;
      gen_si_insn = gen_subsi3;
      gen_insn = gen_sbcdi3;
    }
  else
    gcc_unreachable ();

  /* If this is addition (thus operands are commutative) and if there is one
     addend that duplicates the desination, we want that addend to be the
     first addend.  */
  if (code == PLUS
      && rtx_equal_p (operands[0], operands[2])
      && !rtx_equal_p (operands[1], operands[2]))
    {
      temp = operands[2];
      operands[2] = operands[1];
      operands[1] = temp;
    }

  if (!TARGET_QMATH)
    {
      emit_insn ((*gen_old_insn) (operands[0], operands[1], operands[2]));
    }
  else if (hi_only)
    {
      if (!rtx_equal_p (operands[0], operands[1])
	  && (REG_P (operands[0]) && MEM_P (operands[1])))
	{
	  emit_move_insn (operands[0], operands[1]);
	  operands[1] = operands[0];
	}

      operands[0] = fixup_mathdi_operand (operands[0], DImode);
      operands[1] = fixup_mathdi_operand (operands[1], DImode);
      operands[2] = fixup_mathdi_operand (operands[2], DImode);

      if (!rtx_equal_p (operands[0], operands[1]))
	emit_move_insn (operand_subword (operands[0], 0, 0, DImode),
			  operand_subword (operands[1], 0, 0, DImode));

      emit_insn ((*gen_si_insn) (operand_subword (operands[0], 1, 0, DImode),
				 operand_subword (operands[1], 1, 0, DImode),
				 operand_subword (operands[2], 1, 0, DImode)));
    }
  else
    {
      /* If are adding the same value together, that's really a multiply by 2,
	 and that's just a left shift of 1.  */
      if (rtx_equal_p (operands[1], operands[2]))
	{
	  gcc_assert (code != MINUS);
	  emit_insn (gen_ashldi3 (operands[0], operands[1], const1_rtx));
	  return;
	}

      operands[0] = fixup_mathdi_operand (operands[0], DImode);

      /* If an operand is the same as operand[0], use the operand[0] rtx
	 because fixup will an equivalent rtx but not an equal one. */

      if (rtx_equal_p (operands[0], operands[1]))
	operands[1] = operands[0];
      else
	operands[1] = fixup_mathdi_operand (operands[1], DImode);

      if (rtx_equal_p (operands[0], operands[2]))
	operands[2] = operands[0];
      else
	operands[2] = fixup_mathdi_operand (operands[2], DImode);

      /* If we are subtracting not from ourselves [d = a - b], and because the
	 carry ops are two operand only, we would need to do a move prior to
	 the subtract.  And if d == b, we would need a temp otherwise
	 [d = a, d -= d] and we end up with 0.  Instead we rewrite d = a - b
	 into d = -b, d += a.  Since -b can never overflow, even if b == d,
	 no temp is needed.

	 If we are doing addition, since the carry ops are two operand, if
	 we aren't adding to ourselves, move the first addend to the
	 destination first.  */

      gcc_assert (operands[1] != const0_rtx || code == MINUS);
      if (!rtx_equal_p (operands[0], operands[1]) && operands[1] != const0_rtx)
	{
	  if (code == MINUS && CONSTANT_P (operands[1]))
	    {
	      temp = gen_reg_rtx (DImode);
	      emit_insn (gen_sbcdi3 (operands[0], const0_rtx, operands[2]));
	      code = PLUS;
	      gen_insn = gen_adcdi3;
	      operands[2] = operands[1];
	      operands[1] = operands[0];
	    }
	  else
	    emit_move_insn (operands[0], operands[1]);
	}

      /* Subtracting a constant will have been rewritten to an addition of the
	 negative of that constant before we get here.  */
      gcc_assert (!CONSTANT_P (operands[2]) || code == PLUS);
      emit_insn ((*gen_insn) (operands[0], operands[1], operands[2]));
    }
}

bool
adjacent_operands_p (rtx lo, rtx hi, enum machine_mode mode)
{
  HOST_WIDE_INT lo_offset;
  HOST_WIDE_INT hi_offset;

  if (GET_CODE (lo) != GET_CODE (hi))
    return false;

  if (REG_P (lo))
    return mode == SImode && REGNO (lo) + 1 == REGNO (hi);
  if (CONST_INT_P (lo))
    return INTVAL (hi) == 0 && 0 <= INTVAL (lo) && INTVAL (lo) < 64;
  if (CONST_INT_P (lo))
    return mode != SImode;

  if (!MEM_P (lo))
    return false;

  if (MEM_VOLATILE_P (lo) || MEM_VOLATILE_P (hi))
    return false;

  lo = XEXP (lo, 0);
  hi = XEXP (hi, 0);

  if (GET_CODE (lo) == POST_INC /* || GET_CODE (lo) == PRE_DEC */)
    return rtx_equal_p (lo, hi);

  switch (GET_CODE (lo))
    {
    case REG:
    case SYMBOL_REF:
      lo_offset = 0;
      break;
    case CONST:
      lo = XEXP (lo, 0);
      /* FALLTHROUGH */
    case PLUS:
      if (!CONST_INT_P (XEXP (lo, 1)))
	return false;
      lo_offset = INTVAL (XEXP (lo, 1));
      lo = XEXP (lo, 0);
      break;
    default:
      return false;
    }

  switch (GET_CODE (hi))
    {
    case REG:
    case SYMBOL_REF:
      hi_offset = 0;
      break;
    case CONST:
      hi = XEXP (hi, 0);
      /* FALLTHROUGH */
    case PLUS:
      if (!CONST_INT_P (XEXP (hi, 1)))
	return false;
      hi_offset = INTVAL (XEXP (hi, 1));
      hi = XEXP (hi, 0);
      break;
    default:
      return false;
    }

  if (GET_CODE (lo) == MULT || GET_CODE (lo) == PLUS)
    return false;

  return rtx_equal_p (lo, hi)
	 && hi_offset - lo_offset == GET_MODE_SIZE (mode);
}

/* Output assembler code for a block containing the constant parts
   of a trampoline, leaving space for the variable parts.  */

/* On the VAX, the trampoline contains an entry mask and two instructions:
     .word NN
     movl $STATIC,r0   (store the functions static chain)
     jmp  *$FUNCTION   (jump to function code at address FUNCTION)  */

static void
vax_asm_trampoline_template (FILE *f ATTRIBUTE_UNUSED)
{
  assemble_aligned_integer (2, const0_rtx);
  assemble_aligned_integer (2, GEN_INT (0x8fd0));
  assemble_aligned_integer (4, const0_rtx);
  assemble_aligned_integer (1, GEN_INT (0x50 + STATIC_CHAIN_REGNUM));
  assemble_aligned_integer (2, GEN_INT (0x9f17));
  assemble_aligned_integer (4, const0_rtx);
}

/* We copy the register-mask from the function's pure code
   to the start of the trampoline.  */

static void
vax_trampoline_init (rtx m_tramp, tree fndecl, rtx cxt)
{
  rtx fnaddr = XEXP (DECL_RTL (fndecl), 0);
  rtx mem;

  emit_block_move (m_tramp, assemble_trampoline_template (),
		   GEN_INT (TRAMPOLINE_SIZE), BLOCK_OP_NORMAL);

  mem = adjust_address (m_tramp, HImode, 0);
  emit_move_insn (mem, gen_const_mem (HImode, fnaddr));

  mem = adjust_address (m_tramp, SImode, 4);
  emit_move_insn (mem, cxt);
  mem = adjust_address (m_tramp, SImode, 11);
  emit_move_insn (mem, plus_constant (fnaddr, 2));
  emit_insn (gen_sync_istream ());
}

/* Value is the number of bytes of arguments automatically
   popped when returning from a subroutine call.
   FUNDECL is the declaration node of the function (as a tree),
   FUNTYPE is the data type of the function (as a tree),
   or for a library call it is an identifier node for the subroutine name.
   SIZE is the number of bytes of arguments passed on the stack.

   On the VAX, the RET insn pops a maximum of 255 args for any function.  */

static int
vax_return_pops_args (tree fundecl ATTRIBUTE_UNUSED,
		      tree funtype ATTRIBUTE_UNUSED, int size)
{
  return size > 255 * 4 ? 0 : size;
}

/* Define where to put the arguments to a function.
   Value is zero to push the argument on the stack,
   or a hard register in which to store the argument.

   MODE is the argument's machine mode.
   TYPE is the data type of the argument (as a tree).
    This is null for libcalls where that information may
    not be available.
   CUM is a variable of type CUMULATIVE_ARGS which gives info about
    the preceding args and about the function being called.
   NAMED is nonzero if this argument is a named parameter
    (otherwise it is an extra parameter matching an ellipsis).  */

/* On the VAX all args are pushed.  */

static rtx
vax_function_arg (cumulative_args_t cum ATTRIBUTE_UNUSED,
		  enum machine_mode mode ATTRIBUTE_UNUSED,
		  const_tree type ATTRIBUTE_UNUSED,
		  bool named ATTRIBUTE_UNUSED)
{
  return NULL_RTX;
}

/* Update the data in CUM to advance over an argument of mode MODE and
   data type TYPE.  (TYPE is null for libcalls where that information
   may not be available.)  */

static void
vax_function_arg_advance (cumulative_args_t cum_v, enum machine_mode mode,
			  const_tree type, bool named ATTRIBUTE_UNUSED)
{
  CUMULATIVE_ARGS *cum = get_cumulative_args (cum_v);

  *cum += (mode != BLKmode
	   ? (GET_MODE_SIZE (mode) + 3) & ~3
	   : (int_size_in_bytes (type) + 3) & ~3);
}
