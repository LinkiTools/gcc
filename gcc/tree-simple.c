/* Functions to analyze and validate GIMPLE trees.
   Copyright (C) 2002, 2003 Free Software Foundation, Inc.
   Contributed by Diego Novillo <dnovillo@redhat.com>
   Rewritten by Jason Merrill <jason@redhat.com>

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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "tree-simple.h"
#include "output.h"
#include "rtl.h"
#include "expr.h"

/* GCC GIMPLE structure

   Inspired by the SIMPLE C grammar at

	http://www-acaps.cs.mcgill.ca/info/McCAT/McCAT.html

   function:
     FUNCTION_DECL
       DECL_SAVED_TREE -> block
   block:
     BIND_EXPR
       BIND_EXPR_VARS -> DECL chain
       BIND_EXPR_BLOCK -> BLOCK
       BIND_EXPR_BODY -> compound-stmt
   compound-stmt:
     COMPOUND_EXPR
       op0 -> non-compound-stmt
       op1 -> stmt
     | EXPR_VEC
       (or other alternate solution)
   stmt: compound-stmt | non-compound-stmt
   non-compound-stmt:
     block
     | if-stmt
     | switch-stmt
     | jump-stmt
     | label-stmt
     | try-stmt
     | modify-stmt
     | call-stmt
   if-stmt:
     COND_EXPR
       op0 -> condition
       op1 -> stmt
       op2 -> stmt
   switch-stmt:
     SWITCH_EXPR
       op0 -> val
       op1 -> stmt
       op2 -> array of case labels (as LABEL_DECLs?)
         FIXME: add case value info
   jump-stmt:
       GOTO_EXPR
         op0 -> LABEL_DECL | '*' ID
     | RETURN_EXPR
         op0 -> modify-stmt | NULL_TREE
	 (maybe -> RESULT_DECL | NULL_TREE? seems like some of expand_return
	  depends on getting a MODIFY_EXPR.)
     | THROW_EXPR?  do we need/want such a thing for opts, perhaps
         to generate an ERT_THROW region?  I think so.
	 Hmm...this would only work at the GIMPLE level, where we know that
	   the call args don't have any EH impact.  Perhaps
	   annotation of the CALL_EXPR would work better.
     | RESX_EXPR
   label-stmt:
     LABEL_EXPR
         op0 -> LABEL_DECL
     | CASE_LABEL_EXPR
         CASE_LOW -> val | NULL_TREE
         CASE_HIGH -> val | NULL_TREE
	 CASE_LABEL -> LABEL_DECL  FIXME
   try-stmt:
     TRY_CATCH_EXPR
       op0 -> stmt
       op1 -> handler
     | TRY_FINALLY_EXPR
       op0 -> stmt
       op1 -> stmt
   handler:
     catch-seq
     | EH_FILTER_EXPR
     | stmt
   modify-stmt:
     MODIFY_EXPR
       op0 -> lhs
       op1 -> rhs
   call-stmt: CALL_EXPR
     op0 -> ID | '&' ID
     op1 -> arglist

   addr-expr-arg : compref | ID
   lhs: addr-expr-arg | '*' ID
   min-lval: ID | '*' ID
   compref :
     COMPONENT_REF
       op0 -> compref | min-lval
     | ARRAY_REF
       op0 -> compref | min-lval
       op1 -> val
     | REALPART_EXPR
     | IMAGPART_EXPR

   condition : val | val relop val
   val : ID | CONST

   rhs        : varname | CONST
	      | '*' ID
	      | '&' addr-expr-arg
	      | call_expr
	      | unop val
	      | val binop val
	      | '(' cast ')' val

	      (cast here stands for all valid C typecasts)

      unop
	      : '+'
	      | '-'
	      | '!'
	      | '~'

      binop
	      : relop | '-'
	      | '+'
	      | '/'
	      | '*'
	      | '%'
	      | '&'
	      | '|'
	      | '<<'
	      | '>>'
	      | '^'

      relop
	      : '<'
	      | '<='
	      | '>'
	      | '>='
	      | '=='
	      | '!='

*/

static int is_gimple_id (tree);

/* Validation of GIMPLE expressions.  */

/*  Return nonzero if T is a GIMPLE RHS:

      rhs     : varname | CONST
	      | '*' ID
	      | '&' varname_or_temp
	      | call_expr
	      | unop val
	      | val binop val
	      | '(' cast ')' val  */

int
is_gimple_rhs (tree t)
{
  enum tree_code code = TREE_CODE (t);

  switch (TREE_CODE_CLASS (code))
    {
    case '1':
    case '2':
    case '<':
      return 1;

    default:
      break;
    }

  switch (code)
    {
    case TRUTH_AND_EXPR:
    case TRUTH_OR_EXPR:
    case TRUTH_XOR_EXPR:
    case ADDR_EXPR:
    case CALL_EXPR:
    case CONSTRUCTOR:
      /* FIXME lower VA_ARG_EXPR.  */
    case VA_ARG_EXPR:
      return 1;

    default:
      break;
    }

  if (is_gimple_lvalue (t) || is_gimple_val (t))
    return 1;

  return 0;
}

/* Returns nonzero if T is a valid CONSTRUCTOR component in GIMPLE, either
   a val or another CONSTRUCTOR.  */

int
is_gimple_constructor_elt (tree t)
{
  return (is_gimple_val (t)
	  || TREE_CODE (t) == CONSTRUCTOR);
}

/*  Return nonzero if T is a valid LHS for a GIMPLE assignment expression.  */

int
is_gimple_lvalue (tree t)
{
  return (is_gimple_addr_expr_arg (t)
	  || TREE_CODE (t) == INDIRECT_REF
	  /* These are complex lvalues, but don't have addresses, so they
	     go here.  */
	  || TREE_CODE (t) == BIT_FIELD_REF);
}


/*  Return nonzero if T is a GIMPLE condition:

      condexpr
	      : val
	      | val relop val  */

int
is_gimple_condexpr (tree t)
{
  return (is_gimple_val (t)
	  || TREE_CODE_CLASS (TREE_CODE (t)) == '<');
}


/*  Return nonzero if T is a valid operand for '&':

      varname
	      : arrayref
	      | compref
	      | ID     */

int
is_gimple_addr_expr_arg (tree t)
{
  return (is_gimple_id (t)
	  || TREE_CODE (t) == ARRAY_REF
	  || TREE_CODE (t) == COMPONENT_REF
	  || TREE_CODE (t) == REALPART_EXPR
	  || TREE_CODE (t) == IMAGPART_EXPR);
}

/*  Return nonzero if T is a constant.  This is one of the few predicates
    that looks deeper than the TREE_CODE; this is necessary because, e.g.,
    some GIMPLE PLUS_EXPRs are considered constants and some are not.  */

int
is_gimple_const (tree t)
{
  tree tmp = t;

  /* FIXME lose the STRIP_NOPS once we are more clever about builtins.  */
  STRIP_NOPS (tmp);
  if (TREE_CODE (tmp) == ADDR_EXPR
      && TREE_CODE (TREE_OPERAND (tmp, 0)) == STRING_CST)
    return 1;

  if (TREE_CODE (t) == ADDR_EXPR
#if 0
      && DECL_P (TREE_OPERAND (t, 0))
#else
      && TREE_CODE (TREE_OPERAND (t, 0)) == FUNCTION_DECL
#endif
      && (TREE_STATIC (TREE_OPERAND (t, 0))
	  || DECL_EXTERNAL (TREE_OPERAND (t, 0)))
      && !DECL_WEAK (TREE_OPERAND (t, 0)))
    return 1;

  if (TREE_CODE (t) == PLUS_EXPR
      && TREE_CONSTANT (t)
      && is_gimple_const (TREE_OPERAND (t, 0))
      && is_gimple_const (TREE_OPERAND (t, 1)))
    return 1;

  return (TREE_CODE (t) == INTEGER_CST
	  || TREE_CODE (t) == REAL_CST
	  || TREE_CODE (t) == STRING_CST
	  || TREE_CODE (t) == COMPLEX_CST
	  || TREE_CODE (t) == VECTOR_CST);
}

/* Return nonzero if T looks like a valid GIMPLE statement.  */

int
is_gimple_stmt (tree t)
{
  enum tree_code code = TREE_CODE (t);
  char class = TREE_CODE_CLASS (code);

  if (IS_EMPTY_STMT (t))
    return 1;

  switch (class)
    {
    case 'r':
    case '1':
    case '2':
    case '<':
    case 'd':
    case 'c':
      /* These should never appear at statement level.  */
      return 0;

    case 'e':
    case 's':
      /* Might be OK.  */
      break;

    case 'x':
      if (code == PHI_NODE)
	return 1;
      else
	return 0;

    default:
      /* Not an expression?!?  */
      return 0;
    }

  switch (code)
    {
    case BIND_EXPR:
    case COND_EXPR:
      /* These are only valid if they're void.  */
      return VOID_TYPE_P (TREE_TYPE (t));

    case SWITCH_EXPR:
    case GOTO_EXPR:
    case RETURN_EXPR:
    case LABEL_EXPR:
    case CASE_LABEL_EXPR:
    case TRY_CATCH_EXPR:
    case TRY_FINALLY_EXPR:
    case EH_FILTER_EXPR:
    case CATCH_EXPR:
    case ASM_EXPR:
      /* These are always void.  */
      return 1;

    case VA_ARG_EXPR:
      /* FIXME this should be lowered.  */
      return 1;

    case COMPOUND_EXPR:
      /* FIXME should we work harder to make COMPOUND_EXPRs void?  */
    case CALL_EXPR:
    case MODIFY_EXPR:
      /* These are valid regardless of their type.  */
      return 1;

    default:
      return 0;
    }
}

/* Return nonzero if T is a variable.  */

int
is_gimple_variable (tree t)
{
  return (TREE_CODE (t) == VAR_DECL
	  || TREE_CODE (t) == PARM_DECL
	  || TREE_CODE (t) == RESULT_DECL
	  || TREE_CODE (t) == SSA_NAME);
}

/*  Return nonzero if T is a GIMPLE identifier (something with an address).  */

static int
is_gimple_id (tree t)
{
  return (is_gimple_variable (t)
	  || TREE_CODE (t) == FUNCTION_DECL
	  || TREE_CODE (t) == LABEL_DECL
	  /* Allow string constants, since they are addressable.  */
	  || TREE_CODE (t) == STRING_CST);
}

/* Return nonzero if TYPE is a suitable type for a scalar register
   variable.  */

bool
is_gimple_reg_type (tree type)
{
  return (TYPE_MODE (type) != BLKmode
	  && TREE_CODE (type) != ARRAY_TYPE
	  && !TREE_ADDRESSABLE (type));
}

/* Return nonzero if T is a scalar register variable.  */

int
is_gimple_reg (tree t)
{
  if (TREE_CODE (t) == SSA_NAME)
    t = SSA_NAME_VAR (t);

  return (is_gimple_variable (t)
	  && is_gimple_reg_type (TREE_TYPE (t))
	  && ! TREE_STATIC (t)
	  && ! DECL_EXTERNAL (t)
	  && ! TREE_ADDRESSABLE (t)
	  /* A volatile decl is not acceptable because we can't reuse it as
	     needed.  We need to copy it into a temp first.  */
	  && ! TREE_THIS_VOLATILE (t));
}

/*  Return nonzero if T is a GIMPLE rvalue, i.e. an identifier or a
    constant.  */

int
is_gimple_val (tree t)
{
  /* Make loads from volatiles and memory vars explicit.  */
  if (is_gimple_variable (t)
      && is_gimple_reg_type (TREE_TYPE (t))
      && !is_gimple_reg (t))
    return 0;

  if (/* FIXME make this a decl.  */
      TREE_CODE (t) == EXC_PTR_EXPR
      /* Allow the address of a decl.  */
      || (TREE_CODE (t) == ADDR_EXPR
#if 0
	  && DECL_P (TREE_OPERAND (t, 0))
#else
	  && TREE_CODE (TREE_OPERAND (t, 0)) == FUNCTION_DECL
#endif
	  ))
    return 1;

  /* Allow address of vla, so that we do not replace it in the call_expr of
     stack_alloc builtin.  */
  if (TREE_CODE (t) == ADDR_EXPR
      && TREE_CODE (TREE_OPERAND (t, 0)) == VAR_DECL
      && DECL_SIZE_UNIT (TREE_OPERAND (t, 0))
      && !TREE_CONSTANT (DECL_SIZE_UNIT (TREE_OPERAND (t, 0))))
    return 1;

  return (is_gimple_variable (t) || is_gimple_const (t));
}


/*  Return true if T is a GIMPLE minimal lvalue, of the form

    min_lval: ID | '(' '*' ID ')'

    This never actually appears in the original SIMPLE grammar, but is
    repeated in several places.  */

int
is_gimple_min_lval (tree t)
{
  return (is_gimple_id (t)
	  || TREE_CODE (t) == INDIRECT_REF);
}

/*  Return nonzero if T is a typecast operation of the form
    '(' cast ')' val.  */

int
is_gimple_cast (tree t)
{
  return (TREE_CODE (t) == NOP_EXPR
	  || TREE_CODE (t) == CONVERT_EXPR
          || TREE_CODE (t) == FIX_TRUNC_EXPR
          || TREE_CODE (t) == FIX_CEIL_EXPR
          || TREE_CODE (t) == FIX_FLOOR_EXPR
          || TREE_CODE (t) == FIX_ROUND_EXPR);
}

/* Given an _EXPR TOP, reorganize all of the nested _EXPRs with the same
   code so that they only appear as the second operand.  This should only
   be used for tree codes which are truly associative, such as
   COMPOUND_EXPR and TRUTH_ANDIF_EXPR.  Arithmetic is not associative
   enough, due to the limited precision of arithmetic data types.

   This transformation is conservative; the operand 0 of a matching tree
   node will only change if it is also a matching node.  */

tree
right_assocify_expr (tree top)
{
  tree *p = &top;
  enum tree_code code = TREE_CODE (*p);
  while (TREE_CODE (*p) == code)
    {
      tree cur = *p;
      tree lhs = TREE_OPERAND (cur, 0);
      if (TREE_CODE (lhs) == code)
	{
	  /* There's a left-recursion.  If we have ((a, (b, c)), d), we
	     want to rearrange to (a, (b, (c, d))).  */
	  tree *q;

	  /* Replace cur with the lhs; move (a, *) up.  */
	  *p = lhs;

	  if (code == COMPOUND_EXPR)
	    {
	      /* We need to give (b, c) the type of c; previously lhs had
		 the type of b.  */
	      TREE_TYPE (lhs) = TREE_TYPE (cur);
	      if (TREE_SIDE_EFFECTS (cur))
		TREE_SIDE_EFFECTS (lhs) = 1;
	    }

	  /* Walk through the op1 chain from there until we find something
	     with a different code.  In this case, c.  */
	  for (q = &TREE_OPERAND (lhs, 1); TREE_CODE (*q) == code;
	       q = &TREE_OPERAND (*q, 1))
	    TREE_TYPE (*q) = TREE_TYPE (cur);

	  /* Change (*, d) into (c, d).  */
	  TREE_OPERAND (cur, 0) = *q;

	  /* And plug it in where c used to be.  */
	  *q = cur;
	}
      else
	p = &TREE_OPERAND (cur, 1);
    }
  return top;
}

/* Normalize the statement TOP.  If it is a COMPOUND_EXPR, reorganize it so
   that we can traverse it without recursion.  If it is null, replace it
   with a nop.  */

tree
rationalize_compound_expr (tree top)
{
  if (top == NULL_TREE)
    top = build_empty_stmt ();
  else if (TREE_CODE (top) == COMPOUND_EXPR)
    top = right_assocify_expr (top);

  return top;
}

/* Given a GIMPLE varname (an ID, an arrayref or a compref), return the
   base symbol for the variable.  */

tree
get_base_symbol (tree t)
{
  do
    {
      STRIP_NOPS (t);

      if (DECL_P (t))
	return t;

      switch (TREE_CODE (t))
	{
	case SSA_NAME:
	  t = SSA_NAME_VAR (t);
	  break;

	case ARRAY_REF:
	case COMPONENT_REF:
	case REALPART_EXPR:
	case IMAGPART_EXPR:
	  t = TREE_OPERAND (t, 0);
	  break;

	default:
	  return NULL_TREE;
	}
    }
  while (t);

  return t;
}

void
recalculate_side_effects (tree t)
{
  enum tree_code code = TREE_CODE (t);
  int fro = first_rtl_op (code);
  int i;

  switch (TREE_CODE_CLASS (code))
    {
    case 'e':
      switch (code)
	{
	case INIT_EXPR:
	case MODIFY_EXPR:
	case VA_ARG_EXPR:
	case RTL_EXPR:
	case PREDECREMENT_EXPR:
	case PREINCREMENT_EXPR:
	case POSTDECREMENT_EXPR:
	case POSTINCREMENT_EXPR:
	  /* All of these have side-effects, no matter what their
	     operands are.  */
	  return;

	default:
	  break;
	}
      /* Fall through.  */

    case '<':  /* a comparison expression */
    case '1':  /* a unary arithmetic expression */
    case '2':  /* a binary arithmetic expression */
    case 'r':  /* a reference */
      TREE_SIDE_EFFECTS (t) = TREE_THIS_VOLATILE (t);
      for (i = 0; i < fro; ++i)
	{
	  tree op = TREE_OPERAND (t, i);
	  if (op && TREE_SIDE_EFFECTS (op))
	    TREE_SIDE_EFFECTS (t) = 1;
	}
      break;
   }
}
